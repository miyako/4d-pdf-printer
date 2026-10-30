#include "Records.h"
#include <atomic>
#include <mutex>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.ApplicationModel.Background.h>
#include <winrt/Windows.Devices.Printers.h>
#include <winrt/Windows.Graphics.Printing.Workflow.h>

using namespace smoke;
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::Background;
using namespace Windows::Graphics::Printing::Workflow;

struct Task : implements<Task, IBackgroundTask>
{
    BackgroundTaskDeferral deferral{nullptr};
    IBackgroundTaskInstance instance{nullptr};
    PrintWorkflowVirtualPrinterSession session{nullptr};
    IAsyncOperation<FullTrustProcessLaunchResult> launch{nullptr};
    event_token dataToken{}, cancelToken{};
    std::mutex mutex;
    std::atomic<bool> canceled{false}, working{false}, finished{false};
    bool subscribed = false, cancelSubscribed = false;
    hstring GetRuntimeClassName() const { return L"PsaActivationSmoke.Task"; }

    void Finish() noexcept
    {
        if (finished.exchange(true)) return;
        std::scoped_lock lock(mutex);
        try { if (subscribed) session.VirtualPrinterDataAvailable(dataToken); }
        catch (...) { Failure(L"VirtualPrinterDataAvailable unsubscribe", to_hresult()); }
        try { if (cancelSubscribed) instance.Canceled(cancelToken); }
        catch (...) { Failure(L"Canceled unsubscribe", to_hresult()); }
        try { if (deferral) deferral.Complete(); }
        catch (...) { Failure(L"BackgroundTaskDeferral.Complete", to_hresult()); }
        deferral = nullptr;
    }
    void CancelLaunch() noexcept
    {
        try { std::scoped_lock lock(mutex); if (launch) launch.Cancel(); }
        catch (...) { Failure(L"launch.Cancel (late launch remains possible)", to_hresult()); }
    }
    void Run(IBackgroundTaskInstance const& task)
    {
        wchar_t const* stage = L"GetDeferral";
        try
        {
            instance = task; deferral = task.GetDeferral();
            stage = L"IBackgroundTaskInstance.Canceled subscribe";
            {
                std::scoped_lock lock(mutex);
                cancelToken = task.Canceled([weak = get_weak()](auto const&, auto const&)
                {
                    if (auto self = weak.get())
                    {
                        self->canceled = true; self->CancelLaunch();
                        if (!self->working.exchange(true)) self->Finish();
                    }
                });
                cancelSubscribed = true;
            }
            Begin();
        }
        catch (...) { Failure(stage, to_hresult()); if (!working.exchange(true)) Finish(); }
    }
    fire_and_forget Begin()
    {
        auto lifetime = get_strong();
        wchar_t const* stage = L"PrintWorkflowVirtualPrinterTriggerDetails";
        try
        {
            auto details = instance.TriggerDetails().as<PrintWorkflowVirtualPrinterTriggerDetails>();
            {
                std::scoped_lock lock(mutex);
                if (finished) co_return;
                stage = L"VirtualPrinterSession/VirtualPrinterDataAvailable subscribe";
                session = details.VirtualPrinterSession();
                dataToken = session.VirtualPrinterDataAvailable(
                    [self = get_strong()](auto const&, auto const& args) { self->Data(args); });
                subscribed = true;
            }
            if (canceled) { Finish(); co_return; }
            stage = L"VirtualPrinterSession.Start"; session.Start();
            auto deadline = GetTickCount64() + 45000;
            while (!working && !finished && GetTickCount64() < deadline)
                co_await resume_after(std::chrono::milliseconds(25));
            if (!working.exchange(true) && !finished)
            {
                Failure(L"VirtualPrinterDataAvailable deadline", HRESULT_FROM_WIN32(ERROR_TIMEOUT));
                Finish();
            }
        }
        catch (...) { Failure(stage, to_hresult()); if (!working.exchange(true)) Finish(); }
    }
    fire_and_forget Data(PrintWorkflowVirtualPrinterDataAvailableEventArgs args)
    {
        auto lifetime = get_strong();
        if (working.exchange(true))
        {
            try { args.CompleteJob(PrintWorkflowSubmittedStatus::Failed); }
            catch (...) { Failure(L"duplicate CompleteJob(Failed)", to_hresult()); }
            co_return;
        }
        StorageFolder folder{nullptr};
        JsonObject result;
        wchar_t const* stage = L"Configuration.JobTitle/strict ID parsing";
        try
        {
            auto config = args.Configuration();
            auto id = ParseId(std::wstring_view(config.JobTitle()), TitlePrefix);
            result = Record(id, L"callback-result");
            Flag(result, L"actualCallbackObserved", true);
            Text(result, L"workflowSessionId", config.SessionId());
            Text(result, L"launchResult", L"not-attempted");
            Flag(result, L"launcherAttempted", false);
            bool loggingFailed = false;
            try
            {
                stage = L"callback ApplicationData.Current.LocalCacheFolder";
                folder = co_await Folder(id);
                Text(result, L"contentType", args.SourceContent().ContentType());
                Text(result, L"printerUri", session.Printer().PrinterUri().AbsoluteUri());
                stage = L"publish actual callback.json";
                co_await Publish(folder, L"callback.json", result);
            }
            catch (...) { loggingFailed = true; Failure(stage, to_hresult()); }
            stage = L"FullTrustProcessLauncher.LaunchFullTrustProcessForCurrentAppWithArgumentsAsync";
            if (canceled) throw hresult_canceled();
            {
                std::scoped_lock lock(mutex);
                if (canceled) throw hresult_canceled();
                Flag(result, L"launcherAttempted", true);
                OutputDebugStringW(L"PSA activation smoke: attempting FullTrustProcessLauncher from actual data callback\n");
                launch = FullTrustProcessLauncher::LaunchFullTrustProcessForCurrentAppWithArgumentsAsync(
                    L"--test-id " + id);
            }
            auto deadline = GetTickCount64() + 15000;
            stage = L"FullTrust launch async deadline/status";
            while (launch.Status() == AsyncStatus::Started && !canceled && GetTickCount64() < deadline)
                co_await resume_after(std::chrono::milliseconds(25));
            if (canceled || launch.Status() == AsyncStatus::Started)
            {
                CancelLaunch();
                Flag(result, L"lateLaunchNotExcluded", true);
                throw hresult_error(HRESULT_FROM_WIN32(canceled ? ERROR_CANCELLED : ERROR_TIMEOUT));
            }
            stage = L"FullTrust launch GetResults/LaunchResult/ExtendedError";
            auto launched = launch.GetResults();
            Number(result, L"launchResultRaw", static_cast<int32_t>(launched.LaunchResult()));
            Number(result, L"extendedHresult", launched.ExtendedError().value);
            Text(result, L"launchResult", launched.LaunchResult() == FullTrustLaunchResult::Success ?
                L"Success" : L"non-success");
            Flag(result, L"loggingFailed", loggingFailed);
            if (launched.LaunchResult() != FullTrustLaunchResult::Success)
                Failure(stage, launched.ExtendedError());
        }
        catch (...)
        {
            auto hr = to_hresult(); Failure(stage, hr);
            Number(result, L"hresult", hr.value); Text(result, L"failureStage", stage);
            if (result.HasKey(L"launcherAttempted") && result.GetNamedBoolean(L"launcherAttempted"))
                Flag(result, L"lateLaunchNotExcluded", true);
        }
        try
        {
            args.CompleteJob(canceled ? PrintWorkflowSubmittedStatus::Canceled : PrintWorkflowSubmittedStatus::Failed);
            Text(result, L"printCompletion", canceled ? L"Canceled" : L"Failed");
        }
        catch (...) { Failure(L"CompleteJob(Failed/Canceled)", to_hresult()); }
        try { if (folder) co_await Publish(folder, L"launch.json", result); }
        catch (...) { Failure(L"publish launch.json", to_hresult()); }
        Finish();
    }
};

struct Factory : implements<Factory, IActivationFactory>
{ IInspectable ActivateInstance() { return make<Task>(); } };
extern "C" HRESULT __stdcall DllGetActivationFactory(void* name, void** factory) noexcept
{
    try
    {
        *factory = nullptr;
        hstring className; copy_from_abi(className, name);
        if (className != L"PsaActivationSmoke.Task") return CLASS_E_CLASSNOTAVAILABLE;
        *factory = detach_abi(make<Factory>()); return S_OK;
    }
    catch (...) { return to_hresult(); }
}
extern "C" HRESULT __stdcall DllCanUnloadNow() noexcept
{ return get_module_lock() ? S_FALSE : S_OK; }
