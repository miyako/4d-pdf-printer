#pragma once
#include "../Task.cpp"
#include <filesystem>

namespace pdf::tests {
inline handle TestSignal() {
    handle value(CreateEventExW(nullptr, nullptr, CREATE_EVENT_MANUAL_RESET, EVENT_ALL_ACCESS));
    check_bool(static_cast<bool>(value)); return value;
}
inline void WaitTest(HANDLE event, DWORD timeout = 6000) {
    Require(WaitForSingleObject(event, timeout) == WAIT_OBJECT_0, "bounded native journal test wait");
}
inline void AwaitTest(IAsyncAction const& action) {
    auto done = std::make_shared<handle>(TestSignal());
    action.Completed([done](auto const&, auto) { SetEvent(done->get()); });
    WaitTest(done->get());
    action.GetResults();
}
inline void ReleasedTest(std::weak_ptr<activation::Trace> const& trace) {
    auto end = GetTickCount64() + 5000;
    while (!trace.expired() && GetTickCount64() < end) Sleep(1);
    Require(trace.expired(), "bounded test observation of actual coroutine cleanup");
}
struct TrialDirectory {
    std::filesystem::path path;
    TrialDirectory() {
        path = std::filesystem::temp_directory_path() / (L"DesktopPdfJournalTest-" + RandomId());
        Require(std::filesystem::create_directory(path), "fresh owned native test directory");
    }
    ~TrialDirectory() noexcept {
        std::error_code error;
        std::filesystem::remove_all(path, error);
        if (error) Failure(L"owned journal test directory cleanup", hresult{HRESULT_FROM_WIN32(error.value())});
    }
};
struct TrialIo : activation::JournalIo, std::enable_shared_from_this<TrialIo> {
    StorageFolder root{nullptr};
    std::wstring path;
    activation::Stage fault{activation::Stage::None};
    bool pauseWrite{}, pauseReport{}, failReport{};
    handle entered = TestSignal(), release = TestSignal(), reportEntered = TestSignal(), reportRelease = TestSignal();
    handle reportExited = TestSignal(), writeExited = TestSignal();
    std::atomic<bool> reported{false};
    std::string receipt;
    explicit TrialIo(std::wstring folder) : path(std::move(folder)) {
        root = StorageFolder::GetFolderFromPathAsync(path).get();
    }
    StorageFolder Cache() override { return root; }
    IAsyncOperation<StorageFolder> Folder(StorageFolder const& parent, hstring const& name,
        CreationCollisionOption collision) override {
        auto stage = name == L"DesktopPdf" ? activation::Stage::Root :
            name == L"ActivationDiagnostics" ? activation::Stage::Parent : activation::Stage::Folder;
        if (fault == stage) throw hresult_error(E_ACCESSDENIED);
        return JournalIo::Folder(parent, name, collision);
    }
    IAsyncOperation<StorageFile> Create(StorageFolder const& parent, hstring const& name) override {
        if (fault == activation::Stage::Create) throw hresult_error(E_ACCESSDENIED);
        return JournalIo::Create(parent, name);
    }
    static IAsyncAction PausedWrite(std::shared_ptr<TrialIo> self, StorageFile file, hstring text) {
        activation::ModuleLifetime lifetime;
        activation::FinalAttempt done([&]() noexcept { SetEvent(self->writeExited.get()); });
        SetEvent(self->entered.get());
        co_await resume_on_signal(self->release.get());
        co_await FileIO::WriteTextAsync(file, text, UnicodeEncoding::Utf8);
    }
    IAsyncAction Write(StorageFile const& file, hstring const& text) override {
        if (fault == activation::Stage::Write) throw hresult_error(E_ACCESSDENIED);
        if (pauseWrite) return PausedWrite(shared_from_this(), file, text);
        return JournalIo::Write(file, text);
    }
    IAsyncAction Rename(StorageFile const& file, hstring const& name) override {
        if (fault == activation::Stage::Rename) throw hresult_error(E_ACCESSDENIED);
        return JournalIo::Rename(file, name);
    }
    activation::ReportResult Report(std::wstring_view id, std::string_view bytes) override {
        activation::FinalAttempt done([&]() noexcept { SetEvent(reportExited.get()); });
        SetEvent(reportEntered.get());
        if (pauseReport) WaitTest(reportRelease.get());
        receipt = bytes;
        activation::ReportResult result;
        if (failReport) result = {activation::ReportStage::Create, ERROR_ACCESS_DENIED};
        else {
            activation::NativeReportBackend backend(path, id);
            result = activation::PublishTerminal(backend, bytes);
        }
        reported = result.Published();
        return result;
    }
};
struct TestDeferral : implements<TestDeferral, IBackgroundTaskDeferral> {
    std::atomic<unsigned>& calls;
    HANDLE done;
    bool fail;
    APTTYPE& apartment;
    TestDeferral(std::atomic<unsigned>& count, HANDLE event, bool error, APTTYPE& type) :
        calls(count), done(event), fail(error), apartment(type) {}
    void Complete() {
        APTTYPEQUALIFIER qualifier{};
        check_hresult(CoGetApartmentType(&apartment, &qualifier));
        ++calls; SetEvent(done);
        if (fail) throw hresult_error(E_FAIL);
    }
};
struct ReentrantInstance : implements<ReentrantInstance, IBackgroundTaskInstance> {
    BackgroundTaskDeferral deferral;
    unsigned added{}, removed{}, triggers{};
    explicit ReentrantInstance(BackgroundTaskDeferral value) : deferral(std::move(value)) {}
    guid InstanceId() { return {}; }
    BackgroundTaskRegistration Task() { return nullptr; }
    std::uint32_t Progress() { return 0; }
    void Progress(std::uint32_t) {}
    std::uint32_t SuspendedCount() { return 0; }
    IInspectable TriggerDetails() { ++triggers; return nullptr; }
    BackgroundTaskDeferral GetDeferral() { return deferral; }
    event_token Canceled(BackgroundTaskCanceledEventHandler const& handler) {
        ++added;
        handler(get_strong().as<IBackgroundTaskInstance>(), BackgroundTaskCancellationReason::Abort);
        return event_token{17};
    }
    void Canceled(event_token const& token) {
        Require(token.value == 17, "exact late subscription token"); ++removed;
    }
};
inline void RealFinalizer(std::shared_ptr<activation::Trace> trace, bool failComplete = false) {
    std::atomic<unsigned> calls{};
    auto done = TestSignal(); APTTYPE apartment{};
    auto task = make_self<Task>();
    task->diagnostics = std::move(trace);
    task->deferral = make<TestDeferral>(calls, done.get(), failComplete, apartment).as<BackgroundTaskDeferral>();
    Require(task->gate.ClaimData(), "test primary Data ownership");
    activation::observation::Context cancellation;
    cancellation.entry = activation::ObservationStamp();
    cancellation = task->diagnostics->Observe().EnterCancel(cancellation);
    task->ObserveCancellation(cancellation);
    task->Finish(activation::Reason::Canceled, false, 3);
    Require(calls == 0, "other finisher cannot steal Data deferral");
    task->Finish(activation::Reason::Normal, true, 4);
    task->Finish(activation::Reason::Normal, true, 4);
    WaitTest(done.get());
    Require(calls == 1 && apartment == APTTYPE_MTA, "real finalizer once and caller COM context");
}
inline void NativeJournalChecks() {
    using namespace activation;
    auto moduleBaseline = static_cast<std::uint32_t>(get_module_lock());
    {
        TrialDirectory directory;
        auto id = RandomId();
        NativeReportBackend first(directory.path.wstring(), id);
        Require(PublishTerminal(first, R"({"test":true})").Published(), "real native report publication");
        NativeReportBackend duplicate(directory.path.wstring(), id);
        auto collision = PublishTerminal(duplicate, R"({"test":false})");
        Require(collision.stage == ReportStage::Rename && collision.error != 0, "no-replace final collision");
        NativeReportBackend partial(directory.path.wstring(), id);
        Require(PublishTerminal(partial, R"({"test":false})").stage == ReportStage::Create,
            "existing partial blocks, never retry");
        auto final = directory.path / (L"DesktopPdfJournal-" + id + L".json");
        auto file = StorageFile::GetFileFromPathAsync(final.wstring()).get();
        Require(FileIO::ReadTextAsync(file).get() == L"{\"test\":true}", "collision preserves original bytes");
    }
    for (auto fault : {Stage::None, Stage::Root, Stage::Parent, Stage::Folder, Stage::Create, Stage::Write, Stage::Rename}) {
        TrialDirectory directory;
        auto io = std::make_shared<TrialIo>(directory.path.wstring());
        io->fault = fault;
        auto trace = std::make_shared<Trace>(io);
        trace->Add(Point::RunEnter);
        AwaitTest(trace->Finish(Reason::Normal, true));
        Require(io->reported, "independent receipt even after event sink failure");
        auto r = Parse(io->receipt, 4096);
        Fields(r, {L"schema", L"version", L"activation", L"job", L"finalize_reason", L"drain_state",
            L"accepted_count", L"dequeued_count", L"rejected_before_seal", L"rejected_after_seal_at_decision",
            L"last_published_sequence", L"finish_sequence", L"admission_loss", L"sealed_tick_ms",
            L"decision_tick_ms", L"deadline_tick_ms", L"outstanding_io_at_decision", L"deferral_acquired",
            L"deferral_release_planned", L"deferral_release_observed", L"inflight", L"first_failure", L"observations"});
        Require(r.GetNamedString(L"schema") == L"4d-pdf-printer.activation-journal-terminal" &&
            Integer(r, L"version") == 2 && r.GetNamedString(L"job").empty() &&
            !r.GetNamedBoolean(L"deferral_release_observed"),
            "terminal schema and honest acknowledgement boundary");
        if (fault == Stage::None) {
            Require(r.GetNamedString(L"drain_state") == L"drained" &&
                Integer(r, L"last_published_sequence") == 1 && Integer(r, L"finish_sequence") == 1,
                "actual FileIO finalization through finish");
            auto activation = std::wstring(r.GetNamedString(L"activation"));
            auto folder = directory.path / L"DesktopPdf" / L"ActivationDiagnostics" / activation;
            auto finish = StorageFile::GetFileFromPathAsync((folder / L"1.json").wstring()).get();
            Require(Parse(to_string(FileIO::ReadTextAsync(finish).get())).GetNamedString(L"checkpoint") == L"finish",
                "real finalized finish bytes");
        } else {
            auto failure = r.GetNamedObject(L"first_failure");
            Require(r.GetNamedString(L"drain_state") == L"failed" &&
                failure.GetNamedString(L"stage") == StageNames[static_cast<std::size_t>(fault)] &&
                Integer(failure, L"code_u32") == static_cast<std::uint32_t>(E_ACCESSDENIED) &&
                r.GetNamedValue(L"last_published_sequence").ValueType() == JsonValueType::Null,
                "actual publisher stage failure retains raw HRESULT, not dequeue ack");
        }
    }
    {
        TrialDirectory directory;
        auto io = std::make_shared<TrialIo>(directory.path.wstring());
        io->pauseWrite = true;
        auto trace = std::make_shared<Trace>(io);
        trace->Add(Point::JobCreated, {}, std::wstring(32, L'b'));
        WaitTest(io->entered.get());
        unsigned staging = 0, finalRecords = 0;
        for (auto const& entry : std::filesystem::recursive_directory_iterator(directory.path)) {
            if (!entry.is_regular_file()) continue;
            auto leaf = entry.path().filename().wstring();
            if (leaf.starts_with(L"pending-")) {
                (void)entry.file_size(); ++staging; // Metadata only; never open active staging.
            } else if (leaf == L"0.json") {
                ++finalRecords;
            }
        }
        Require(staging != 0 && finalRecords == 0, "live observer retains actual pending metadata, no staging content reads");
        AwaitTest(trace->Finish(Reason::Normal, true));
        auto r = Parse(io->receipt, 4096);
        Require(r.GetNamedString(L"drain_state") == L"deadline" &&
            r.GetNamedBoolean(L"outstanding_io_at_decision") &&
            !r.GetNamedObject(L"inflight").GetNamedBoolean(L"cancel_requested") &&
            r.GetNamedValue(L"last_published_sequence").ValueType() == JsonValueType::Null &&
            r.GetNamedValue(L"first_failure").ValueType() == JsonValueType::Null, "pending timeout, not fabricated I/O error");
        std::weak_ptr<Trace> weak = trace;
        trace.reset();
        Require(!weak.expired(), "pending coroutine retains actual trace/module owner");
        SetEvent(io->release.get()); WaitTest(io->writeExited.get());
        ReleasedTest(weak);
        Require(Parse(io->receipt, 4096).GetNamedString(L"drain_state") == L"deadline", "late completion cannot rewrite receipt");
    }
    {
        TrialDirectory directory;
        auto io = std::make_shared<TrialIo>(directory.path.wstring());
        auto trace = std::make_shared<Trace>(io);
        auto expired = GetTickCount64() - 1000;
        observation::Context cancellation;
        cancellation.entry = {expired, GetCurrentThreadId()};
        cancellation = trace->Observe().EnterCancel(cancellation);
        trace->Cancel(expired, cancellation);
        AwaitTest(trace->Finish(Reason::Canceled, true));
        Require(!io->reported && std::filesystem::is_empty(directory.path), "expired cancellation issues no diagnostic I/O");
        auto late = std::make_shared<Trace>(io);
        AwaitTest(late->Finish(Reason::Normal, true, GetTickCount64() - 3000));
        Require(!io->reported && std::filesystem::is_empty(directory.path), "pre-detach normal deadline never resets");
    }
    for (bool fail : {false, true}) {
        TrialDirectory directory;
        auto io = std::make_shared<TrialIo>(directory.path.wstring());
        io->failReport = fail;
        RealFinalizer(std::make_shared<Trace>(io), fail);
    }
    {
        TrialDirectory directory;
        auto io = std::make_shared<TrialIo>(directory.path.wstring());
        io->pauseReport = true;
        auto trace = std::make_shared<Trace>(io);
        AwaitTest(trace->Finish(Reason::Normal, true));
        WaitTest(io->reportEntered.get());
        Require(!io->reported, "native receipt wait bounded independently of blocked worker");
        std::weak_ptr<Trace> weak = trace; trace.reset();
        Require(!weak.expired(), "pending native worker retains module/buffers");
        SetEvent(io->reportRelease.get()); WaitTest(io->reportExited.get());
        Require(io->reported && !Parse(io->receipt, 4096).GetNamedBoolean(L"deferral_release_observed"),
            "late native publication is not retrospective deferral proof");
        ReleasedTest(weak);
    }
    {
        std::atomic<unsigned> calls{};
        auto done = TestSignal(); APTTYPE apartment{};
        auto task = make_self<::Task>();
        auto deferral = make<TestDeferral>(calls, done.get(), false, apartment).as<BackgroundTaskDeferral>();
        auto instance = make_self<ReentrantInstance>(deferral);
        task->Run(instance.as<IBackgroundTaskInstance>());
        WaitTest(done.get());
        Require(calls == 1 && instance->added == 1 && instance->removed == 1 && instance->triggers == 0,
            "actual Run handles synchronous cancellation before token return, no second deferral");
        auto snapshot = task->diagnostics->Observe().Read();
        Require(snapshot.cancellation[0].payload &&
            snapshot.cancellation[0].payload->raw == static_cast<std::uint32_t>(BackgroundTaskCancellationReason::Abort) &&
            snapshot.winner.payload && snapshot.winner.payload->caller == 3,
            "actual pre-job framework callback observes raw category and elected caller, not an OS cause");
    }
    for (bool dataOwner : {false, true}) {
        TrialDirectory directory;
        auto io = std::make_shared<TrialIo>(directory.path.wstring());
        std::atomic<unsigned> calls{};
        auto done = TestSignal(); APTTYPE apartment{};
        auto task = make_self<::Task>();
        task->diagnostics = std::make_shared<Trace>(io);
        task->deferral = make<TestDeferral>(calls, done.get(), false, apartment).as<BackgroundTaskDeferral>();
        if (dataOwner) Require(task->gate.ClaimData(), "pre-job Data owner");
        task->Trace(dataOwner ? Point::PreJobFailure : Point::RunFailure, hresult{E_FAIL});
        task->Finish(dataOwner ? Reason::Normal : Reason::ActivationError, dataOwner, dataOwner ? 4 : 0);
        WaitTest(done.get());
        auto receipt = Parse(io->receipt, 4096);
        Require(receipt.GetNamedString(L"job").empty() &&
            receipt.GetNamedString(L"finalize_reason") == (dataOwner ? L"normal" : L"activation-error") &&
            receipt.GetNamedString(L"drain_state") == L"drained",
            "actual finalizer can drain pre-job activation/Data failure without inventing a job binding");
    }
    {
        TrialDirectory directory;
        auto io = std::make_shared<TrialIo>(directory.path.wstring());
        std::exception_ptr error;
        std::thread sta([&] {
            try {
                init_apartment(apartment_type::single_threaded);
                std::atomic<unsigned> calls{};
                auto done = TestSignal(); APTTYPE apartment{};
                auto task = make_self<::Task>();
                task->diagnostics = std::make_shared<Trace>(io);
                task->deferral = make<TestDeferral>(calls, done.get(), false, apartment).as<BackgroundTaskDeferral>();
                Require(task->gate.ClaimData(), "STA fixture Data ownership");
                task->Finish(Reason::Normal, true, 4);
                DWORD index{}; auto signal = done.get();
                check_hresult(CoWaitForMultipleHandles(COWAIT_DISPATCH_CALLS | COWAIT_DISPATCH_WINDOW_MESSAGES,
                    6000, 1, &signal, &index));
                Require(calls == 1 && apartment == APTTYPE_STA, "actual Task finalizer restores STA context");
            } catch (...) { error = std::current_exception(); }
        });
        sta.join();
        if (error) std::rethrow_exception(error);
    }
    auto end = GetTickCount64() + 5000;
    while (static_cast<std::uint32_t>(get_module_lock()) != moduleBaseline && GetTickCount64() < end) Sleep(1);
    Require(static_cast<std::uint32_t>(get_module_lock()) == moduleBaseline, "all native fixture module owners actually released");
}
}
