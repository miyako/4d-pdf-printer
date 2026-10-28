#include "../common/RpcClient.h"
#include <atomic>
#include <mutex>
#include <objbase.h>
#include <winrt/Windows.ApplicationModel.Background.h>
#include <winrt/Windows.Devices.Printers.h>
#include <winrt/Windows.Graphics.Printing.Workflow.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>

using namespace winrt;
using namespace desktop;
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::Background;
using namespace Windows::Graphics::Printing::Workflow;
using namespace Windows::Storage;
using namespace Windows::Storage::Streams;
using namespace Windows::Foundation;

struct Task : implements<Task, IBackgroundTask>
{
    BackgroundTaskDeferral deferral{nullptr};
    IBackgroundTaskInstance instance{nullptr};
    PrintWorkflowVirtualPrinterSession session{nullptr};
    event_token dataToken{}, cancelToken{};
    std::atomic<bool> canceled{false}, working{false}, finished{false};
    bool subscribed = false;
    std::mutex lifecycle;
    hstring GetRuntimeClassName() const { return L"DesktopPdf.Task"; }

    void Finish() noexcept
    {
        if (finished.exchange(true)) return;
        std::scoped_lock guard(lifecycle);
        try
        {
            if (subscribed) session.VirtualPrinterDataAvailable(dataToken);
            if (instance) instance.Canceled(cancelToken);
            if (deferral) deferral.Complete();
        }
        catch (...) { OutputDebugStringW(L"Desktop PDF gate lifecycle cleanup failed\n"); }
        deferral = nullptr;
    }

    void Run(IBackgroundTaskInstance const& task)
    {
        instance = task;
        deferral = task.GetDeferral();
        try
        {
            cancelToken = task.Canceled([weak = get_weak()](auto const&, auto const&)
            {
                if (auto self = weak.get())
                {
                    self->canceled = true;
                    if (!self->working.exchange(true)) self->Finish();
                }
            });
            Begin();
        }
        catch (...) { Finish(); throw; }
    }

    fire_and_forget Begin()
    {
        auto lifetime = get_strong();
        JsonObject evidence = Record(L"activation");
        StorageFile log{nullptr};
        bool failed = false;
        try
        {
            auto folder = co_await ApplicationData::Current().LocalFolder().CreateFolderAsync(
                L"Gate", CreationCollisionOption::OpenIfExists);
            log = co_await folder.CreateFileAsync(RandomToken() + L".json", CreationCollisionOption::FailIfExists);
            co_await FileIO::WriteTextAsync(log, evidence.Stringify());
            if (canceled) co_return;
            auto details = instance.TriggerDetails().as<PrintWorkflowVirtualPrinterTriggerDetails>();
            {
                std::scoped_lock guard(lifecycle);
                if (canceled) co_return;
                session = details.VirtualPrinterSession();
                dataToken = session.VirtualPrinterDataAvailable(
                    [self = get_strong()](auto const&, auto const& args) { self->OnData(args); });
                subscribed = true;
            }
            if (canceled) { Finish(); co_return; }
            session.Start();
        }
        catch (...)
        {
            auto code = to_hresult();
            evidence.Insert(L"hresult", JsonValue::CreateNumberValue(code.value));
            failed = true;
        }
        if (failed)
        {
            try { if (log) co_await FileIO::WriteTextAsync(log, evidence.Stringify()); }
            catch (...) { OutputDebugStringW(L"Desktop PDF gate activation evidence failed\n"); }
            Finish();
        }
    }

    fire_and_forget OnData(PrintWorkflowVirtualPrinterDataAvailableEventArgs args)
    {
        auto lifetime = get_strong();
        if (working.exchange(true))
        {
            try { args.CompleteJob(PrintWorkflowSubmittedStatus::Failed); }
            catch (...) { OutputDebugStringW(L"Desktop PDF gate duplicate completion failed\n"); }
            co_return;
        }
        auto evidence = Record(L"metadata");
        StorageFile log{nullptr};
        JsonObject claim;
        bool claimed = false;
        uint64_t bytes = 0;
        try
        {
            auto folder = co_await ApplicationData::Current().LocalFolder().CreateFolderAsync(
                L"Gate", CreationCollisionOption::OpenIfExists);
            log = co_await folder.CreateFileAsync(RandomToken() + L".json", CreationCollisionOption::FailIfExists);
            auto title = std::wstring(args.Configuration().JobTitle());
            Text(evidence, L"titleSha256", Hash(title));
            Text(evidence, L"workflowSha256", Hash(std::wstring(args.Configuration().SessionId())));
            Text(claim, L"title", title);
            Text(claim, L"endpoint", std::wstring(session.Printer().PrinterUri().AbsoluteUri()));
            Text(claim, L"workflowSession", std::wstring(args.Configuration().SessionId()));
            if (session.Printer().PrinterUri().AbsoluteUri() != AutomationUri ||
                args.SourceContent().ContentType() != L"application/oxps" || !IsTitle(title))
                throw hresult_invalid_argument();
            auto identity = CurrentIdentity();
            evidence.Insert(L"callerAppContainer", JsonValue::CreateBooleanValue(identity.appContainer));
            evidence.Insert(L"callerSession", JsonValue::CreateNumberValue(identity.session));
            evidence.Insert(L"expectedPackage", JsonValue::CreateBooleanValue(
                Package::Current().Id().Name() == L"Miyako.DesktopPdfPrototype"));
            APTTYPE apartment{};
            APTTYPEQUALIFIER qualifier{};
            check_hresult(CoGetApartmentType(&apartment, &qualifier));
            evidence.Insert(L"apartmentType", JsonValue::CreateNumberValue(static_cast<int>(apartment)));
            evidence.Insert(L"apartmentQualifier", JsonValue::CreateNumberValue(static_cast<int>(qualifier)));
            evidence.Insert(L"launchResultAvailable", JsonValue::CreateBooleanValue(false));
            Text(evidence, L"stage", L"full-trust-launch-create");
            co_await FileIO::WriteTextAsync(log, evidence.Stringify());
            auto operation = FullTrustProcessLauncher::LaunchFullTrustProcessForCurrentAppWithArgumentsAsync(L"--serve");
            Text(evidence, L"stage", L"full-trust-launch-await-get-results");
            co_await FileIO::WriteTextAsync(log, evidence.Stringify());
            auto launch = co_await operation;
            Text(evidence, L"stage", L"full-trust-launch-result");
            evidence.Insert(L"launchResultAvailable", JsonValue::CreateBooleanValue(true));
            evidence.Insert(L"launchResult", JsonValue::CreateNumberValue(static_cast<int>(launch.LaunchResult())));
            evidence.Insert(L"launchExtendedError", JsonValue::CreateNumberValue(launch.ExtendedError().value));
            if (launch.LaunchResult() != FullTrustLaunchResult::Success)
                throw hresult_error(launch.ExtendedError().value < 0 ? launch.ExtendedError() : hresult(E_FAIL));
            co_await resume_background();
            Text(evidence, L"stage", L"rpc-readiness");
            if (!WaitReady(std::chrono::seconds(10))) throw hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
            evidence.Insert(L"authenticatedRpcReady", JsonValue::CreateBooleanValue(true));
            try { Call(PrepareGate); throw hresult_error(E_UNEXPECTED); }
            catch (hresult_error const& error)
            {
                if (error.code() != E_ACCESSDENIED) throw;
                evidence.Insert(L"desktopMethodDenied", JsonValue::CreateBooleanValue(true));
            }
            Text(evidence, L"stage", L"one-time-claim");
            auto response = Call(ClaimGate, claim);
            Text(claim, L"lease", std::wstring(response.GetNamedString(L"lease")));
            claimed = true;
            evidence.Insert(L"authenticatedClaim", JsonValue::CreateBooleanValue(true));
            Text(evidence, L"stage", L"source");
            auto input = args.SourceContent().GetInputStream();
            Buffer buffer(65536);
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            for (;;)
            {
                if (canceled) throw hresult_canceled();
                if (std::chrono::steady_clock::now() >= deadline) throw hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
                auto read = co_await input.ReadAsync(buffer, buffer.Capacity(), InputStreamOptions::Partial);
                if (!read.Length()) break;
                bytes += read.Length();
                if (bytes > 256ull * 1024 * 1024) throw hresult_error(HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
            }
            input.Close();
            if (!bytes) throw hresult_error(HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
            evidence.Insert(L"sourceBytes", JsonValue::CreateNumberValue(static_cast<double>(bytes)));
            Text(evidence, L"stage", L"gate-verified-no-pdf");
        }
        catch (...)
        {
            evidence.Insert(L"hresult", JsonValue::CreateNumberValue(to_hresult().value));
            claimed = false;
        }
        bool completionReturned = false;
        try
        {
            // This early gate never produces a PDF and must never complete as success.
            args.CompleteJob(canceled ? PrintWorkflowSubmittedStatus::Canceled : PrintWorkflowSubmittedStatus::Failed);
            completionReturned = true;
        }
        catch (...) { evidence.Insert(L"completionHresult", JsonValue::CreateNumberValue(to_hresult().value)); }
        if (claimed && completionReturned && !canceled)
        {
            try
            {
                claim.Insert(L"sourceBytes", JsonValue::CreateNumberValue(static_cast<double>(bytes)));
                claim.Insert(L"completeJobFailedReturned", JsonValue::CreateBooleanValue(true));
                Call(FinishGate, claim);
                evidence.Insert(L"gateAcknowledged", JsonValue::CreateBooleanValue(true));
            }
            catch (...) { evidence.Insert(L"acknowledgmentHresult", JsonValue::CreateNumberValue(to_hresult().value)); }
        }
        try { if (log) co_await FileIO::WriteTextAsync(log, evidence.Stringify()); }
        catch (...) { OutputDebugStringW(L"Desktop PDF gate evidence failed\n"); }
        Finish();
    }
};

struct Factory : implements<Factory, IActivationFactory>
{
    IInspectable ActivateInstance() { return make<Task>(); }
};

extern "C" HRESULT __stdcall DllGetActivationFactory(void* name, void** factory) noexcept
{
    try
    {
        *factory = nullptr;
        hstring className;
        copy_from_abi(className, name);
        if (className != L"DesktopPdf.Task") return CLASS_E_CLASSNOTAVAILABLE;
        *factory = detach_abi(make<Factory>());
        return S_OK;
    }
    catch (...) { return to_hresult(); }
}

extern "C" HRESULT __stdcall DllCanUnloadNow() noexcept
{
    return get_module_lock() ? S_FALSE : S_OK;
}
