#include <windows.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <winrt/Windows.ApplicationModel.Background.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.Devices.Printers.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Printing.PrintTicket.h>
#include <winrt/Windows.Graphics.Printing.Workflow.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Security.Cryptography.Core.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>

using namespace winrt;
using namespace Windows::ApplicationModel::Background;
using namespace Windows::Data::Json;
using namespace Windows::Graphics::Printing::Workflow;
using namespace Windows::Security::Cryptography;
using namespace Windows::Security::Cryptography::Core;
using namespace Windows::Storage;
using namespace Windows::Storage::Streams;
using namespace Windows::Foundation;

namespace
{
    hstring RandomId()
    {
        return CryptographicBuffer::EncodeToHexString(CryptographicBuffer::GenerateRandom(16));
    }

    hstring Hash(hstring const& text)
    {
        auto algorithm = HashAlgorithmProvider::OpenAlgorithm(HashAlgorithmNames::Sha256());
        return CryptographicBuffer::EncodeToHexString(algorithm.HashData(
            CryptographicBuffer::ConvertStringToBinary(text, BinaryStringEncoding::Utf8)));
    }

    void Text(JsonObject const& json, wchar_t const* key, hstring const& value)
    {
        json.Insert(key, JsonValue::CreateStringValue(value));
    }

    void DebugFailure(wchar_t const* stage, hresult code) noexcept
    {
        wchar_t buffer[160]{};
        swprintf_s(buffer, L"PSA diagnostic: %s HRESULT=0x%08X\n", stage,
            static_cast<unsigned int>(code));
        OutputDebugStringW(buffer);
    }

    IAsyncOperation<uint64_t> CopyBounded(IInputStream input, IOutputStream output,
        std::atomic<bool> const* canceled)
    {
        auto start = std::chrono::steady_clock::now();
        uint64_t total = 0;
        Buffer buffer(65536);
        for (;;)
        {
            if (canceled->load()) throw hresult_canceled();
            if (std::chrono::steady_clock::now() - start > std::chrono::seconds(90))
                throw hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
            auto read = co_await input.ReadAsync(buffer, buffer.Capacity(), InputStreamOptions::Partial);
            if (read.Length() == 0) break;
            total += read.Length();
            if (total > 256ull * 1024 * 1024)
                throw hresult_error(HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
            if (co_await output.WriteAsync(read) != read.Length()) throw hresult_error(E_FAIL);
        }
        if (!(co_await output.FlushAsync())) throw hresult_error(E_FAIL);
        co_return total;
    }
}

struct Task : implements<Task, IBackgroundTask>
{
    BackgroundTaskDeferral deferral{nullptr};
    IBackgroundTaskInstance instance{nullptr};
    PrintWorkflowVirtualPrinterSession session{nullptr};
    event_token dataToken{}, cancelToken{};
    bool subscribed = false;
    std::mutex lifecycle;
    std::atomic<bool> finishStarted{false};
    std::atomic<bool> canceled{false};
    // The host may reuse a process; this object accepts only one job per activation.
    enum State { Waiting, Working, Finished };
    std::atomic<State> state{Waiting};
    StorageFolder activation{nullptr};
    hstring activationId;
    hstring GetRuntimeClassName() const { return L"PsaDiagnostic.Task"; }

    void Finish() noexcept
    {
        if (finishStarted.exchange(true)) return;
        std::scoped_lock guard(lifecycle);
        try
        {
            if (subscribed) session.VirtualPrinterDataAvailable(dataToken);
            if (instance) instance.Canceled(cancelToken);
        }
        catch (...) { DebugFailure(L"unsubscribe", to_hresult()); }
        if (deferral)
        {
            try { deferral.Complete(); }
            catch (...) { DebugFailure(L"task-deferral", to_hresult()); }
            deferral = nullptr;
        }
    }

    void Run(IBackgroundTaskInstance const& task)
    {
        instance = task;
        deferral = task.GetDeferral();
        cancelToken = task.Canceled([weak = get_weak()](auto const&, auto const&)
        {
            if (auto self = weak.get())
            {
                self->canceled = true;
                State expected = Waiting;
                if (self->state.compare_exchange_strong(expected, Finished)) self->Finish();
            }
        });
        Begin();
    }

    fire_and_forget Begin()
    {
        auto lifetime = get_strong();
        try
        {
            activationId = RandomId();
            auto captures = co_await ApplicationData::Current().LocalFolder().CreateFolderAsync(
                L"Captures", CreationCollisionOption::OpenIfExists);
            activation = co_await captures.CreateFolderAsync(activationId, CreationCollisionOption::FailIfExists);
            JsonObject info;
            Text(info, L"schema", L"psa-activation-v1");
            Text(info, L"activationId", activationId);
            Text(info, L"utcFileTime", std::to_wstring(clock::now().time_since_epoch().count()));
            info.Insert(L"processId", JsonValue::CreateNumberValue(GetCurrentProcessId()));
            auto log = co_await activation.CreateFileAsync(L"activation.json", CreationCollisionOption::FailIfExists);
            co_await FileIO::WriteTextAsync(log, info.Stringify());
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
            if (!canceled) session.Start();
        }
        catch (...)
        {
            DebugFailure(L"activation", to_hresult());
            state = Finished;
            Finish();
        }
    }

    fire_and_forget OnData(PrintWorkflowVirtualPrinterDataAvailableEventArgs args)
    {
        auto lifetime = get_strong();
        State expected = Waiting;
        if (!state.compare_exchange_strong(expected, Working))
        {
            try { args.CompleteJob(PrintWorkflowSubmittedStatus::Failed); }
            catch (...) { DebugFailure(L"duplicate-job-completion", to_hresult()); }
            co_return;
        }
        auto status = PrintWorkflowSubmittedStatus::Failed;
        try { co_await Capture(args, &status); }
        catch (...)
        {
            DebugFailure(L"capture-boundary", to_hresult());
            status = canceled ? PrintWorkflowSubmittedStatus::Canceled : PrintWorkflowSubmittedStatus::Failed;
        }
        if (canceled) status = PrintWorkflowSubmittedStatus::Canceled;
        try
        {
            args.CompleteJob(status);
            auto completed = co_await activation.CreateFileAsync(L"completion.txt", CreationCollisionOption::FailIfExists);
            co_await FileIO::WriteTextAsync(completed,
                status == PrintWorkflowSubmittedStatus::Succeeded ? L"CompleteJob returned: succeeded (diagnostic OXPS only)" :
                status == PrintWorkflowSubmittedStatus::Canceled ? L"CompleteJob returned: canceled" :
                L"CompleteJob returned: failed");
        }
        catch (...) { DebugFailure(L"job-completion", to_hresult()); }
        state = Finished;
        Finish();
    }

    IAsyncAction Capture(PrintWorkflowVirtualPrinterDataAvailableEventArgs args,
        PrintWorkflowSubmittedStatus* requested)
    {
        auto& status = *requested;
        StorageFolder lock{nullptr};
        JsonObject record;
        Text(record, L"schema", L"psa-capture-v1");
        Text(record, L"activationId", activationId);
        Text(record, L"utcFileTime", std::to_wstring(clock::now().time_since_epoch().count()));
        Text(record, L"result", L"failed");
        hstring stage = L"metadata";
        try
        {
            auto config = args.Configuration();
            Text(record, L"workflowSessionSha256", Hash(config.SessionId()));
            Text(record, L"jobTitleSha256", Hash(config.JobTitle()));
            auto uri = session.Printer().PrinterUri().AbsoluteUri();
            bool interactive = uri == L"psa-diagnostic:interactive";
            bool automation = uri == L"psa-diagnostic:automation";
            if (!interactive && !automation) throw hresult_invalid_argument();
            Text(record, L"endpoint", interactive ? L"interactive" : L"automation");
            record.Insert(L"uiLaunchEnabled", JsonValue::CreateBooleanValue(args.UILauncher().IsUILaunchEnabled()));
            auto content = args.SourceContent();
            Text(record, L"contentType", content.ContentType());
            if (content.ContentType() != L"application/oxps") throw hresult_error(E_NOTIMPL);
            stage = L"serialization-lock";
            lock = co_await ApplicationData::Current().LocalFolder().CreateFolderAsync(
                L"CaptureLock", CreationCollisionOption::FailIfExists);
            stage = L"print-ticket";
            auto ticket = co_await activation.CreateFileAsync(L"ticket.xml", CreationCollisionOption::FailIfExists);
            co_await FileIO::WriteTextAsync(ticket, args.GetJobPrintTicket().XmlNode().GetXml());
            stage = L"source-capture";
            auto file = co_await activation.CreateFileAsync(L"source.partial", CreationCollisionOption::FailIfExists);
            {
                auto input = content.GetInputStream();
                auto output = co_await file.OpenAsync(FileAccessMode::ReadWrite);
                uint64_t bytes = co_await CopyBounded(input, output, &canceled);
                record.Insert(L"sourceBytes", JsonValue::CreateNumberValue(static_cast<double>(bytes)));
                input.Close();
                output.Close();
            }
            co_await file.RenameAsync(L"source.oxps", NameCollisionOption::FailIfExists);
            stage = L"target-file";
            Text(record, L"lastStage", stage);
            auto pending = co_await activation.CreateFileAsync(L"before-target.json",
                CreationCollisionOption::FailIfExists);
            co_await FileIO::WriteTextAsync(pending, record.Stringify());
            // Both queues deliberately probe this API. Omission of OutputFileTypes is not routing.
            auto target = co_await args.GetTargetFileAsync();
            record.Insert(L"targetAvailable", JsonValue::CreateBooleanValue(target != nullptr));
            if (interactive)
            {
                if (!target || target.FileType() != L".oxps") throw hresult_invalid_argument();
                stage = L"diagnostic-output";
                auto input = co_await file.OpenReadAsync();
                auto output = co_await target.OpenAsync(FileAccessMode::ReadWrite);
                output.Size(0);
                co_await CopyBounded(input, output, &canceled);
                input.Close();
                output.Close();
                status = PrintWorkflowSubmittedStatus::Succeeded;
                Text(record, L"result", L"diagnostic-oxps-written");
            }
            else
            {
                // No renderer/coordinator is attached. Capturing evidence is NOT a successful print.
                Text(record, L"result", L"captured-no-renderer");
            }
        }
        catch (...)
        {
            auto code = to_hresult();
            if (canceled) status = PrintWorkflowSubmittedStatus::Canceled;
            record.Insert(L"hresult", JsonValue::CreateNumberValue(static_cast<int32_t>(code)));
            DebugFailure(stage.c_str(), code);
        }
        Text(record, L"lastStage", stage);
        if (canceled) status = PrintWorkflowSubmittedStatus::Canceled;
        Text(record, L"completionRequested", status == PrintWorkflowSubmittedStatus::Succeeded ? L"succeeded" :
            status == PrintWorkflowSubmittedStatus::Canceled ? L"canceled" : L"failed");
        try
        {
            auto log = co_await activation.CreateFileAsync(L"job.json", CreationCollisionOption::FailIfExists);
            co_await FileIO::WriteTextAsync(log, record.Stringify());
        }
        catch (...)
        {
            DebugFailure(L"evidence-write", to_hresult());
            status = PrintWorkflowSubmittedStatus::Failed;
        }
        if (lock)
        {
            try { co_await lock.DeleteAsync(StorageDeleteOption::PermanentDelete); }
            catch (...)
            {
                DebugFailure(L"lock-cleanup", to_hresult());
                status = PrintWorkflowSubmittedStatus::Failed;
            }
        }
    }
};

struct Factory : implements<Factory, IActivationFactory>
{
    IInspectable ActivateInstance() { return make<Task>(); }
};

extern "C" HRESULT __stdcall DllGetActivationFactory(HSTRING name, void** factory) noexcept
{
    try
    {
        *factory = nullptr;
        if (hstring(name, copy_from_abi) != L"PsaDiagnostic.Task") return CLASS_E_CLASSNOTAVAILABLE;
        *factory = detach_abi(make<Factory>());
        return S_OK;
    }
    catch (...) { return to_hresult(); }
}

extern "C" HRESULT __stdcall DllCanUnloadNow() noexcept
{
    return get_module_lock() ? S_FALSE : S_OK;
}
