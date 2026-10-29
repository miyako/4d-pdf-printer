#include "Contract.h"
#include <atomic>
#include <mutex>
#include <objbase.h>
#include <winrt/Windows.ApplicationModel.Background.h>
#include <winrt/Windows.Devices.Printers.h>
#include <winrt/Windows.Graphics.Printing.Workflow.h>
#include <winrt/Windows.Storage.Streams.h>

using namespace activation;
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::Background;
using namespace Windows::Graphics::Printing::Workflow;
using namespace Windows::Storage::Streams;
using namespace Windows::Foundation;

namespace
{
    IAsyncAction Publish(StorageFolder folder, hstring name, JsonObject value)
    {
        auto text = value.Stringify();
        auto bytes = Windows::Security::Cryptography::CryptographicBuffer::ConvertStringToBinary(
            text, Windows::Security::Cryptography::BinaryStringEncoding::Utf8);
        Require(bytes.Length() <= RecordLimit);
        auto file = co_await folder.CreateFileAsync(name + L".pending", CreationCollisionOption::FailIfExists);
        co_await FileIO::WriteBufferAsync(file, bytes);
        co_await file.RenameAsync(name, NameCollisionOption::FailIfExists);
    }
    IAsyncOperation<JsonObject> ReadRecord(StorageFolder folder, hstring name)
    {
        auto file = co_await folder.GetFileAsync(name);
        auto properties = co_await file.GetBasicPropertiesAsync();
        Require(properties.Size() > 0 && properties.Size() <= RecordLimit);
        auto bytes = co_await FileIO::ReadBufferAsync(file);
        Require(bytes.Length() > 0 && bytes.Length() <= RecordLimit);
        co_return JsonObject::Parse(Windows::Security::Cryptography::CryptographicBuffer::ConvertBinaryToString(
            Windows::Security::Cryptography::BinaryStringEncoding::Utf8, bytes));
    }
}
struct ProbeTask : implements<ProbeTask, IBackgroundTask>
{
    BackgroundTaskDeferral deferral{nullptr};
    IBackgroundTaskInstance instance{nullptr};
    PrintWorkflowVirtualPrinterSession session{nullptr};
    IAsyncOperation<FullTrustProcessLaunchResult> launchOperation{nullptr};
    event_token dataToken{}, cancelToken{};
    std::mutex lifecycle;
    std::atomic<bool> canceled{false}, working{false}, finished{false};
    bool subscribed = false;
    StorageFile entryLog{nullptr};
    hstring GetRuntimeClassName() const { return L"DesktopPdfProbe.Task"; }

    void Finish() noexcept
    {
        if (finished.exchange(true)) return;
        std::scoped_lock lock(lifecycle);
        try
        {
            if (subscribed) session.VirtualPrinterDataAvailable(dataToken);
            if (instance) instance.Canceled(cancelToken);
            if (deferral) deferral.Complete();
        }
        catch (...) { OutputDebugStringW(L"PSA activation probe deferral cleanup failed.\n"); }
        deferral = nullptr;
    }
    void Run(IBackgroundTaskInstance const& task)
    {
        instance = task; deferral = task.GetDeferral();
        cancelToken = task.Canceled([weak = get_weak()](auto const&, auto const&)
        {
            if (auto self = weak.get())
            {
                self->canceled = true;
                try
                {
                    std::scoped_lock lock(self->lifecycle);
                    if (self->launchOperation) self->launchOperation.Cancel();
                }
                catch (...) { OutputDebugStringW(L"PSA activation probe launch cancellation failed; late launch remains possible.\n"); }
                if (!self->working.exchange(true)) self->Finish();
            }
        });
        Begin();
    }
    fire_and_forget Begin()
    {
        auto lifetime = get_strong();
        auto record = activation::Record(L"background-task-entry");
        bool failed = false;
        try
        {
            auto context = Current(true);
            Flag(record, L"appcontainer", true);
            Number(record, L"session", context.identity.session);
            auto logs = co_await context.cache.CreateFolderAsync(L"ActivationProbeEntry", CreationCollisionOption::OpenIfExists);
            for (unsigned index = 0; index < 16; ++index)
            {
                try
                {
                    entryLog = co_await logs.CreateFileAsync(to_hstring(index) + L".json",
                        CreationCollisionOption::FailIfExists);
                    break;
                }
                catch (hresult_error const& error)
                { if (error.code() != HRESULT_FROM_WIN32(ERROR_FILE_EXISTS) &&
                      error.code() != HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) throw; }
            }
            Require(entryLog != nullptr, HRESULT_FROM_WIN32(ERROR_TOO_MANY_CMDS));
            co_await FileIO::WriteTextAsync(entryLog, record.Stringify());
            if (canceled) { Finish(); co_return; }
            auto details = instance.TriggerDetails().as<PrintWorkflowVirtualPrinterTriggerDetails>();
            {
                std::scoped_lock lock(lifecycle);
                if (finished) co_return;
                session = details.VirtualPrinterSession();
                dataToken = session.VirtualPrinterDataAvailable(
                    [self = get_strong()](auto const&, auto const& args) { self->Data(args); });
                subscribed = true;
            }
            if (canceled) { Finish(); co_return; }
            session.Start();
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(CallbackMs);
            while (!working && !finished && std::chrono::steady_clock::now() < deadline)
                co_await resume_after(std::chrono::milliseconds(25));
            if (!finished && !working.exchange(true))
            {
                Text(record, L"stage", L"no-actual-callback-before-deadline");
                Number(record, L"hresult", static_cast<uint32_t>(HRESULT_FROM_WIN32(ERROR_TIMEOUT)));
                failed = true;
            }
        }
        catch (...)
        {
            Number(record, L"hresult", static_cast<uint32_t>(to_hresult().value));
            failed = true;
        }
        if (failed)
        {
            try { if (entryLog) co_await FileIO::WriteTextAsync(entryLog, record.Stringify()); }
            catch (...) { OutputDebugStringW(L"PSA activation probe entry evidence failed.\n"); }
            Finish();
        }
    }
    fire_and_forget Data(PrintWorkflowVirtualPrinterDataAvailableEventArgs args)
    {
        auto lifetime = get_strong();
        if (working.exchange(true))
        {
            try { args.CompleteJob(PrintWorkflowSubmittedStatus::Failed); }
            catch (...) { OutputDebugStringW(L"PSA activation probe duplicate callback completion failed.\n"); }
            co_return;
        }
        StorageFolder job{nullptr};
        JsonObject result = activation::Record(L"callback-entry");
        bool roundtrip = false;
        bool failed = false;
        wchar_t const* stage = L"callback-metadata";
        try
        {
            auto context = Current(true);
            auto id = JobFromTitle(std::wstring(args.Configuration().JobTitle()));
            Require(!id.empty() && session.Printer().PrinterUri().AbsoluteUri() == activation::Endpoint &&
                args.SourceContent().ContentType() == L"application/oxps", E_ACCESSDENIED);
            auto root = co_await context.cache.GetFolderAsync(Folder);
            job = co_await root.GetFolderAsync(id);
            auto request = co_await ReadRecord(job, L"request.json");
            Validate(request, context, id, L"request", Now());
            Require(request.GetNamedString(L"endpoint") == activation::Endpoint, E_ACCESSDENIED);
            auto now = Now();
            auto expires = std::min(Integer(request, L"expires_ms"), now + CallbackMs);
            auto callback = Bound(context, id, L"callback", now, expires);
            Flag(callback, L"appcontainer", true); Text(callback, L"endpoint", activation::Endpoint);
            Text(callback, L"request_hash", Hash(std::wstring(request.Stringify())));
            Text(callback, L"title_hash", Hash(std::wstring(args.Configuration().JobTitle())));
            Text(callback, L"workflow_hash", Hash(std::wstring(args.Configuration().SessionId())));
            stage = L"callback-one-time-claim";
            co_await Publish(job, L"callback.json", callback);
            result = Bound(context, id, L"callback-result", Integer(request, L"created_ms"), Integer(request, L"expires_ms"));
            Text(result, L"workflow_hash", std::wstring(callback.GetNamedString(L"workflow_hash")));
            Flag(result, L"appcontainer", true); Flag(result, L"launch_result_available", false);
            APTTYPE apartment{};
            APTTYPEQUALIFIER qualifier{};
            check_hresult(CoGetApartmentType(&apartment, &qualifier));
            Number(result, L"apartment", static_cast<uint64_t>(apartment));
            stage = L"launch-create";
            co_await Publish(job, L"launch-start.json", callback);
            Require(!canceled, HRESULT_FROM_WIN32(ERROR_CANCELLED));
            {
                std::scoped_lock lock(lifecycle);
                launchOperation = FullTrustProcessLauncher::LaunchFullTrustProcessForCurrentAppWithArgumentsAsync(
                    L"--job " + id);
            }
            stage = L"launch-await";
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(CallbackMs);
            while (launchOperation.Status() == AsyncStatus::Started)
            {
                Require(!canceled && Now() < expires && std::chrono::steady_clock::now() < deadline,
                    HRESULT_FROM_WIN32(ERROR_TIMEOUT));
                co_await resume_after(std::chrono::milliseconds(25));
            }
            stage = L"launch-get-results";
            auto launched = launchOperation.GetResults();
            Flag(result, L"launch_result_available", true);
            Number(result, L"launch_result", static_cast<uint64_t>(launched.LaunchResult()));
            Number(result, L"launch_extended_hresult", static_cast<uint32_t>(launched.ExtendedError().value));
            Require(launched.LaunchResult() == FullTrustLaunchResult::Success, E_FAIL);
            stage = L"waiting-for-ack";
            JsonObject ack{nullptr};
            while (!ack)
            {
                Require(!canceled && Now() < expires && std::chrono::steady_clock::now() < deadline,
                    HRESULT_FROM_WIN32(ERROR_TIMEOUT));
                auto item = co_await job.TryGetItemAsync(L"ack.json");
                if (item) ack = co_await ReadRecord(job, L"ack.json");
                else co_await resume_after(std::chrono::milliseconds(25));
            }
            Validate(ack, context, id, L"ack", Now());
            Require(ack.GetNamedString(L"workflow_hash") == callback.GetNamedString(L"workflow_hash") &&
                Integer(ack, L"ack_ms") < expires && !canceled, E_ACCESSDENIED);
            roundtrip = true;
            stage = L"callback-host-roundtrip-no-pdf";
        }
        catch (...)
        {
            failed = true;
            Number(result, L"hresult", static_cast<uint32_t>(to_hresult().value));
            Flag(result, L"late_launch_not_excluded", true);
            try { if (launchOperation) launchOperation.Cancel(); }
            catch (...) { Number(result, L"launch_cancel_hresult", static_cast<uint32_t>(to_hresult().value)); }
        }
        if (failed)
        {
            try
            {
                if (job && !(co_await job.TryGetItemAsync(L"closed.json")))
                    co_await Publish(job, L"closed.json", activation::Record(L"callback-closed"));
            }
            catch (...) { Number(result, L"close_record_hresult", static_cast<uint32_t>(to_hresult().value)); }
        }
        Text(result, L"stage", stage); Flag(result, L"roundtrip", roundtrip);
        bool completed = false;
        try
        {
            args.CompleteJob(canceled ? PrintWorkflowSubmittedStatus::Canceled : PrintWorkflowSubmittedStatus::Failed);
            completed = true;
        }
        catch (...) { Number(result, L"completion_hresult", static_cast<uint32_t>(to_hresult().value)); }
        Flag(result, L"complete_failed_returned", completed && !canceled);
        try
        {
            if (job) co_await Publish(job, L"callback-result.json", result);
            if (entryLog) co_await FileIO::WriteTextAsync(entryLog, result.Stringify());
        }
        catch (...) { OutputDebugStringW(L"PSA activation probe terminal evidence failed.\n"); }
        Finish();
    }
};
struct Factory : implements<Factory, IActivationFactory>
{ IInspectable ActivateInstance() { return make<ProbeTask>(); } };
extern "C" HRESULT __stdcall DllGetActivationFactory(void* name, void** factory) noexcept
{
    try
    {
        *factory = nullptr;
        hstring value; copy_from_abi(value, name);
        if (value != L"DesktopPdfProbe.Task") return CLASS_E_CLASSNOTAVAILABLE;
        *factory = detach_abi(make<Factory>()); return S_OK;
    }
    catch (...) { return to_hresult(); }
}
extern "C" HRESULT __stdcall DllCanUnloadNow() { return get_module_lock() ? S_FALSE : S_OK; }
