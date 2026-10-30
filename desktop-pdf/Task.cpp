#include "Job.h"
#include <atomic>
#include <mutex>
#include <winrt/Windows.ApplicationModel.Background.h>
#include <winrt/Windows.Devices.Printers.h>
#include <winrt/Windows.Graphics.Printing.Workflow.h>
#include <winrt/Windows.Security.Cryptography.Core.h>

using namespace pdf;
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::Background;
using namespace Windows::Graphics::Printing::Workflow;

struct Task : implements<Task, IBackgroundTask> {
    BackgroundTaskDeferral deferral{nullptr};
    IBackgroundTaskInstance instance{nullptr};
    PrintWorkflowVirtualPrinterSession session{nullptr};
    IAsyncOperation<FullTrustProcessLaunchResult> launch{nullptr};
    event_token dataToken{}, cancelToken{};
    std::mutex mutex;
    std::atomic<bool> canceled{false}, working{false}, finished{false};
    bool subscribed = false, cancelSubscribed = false;
    hstring GetRuntimeClassName() const { return L"DesktopPdfIntegration.Task"; }

    void Finish() noexcept {
        if (finished.exchange(true)) return;
        std::scoped_lock lock(mutex);
        try { if (subscribed) session.VirtualPrinterDataAvailable(dataToken); }
        catch (...) { Failure(L"unsubscribe data", to_hresult()); }
        try { if (cancelSubscribed) instance.Canceled(cancelToken); }
        catch (...) { Failure(L"unsubscribe cancellation", to_hresult()); }
        try { if (deferral) deferral.Complete(); }
        catch (...) { Failure(L"task deferral", to_hresult()); }
        deferral = nullptr;
    }
    void Run(IBackgroundTaskInstance const& task) {
        try {
            instance = task; deferral = task.GetDeferral();
            {
                std::scoped_lock lock(mutex);
                cancelToken = task.Canceled([weak = get_weak()](auto const&, auto const&) {
                    if (auto self = weak.get()) {
                        self->canceled = true;
                        if (!self->working.exchange(true)) self->Finish();
                    }
                });
                cancelSubscribed = true;
            }
            Begin();
        } catch (...) { Failure(L"task activation", to_hresult()); Finish(); }
    }
    fire_and_forget Begin() {
        auto lifetime = get_strong();
        try {
            auto details = instance.TriggerDetails().as<PrintWorkflowVirtualPrinterTriggerDetails>();
            {
                std::scoped_lock lock(mutex);
                if (finished || canceled) co_return;
                session = details.VirtualPrinterSession();
                dataToken = session.VirtualPrinterDataAvailable(
                    [self = get_strong()](auto const&, auto const& args) { self->Data(args); });
                subscribed = true;
            }
            session.Start();
            auto deadline = GetTickCount64() + 45000;
            while (!working && !finished && GetTickCount64() < deadline)
                co_await resume_after(std::chrono::milliseconds(25));
            if (!working.exchange(true) && !finished) {
                Failure(L"data callback deadline", HRESULT_FROM_WIN32(ERROR_TIMEOUT));
                Finish();
            }
        } catch (...) { Failure(L"session start", to_hresult()); if (!working.exchange(true)) Finish(); }
    }
    fire_and_forget Data(PrintWorkflowVirtualPrinterDataAvailableEventArgs args) {
        auto lifetime = get_strong();
        if (working.exchange(true)) {
            try { args.CompleteJob(PrintWorkflowSubmittedStatus::Failed); }
            catch (...) { Failure(L"duplicate completion", to_hresult()); }
            co_return;
        }
        StorageFolder job{nullptr}, registrationFolder{nullptr};
        std::wstring id, registration, stage = L"metadata";
        Mode mode = Mode::SaveAs;
        bool commitStarted = false, published = false;
        auto status = PrintWorkflowSubmittedStatus::Failed;
        std::wstring outcome = L"failed";
        hresult primary{};
        try {
            id = RandomId();
            auto uri = session.Printer().PrinterUri().AbsoluteUri();
            Require(uri == L"desktop-pdf:save-as" || uri == L"desktop-pdf:silent", "unknown endpoint");
            mode = uri == L"desktop-pdf:save-as" ? Mode::SaveAs : Mode::Silent;
            Require(args.SourceContent().ContentType() == L"application/oxps", "OXPS required");
            auto config = args.Configuration();
            Require(!config.SessionId().empty(), "missing session");
            auto timeout = DefaultCompletionMs, deadline = Deadline(Now(), timeout);
            if (mode == Mode::Silent) {
                registration = Registration(std::wstring_view(config.JobTitle()));
                registrationFolder = co_await Folder(L"Registrations", registration);
                auto registered = co_await Read(registrationFolder, L"registered.json");
                RegisteredRecord(registered, registration);
                Require(!(co_await Exists(registrationFolder, L"cancel.json")), "registration canceled");
                auto submitted = co_await Read(registrationFolder, L"submitted.json");
                timeout = Integer(registered, L"timeout");
                deadline = SubmissionDeadline(submitted, registered, registration);
                auto now = Now();
                Require(deadline > now && deadline - now <= timeout, "submission budget expired/invalid");
            }
            job = co_await Folder(L"Jobs", id, true);
            if (mode == Mode::Silent) {
                auto claim = co_await registrationFolder.CreateFolderAsync(L"claim", CreationCollisionOption::FailIfExists);
                auto binding = Record(registration, mode, L"binding"); Text(binding, L"job", id);
                co_await Publish(claim, L"binding.json", binding);
            }
            auto tickEnd = GetTickCount64() + Remaining(Now(), deadline, timeout);
            auto stop = [&] { return canceled || Expired(Now(), deadline) || GetTickCount64() >= tickEnd; };
            StorageFile target{nullptr};
            std::wstring destination;
            if (mode == Mode::SaveAs) {
                stage = L"system target";
                target = co_await args.GetTargetFileAsync();
                Require(target != nullptr && _wcsicmp(target.FileType().c_str(), L".pdf") == 0 && !target.Path().empty(),
                    "local PDF target required");
                Require(LocalProvider(target.Provider()), "cloud/network target provider unsupported");
                destination = std::wstring(target.Path());
            } else {
                auto registered = co_await Read(registrationFolder, L"registered.json");
                RegisteredRecord(registered, registration);
                destination = std::wstring(registered.GetNamedString(L"destination"));
                Require(!(co_await Exists(registrationFolder, L"cancel.json")), "canceled before capture");
            }
            stage = L"capture";
            auto source = co_await job.CreateFileAsync(L"source.partial", CreationCollisionOption::FailIfExists);
            auto input = args.SourceContent().GetInputStream();
            auto output = co_await source.OpenAsync(FileAccessMode::ReadWrite);
            auto captureEnd = GetTickCount64() + 90000;
            auto bytes = co_await Copy(input, output, [&] { return stop() || GetTickCount64() >= captureEnd; });
            input.Close(); output.Close();
            co_await source.RenameAsync(L"source.oxps", NameCollisionOption::FailIfExists);
            auto request = Record(id, mode, L"request");
            Text(request, L"session", config.SessionId()); Text(request, L"registration", registration);
            Text(request, L"destination", destination); Number(request, L"deadline", deadline);
            Number(request, L"timeout", timeout); Number(request, L"sourceBytes", bytes);
            co_await Publish(job, L"request.json", request);
            if (stop()) throw hresult_canceled();
            stage = L"fixed full-trust launch";
            launch = FullTrustProcessLauncher::LaunchFullTrustProcessForCurrentAppWithArgumentsAsync(
                L"--mode " + hstring(Name(mode)) + L" --job " + hstring(id));
            auto launchEnd = GetTickCount64() + 15000;
            while (launch.Status() == AsyncStatus::Started && !stop() && GetTickCount64() < launchEnd)
                co_await resume_after(std::chrono::milliseconds(25));
            if (launch.Status() == AsyncStatus::Started) {
                launch.Cancel();
                throw hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT), L"launch pending; late launch remains possible");
            }
            auto result = launch.GetResults();
            Require(result.LaunchResult() == FullTrustLaunchResult::Success, "fixed host launch failed");
            stage = L"host/serialization/worker";
            bool cancelSent = false;
            auto cleanupEnd = std::uint64_t{0};
            while (!(co_await Exists(job, L"result.json"))) {
                bool regCanceled = registrationFolder && (co_await Exists(registrationFolder, L"cancel.json"));
                if (stop() || regCanceled) {
                    canceled = true;
                    if (!cancelSent) {
                        co_await Publish(job, L"cancel.json", Record(id, mode, L"cancel"));
                        cancelSent = true; cleanupEnd = GetTickCount64() + 5000;
                    }
                    if (GetTickCount64() >= cleanupEnd) throw hresult_canceled();
                }
                if (mode == Mode::SaveAs && !commitStarted && !cancelSent && (co_await Exists(job, L"ready.json"))) {
                    stage = L"interactive transaction";
                    auto ready = co_await Read(job, L"ready.json");
                    Validate(ready, id, mode, L"ready");
                    Fields(ready, {L"schema", L"version", L"id", L"mode", L"kind", L"bytes", L"sha256"});
                    auto pdf = co_await job.GetFileAsync(L"worker.pdf");
                    auto stream = co_await pdf.OpenReadAsync();
                    auto transaction = co_await target.OpenTransactedWriteAsync();
                    auto hash = Windows::Security::Cryptography::Core::HashAlgorithmProvider::OpenAlgorithm(L"SHA256").CreateHash();
                    transaction.Stream().Size(0);
                    auto count = co_await Copy(stream, transaction.Stream(), stop,
                        [&](IBuffer const& buffer) { hash.Append(buffer); });
                    stream.Close();
                    Require(count == Integer(ready, L"bytes") &&
                        Windows::Security::Cryptography::CryptographicBuffer::EncodeToHexString(hash.GetValueAndReset()) ==
                        ready.GetNamedString(L"sha256"), "staged PDF mismatch");
                    transaction.Stream().Size(count);
                    if (stop()) throw hresult_canceled();
                    auto intent = Record(id, mode, L"publish-intent");
                    Number(intent, L"bytes", count); Text(intent, L"sha256", ready.GetNamedString(L"sha256"));
                    co_await Publish(job, L"publish-intent.json", intent);
                    if (stop()) throw hresult_canceled();
                    commitStarted = true;
                    co_await transaction.CommitAsync();
                    transaction.Stream().Close(); transaction.Close();
                    auto publication = Record(id, mode, L"published");
                    Number(publication, L"bytes", count); Text(publication, L"sha256", ready.GetNamedString(L"sha256"));
                    co_await Publish(job, L"publication.json", publication);
                    published = true;
                }
                co_await resume_after(std::chrono::milliseconds(25));
            }
            auto terminal = co_await Read(job, L"result.json");
            Validate(terminal, id, mode, L"result");
            outcome = std::wstring(terminal.GetNamedString(L"outcome"));
            if (outcome == L"published") {
                Fields(terminal, {L"schema", L"version", L"id", L"mode", L"kind", L"outcome", L"stage", L"bytes", L"sha256"});
                auto publication = co_await Read(job, L"publication.json");
                Validate(publication, id, mode, L"published");
                Require(terminal.GetNamedString(L"stage") == L"complete", "incomplete host outcome");
                if (mode == Mode::SaveAs) {
                    Fields(publication, {L"schema", L"version", L"id", L"mode", L"kind", L"bytes", L"sha256"});
                    Require(Integer(publication, L"bytes") == Integer(terminal, L"bytes") &&
                        publication.GetNamedString(L"sha256") == terminal.GetNamedString(L"sha256"), "publication result mismatch");
                } else Fields(publication, {L"schema", L"version", L"id", L"mode", L"kind"});
                published = true; status = PrintWorkflowSubmittedStatus::Succeeded; outcome = L"succeeded";
            } else {
                Fields(terminal, {L"schema", L"version", L"id", L"mode", L"kind", L"outcome", L"stage", L"hresult"});
                Require(outcome == L"failed" || outcome == L"indeterminate" ||
                    outcome == L"canceled-or-expired", "invalid host outcome");
                auto error = Integer(terminal, L"hresult");
                Require(error <= 0xffffffffull, "invalid host HRESULT");
                primary = hresult{static_cast<std::int32_t>(static_cast<std::uint32_t>(error))};
                Require(primary.value < 0, "success HRESULT on failed host result");
                stage = std::wstring(terminal.GetNamedString(L"stage"));
                if (outcome == L"canceled-or-expired") status = PrintWorkflowSubmittedStatus::Canceled;
            }
        } catch (...) {
            primary = to_hresult(); Failure(stage.c_str(), primary);
            outcome = commitStarted || published ? L"indeterminate" :
                canceled || primary == hresult_canceled().code() ? L"canceled-or-expired" : L"failed";
            status = outcome == L"canceled-or-expired" ? PrintWorkflowSubmittedStatus::Canceled : PrintWorkflowSubmittedStatus::Failed;
        }
        if (primary.value < 0 && job) {
            try {
                if (!(co_await Exists(job, L"cancel.json")))
                    co_await Publish(job, L"cancel.json", Record(id, mode, L"cancel"));
                if (commitStarted && !(co_await Exists(job, L"publication.json")))
                    co_await Publish(job, L"publication-unknown.json", Record(id, mode, L"publication-unknown"));
                if (mode == Mode::Silent && (co_await Exists(job, L"publish-intent.json"))) outcome = L"indeterminate";
            } catch (...) { Failure(L"failure record", to_hresult()); }
        }
        if (outcome == L"indeterminate") status = PrintWorkflowSubmittedStatus::Failed;
        try {
            args.CompleteJob(status);
        } catch (...) {
            primary = to_hresult(); Failure(L"CompleteJob", primary);
            stage = L"CompleteJob";
            outcome = published || commitStarted || outcome == L"indeterminate" ? L"saved-or-unknown-acknowledgement" : L"completion-failed";
        }
        if (job) {
            try {
                auto completion = Record(id, mode, L"completion");
                Text(completion, L"outcome", outcome); Text(completion, L"stage", stage);
                Number(completion, L"hresult", static_cast<std::uint32_t>(primary.value));
                co_await Publish(job, L"completion.json", completion);
            } catch (...) { Failure(L"completion record", to_hresult()); }
        }
        Finish();
    }
};
struct Factory : implements<Factory, IActivationFactory> {
    IInspectable ActivateInstance() { return make<Task>(); }
};
extern "C" HRESULT __stdcall DllGetActivationFactory(void* name, void** factory) noexcept {
    try {
        *factory = nullptr; hstring className; copy_from_abi(className, name);
        if (className != L"DesktopPdfIntegration.Task") return CLASS_E_CLASSNOTAVAILABLE;
        *factory = detach_abi(make<Factory>()); return S_OK;
    } catch (...) { return to_hresult(); }
}
extern "C" HRESULT __stdcall DllCanUnloadNow() noexcept { return get_module_lock() ? S_FALSE : S_OK; }
