#pragma once
#include "../Task.cpp"
#include <filesystem>
#include <fstream>
#include "NativeTrialRoot.h"

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
    bool retained{};
    explicit TrialDirectory(FreshTrialRoot const* suite = nullptr) : retained(suite != nullptr) {
        if (suite) path = suite->NewCase();
        else {
            path = std::filesystem::temp_directory_path() / (L"DesktopPdfJournalTest-" + RandomId());
            Require(std::filesystem::create_directory(path), "fresh owned native test directory");
        }
    }
    ~TrialDirectory() noexcept {
        if (retained) return;
        std::error_code error;
        std::filesystem::remove_all(path, error);
        if (error) Failure(L"owned journal test directory cleanup", hresult{HRESULT_FROM_WIN32(error.value())});
    }
};
struct TrialIo : activation::JournalIo {
    std::wstring path;
    activation::Stage fault{activation::Stage::None};
    bool pauseWrite{}, pauseReport{}, failReport{};
    handle entered = TestSignal(), release = TestSignal(), reportEntered = TestSignal(), reportRelease = TestSignal();
    handle reportExited = TestSignal(), writeExited = TestSignal();
    std::atomic<bool> reported{false};
    std::string receipt;
    explicit TrialIo(std::wstring folder) : path(std::move(folder)) {}
    // Returns the leaf of a target whose parent is a plain directory inside this case tree.
    std::wstring Confined(std::wstring const& target) const {
        auto split = target.find_last_of(L'\\');
        Require(split != std::wstring::npos, "trial I/O absolute target");
        auto parent = target.substr(0, split), leaf = target.substr(split + 1);
        Require((parent == path || parent.starts_with(path + L"\\")) && parent.find(L"\\..") == std::wstring::npos &&
            !leaf.empty() && leaf != L"." && leaf != L".." && leaf.find(L'/') == std::wstring::npos,
            "trial I/O folder confinement");
        auto attributes = GetFileAttributesW(parent.c_str());
        Require(attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) &&
            !(attributes & FILE_ATTRIBUTE_REPARSE_POINT), "plain trial I/O folder");
        return leaf;
    }
    std::wstring Cache() override { return path; }
    void Folder(std::wstring const& target, bool openIfExists) override {
        auto name = Confined(target);
        Require(name == L"DesktopPdf" || name == L"ActivationDiagnostics" || Id(name), "fixed trial journal folder name");
        auto stage = name == L"DesktopPdf" ? activation::Stage::Root :
            name == L"ActivationDiagnostics" ? activation::Stage::Parent : activation::Stage::Folder;
        Require(openIfExists == (stage != activation::Stage::Folder), "fixed trial folder collision policy");
        if (fault == stage) throw hresult_error(E_ACCESSDENIED);
        JournalIo::Folder(target, openIfExists);
    }
    handle Create(std::wstring const& target) override {
        auto leaf = Confined(target);
        Require(leaf.starts_with(L"pending-") && Id(std::wstring_view(leaf).substr(8)), "fixed trial staging filename");
        if (fault == activation::Stage::Create) throw hresult_error(E_ACCESSDENIED);
        return JournalIo::Create(target);
    }
    // Holding blocks the single background writer thread until the test releases it.
    void Write(handle file, std::string_view bytes) override {
        activation::FinalAttempt done([&]() noexcept { SetEvent(writeExited.get()); });
        Require(static_cast<bool>(file), "trial staging handle");
        if (fault == activation::Stage::Write) throw hresult_error(E_ACCESSDENIED);
        if (pauseWrite) {
            SetEvent(entered.get());
            WaitTest(release.get(), 30000);
        }
        JournalIo::Write(std::move(file), bytes);
    }
    void Rename(std::wstring const& from, std::wstring const& to) override {
        auto source = Confined(from);
        auto leaf = Confined(to);
        Require(source.starts_with(L"pending-") && leaf.size() >= 6 && leaf.size() <= 7 && leaf.ends_with(L".json") &&
            std::all_of(leaf.begin(), leaf.end() - 5, [](wchar_t c) { return c >= L'0' && c <= L'9'; }),
            "fixed trial final filename");
        if (fault == activation::Stage::Rename) throw hresult_error(E_ACCESSDENIED);
        JournalIo::Rename(from, to);
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
// Unwrapped production record backend rooted at a test case; used for timing only.
struct RootedIo : activation::JournalIo {
    std::wstring path;
    std::string receipt;
    explicit RootedIo(std::wstring folder) : path(std::move(folder)) {}
    std::wstring Cache() override { return path; }
    activation::ReportResult Report(std::wstring_view id, std::string_view bytes) override {
        receipt = bytes;
        activation::NativeReportBackend backend(path, id);
        return activation::PublishTerminal(backend, bytes);
    }
};
inline std::string ReadBytes(std::filesystem::path const& file) {
    std::ifstream stream(file, std::ios::binary);
    Require(static_cast<bool>(stream), "read retained test file");
    return std::string(std::istreambuf_iterator<char>(stream), {});
}
inline JsonObject ReadRecord(std::filesystem::path const& file) {
    auto bytes = ReadBytes(file);
    Require(bytes.starts_with("\xEF\xBB\xBF"), "native record keeps the 0.1.0.6 UTF-8 BOM");
    return Parse(std::string_view(bytes).substr(3), activation::RecordLimit);
}
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
inline void NativeJournalChecks(FreshTrialRoot const* suite = nullptr) {
    using namespace activation;
    auto moduleBaseline = static_cast<std::uint32_t>(get_module_lock());
    if (suite) {
        std::filesystem::path retained;
        {
            TrialDirectory directory(suite);
            retained = directory.path;
        }
        Require(std::filesystem::is_directory(retained), "successful case tree retained");
        struct RetainedCaseFailure {};
        try {
            TrialDirectory directory(suite);
            retained = directory.path;
            throw RetainedCaseFailure{};
        } catch (RetainedCaseFailure const&) {}
        Require(std::filesystem::is_directory(retained), "failed case tree retained");
        bool rejected = false;
        try { FreshTrialRoot existing(suite->path.wstring()); }
        catch (std::exception const&) { rejected = true; }
        Require(rejected, "existing root rejected without reuse");
        TrialDirectory directory(suite);
        auto io = std::make_shared<TrialIo>(directory.path.wstring());
        rejected = false;
        try { (void)io->Confined(suite->path.wstring() + L"\\DesktopPdf"); }
        catch (std::exception const&) { rejected = true; }
        Require(rejected, "parent outside case rejected before trial writes");
        rejected = false;
        try { io->Folder(io->path + L"\\..", true); }
        catch (std::exception const&) { rejected = true; }
        Require(rejected && std::filesystem::is_empty(directory.path), "folder traversal rejected without writes");
        rejected = false;
        try { (void)make_self<::Task>(std::shared_ptr<Trace>{}); }
        catch (std::exception const&) { rejected = true; }
        Require(rejected, "null explicit diagnostic override rejected");
        auto defaultTask = make_self<::Task>();
        Require(defaultTask->diagnostics == nullptr, "default construction creates no Trace or I/O");
    }
    {
        TrialDirectory directory(suite);
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
        Require(ReadBytes(final) == R"({"test":true})", "collision preserves original bytes");
    }
    for (auto fault : {Stage::None, Stage::Root, Stage::Parent, Stage::Folder, Stage::Create, Stage::Write, Stage::Rename}) {
        TrialDirectory directory(suite);
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
            L"deferral_release_planned", L"deferral_release_observed", L"inflight", L"first_failure", L"observations",
            L"pre_complete_flush", L"cancellation_drain"});
        Require(r.GetNamedString(L"schema") == L"4d-pdf-printer.activation-journal-terminal" &&
            Integer(r, L"version") == 5 && r.GetNamedString(L"job").empty() &&
            r.GetNamedString(L"cancellation_drain") == L"none" &&
            r.GetNamedObject(L"pre_complete_flush").GetNamedString(L"state") == L"not-attempted" &&
            !r.GetNamedBoolean(L"deferral_release_observed"),
            "terminal schema and honest acknowledgement boundary");
        if (fault == Stage::None) {
            Require(r.GetNamedString(L"drain_state") == L"drained" &&
                Integer(r, L"last_published_sequence") == 1 && Integer(r, L"finish_sequence") == 1,
                "actual native finalization through finish");
            auto activation = std::wstring(r.GetNamedString(L"activation"));
            auto folder = directory.path / L"DesktopPdf" / L"ActivationDiagnostics" / activation;
            Require(ReadRecord(folder / L"0.json").GetNamedString(L"checkpoint") == L"run-enter" &&
                ReadRecord(folder / L"1.json").GetNamedString(L"checkpoint") == L"finish",
                "real native finalized record bytes");
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
        TrialDirectory directory(suite);
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
        Require(std::filesystem::is_directory(directory.path), "pending owner tree retained");
        SetEvent(io->release.get()); WaitTest(io->writeExited.get());
        ReleasedTest(weak);
        Require(Parse(io->receipt, 4096).GetNamedString(L"drain_state") == L"deadline", "late completion cannot rewrite receipt");
    }
    {
        TrialDirectory directory(suite);
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
        TrialDirectory directory(suite);
        auto io = std::make_shared<TrialIo>(directory.path.wstring());
        io->failReport = fail;
        RealFinalizer(std::make_shared<Trace>(io), fail);
    }
    {
        TrialDirectory directory(suite);
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
    for (unsigned instanceIndex = 0; instanceIndex < 2; ++instanceIndex) {
        TrialDirectory directory(suite);
        auto io = std::make_shared<TrialIo>(directory.path.wstring());
        auto injected = std::make_shared<Trace>(io);
        std::weak_ptr<Trace> weak = injected;
        std::atomic<unsigned> calls{};
        auto done = TestSignal(); APTTYPE apartment{};
        auto task = make_self<::Task>(injected);
        Require(task->diagnostics == nullptr, "override construction does not begin diagnostics");
        auto deferral = make<TestDeferral>(calls, done.get(), false, apartment).as<BackgroundTaskDeferral>();
        auto instance = make_self<ReentrantInstance>(deferral);
        task->Run(instance.as<IBackgroundTaskInstance>());
        Require(task->diagnostics == injected, "real Run preserves per-instance injected diagnostics");
        WaitTest(done.get());
        Require(calls == 1 && instance->added == 1 && instance->removed == 1 && instance->triggers == 0,
            "actual Run handles synchronous cancellation before token return, no second deferral");
        auto snapshot = task->diagnostics->Observe().Read();
        Require(snapshot.cancellation[0].payload &&
            snapshot.cancellation[0].payload->raw == static_cast<std::uint32_t>(BackgroundTaskCancellationReason::Abort) &&
            snapshot.winner.payload && snapshot.winner.payload->caller == 3,
            "actual pre-job framework callback observes raw category and elected caller, not an OS cause");
        task->diagnostics.reset();
        injected.reset();
        Require(!weak.expired(), "immutable override retains injected trace through Task lifetime");
        task = nullptr;
        ReleasedTest(weak);
    }
    for (bool dataOwner : {false, true}) {
        TrialDirectory directory(suite);
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
    for (auto fault : {Stage::None, Stage::Write}) {
        TrialDirectory directory(suite);
        auto io = std::make_shared<TrialIo>(directory.path.wstring());
        io->fault = fault;
        auto trace = std::make_shared<Trace>(io);
        Require(trace->PreComplete().state == FlushState::NotAttempted, "flush not attempted before call");
        trace->Add(Point::RunEnter);
        trace->Add(Point::CompleteBefore);
        AwaitTest(trace->FlushBeforeComplete());
        auto flush = trace->PreComplete();
        auto expected = fault == Stage::None ? FlushState::Flushed : FlushState::Failed;
        Require(flush.state == expected && flush.target == 2 && flush.begin <= flush.end &&
            (fault != Stage::None || flush.published == 2), "pre-complete flush drains every accepted event or stops on failure");
        AwaitTest(trace->Finish(Reason::Normal, true));
        auto r = Parse(io->receipt, 4096);
        auto wire = r.GetNamedObject(L"pre_complete_flush");
        Require(Integer(r, L"version") == 5 &&
            wire.GetNamedString(L"state") == (fault == Stage::None ? L"flushed" : L"failed") &&
            Integer(wire, L"target_count") == 2 && Integer(wire, L"published_count") == flush.published &&
            Integer(wire, L"begin_tick_ms") == flush.begin && Integer(wire, L"end_tick_ms") == flush.end &&
            r.GetNamedString(L"drain_state") == (fault == Stage::None ? L"drained" : L"failed"),
            "terminal v4 records actual pre-complete flush outcome");
    }
    {
        TrialDirectory directory(suite);
        auto io = std::make_shared<TrialIo>(directory.path.wstring());
        io->pauseWrite = true;
        auto trace = std::make_shared<Trace>(io);
        trace->Add(Point::CompleteBefore);
        WaitTest(io->entered.get());
        AwaitTest(trace->FlushBeforeComplete());
        auto flush = trace->PreComplete();
        Require(flush.state == FlushState::Deadline && flush.target == 1 && flush.published == 0 &&
            flush.end - flush.begin >= PreCompleteFlushMs && flush.end - flush.begin < PreCompleteFlushMs + 1000,
            "held journal I/O bounds the pre-complete flush at its budget");
        SetEvent(io->release.get()); WaitTest(io->writeExited.get());
        AwaitTest(trace->Finish(Reason::Normal, true));
        auto r = Parse(io->receipt, 4096);
        Require(r.GetNamedObject(L"pre_complete_flush").GetNamedString(L"state") == L"deadline" &&
            Integer(r.GetNamedObject(L"pre_complete_flush"), L"published_count") == 0,
            "terminal v4 keeps deadline flush outcome after late publication");
    }
    {
        TrialDirectory directory(suite);
        auto io = std::make_shared<TrialIo>(directory.path.wstring());
        io->pauseWrite = true;
        auto trace = std::make_shared<Trace>(io);
        trace->Add(Point::CompleteBefore);
        WaitTest(io->entered.get());
        auto pending = trace->FlushBeforeComplete();
        Require(trace->PreComplete().state == FlushState::InProgress, "flush waits on held I/O");
        observation::Context cancellation;
        cancellation.entry = ObservationStamp();
        cancellation = trace->Observe().EnterCancel(cancellation);
        trace->Cancel(GetTickCount64(), cancellation);
        AwaitTest(pending);
        auto flush = trace->PreComplete();
        Require(flush.state == FlushState::Canceled && flush.published == 0 &&
            flush.end - flush.begin < PreCompleteFlushMs, "cancellation ends the pre-complete flush early");
        AwaitTest(trace->Finish(Reason::Canceled, false));
        WaitTest(io->reportExited.get());
        auto r = Parse(io->receipt, 4096);
        Require(r.GetNamedString(L"drain_state") == L"canceled" && r.GetNamedString(L"cancellation_drain") == L"stopped" &&
            r.GetNamedBoolean(L"outstanding_io_at_decision") &&
            r.GetNamedValue(L"last_published_sequence").ValueType() == JsonValueType::Null &&
            r.GetNamedObject(L"pre_complete_flush").GetNamedString(L"state") == L"canceled",
            "real cancel before CompleteJob stops the held writer at once and records canceled flush");
        SetEvent(io->release.get()); WaitTest(io->writeExited.get());
    }
    for (bool entered : {false, true}) {
        TrialDirectory directory(suite);
        auto io = std::make_shared<TrialIo>(directory.path.wstring());
        auto injected = std::make_shared<Trace>(io);
        if (entered) Require(injected->Observe().CompleteEnter({ObservationStamp(), 0}), "CompleteJob entry recorded");
        std::atomic<unsigned> calls{};
        auto done = TestSignal(); APTTYPE apartment{};
        auto task = make_self<::Task>(injected);
        auto deferral = make<TestDeferral>(calls, done.get(), false, apartment).as<BackgroundTaskDeferral>();
        auto instance = make_self<ReentrantInstance>(deferral);
        task->Run(instance.as<IBackgroundTaskInstance>());
        WaitTest(done.get());
        WaitTest(io->reportExited.get());
        auto r = Parse(io->receipt, 4096);
        auto expected = entered ? Reason::PostCompleteTeardown : Reason::Canceled;
        Require(calls == 1 && task->finalReason.load() == expected && Integer(r, L"version") == 5 &&
            r.GetNamedString(L"finalize_reason") == (entered ? L"post-complete-teardown" : L"canceled") &&
            r.GetNamedString(L"drain_state") == (entered ? L"drained" : L"canceled") &&
            r.GetNamedString(L"cancellation_drain") == (entered ? L"continued" : L"stopped") &&
            (!entered || Integer(r, L"last_published_sequence") == Integer(r, L"finish_sequence")) &&
            r.GetNamedObject(L"pre_complete_flush").GetNamedString(L"state") == L"not-attempted",
            "framework cancel after CompleteJob entry drains as post-complete teardown; before entry stops canceled");
        auto snapshot = injected->Observe().Read();
        Require(snapshot.reason.payload && snapshot.reason.payload->chosen == static_cast<std::uint8_t>(expected) &&
            snapshot.attempts[3].payload && snapshot.attempts[3].payload->requested == static_cast<std::uint8_t>(expected),
            "classified reason drives both reason setter and framework Finish");
        task = nullptr;
    }
    {
        TrialDirectory directory(suite);
        auto io = std::make_shared<TrialIo>(directory.path.wstring());
        std::atomic<unsigned> calls{};
        auto done = TestSignal(); APTTYPE apartment{};
        auto task = make_self<::Task>();
        task->diagnostics = std::make_shared<Trace>(io);
        task->deferral = make<TestDeferral>(calls, done.get(), false, apartment).as<BackgroundTaskDeferral>();
        Require(task->diagnostics->Observe().CompleteEnter({ObservationStamp(), 0}), "CompleteJob entry recorded");
        observation::Context host;
        host.origin = 1; host.entry = ObservationStamp(); host.evaluated = 9; host.values = 9;
        host = task->diagnostics->Observe().EnterCancel(host);
        task->ObserveCancellation(host);
        task->Finish(Reason::Canceled, false, 3);
        WaitTest(done.get());
        WaitTest(io->reportExited.get());
        auto r = Parse(io->receipt, 4096);
        Require(task->finalReason.load() == Reason::Canceled && r.GetNamedString(L"finalize_reason") == L"canceled" &&
            r.GetNamedString(L"drain_state") == L"canceled" && r.GetNamedString(L"cancellation_drain") == L"stopped",
            "host-origin cancellation after CompleteJob entry stays canceled");
    }
    {
        TrialDirectory directory(suite);
        auto io = std::make_shared<RootedIo>(directory.path.wstring());
        auto trace = std::make_shared<Trace>(io);
        trace->Add(Point::RunEnter);
        AwaitTest(trace->FlushBeforeComplete());
        Require(trace->PreComplete().state == FlushState::Flushed, "native journal initialized and first record published");
        constexpr unsigned timed = 20;
        LARGE_INTEGER frequency{}, begin{}, end{};
        check_bool(QueryPerformanceFrequency(&frequency)); check_bool(QueryPerformanceCounter(&begin));
        for (unsigned index = 0; index < timed; ++index) trace->Add(Point::DataEnter);
        AwaitTest(trace->FlushBeforeComplete());
        check_bool(QueryPerformanceCounter(&end));
        auto flush = trace->PreComplete();
        Require(flush.state == FlushState::Flushed && flush.target == timed + 1 && flush.published == timed + 1,
            "real native backend publishes every accepted record before the flush returns");
        AwaitTest(trace->Finish(Reason::Normal, true));
        auto r = Parse(io->receipt, 4096);
        Require(r.GetNamedString(L"drain_state") == L"drained" && Integer(r, L"accepted_count") == timed + 2 &&
            Integer(r, L"last_published_sequence") == timed + 1 && Integer(r, L"finish_sequence") == timed + 1 &&
            r.GetNamedString(L"cancellation_drain") == L"none", "real native backend drains through finish");
        auto activation = std::wstring(r.GetNamedString(L"activation"));
        auto folder = directory.path / L"DesktopPdf" / L"ActivationDiagnostics" / activation;
        unsigned files = 0;
        for (auto const& entry : std::filesystem::directory_iterator(folder)) {
            Require(entry.is_regular_file() && !entry.path().filename().wstring().starts_with(L"pending-"),
                "no staging leaf survives a drained native journal");
            ++files;
        }
        Require(files == timed + 2, "one final file per accepted record");
        for (unsigned sequence = 0; sequence < timed + 2; ++sequence) {
            auto record = ReadRecord(folder / (std::to_wstring(sequence) + L".json"));
            auto checkpoint = sequence == 0 ? L"run-enter" : sequence == timed + 1 ? L"finish" : L"data-enter";
            Require(record.GetNamedString(L"schema") == L"4d-pdf-printer.activation-diagnostic" &&
                Integer(record, L"sequence") == sequence && record.GetNamedString(L"activation") == activation &&
                record.GetNamedString(L"checkpoint") == checkpoint, "native record name and content binding");
        }
        auto elapsed = static_cast<double>(end.QuadPart - begin.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
        fwprintf(stderr, L"Native journal timing: %u records published in %.3f ms (%.3f ms/record) via "
            L"CreateFile2FromAppW/WriteFile/MoveFileFromAppW in the isolated test root.\n", timed, elapsed, elapsed / timed);
    }
    {
        // 0.1.0.7 device runs 02/03: completion.json delayed Finish until cancel + 125 ms (seal after the old window).
        TrialDirectory directory(suite);
        auto io = std::make_shared<TrialIo>(directory.path.wstring());
        auto trace = std::make_shared<Trace>(io);
        trace->Add(Point::CompleteBefore);
        trace->Add(Point::CompleteReturned);
        trace->Add(Point::Canceled);
        AwaitTest(trace->FlushBeforeComplete());
        Require(trace->PreComplete().state == FlushState::Flushed, "late-seal fixture starts with an empty queue");
        // Backdated cancel (entry and applied tick) instead of Sleep: the seal deterministically
        // lands past the old 125 ms window while the receipt keeps its 120 ms budget.
        observation::Context cancellation;
        cancellation.entry = ObservationStamp();
        cancellation.entry.tick -= 130;
        cancellation = trace->Observe().EnterCancel(cancellation);
        auto tick = cancellation.entry.tick;
        trace->Cancel(tick, cancellation, true);
        AwaitTest(trace->Finish(Reason::PostCompleteTeardown, false));
        Require(io->reported, "late-sealed teardown still writes the terminal receipt");
        auto r = Parse(io->receipt, 4096);
        Require(r.GetNamedString(L"drain_state") == L"drained" && r.GetNamedString(L"cancellation_drain") == L"continued" &&
            Integer(r, L"accepted_count") == 4 && Integer(r, L"last_published_sequence") == 3 &&
            Integer(r, L"sealed_tick_ms") >= tick + 125,
            "finish sealed after the old 125 ms teardown window is still drained");
        auto folder = directory.path / L"DesktopPdf" / L"ActivationDiagnostics" / std::wstring(r.GetNamedString(L"activation"));
        Require(ReadRecord(folder / L"3.json").GetNamedString(L"checkpoint") == L"finish",
            "late-sealed finish record published");
    }
    {
        TrialDirectory directory(suite);
        auto io = std::make_shared<TrialIo>(directory.path.wstring());
        io->pauseWrite = true;
        auto trace = std::make_shared<Trace>(io);
        trace->Add(Point::CompleteBefore);
        WaitTest(io->entered.get());
        trace->Add(Point::CompleteReturned);
        trace->Add(Point::Canceled);
        observation::Context cancellation;
        cancellation.entry = ObservationStamp();
        cancellation = trace->Observe().EnterCancel(cancellation);
        auto tick = GetTickCount64();
        trace->Cancel(tick, cancellation, true);
        auto pending = trace->Finish(Reason::PostCompleteTeardown, false);
        SetEvent(io->release.get());
        AwaitTest(pending);
        auto r = Parse(io->receipt, 4096);
        Require(r.GetNamedString(L"finalize_reason") == L"post-complete-teardown" &&
            r.GetNamedString(L"drain_state") == L"drained" && r.GetNamedString(L"cancellation_drain") == L"continued" &&
            Integer(r, L"accepted_count") == 4 && Integer(r, L"last_published_sequence") == 3 &&
            Integer(r, L"finish_sequence") == 3 && Integer(r, L"deadline_tick_ms") == tick + CancelMs &&
            !r.GetNamedBoolean(L"outstanding_io_at_decision"),
            "post-complete teardown keeps draining through finish and reports applied provenance");
        auto applied = r.GetNamedObject(L"observations").GetNamedArray(L"a");
        Require(applied.GetAt(0).ValueType() == JsonValueType::Array && applied.GetAt(1).ValueType() == JsonValueType::Array,
            "continued drain keeps frozen cancellation provenance");
        auto folder = directory.path / L"DesktopPdf" / L"ActivationDiagnostics" / std::wstring(r.GetNamedString(L"activation"));
        Require(ReadRecord(folder / L"2.json").GetNamedString(L"checkpoint") == L"canceled" &&
            ReadRecord(folder / L"3.json").GetNamedString(L"checkpoint") == L"finish",
            "canceled checkpoint and finish published after post-complete teardown");
    }
    {
        TrialDirectory directory(suite);
        auto io = std::make_shared<TrialIo>(directory.path.wstring());
        io->pauseWrite = true;
        auto trace = std::make_shared<Trace>(io);
        trace->Add(Point::CompleteBefore);
        WaitTest(io->entered.get());
        trace->Add(Point::Canceled);
        observation::Context cancellation;
        cancellation.entry = ObservationStamp();
        cancellation = trace->Observe().EnterCancel(cancellation);
        auto tick = GetTickCount64();
        trace->Cancel(tick, cancellation, true);
        AwaitTest(trace->Finish(Reason::PostCompleteTeardown, false));
        Require(io->reported, "held teardown drain still leaves budget for the terminal receipt");
        auto r = Parse(io->receipt, 4096);
        Require(r.GetNamedString(L"finalize_reason") == L"post-complete-teardown" &&
            r.GetNamedString(L"drain_state") == L"deadline" && r.GetNamedString(L"cancellation_drain") == L"continued" &&
            Integer(r, L"deadline_tick_ms") == tick + CancelMs && Integer(r, L"decision_tick_ms") >= tick + TeardownDrainMs &&
            Integer(r, L"decision_tick_ms") < tick + CancelMs && r.GetNamedBoolean(L"outstanding_io_at_decision") &&
            !r.GetNamedObject(L"inflight").GetNamedBoolean(L"cancel_requested") &&
            r.GetNamedValue(L"last_published_sequence").ValueType() == JsonValueType::Null &&
            r.GetNamedValue(L"first_failure").ValueType() == JsonValueType::Null,
            "held post-complete teardown drain stops at its share of the cancel budget and reports deadline");
        std::weak_ptr<Trace> weak = trace;
        trace.reset();
        Require(!weak.expired(), "blocked native writer retains actual trace/module owner");
        SetEvent(io->release.get()); WaitTest(io->writeExited.get());
        ReleasedTest(weak);
        Require(Parse(io->receipt, 4096).GetNamedString(L"drain_state") == L"deadline", "late teardown write cannot rewrite receipt");
    }
    {
        TrialDirectory directory(suite);
        auto io = std::make_shared<TrialIo>(directory.path.wstring());
        std::exception_ptr error;
        std::thread sta([&] {
            try {
                init_apartment(apartment_type::single_threaded);
                // The first STA in the process reports MAINSTA; native record I/O no longer creates one earlier.
                APTTYPE own{}; APTTYPEQUALIFIER ownQualifier{};
                check_hresult(CoGetApartmentType(&own, &ownQualifier));
                Require(own == APTTYPE_STA || own == APTTYPE_MAINSTA, "STA fixture thread");
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
                Require(calls == 1 && apartment == own, "actual Task finalizer restores STA context");
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
