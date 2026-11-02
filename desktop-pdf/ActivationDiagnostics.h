#pragma once
#include "Contract.h"
#include <array>
#include <type_traits>
#include <utility>
#include <atomic>
#include <optional>
#include "ActivationJournalReport.h"

namespace pdf::activation {
template<typename Counter> class ScopedCounter {
    Counter& counter;
public:
    explicit ScopedCounter(Counter& value) noexcept : counter(value) {
        static_assert(noexcept(++counter) && noexcept(--counter));
        ++counter;
    }
    ScopedCounter(ScopedCounter const&) = delete;
    ScopedCounter& operator=(ScopedCounter const&) = delete;
    ScopedCounter(ScopedCounter&&) = delete;
    ScopedCounter& operator=(ScopedCounter&&) = delete;
    ~ScopedCounter() noexcept { --counter; }
};
inline constexpr std::size_t EventLimit = 32, IdentityChars = 256, RecordLimit = 8192;
enum class Point {
    RunEnter, DeferralAcquired, TriggerBefore, TriggerAcquired, SessionBefore,
    SessionSubscribed, StartBefore, StartReturned, DataEnter, DuplicateData,
    EndpointChecked, ContentChecked, ConfigurationChecked, SelectorChecked,
    RegistrationChecked, SubmissionChecked, MetadataValidated, JobCreated,
    PreJobFailure, DataFailure, CompleteBefore, CompleteReturned, CompleteFailure,
    Canceled, Finish, RunFailure, SessionFailure, CallbackDeadline, Count
};
inline constexpr std::array<std::wstring_view, static_cast<std::size_t>(Point::Count)> Names{
    L"run-enter", L"deferral-acquired", L"trigger-before", L"trigger-acquired", L"session-before",
    L"session-subscribed", L"session-start-before", L"session-start-returned", L"data-enter", L"duplicate-data",
    L"endpoint-checked", L"content-checked", L"configuration-checked", L"selector-checked",
    L"registration-checked", L"submission-checked", L"metadata-validated", L"job-created",
    L"pre-job-failed", L"data-failed", L"complete-job-before", L"complete-job-returned", L"complete-job-failed",
    L"canceled", L"finish", L"run-failed", L"session-failed", L"callback-deadline"
};
inline bool IdentityText(std::wstring_view value) {
    return !value.empty() && value.size() < IdentityChars && value.find(L'\0') == value.npos;
}
struct Event {
    Point point{};
    std::uint32_t sequence{}, error{}, process{}, thread{};
    std::uint64_t tick{};
    std::array<wchar_t, 33> job{};
};
class Journal {
    std::array<Event, EventLimit> events{};
    std::size_t accepted{}, consumed{};
    bool sealed{};
    std::uint32_t rejectedBefore{}, rejectedAfter{};
    static void Reject(std::uint32_t& count) noexcept {
        if (count != UINT32_MAX) ++count;
    }
public:
    bool Push(Event event) {
        if (sealed) { Reject(rejectedAfter); return false; }
        if (accepted == EventLimit - 1 || event.point == Point::Finish ||
            static_cast<std::size_t>(event.point) >= Names.size()) { Reject(rejectedBefore); return false; }
        event.sequence = static_cast<std::uint32_t>(accepted);
        events[accepted++] = event;
        return true;
    }
    bool Pop(Event& event) {
        if (consumed == accepted) return false;
        event = events[consumed++];
        return true;
    }
    void Seal(Event finish) {
        if (sealed) return;
        Require(finish.point == Point::Finish, "finish slot");
        sealed = true; finish.sequence = static_cast<std::uint32_t>(accepted);
        events[accepted++] = finish;
    }
    std::uint32_t Accepted() const noexcept { return static_cast<std::uint32_t>(accepted); }
    std::uint32_t Consumed() const noexcept { return static_cast<std::uint32_t>(consumed); }
    std::uint32_t RejectedBefore() const noexcept { return rejectedBefore; }
    std::uint32_t RejectedAfter() const noexcept { return rejectedAfter; }
    bool Sealed() const noexcept { return sealed; }
};
enum class Owner { AwaitingData, Data, Finalizing, Closed };
class TaskGate {
    std::atomic<Owner> owner{Owner::AwaitingData};
public:
    bool ClaimData() noexcept {
        auto expected = Owner::AwaitingData;
        return owner.compare_exchange_strong(expected, Owner::Data);
    }
    bool ClaimFinish(bool dataOwner) noexcept {
        auto expected = dataOwner ? Owner::Data : Owner::AwaitingData;
        return owner.compare_exchange_strong(expected, Owner::Finalizing);
    }
    bool Waiting() const noexcept { return owner == Owner::AwaitingData; }
    bool Closing() const noexcept { return owner == Owner::Finalizing || owner == Owner::Closed; }
    void Closed() noexcept { owner = Owner::Closed; }
};
template<typename F> class FinalAttempt {
    F complete;
public:
    explicit FinalAttempt(F action) : complete(std::move(action)) {}
    FinalAttempt(FinalAttempt const&) = delete;
    ~FinalAttempt() noexcept { complete(); }
};
template<typename Token> class Subscription {
    std::optional<Token> token;
public:
    bool Install(Token value, bool closing) {
        Require(!token, "duplicate subscription token");
        if (closing) return false;
        token = value; return true;
    }
    std::optional<Token> Take() { return std::exchange(token, std::nullopt); }
};
inline constexpr std::uint64_t DrainMs = 1500, FinalizeMs = 2000, ReportMs = 500, CancelMs = 250;
inline std::uint64_t FinalizeDeadline(std::uint64_t started, std::optional<std::uint64_t> cancel) {
    auto normal = started + FinalizeMs;
    return cancel ? (std::min)(normal, *cancel + CancelMs) : normal;
}
enum class Stage { None, Enqueue, Initialize, Root, Parent, Folder, Prepare, Create, Write, Rename };
inline constexpr std::array<std::wstring_view, 10> StageNames{
    L"none", L"enqueue", L"initialize", L"create-root", L"create-parent", L"create-activation-folder",
    L"prepare-record", L"create-pending", L"write-pending", L"rename-final"
};
enum class Reason { Normal, ActivationError, SessionError, CallbackDeadline, Canceled };
inline constexpr std::array<std::wstring_view, 5> ReasonNames{
    L"normal", L"activation-error", L"session-error", L"callback-deadline", L"canceled"
};
enum class DrainState { Drained, Failed, Deadline, Canceled, Unavailable };
inline constexpr std::array<std::wstring_view, 5> DrainNames{
    L"drained", L"failed", L"deadline", L"canceled", L"unavailable"
};
struct DiagnosticFailure {
    Stage stage{};
    std::uint32_t code{};
    std::optional<Event> event;
};
struct Decision {
    DrainState state{};
    Reason reason{};
    std::uint64_t sealed{}, tick{}, deadline{};
    std::uint32_t accepted{}, consumed{}, rejectedBefore{}, rejectedAfter{};
    std::optional<std::uint32_t> published, finish;
    std::optional<DiagnosticFailure> failure;
    std::optional<Event> inflight;
    Stage stage{};
    std::array<wchar_t, 41> pending{};
    bool outstanding{}, cancel{}, deferral{};
};
class PublicationState {
public:
    Journal journal;
    bool writing{}, stop{}, cancelRequested{};
    std::optional<std::uint32_t> published;
    std::optional<DiagnosticFailure> failure;
    std::optional<Event> inflight;
    Stage stage{};
    std::array<wchar_t, 41> pending{};
    std::optional<std::uint64_t> canceled;
    std::optional<std::uint64_t> finalizing;
    std::optional<Decision> decision;
    void Fail(std::uint32_t code) noexcept {
        if (!failure) failure = DiagnosticFailure{stage, code, inflight};
        stop = true;
    }
    void Published(Event const& event) {
        Require(event.sequence == (published ? *published + 1 : 0), "diagnostic publication cursor");
        published = event.sequence; inflight.reset(); pending.fill(0); stage = Stage::None;
    }
    void Cancel(std::uint64_t tick) noexcept {
        if (!canceled || tick < *canceled) canceled = tick;
    }
    bool MayIssue(std::uint64_t now) noexcept {
        if (finalizing && (canceled || now >= (std::min)(*finalizing + DrainMs,
            FinalizeDeadline(*finalizing, canceled)))) stop = true;
        return !stop;
    }
    bool Drained() const noexcept {
        return journal.Sealed() && !writing && !failure && !journal.RejectedBefore() &&
            published && *published + 1 == journal.Accepted();
    }
    Decision Freeze(Reason reason, std::uint64_t seal, std::uint64_t now, std::uint64_t end, bool deferral) {
        Require(!decision, "one journal terminal decision");
        auto state = canceled ? DrainState::Canceled : failure || journal.RejectedBefore() ? DrainState::Failed :
            Drained() ? DrainState::Drained : DrainState::Deadline;
        decision = Decision{state, reason, seal, now, end, journal.Accepted(), journal.Consumed(),
            journal.RejectedBefore(), journal.RejectedAfter(), published,
            journal.Sealed() ? std::optional<std::uint32_t>(journal.Accepted() - 1) : std::nullopt,
            failure, inflight, stage, pending, writing, cancelRequested, deferral};
        stop = true;
        return *decision;
    }
};
}

#ifdef _WIN32
#include "Job.h"
#include <appmodel.h>
#include <combaseapi.h>
#include <memory>
#include <mutex>

namespace pdf::activation {
#if defined(WINRT_NO_MODULE_LOCK) || defined(WINRT_CUSTOM_MODULE_LOCK)
#error Activation diagnostics require the default C++/WinRT own-module lock.
#endif
using ModuleCounter = std::remove_reference_t<decltype(get_module_lock())>;
struct ModuleLifetime : ScopedCounter<ModuleCounter> {
    ModuleLifetime() noexcept : ScopedCounter<ModuleCounter>(get_module_lock()) {}
};
struct Identity {
    std::uint32_t error{}, validationError{};
    std::wstring value;
};
inline JsonObject DiagnosticRecord(std::wstring_view id, Event const& event, Identity const& identity) {
    Require(Id(id) && event.sequence < EventLimit &&
        static_cast<std::size_t>(event.point) < Names.size(), "activation diagnostic binding");
    auto end = std::find(event.job.begin(), event.job.end(), L'\0');
    Require(end != event.job.end(), "activation diagnostic job terminator");
    std::wstring_view job(event.job.data(), static_cast<std::size_t>(end - event.job.begin()));
    Require(job.empty() || Id(job), "activation diagnostic job");
    bool available = identity.error == 0 && identity.validationError == 0 && IdentityText(identity.value);
    Require(available || identity.value.empty(), "activation diagnostic identity");
    Require(available || identity.error != 0 || identity.validationError != 0, "activation diagnostic identity error required");
    JsonObject record;
    Text(record, L"schema", L"4d-pdf-printer.activation-diagnostic"); Number(record, L"version", 1);
    Text(record, L"activation", id); Number(record, L"sequence", event.sequence);
    Text(record, L"checkpoint", Names[static_cast<std::size_t>(event.point)]);
    Number(record, L"hresult", event.error); Number(record, L"process", event.process);
    Number(record, L"thread", event.thread); Number(record, L"tick_ms", event.tick);
    Text(record, L"job", job); Text(record, L"identity_api", L"GetCurrentApplicationUserModelId");
    Text(record, L"identity_status", available ? L"available" : identity.validationError ? L"invalid-response" :
        identity.error == APPMODEL_ERROR_NO_APPLICATION ? L"unavailable" : L"error");
    Number(record, L"identity_error", identity.error); Number(record, L"identity_validation_error", identity.validationError);
    Text(record, L"app_user_model_id", identity.value);
    JsonWire(to_string(record.Stringify()), RecordLimit).Check();
    return record;
}
struct Apartment {
    CO_MTA_USAGE_COOKIE cookie{};
    Apartment() {
        check_hresult(CoIncrementMTAUsage(&cookie));
    }
    ~Apartment() {
        if (cookie) {
            auto hr = CoDecrementMTAUsage(cookie);
            if (FAILED(hr)) Failure(L"activation diagnostics apartment cleanup", hresult{hr});
        }
    }
};
struct JournalIo {
    virtual ~JournalIo() = default;
    virtual StorageFolder Cache() { return JournalCache(); }
    virtual IAsyncOperation<StorageFolder> Folder(StorageFolder const& parent, hstring const& name,
        CreationCollisionOption collision) { return parent.CreateFolderAsync(name, collision); }
    virtual IAsyncOperation<StorageFile> Create(StorageFolder const& parent, hstring const& name) {
        return parent.CreateFileAsync(name, CreationCollisionOption::FailIfExists);
    }
    virtual IAsyncAction Write(StorageFile const& file, hstring const& text) {
        return FileIO::WriteTextAsync(file, text, UnicodeEncoding::Utf8);
    }
    virtual IAsyncAction Rename(StorageFile const& file, hstring const& name) {
        return file.RenameAsync(name, NameCollisionOption::FailIfExists);
    }
    virtual ReportResult Report(std::wstring_view activation, std::string_view bytes) {
        NativeReportBackend backend(TerminalCachePath(), activation);
        return PublishTerminal(backend, bytes);
    }
};
// First base is destroyed last, after members and enable_shared_from_this cleanup.
class Trace : private ModuleLifetime, public std::enable_shared_from_this<Trace> {
    std::mutex mutex;
    std::shared_ptr<JournalIo> io;
    PublicationState state;
    handle changed;
    std::shared_ptr<IAsyncInfo> operation;
    StorageFolder folder{nullptr};
    std::wstring id;
    Identity identity;
    std::array<wchar_t, 33> job{};
    bool reportDone{};
    ReportResult reportResult;

    void Signal() noexcept {
        if (!SetEvent(changed.get())) Failure(L"journal signal", hresult{HRESULT_FROM_WIN32(GetLastError())});
    }
    bool StageAt(Stage stage) {
        std::scoped_lock lock(mutex);
        if (!state.MayIssue(GetTickCount64())) return false;
        state.stage = stage;
        return true;
    }
    template<typename T> T Track(T const& value) {
        auto tracked = std::make_shared<IAsyncInfo>(value.template as<IAsyncInfo>());
        std::shared_ptr<IAsyncInfo> old;
        bool cancel;
        {
            std::scoped_lock lock(mutex);
            old = std::exchange(operation, tracked);
            cancel = state.stop;
            if (cancel) state.cancelRequested = true;
        }
        if (cancel) {
            try { tracked->Cancel(); }
            catch (...) { Failure(L"journal cancel", to_hresult()); }
        }
        return value;
    }
    void ClearOperation() {
        std::shared_ptr<IAsyncInfo> old;
        { std::scoped_lock lock(mutex); old = std::exchange(operation, nullptr); }
    }
    static fire_and_forget CancelCurrent(std::shared_ptr<Trace> self, std::shared_ptr<IAsyncInfo> current) {
        try {
            co_await resume_background();
            Apartment apartment;
            { std::scoped_lock lock(self->mutex); self->state.cancelRequested = true; }
            current->Cancel();
        } catch (...) { Failure(L"journal cancel", to_hresult()); }
    }
    void CancelOperation() noexcept {
        std::shared_ptr<IAsyncInfo> current;
        { std::scoped_lock lock(mutex); current = operation; }
        try { if (current) CancelCurrent(shared_from_this(), current); }
        catch (...) { Failure(L"journal cancel scheduling", to_hresult()); }
    }
    static Event MakeEvent(Point point, hresult error = {}, std::wstring_view job = {}) {
        Require(static_cast<std::size_t>(point) < Names.size(), "activation diagnostic checkpoint");
        Require(job.empty() || Id(job), "activation diagnostic job id");
        Event event;
        event.point = point; event.error = static_cast<std::uint32_t>(error.value);
        event.process = GetCurrentProcessId(); event.thread = GetCurrentThreadId(); event.tick = GetTickCount64();
        std::copy(job.begin(), job.end(), event.job.begin());
        return event;
    }
    static IAsyncAction PublishEvent(std::shared_ptr<Trace> self, Event event) {
        if (!self->StageAt(Stage::Prepare)) co_return;
        auto record = DiagnosticRecord(self->id, event, self->identity);
        auto text = record.Stringify();
        auto pending = L"pending-" + RandomId();
        {
            std::scoped_lock lock(self->mutex);
            std::copy(pending.begin(), pending.end(), self->state.pending.begin());
        }
        if (!self->StageAt(Stage::Create)) co_return;
        auto file = co_await self->Track(self->io->Create(self->folder, hstring(pending)));
        self->ClearOperation();
        if (!self->StageAt(Stage::Write)) co_return;
        co_await self->Track(self->io->Write(file, text));
        self->ClearOperation();
        if (!self->StageAt(Stage::Rename)) co_return;
        co_await self->Track(self->io->Rename(file, to_hstring(event.sequence) + L".json"));
        self->ClearOperation();
        { std::scoped_lock lock(self->mutex); self->state.Published(event); }
        self->Signal();
    }
    static IAsyncAction Initialize(std::shared_ptr<Trace> self) {
                if (!self->StageAt(Stage::Initialize)) co_return;
                std::array<wchar_t, IdentityChars> buffer{};
                UINT32 length = static_cast<UINT32>(buffer.size());
                self->identity.error = static_cast<std::uint32_t>(GetCurrentApplicationUserModelId(&length, buffer.data()));
                if (self->identity.error == 0) {
                    if (length > 1 && length <= buffer.size() && buffer[length - 1] == L'\0' &&
                        IdentityText(std::wstring_view(buffer.data(), length - 1))) {
                        self->identity.value.assign(buffer.data(), length - 1);
                    } else {
                        self->identity.validationError = static_cast<std::uint32_t>(HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
                        Failure(L"activation diagnostics identity response", hresult{HRESULT_FROM_WIN32(ERROR_INVALID_DATA)});
                    }
                } else if (self->identity.error != APPMODEL_ERROR_NO_APPLICATION) {
                    Failure(L"activation diagnostics identity", hresult{HRESULT_FROM_WIN32(self->identity.error)});
                }
                if (!self->StageAt(Stage::Root)) co_return;
                auto root = co_await self->Track(self->io->Folder(self->io->Cache(), L"DesktopPdf", CreationCollisionOption::OpenIfExists));
                self->ClearOperation();
                if (!self->StageAt(Stage::Parent)) co_return;
                auto parent = co_await self->Track(self->io->Folder(root, L"ActivationDiagnostics", CreationCollisionOption::OpenIfExists));
                self->ClearOperation();
                if (!self->StageAt(Stage::Folder)) co_return;
                self->folder = co_await self->Track(self->io->Folder(parent, hstring(self->id), CreationCollisionOption::FailIfExists));
                self->ClearOperation();
    }
    static fire_and_forget Drain(std::shared_ptr<Trace> self) {
        try {
            co_await resume_background();
            Apartment apartment;
            APTTYPE type{}; APTTYPEQUALIFIER qualifier{};
            check_hresult(CoGetApartmentType(&type, &qualifier));
            Require(type == APTTYPE_MTA, "activation diagnostics writer requires MTA");
            if (!self->folder) co_await Initialize(self);
            for (;;) {
                Event event;
                {
                    std::scoped_lock lock(self->mutex);
                    if (self->state.stop || !self->state.journal.Pop(event)) break;
                    self->state.inflight = event;
                }
                co_await PublishEvent(self, event);
            }
        } catch (...) {
            auto hr = to_hresult();
            Failure(L"activation diagnostics write", hr);
            std::scoped_lock lock(self->mutex);
            self->state.Fail(static_cast<std::uint32_t>(hr.value));
        }
        self->ClearOperation();
        bool restart = false;
        {
            std::scoped_lock lock(self->mutex);
            self->state.writing = false;
            self->state.inflight.reset();
            if (!self->state.stop && self->state.journal.Consumed() < self->state.journal.Accepted())
                restart = self->state.writing = true;
        }
        self->Signal();
        if (restart) Drain(self);
    }
    static JsonValue Sequence(std::optional<std::uint32_t> value) {
        return value ? JsonValue::CreateNumberValue(*value) : JsonValue::CreateNullValue();
    }
    static void EventFields(JsonObject const& r, std::optional<Event> const& event) {
        r.Insert(L"sequence", Sequence(event ? std::optional<std::uint32_t>(event->sequence) : std::nullopt));
        if (event) Text(r, L"checkpoint", Names[static_cast<std::size_t>(event->point)]);
        else r.Insert(L"checkpoint", JsonValue::CreateNullValue());
    }
    JsonObject Receipt(Decision const& d, std::wstring_view jobId) const {
        JsonObject r;
        Text(r, L"schema", L"4d-pdf-printer.activation-journal-terminal"); Number(r, L"version", 1);
        Text(r, L"activation", id); Text(r, L"job", jobId);
        Text(r, L"finalize_reason", ReasonNames[static_cast<std::size_t>(d.reason)]);
        Text(r, L"drain_state", DrainNames[static_cast<std::size_t>(d.state)]);
        Number(r, L"accepted_count", d.accepted); Number(r, L"dequeued_count", d.consumed);
        Number(r, L"rejected_before_seal", d.rejectedBefore);
        Number(r, L"rejected_after_seal_at_decision", d.rejectedAfter);
        r.Insert(L"last_published_sequence", Sequence(d.published));
        r.Insert(L"finish_sequence", Sequence(d.finish));
        r.Insert(L"admission_loss", JsonValue::CreateBooleanValue(d.rejectedBefore != 0));
        Number(r, L"sealed_tick_ms", d.sealed); Number(r, L"decision_tick_ms", d.tick);
        Number(r, L"deadline_tick_ms", d.deadline);
        r.Insert(L"outstanding_io_at_decision", JsonValue::CreateBooleanValue(d.outstanding));
        r.Insert(L"deferral_acquired", JsonValue::CreateBooleanValue(d.deferral));
        r.Insert(L"deferral_release_planned", JsonValue::CreateBooleanValue(d.deferral));
        r.Insert(L"deferral_release_observed", JsonValue::CreateBooleanValue(false));
        if (d.outstanding) {
            JsonObject inflight;
            EventFields(inflight, d.inflight);
            Text(inflight, L"stage", StageNames[static_cast<std::size_t>(d.stage)]);
            Text(inflight, L"temporary_leaf", d.pending.data());
            inflight.Insert(L"cancel_requested", JsonValue::CreateBooleanValue(d.cancel));
            r.Insert(L"inflight", inflight);
        } else r.Insert(L"inflight", JsonValue::CreateNullValue());
        if (d.failure) {
            JsonObject failure;
            EventFields(failure, d.failure->event);
            Text(failure, L"stage", StageNames[static_cast<std::size_t>(d.failure->stage)]);
            Text(failure, L"code_domain", L"hresult"); Number(failure, L"code_u32", d.failure->code);
            r.Insert(L"first_failure", failure);
        } else r.Insert(L"first_failure", JsonValue::CreateNullValue());
        JsonWire(to_string(r.Stringify()), 4096).Check();
        return r;
    }
    static fire_and_forget WriteReceipt(std::shared_ptr<Trace> self, Decision decision, std::wstring jobId) {
        ReportResult result{ReportStage::Resolve, 0};
        try {
            co_await resume_background();
            Apartment apartment;
            auto bytes = to_string(self->Receipt(decision, jobId).Stringify());
            result = self->io->Report(self->id, bytes);
            if (!result.Published()) {
                auto stage = L"journal terminal native stage " + std::to_wstring(static_cast<unsigned>(result.stage));
                Failure(stage.c_str(), hresult{HRESULT_FROM_WIN32(result.error)});
            }
        } catch (...) {
            auto hr = to_hresult();
            Failure(L"journal terminal setup", hr);
            result = {ReportStage::Resolve, static_cast<std::uint32_t>(hr.value)};
        }
        {
            std::scoped_lock lock(self->mutex);
            self->reportResult = result; self->reportDone = true;
        }
        self->Signal();
    }
public:
    explicit Trace(std::shared_ptr<JournalIo> backend = std::make_shared<JournalIo>()) :
        io(std::move(backend)), changed(CreateEventExW(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE)) {
        check_bool(static_cast<bool>(changed));
        Require(io != nullptr, "journal backend");
        id = RandomId();
    }
    void Add(Point point, hresult error = {}, std::wstring_view job = {}) noexcept {
        try {
            auto event = MakeEvent(point, error, job);
            bool start = false, overflow = false;
            {
                std::scoped_lock lock(mutex);
                if (!job.empty()) std::copy(job.begin(), job.end(), this->job.begin());
                if (!state.journal.Push(event)) overflow = true;
                else if (!state.stop && !state.writing) start = state.writing = true;
            }
            if (overflow) Failure(L"activation diagnostics event limit", hresult{E_BOUNDS});
            if (start) Drain(shared_from_this());
        } catch (...) {
            auto hr = to_hresult();
            Failure(L"activation diagnostics enqueue", hr);
            std::scoped_lock lock(mutex); state.stage = Stage::Enqueue; state.Fail(static_cast<std::uint32_t>(hr.value));
        }
        Signal();
    }
    void Cancel(std::uint64_t tick) noexcept {
        { std::scoped_lock lock(mutex); state.Cancel(tick); }
        Signal();
    }
    IAsyncAction Finish(Reason reason, bool deferral, std::uint64_t began = GetTickCount64()) {
        auto self = shared_from_this();
        auto sealed = GetTickCount64();
        bool start = false;
        {
            auto finish = MakeEvent(Point::Finish);
            std::scoped_lock lock(mutex);
            Require(!state.journal.Sealed(), "one journal finalizer");
            state.journal.Seal(finish);
            state.finalizing = began;
            state.MayIssue(GetTickCount64());
            if (!state.writing && !state.stop) start = state.writing = true;
        }
        if (start) Drain(self);
        Decision decision;
        std::wstring jobId;
        for (;;) {
            std::uint64_t end;
            bool done;
            {
                std::scoped_lock lock(mutex);
                auto now = GetTickCount64();
                end = (std::min)(began + DrainMs, FinalizeDeadline(began, state.canceled));
                done = state.Drained() || state.failure || state.journal.RejectedBefore() ||
                    state.canceled || now >= end;
                if (done) {
                    decision = state.Freeze(reason, sealed, now, FinalizeDeadline(began, state.canceled), deferral);
                    jobId = job.data();
                }
            }
            if (done) break;
            auto now = GetTickCount64();
            if (now < end) co_await resume_on_signal(changed.get(), std::chrono::milliseconds(end - now));
        }
        CancelOperation();
        auto reportEnd = (std::min)(decision.deadline, GetTickCount64() + ReportMs);
        bool reportStarted = false, reportCompleted = false;
        if (GetTickCount64() < reportEnd) {
            reportStarted = true;
            WriteReceipt(self, decision, std::move(jobId));
            for (;;) {
                std::uint64_t end;
                {
                    std::scoped_lock lock(mutex);
                    reportCompleted = reportDone;
                    end = (std::min)(reportEnd, FinalizeDeadline(began, state.canceled));
                }
                auto now = GetTickCount64();
                if (reportCompleted || now >= end) break;
                co_await resume_on_signal(changed.get(), std::chrono::milliseconds(end - now));
            }
        }
        if (!reportStarted || !reportCompleted) Failure(L"journal terminal unavailable/deadline", hresult{HRESULT_FROM_WIN32(ERROR_TIMEOUT)});
        if (decision.state != DrainState::Drained)
            Failure(L"journal drain incomplete", hresult{decision.failure ?
                static_cast<std::int32_t>(decision.failure->code) : HRESULT_FROM_WIN32(ERROR_TIMEOUT)});
    }
};
}
#endif
