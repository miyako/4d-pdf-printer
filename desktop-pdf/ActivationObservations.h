#pragma once
#include "Contract.h"
#include <array>
#include <atomic>
#include <optional>
#include <type_traits>

namespace pdf::activation::observation {
struct Stamp { std::uint64_t tick{}; std::uint32_t thread{}; };
struct Context {
    std::uint8_t origin{};
    bool first{};
    Stamp entry;
    std::uint32_t raw{};
    std::uint8_t evaluated{}, values{};
};
struct Applied { Context context; std::uint64_t tick{}; };
struct Finish {
    Stamp entry;
    std::uint8_t caller{}, requested{}, cas{};
    bool owner{}, gate{}, first{};
    std::optional<std::uint8_t> expected;
};
struct Reason { Context context; std::uint64_t sampled{}; std::uint8_t chosen{}; };
struct PassedReason { std::uint64_t tick{}; std::uint8_t value{}; };
struct Operation {
    std::optional<std::uint16_t> key;
    std::uint8_t stage{};
    std::optional<std::uint8_t> sequence;
};
struct ApiHead {
    Stamp entry;
    Operation operation;
    std::uintptr_t identity{};
    std::optional<Stamp> launch;
};
struct Outcome { Stamp observed; std::optional<std::uint32_t> error; };
class OperationKeys {
    std::uint32_t next{1};
public:
    std::optional<std::uint16_t> Next() noexcept {
        if (next > 65535) return {};
        return static_cast<std::uint16_t>(next++);
    }
};
struct StopSample {
    std::uint8_t evaluated{}, values{};
    template<typename Canceled, typename Wall, typename Monotonic>
    bool Evaluate(Canceled canceled, Wall wall, Monotonic monotonic) {
        evaluated = 1; values = 0;
        if (canceled()) { values = 1; return true; }
        evaluated |= 2;
        if (wall()) { values = 2; return true; }
        evaluated |= 4;
        if (monotonic()) { values = 4; return true; }
        return false;
    }
};

template<typename T> struct Sample {
    std::uint8_t state{};
    bool repeated{};
    std::optional<T> payload;
};
template<typename T> class Once {
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(std::atomic<std::uint8_t>::is_always_lock_free);
    std::atomic<std::uint8_t> state{};
    T payload{};
public:
    bool Claim() noexcept {
        std::uint8_t empty{};
        return state.compare_exchange_strong(empty, 1, std::memory_order_acq_rel, std::memory_order_acquire);
    }
    void Publish(T const& value) noexcept {
        payload = value;
        state.store(2, std::memory_order_release);
    }
    bool TryPublish(T const& value) noexcept {
        if (!Claim()) return false;
        Publish(value); return true;
    }
    Sample<T> Read() const noexcept {
        Sample<T> result;
        result.state = state.load(std::memory_order_acquire);
        if (result.state == 2) result.payload = payload;
        return result;
    }
};
template<typename T> class First {
    static_assert(std::atomic<bool>::is_always_lock_free);
    Once<T> value;
    std::atomic<bool> repeated{};
public:
    bool Claim() noexcept {
        if (value.Claim()) return true;
        repeated.store(true, std::memory_order_release); return false;
    }
    void Publish(T const& payload) noexcept { value.Publish(payload); }
    Sample<T> Read() const noexcept {
        auto again = repeated.load(std::memory_order_acquire);
        auto result = value.Read();
        result.repeated = again;
        return result;
    }
};
struct Api {
    First<ApiHead> head;
    Once<Outcome> returned, awaited;
};
struct ApiSample {
    Sample<ApiHead> head;
    Sample<Outcome> returned, awaited;
};
struct CompleteEntry { Stamp observed; std::uint32_t status{}; };
struct CompleteSample {
    Sample<CompleteEntry> entry;
    Sample<Outcome> returned;
};
struct CompleteCancellation {
    Stamp begin, end;
    CompleteSample call;
};
struct Snapshot {
    std::array<Sample<Context>, 2> cancellation;
    Sample<Reason> reason;
    std::array<Sample<Finish>, 5> attempts;
    Sample<Finish> winner;
    Sample<Stamp> began;
    Sample<PassedReason> passed;
    std::array<ApiSample, 3> api;
    CompleteSample complete;
    Sample<CompleteCancellation> completeCancellation;
    bool invalid{};
};
struct Frozen {
    std::optional<Applied> first, minimum;
    std::optional<Operation> operation;
};
class Observations {
    std::array<First<Context>, 2> cancellation;
    Once<Reason> reason;
    std::array<First<Finish>, 5> attempts;
    Once<Finish> winner;
    Once<Stamp> began;
    Once<PassedReason> passed;
    std::array<Api, 3> api;
    First<CompleteEntry> completeEntry;
    Once<Outcome> completeReturned;
    Once<CompleteCancellation> completeCancellation;
    std::atomic<bool> invalid{};
public:
    bool CompleteEnter(CompleteEntry value) noexcept {
        if (!completeEntry.Claim()) return false;
        completeEntry.Publish(value); return true;
    }
    void CompleteReturn(bool owned, Outcome value) noexcept {
        if (owned && !completeReturned.TryPublish(value)) invalid = true;
    }
    CompleteSample ReadComplete() const noexcept {
        CompleteSample result;
        result.returned = completeReturned.Read();
        result.entry = completeEntry.Read();
        return result;
    }
    void CompleteCanceled(Context const& context, CompleteSample const& sample, Stamp end) noexcept {
        if (context.origin == 0 && context.first &&
            !completeCancellation.TryPublish({context.entry, end, sample})) invalid = true;
    }
    Context EnterCancel(Context context) noexcept {
        if (context.origin >= cancellation.size()) { invalid = true; return context; }
        context.first = cancellation[context.origin].Claim();
        if (context.first) cancellation[context.origin].Publish(context);
        return context;
    }
    void SetReason(Context const& context, std::uint64_t sampled, std::uint8_t chosen) noexcept {
        if (!reason.TryPublish({context, sampled, chosen})) invalid = true;
    }
    Finish EnterFinish(Finish value) noexcept {
        if (value.caller >= attempts.size()) { invalid = true; return value; }
        value.first = attempts[value.caller].Claim();
        return value;
    }
    void FinishAttempt(Finish const& value) noexcept {
        if (value.caller >= attempts.size()) { invalid = true; return; }
        if (value.first) attempts[value.caller].Publish(value);
        if (value.gate && !winner.TryPublish(value)) invalid = true;
    }
    void Began(Stamp value) noexcept {
        if (!began.TryPublish(value)) invalid = true;
    }
    void Passed(PassedReason value) noexcept {
        if (!passed.TryPublish(value)) invalid = true;
    }
    bool ApiEntry(std::uint8_t route, ApiHead const& value) noexcept {
        if (route >= api.size()) { invalid = true; return false; }
        if (!api[route].head.Claim()) return false;
        api[route].head.Publish(value); return true;
    }
    void ApiReturn(std::uint8_t route, bool owned, Outcome const& value) noexcept {
        if (route >= api.size()) { invalid = true; return; }
        if (owned && !api[route].returned.TryPublish(value)) invalid = true;
    }
    void Await(Operation const& operation, std::uintptr_t identity, Outcome const& value) noexcept {
        if (!operation.key) return;
        for (std::size_t route = 0; route != 2; ++route) {
            auto head = api[route].head.Read();
            if (head.payload && head.payload->operation.key == operation.key &&
                head.payload->identity == identity) api[route].awaited.TryPublish(value);
        }
    }
    Snapshot Read() const noexcept {
        Snapshot result;
        result.completeCancellation = completeCancellation.Read();
        result.complete = ReadComplete();
        result.passed = passed.Read(); result.began = began.Read(); result.winner = winner.Read();
        for (std::size_t i = 0; i != api.size(); ++i) {
            result.api[i].returned = api[i].returned.Read();
            result.api[i].awaited = api[i].awaited.Read();
            result.api[i].head = api[i].head.Read();
        }
        for (std::size_t i = 0; i != attempts.size(); ++i) result.attempts[i] = attempts[i].Read();
        result.reason = reason.Read();
        for (std::size_t i = 0; i != cancellation.size(); ++i) result.cancellation[i] = cancellation[i].Read();
        result.invalid = invalid.load(std::memory_order_acquire);
        return result;
    }
};
}
