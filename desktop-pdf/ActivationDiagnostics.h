#pragma once
#include "Contract.h"
#include <array>
#include <type_traits>
#include <utility>
#include <atomic>
#include <optional>
#include "ActivationJournalReport.h"
#include "ObservationWire.h"

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
inline constexpr std::uint64_t PreCompleteFlushMs = 500, PreCompleteSliceMs = 25;
// Post-complete teardown drains until ReceiptReserveMs before the cancel deadline; the reserve is
// kept for the terminal receipt. Finish can seal late (completion.json is published first), so the
// drain must not end early in the cancel budget.
inline constexpr std::uint64_t ReceiptReserveMs = 60, TeardownDrainMs = CancelMs - ReceiptReserveMs;
inline std::uint64_t FinalizeDeadline(std::uint64_t started, std::optional<std::uint64_t> cancel) {
    auto normal = started + FinalizeMs;
    return cancel ? (std::min)(normal, *cancel + CancelMs) : normal;
}
enum class Stage { None, Enqueue, Initialize, Root, Parent, Folder, Prepare, Create, Write, Rename };
inline constexpr std::array<std::wstring_view, 10> StageNames{
    L"none", L"enqueue", L"initialize", L"create-root", L"create-parent", L"create-activation-folder",
    L"prepare-record", L"create-pending", L"write-pending", L"rename-final"
};
enum class Reason { Normal, ActivationError, SessionError, CallbackDeadline, Canceled, PostCompleteTeardown };
inline constexpr std::array<std::wstring_view, 6> ReasonNames{
    L"normal", L"activation-error", L"session-error", L"callback-deadline", L"canceled", L"post-complete-teardown"
};
enum class FlushState { NotAttempted, InProgress, Flushed, Deadline, Canceled, Failed, Stopped };
inline constexpr std::array<std::wstring_view, 7> FlushNames{
    L"not-attempted", L"in-progress", L"flushed", L"deadline", L"canceled", L"failed", L"stopped"
};
struct PreCompleteFlush {
    FlushState state{};
    std::uint32_t target{}, published{};
    std::uint64_t begin{}, end{};
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
    // continued: post-complete teardown applied, but the writer kept draining (terminal v5).
    bool outstanding{}, continued{}, deferral{};
    observation::Frozen observations;
    PreCompleteFlush flush{};
};
inline std::string FlushWire(PreCompleteFlush const& value) {
    using namespace observation;
    Require(static_cast<std::size_t>(value.state) < FlushNames.size() && value.target <= EventLimit &&
        value.published <= EventLimit, "pre-complete flush domain");
    auto attempted = value.state != FlushState::NotAttempted;
    auto ended = attempted && value.state != FlushState::InProgress;
    Require(attempted || (!value.target && !value.published && !value.begin && !value.end), "unattempted flush fields");
    Require(ended || (!value.published && !value.end), "in-progress flush fields");
    Require(!ended || value.begin <= value.end, "pre-complete flush interval");
    Require(value.state != FlushState::Flushed || value.published >= value.target, "flushed target published");
    return Object({{"state", Text(Ascii(FlushNames[static_cast<std::size_t>(value.state)]))},
        {"target_count", attempted ? Number(value.target, EventLimit) : "null"},
        {"published_count", ended ? Number(value.published, EventLimit) : "null"},
        {"begin_tick_ms", attempted ? Number(value.begin) : "null"},
        {"end_tick_ms", ended ? Number(value.end) : "null"}});
}
class PublicationState {
public:
    Journal journal;
    bool writing{}, stop{};
    std::optional<std::uint32_t> published;
    std::optional<DiagnosticFailure> failure;
    std::optional<Event> inflight;
    Stage stage{};
    std::array<wchar_t, 41> pending{};
    // canceled stops the writer at once; teardown only bounds it to the cancel budget.
    std::optional<std::uint64_t> canceled, teardown;
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
    void Cancel(std::uint64_t tick, bool postComplete = false) noexcept {
        auto& cutoff = postComplete ? teardown : canceled;
        if (!cutoff || tick < *cutoff) cutoff = tick;
    }
    std::optional<std::uint64_t> Cutoff() const noexcept {
        if (!canceled || !teardown) return canceled ? canceled : teardown;
        return (std::min)(*canceled, *teardown);
    }
    std::uint64_t DrainEnd(std::uint64_t began) const noexcept {
        auto end = (std::min)(began + DrainMs, FinalizeDeadline(began, Cutoff()));
        return teardown ? (std::min)(end, *teardown + TeardownDrainMs) : end;
    }
    bool MayIssue(std::uint64_t now) noexcept {
        if (finalizing && (canceled || now >= DrainEnd(*finalizing))) stop = true;
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
            failure, inflight, stage, pending, writing, !canceled && teardown.has_value(), deferral, {}};
        stop = true;
        return *decision;
    }
};
inline std::string ReceiptWire(Decision const& d, std::wstring_view id, std::wstring_view job,
    observation::Snapshot const& snapshot, std::uint32_t process, std::uint64_t begin, std::uint64_t end,
    std::uint8_t version = 5) {
    using namespace observation;
    Require(version >= 2 && version <= 5, "explicit terminal version");
    Require(Id(id) && (job.empty() || Id(job)), "terminal activation/known-job domain");
    Require(static_cast<std::size_t>(d.reason) < ReasonNames.size() &&
        static_cast<std::size_t>(d.state) < DrainNames.size(), "terminal decision enum");
    Require(version >= 4 || d.reason != decltype(d.reason)::PostCompleteTeardown, "post-complete teardown requires terminal v4+");
    auto provenance = d.observations.first.has_value() && d.observations.minimum.has_value();
    if (version == 5) {
        // v5: a post-complete teardown is applied provenance whose drain continued instead of stopping.
        Require(!d.continued || d.state != DrainState::Canceled, "continued drain is not a canceled drain");
        Require((d.state == DrainState::Canceled || d.continued) == provenance, "frozen cancellation provenance");
    } else {
        Require(!d.continued, "continued post-complete drain requires terminal v5");
        Require((d.state == DrainState::Canceled) == provenance, "frozen cancellation provenance");
    }
    auto event = [](std::optional<Event> const& value) {
        return value ? Number(value->sequence, 31) : "null";
    };
    auto point = [](std::optional<Event> const& value) {
        if (!value) return std::string("null");
        Require(static_cast<std::size_t>(value->point) < Names.size(), "terminal checkpoint");
        return Text(Ascii(Names[static_cast<std::size_t>(value->point)]));
    };
    auto stage = [](Stage value) {
        Require(static_cast<std::size_t>(value) < StageNames.size(), "terminal I/O stage");
        return Text(Ascii(StageNames[static_cast<std::size_t>(value)]));
    };
    std::string inflight = "null", failure = "null";
    if (d.outstanding) {
        std::wstring_view pending(d.pending.data());
        Require(pending.empty() || (pending.starts_with(L"pending-") && Id(pending.substr(8))), "terminal pending leaf");
        inflight = Object({{"sequence", event(d.inflight)}, {"checkpoint", point(d.inflight)}, {"stage", stage(d.stage)},
            {"temporary_leaf", Text(Ascii(pending))}, {"cancel_requested", "false"}});
    }
    if (d.failure) failure = Object({{"sequence", event(d.failure->event)}, {"checkpoint", point(d.failure->event)},
        {"stage", stage(d.failure->stage)}, {"code_domain", Text("hresult")}, {"code_u32", Number(d.failure->code, UINT32_MAX)}});
    auto wire = Object({
        {"schema", Text("4d-pdf-printer.activation-journal-terminal")}, {"version", Number(version, 5)},
        {"activation", Text(Ascii(id))}, {"job", Text(Ascii(job))},
        {"finalize_reason", Text(Ascii(ReasonNames[static_cast<std::size_t>(d.reason)]))},
        {"drain_state", Text(Ascii(DrainNames[static_cast<std::size_t>(d.state)]))},
        {"accepted_count", Number(d.accepted, 32)}, {"dequeued_count", Number(d.consumed, 32)},
        {"rejected_before_seal", Number(d.rejectedBefore, UINT32_MAX)}, {"rejected_after_seal_at_decision", Number(d.rejectedAfter, UINT32_MAX)},
        {"last_published_sequence", Optional(d.published, 31)}, {"finish_sequence", Optional(d.finish, 31)},
        {"admission_loss", Boolean(d.rejectedBefore != 0)}, {"sealed_tick_ms", Number(d.sealed)},
        {"decision_tick_ms", Number(d.tick)}, {"deadline_tick_ms", Number(d.deadline)},
        {"outstanding_io_at_decision", Boolean(d.outstanding)}, {"deferral_acquired", Boolean(d.deferral)},
        {"deferral_release_planned", Boolean(d.deferral)}, {"deferral_release_observed", "false"},
        {"inflight", inflight}, {"first_failure", failure},
        {"observations", Encode(snapshot, d.observations, process, begin, end, version)}
    });
    if (version >= 4) {
        wire.pop_back();
        wire += ",\"pre_complete_flush\":" + FlushWire(d.flush);
        if (version == 5) wire += std::string(",\"cancellation_drain\":") +
            (!provenance ? "\"none\"" : d.continued ? "\"continued\"" : "\"stopped\"");
        wire += '}';
    } else Require(d.flush.state == FlushState::NotAttempted, "pre-complete flush requires terminal v4+");
    Require(wire.size() <= (version == 2 ? 2300u : 3300u), "source-bound terminal wire maximum");
    JsonWire(wire, 4096).Check();
    return wire;
}
}

#ifdef _WIN32
#include "Job.h"
#include <appmodel.h>
#include <combaseapi.h>
#include <memory>
#include <mutex>

namespace pdf::activation {
inline observation::Stamp ObservationStamp() noexcept { return {GetTickCount64(), GetCurrentThreadId()}; }
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
// Synchronous native record I/O on the single background writer; failures throw hresult_error.
struct JournalIo {
    virtual ~JournalIo() = default;
    virtual std::wstring Cache() { return TerminalCachePath(); }
    // openIfExists: create-if-missing plain directory; otherwise fail-if-exists.
    virtual void Folder(std::wstring const& path, bool openIfExists) {
        if (CreateDirectoryFromAppW(path.c_str(), nullptr)) return;
        auto error = GetLastError();
        if (!openIfExists || error != ERROR_ALREADY_EXISTS) throw_hresult(HRESULT_FROM_WIN32(error));
        WIN32_FILE_ATTRIBUTE_DATA attributes{};
        if (!GetFileAttributesExFromAppW(path.c_str(), GetFileExInfoStandard, &attributes)) throw_last_error();
        if (!(attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (attributes.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) throw_hresult(HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
    }
    virtual handle Create(std::wstring const& path) {
        CREATEFILE2_EXTENDED_PARAMETERS options{};
        options.dwSize = sizeof(options); options.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
        options.dwFileFlags = FILE_FLAG_OPEN_REPARSE_POINT;
        auto raw = CreateFile2FromAppW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, CREATE_NEW, &options);
        if (raw == INVALID_HANDLE_VALUE) throw_last_error();
        return handle(raw);
    }
    // Writes every byte, then closes; no per-record FlushFileBuffers.
    virtual void Write(handle file, std::string_view bytes) {
        DWORD count{};
        if (!WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr)) throw_last_error();
        if (count != bytes.size()) throw_hresult(HRESULT_FROM_WIN32(ERROR_WRITE_FAULT));
        if (!CloseHandle(file.detach())) throw_last_error();
    }
    virtual void Rename(std::wstring const& from, std::wstring const& to) {
        if (!MoveFileFromAppW(from.c_str(), to.c_str())) throw_last_error();
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
    handle changed, flushChanged;
    observation::Observations observed;
    std::optional<observation::Applied> firstApplied, minimumApplied;
    std::wstring folder;
    std::wstring id;
    Identity identity;
    std::array<wchar_t, 33> knownJob{};
    bool reportDone{};
    ReportResult reportResult;
    PreCompleteFlush preComplete;

    void Signal() noexcept {
        if (!SetEvent(changed.get())) Failure(L"journal signal", hresult{HRESULT_FROM_WIN32(GetLastError())});
        // Separate auto-reset event: the pre-complete flush never consumes a Finish wake-up.
        if (!SetEvent(flushChanged.get())) Failure(L"journal flush signal", hresult{HRESULT_FROM_WIN32(GetLastError())});
    }
    bool StageAt(Stage stage) {
        std::scoped_lock lock(mutex);
        if (!state.MayIssue(GetTickCount64())) return false;
        state.stage = stage;
        return true;
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
    // Record bytes match the 0.1.0.6 FileIO UTF-8 text: BOM plus compact JSON.
    static void PublishEvent(std::shared_ptr<Trace> const& self, Event const& event) {
        if (!self->StageAt(Stage::Prepare)) return;
        auto record = DiagnosticRecord(self->id, event, self->identity);
        auto bytes = "\xEF\xBB\xBF" + to_string(record.Stringify());
        auto pending = L"pending-" + RandomId();
        {
            std::scoped_lock lock(self->mutex);
            std::copy(pending.begin(), pending.end(), self->state.pending.begin());
        }
        auto staging = self->folder + L"\\" + pending;
        if (!self->StageAt(Stage::Create)) return;
        auto file = self->io->Create(staging);
        if (!self->StageAt(Stage::Write)) return;
        self->io->Write(std::move(file), bytes);
        if (!self->StageAt(Stage::Rename)) return;
        self->io->Rename(staging, self->folder + L"\\" + std::to_wstring(event.sequence) + L".json");
        { std::scoped_lock lock(self->mutex); self->state.Published(event); }
        self->Signal();
    }
    static void Initialize(std::shared_ptr<Trace> const& self) {
        if (!self->StageAt(Stage::Initialize)) return;
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
        if (!self->StageAt(Stage::Root)) return;
        auto root = self->io->Cache() + L"\\DesktopPdf";
        self->io->Folder(root, true);
        if (!self->StageAt(Stage::Parent)) return;
        auto parent = root + L"\\" + L"ActivationDiagnostics";
        self->io->Folder(parent, true);
        if (!self->StageAt(Stage::Folder)) return;
        auto folder = parent + L"\\" + self->id;
        self->io->Folder(folder, false);
        self->folder = std::move(folder);
    }
    static fire_and_forget Drain(std::shared_ptr<Trace> self) {
        try {
            co_await resume_background();
            Apartment apartment;
            APTTYPE type{}; APTTYPEQUALIFIER qualifier{};
            check_hresult(CoGetApartmentType(&type, &qualifier));
            Require(type == APTTYPE_MTA, "activation diagnostics writer requires MTA");
            if (self->folder.empty()) Initialize(self);
            for (;;) {
                Event event;
                {
                    std::scoped_lock lock(self->mutex);
                    if (self->state.stop || !self->state.journal.Pop(event)) break;
                    self->state.inflight = event;
                }
                PublishEvent(self, event);
            }
        } catch (...) {
            auto hr = to_hresult();
            Failure(L"activation diagnostics write", hr);
            std::scoped_lock lock(self->mutex);
            self->state.Fail(static_cast<std::uint32_t>(hr.value));
        }
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
    static fire_and_forget WriteReceipt(std::shared_ptr<Trace> self, Decision decision, std::wstring jobId) {
        ReportResult result{ReportStage::Resolve, 0};
        try {
            co_await resume_background();
            Apartment apartment;
            auto begin = GetTickCount64();
            auto snapshot = self->observed.Read();
            auto end = GetTickCount64();
            auto bytes = ReceiptWire(decision, self->id, jobId, snapshot, GetCurrentProcessId(), begin, end);
            result = self->io->Report(self->id, bytes);
            ReportFailures(result, [](ReportStage stage, std::uint32_t code, bool secondary) {
                auto label = secondary ? std::wstring(L"journal terminal secondary close") :
                    L"journal terminal native stage " + std::to_wstring(static_cast<unsigned>(stage));
                Failure(label.c_str(), hresult{HRESULT_FROM_WIN32(code)});
            });
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
    observation::Observations& Observe() noexcept { return observed; }
    explicit Trace(std::shared_ptr<JournalIo> backend = std::make_shared<JournalIo>()) :
        io(std::move(backend)), changed(CreateEventExW(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE)),
        flushChanged(CreateEventExW(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE)) {
        check_bool(static_cast<bool>(changed) && static_cast<bool>(flushChanged));
        Require(io != nullptr, "journal backend");
        id = RandomId();
    }
    PreCompleteFlush PreComplete() {
        std::scoped_lock lock(mutex);
        return preComplete;
    }
    // Bounded wait for every event accepted at entry; never throws to the CompleteJob caller.
    IAsyncAction FlushBeforeComplete() {
        auto self = shared_from_this();
        PreCompleteFlush result;
        try {
            {
                std::scoped_lock lock(self->mutex);
                result.state = FlushState::InProgress;
                result.begin = GetTickCount64();
                result.target = self->state.journal.Accepted();
                self->preComplete = result;
            }
            for (;;) {
                std::uint64_t now;
                {
                    std::scoped_lock lock(self->mutex);
                    auto const& current = self->state;
                    now = GetTickCount64();
                    result.published = current.published ? *current.published + 1 : 0;
                    result.state = result.published >= result.target ? FlushState::Flushed :
                        current.canceled ? FlushState::Canceled : current.failure ? FlushState::Failed :
                        current.stop || current.journal.Sealed() ? FlushState::Stopped :
                        now >= result.begin + PreCompleteFlushMs ? FlushState::Deadline : FlushState::InProgress;
                    if (result.state != FlushState::InProgress) {
                        result.end = now;
                        self->preComplete = result;
                    }
                }
                if (result.state != FlushState::InProgress) break;
                auto slice = (std::min)(result.begin + PreCompleteFlushMs - now, PreCompleteSliceMs);
                co_await resume_on_signal(self->flushChanged.get(), std::chrono::milliseconds(slice));
            }
        } catch (...) {
            auto hr = to_hresult();
            Failure(L"journal pre-complete flush", hr);
            try {
                std::scoped_lock lock(self->mutex);
                result.state = FlushState::Failed;
                result.end = (std::max)(result.begin, GetTickCount64());
                self->preComplete = result;
            } catch (...) { Failure(L"journal pre-complete flush record", to_hresult()); }
        }
    }
    void Add(Point point, hresult error = {}, std::wstring_view job = {}) noexcept {
        try {
            auto event = MakeEvent(point, error, job);
            bool start = false, overflow = false;
            {
                std::scoped_lock lock(mutex);
                if (!job.empty()) std::copy(job.begin(), job.end(), knownJob.begin());
                if (!state.journal.Push(event)) overflow = true;
                else if (!state.stop && !state.writing) start = state.writing = true;
            }
            if (overflow) Failure(L"activation diagnostics admission rejected", hresult{E_BOUNDS});
            if (start) Drain(shared_from_this());
        } catch (...) {
            auto hr = to_hresult();
            Failure(L"activation diagnostics enqueue", hr);
            std::scoped_lock lock(mutex); state.stage = Stage::Enqueue; state.Fail(static_cast<std::uint32_t>(hr.value));
        }
        Signal();
    }
    // postComplete: teardown after CompleteJob entry keeps draining until the cancel budget expires.
    void Cancel(std::uint64_t tick, observation::Context const& context, bool postComplete = false) noexcept {
        {
            std::scoped_lock lock(mutex);
            if (!firstApplied) firstApplied = observation::Applied{context, tick};
            if (!minimumApplied || tick < minimumApplied->tick) minimumApplied = observation::Applied{context, tick};
            state.Cancel(tick, postComplete);
        }
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
                end = state.DrainEnd(began);
                done = state.Drained() || state.failure || state.journal.RejectedBefore() ||
                    state.canceled || now >= end;
                if (done) {
                    decision = state.Freeze(reason, sealed, now, FinalizeDeadline(began, state.Cutoff()), deferral);
                    decision.observations.first = firstApplied;
                    decision.observations.minimum = minimumApplied;
                    decision.flush = preComplete;
                    jobId = knownJob.data();
                }
            }
            if (done) break;
            auto now = GetTickCount64();
            if (now < end) co_await resume_on_signal(changed.get(), std::chrono::milliseconds(end - now));
        }
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
                    end = (std::min)(reportEnd, FinalizeDeadline(began, state.Cutoff()));
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
