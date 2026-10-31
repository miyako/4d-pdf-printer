#pragma once
#include "Contract.h"
#include <array>
#include <type_traits>
#include <utility>

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
public:
    bool Push(Event event) {
        if (accepted == EventLimit || static_cast<std::size_t>(event.point) >= Names.size()) return false;
        event.sequence = static_cast<std::uint32_t>(accepted);
        events[accepted++] = event;
        return true;
    }
    bool Pop(Event& event) {
        if (consumed == accepted) return false;
        event = events[consumed++];
        return true;
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
// First base is destroyed last, after members and enable_shared_from_this cleanup.
class Trace : private ModuleLifetime, public std::enable_shared_from_this<Trace> {
    std::mutex mutex;
    Journal journal;
    bool writing{}, failed{}, limitReported{};
    StorageFolder folder{nullptr};
    std::wstring id;
    Identity identity;

    static fire_and_forget Drain(std::shared_ptr<Trace> self) {
        try {
            co_await resume_background();
            Apartment apartment;
            APTTYPE type{}; APTTYPEQUALIFIER qualifier{};
            check_hresult(CoGetApartmentType(&type, &qualifier));
            Require(type == APTTYPE_MTA, "activation diagnostics writer requires MTA");
            if (!self->folder) {
                self->id = RandomId();
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
                auto root = co_await Root();
                auto parent = co_await root.CreateFolderAsync(L"ActivationDiagnostics", CreationCollisionOption::OpenIfExists);
                self->folder = co_await parent.CreateFolderAsync(hstring(self->id), CreationCollisionOption::FailIfExists);
            }
            for (;;) {
                Event event;
                {
                    std::scoped_lock lock(self->mutex);
                    if (self->failed || !self->journal.Pop(event)) { self->writing = false; co_return; }
                }
                auto record = DiagnosticRecord(self->id, event, self->identity);
                co_await Publish(self->folder, to_hstring(event.sequence) + L".json", record);
            }
        } catch (...) {
            auto hr = to_hresult();
            Failure(L"activation diagnostics write", hr);
            std::scoped_lock lock(self->mutex);
            self->failed = true; self->writing = false;
        }
    }
public:
    void Add(Point point, hresult error = {}, std::wstring_view job = {}) noexcept {
        try {
            Require(static_cast<std::size_t>(point) < Names.size(), "activation diagnostic checkpoint");
            Require(job.empty() || Id(job), "activation diagnostic job id");
            Event event;
            event.point = point; event.error = static_cast<std::uint32_t>(error.value);
            event.process = GetCurrentProcessId(); event.thread = GetCurrentThreadId(); event.tick = GetTickCount64();
            std::copy(job.begin(), job.end(), event.job.begin());
            bool start = false, overflow = false;
            {
                std::scoped_lock lock(mutex);
                if (failed) return; // The first sink failure was reported; no write retry.
                if (!journal.Push(event)) {
                    overflow = !limitReported; limitReported = true;
                } else if (!writing) { writing = true; start = true; }
            }
            if (overflow) Failure(L"activation diagnostics event limit", hresult{E_BOUNDS});
            if (start) Drain(shared_from_this());
        } catch (...) {
            Failure(L"activation diagnostics enqueue", to_hresult());
            std::scoped_lock lock(mutex); failed = true;
        }
    }
};
}
#endif
