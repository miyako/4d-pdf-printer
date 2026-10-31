#pragma once
#include "../ActivationDiagnostics.h"
#include <memory>

namespace pdf::tests {
template<typename Counter> void LifetimeChecks(Counter& counter) {
    using Guard = activation::ScopedCounter<Counter>;
    static_assert(!std::is_copy_constructible_v<Guard> && !std::is_copy_assignable_v<Guard> &&
        !std::is_move_constructible_v<Guard> && !std::is_move_assignable_v<Guard>);
    auto baseline = static_cast<std::uint32_t>(counter);
    {
        Guard guard(counter);
        Require(static_cast<std::uint32_t>(counter) == baseline + 1, "lifetime acquire");
    }
    Require(static_cast<std::uint32_t>(counter) == baseline, "lifetime release");
    bool cleanedWhileLocked = false;
    struct State : private Guard {
        Counter& count;
        std::uint32_t expected;
        bool& cleaned;
        State(Counter& value, std::uint32_t initial, bool& flag) :
            Guard(value), count(value), expected(initial + 1), cleaned(flag) {}
        ~State() noexcept { cleaned = static_cast<std::uint32_t>(count) == expected; }
    };
    struct Unwind : State {
        Unwind(Counter& value, std::uint32_t initial, bool& flag) : State(value, initial, flag) {
            throw std::runtime_error("expected lifetime construction failure");
        }
    };
    bool unwound = false;
    try { Unwind failed(counter, baseline, cleanedWhileLocked); }
    catch (std::runtime_error const&) { unwound = true; }
    Require(unwound && cleanedWhileLocked && static_cast<std::uint32_t>(counter) == baseline,
        "construction unwind cleans state before releasing lock");
    cleanedWhileLocked = false;
    auto owner = std::make_shared<State>(counter, baseline, cleanedWhileLocked);
    auto pending = owner;
    std::weak_ptr<State> weak = owner;
    owner.reset();
    Require(!weak.expired() && !cleanedWhileLocked && static_cast<std::uint32_t>(counter) == baseline + 1,
        "pending shared state keeps lifetime independent of owner");
    pending.reset();
    Require(weak.expired() && cleanedWhileLocked && static_cast<std::uint32_t>(counter) == baseline,
        "last shared state cleans before releasing lifetime");
}
}
