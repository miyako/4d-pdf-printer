#pragma once
#include "ActivationObservations.h"
#include <charconv>

namespace pdf::activation::observation {
inline std::string Number(std::uint64_t value, std::uint64_t maximum = 9007199254740991ull) {
    Require(value <= maximum, "observation integer range");
    char buffer[24];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    Require(result.ec == std::errc{}, "observation decimal encoding");
    return {buffer, result.ptr};
}
inline std::string Native(std::uint32_t value) {
    Require(value != 0, "observation native identifier"); return Number(value, UINT32_MAX);
}
inline std::string Boolean(bool value) { return value ? "true" : "false"; }
inline std::string Text(std::string_view value) {
    for (unsigned char c : value) Require(c >= 32 && c < 127 && c != '"' && c != '\\', "observation ASCII literal");
    return "\"" + std::string(value) + "\"";
}
inline std::string Ascii(std::wstring_view value) {
    std::string result;
    for (auto c : value) {
        Require(c >= 32 && c < 127 && c != L'"' && c != L'\\', "observation wide literal");
        result += static_cast<char>(c);
    }
    return result;
}
inline std::string Array(std::initializer_list<std::string> values) {
    std::string result = "["; bool comma = false;
    for (auto const& value : values) { if (comma) result += ','; result += value; comma = true; }
    return result + ']';
}
inline std::string Object(std::initializer_list<std::pair<std::string_view, std::string>> values) {
    std::string result = "{"; bool comma = false;
    for (auto const& [key, value] : values) {
        if (comma) result += ',';
        result += Text(key) + ':' + value; comma = true;
    }
    return result + '}';
}
template<typename T> std::string Optional(std::optional<T> const& value, std::uint64_t maximum) {
    return value ? Number(*value, maximum) : "null";
}
inline bool HostMask(std::uint8_t evaluated, std::uint8_t value) {
    return (evaluated == 9 && (value == 1 || value == 9)) ||
        (evaluated == 11 && (value == 2 || value == 10)) ||
        (evaluated == 15 && (value == 4 || value == 8 || value == 12));
}
inline std::string Mask(Context const& value) {
    Require(HostMask(value.evaluated, value.values), "observation evaluated predicates");
    return Array({Number(value.evaluated), Number(value.values)});
}
inline std::string Cancellation(Context const& value) {
    Require(value.origin < 2, "cancellation origin");
    if (value.origin == 0) return Array({Number(value.entry.tick), Native(value.entry.thread), Number(value.raw, UINT32_MAX)});
    Require(HostMask(value.evaluated, value.values), "host cancellation mask");
    return Array({Number(value.entry.tick), Native(value.entry.thread), Number(value.evaluated), Number(value.values)});
}
inline std::string Put(Applied const& value) {
    Require(value.context.entry.tick <= value.tick && value.context.origin < 2, "applied cancellation context");
    return Array({Number(value.context.origin), Boolean(value.context.first),
        Number(value.context.entry.tick), Native(value.context.entry.thread),
        value.context.origin == 0 ? Number(value.context.raw, UINT32_MAX) : Mask(value.context), Number(value.tick)});
}
inline std::string Put(Reason const& value) {
    static constexpr std::array<std::uint8_t, 5> choices{4, 4, 1, 3, 2};
    Require(value.context.origin < choices.size() && value.chosen == choices[value.context.origin] &&
        value.context.entry.tick <= value.sampled, "actual reason setter");
    return Array({Number(value.context.origin), Boolean(value.context.first), Number(value.context.entry.tick),
        Native(value.context.entry.thread), Number(value.sampled), Number(value.chosen, 4)});
}
inline std::string Put(Finish const& value) {
    static constexpr std::array<std::uint8_t, 5> requested{1, 3, 2, 4, 0};
    Require(value.caller < requested.size() && value.requested == requested[value.caller] &&
        value.owner == (value.caller == 4), "finish caller");
    if (value.caller == 4) Require(value.cas == 0 && !value.expected, "normal finish does not CAS");
    else {
        Require(value.expected.has_value(), "finish actual CAS expected");
        Require((value.cas == 1 && *value.expected == 0 && value.caller != 3) ||
            (value.cas == 2 && *value.expected >= 1 && *value.expected <= 4), "finish actual CAS outcome");
    }
    return Array({Number(value.entry.tick), Native(value.entry.thread), Number(value.requested, 4),
        Boolean(value.owner), Number(value.cas, 2), Optional(value.expected, 4), Boolean(value.gate)});
}
inline std::string Winner(Finish const& value) {
    Require(value.gate, "actual gate winner");
    return Array({Number(value.caller, 4), Boolean(value.first), Put(value)});
}
inline std::string Put(Stamp value) { return Array({Number(value.tick), Native(value.thread)}); }
inline std::string Put(PassedReason value) { return Array({Number(value.tick), Number(value.value, 4)}); }
inline std::string Put(Operation const& value) {
    Require(!value.key || *value.key != 0, "operation key domain");
    return Array({Optional(value.key, 65535), Number(value.stage, 9), Optional(value.sequence, 31)});
}
inline std::string Put(ApiHead const& value) {
    if (value.launch) {
        Require(value.launch->tick <= value.entry.tick, "launch guard before cancel");
        return Array({Number(value.entry.tick), Native(value.entry.thread), "0", "null", "null",
            Array({Number(value.launch->tick), Native(value.launch->thread), "0"})});
    }
    Require(!value.operation.key || *value.operation.key != 0, "API operation key");
    return Array({Number(value.entry.tick), Native(value.entry.thread), Optional(value.operation.key, 65535),
        Number(value.operation.stage, 9), Optional(value.operation.sequence, 31), "null"});
}
inline std::string Put(Outcome const& value) {
    Require(!value.error || *value.error >= 0x80000000u, "original failed HRESULT");
    return Array({Number(value.observed.tick), Native(value.observed.thread), value.error ? "1" : "0",
        Optional(value.error, UINT32_MAX)});
}
template<typename T, typename Encode> std::string OnceWire(Sample<T> const& value, Encode encode) {
    Require(value.state <= 2 && (value.state == 2) == value.payload.has_value(), "observation phase");
    return Array({Number(value.state, 2), value.payload ? encode(*value.payload) : "null"});
}
template<typename T, typename Encode> std::string FirstWire(Sample<T> const& value, Encode encode) {
    Require(value.state <= 2 && (value.state == 2) == value.payload.has_value() &&
        (value.state != 0 || !value.repeated), "first-entry observation phase");
    return Array({Number(value.state, 2), Boolean(value.repeated), value.payload ? encode(*value.payload) : "null"});
}
template<typename T> std::string OnceWire(Sample<T> const& value) {
    return OnceWire(value, [](T const& item) { return Put(item); });
}
template<typename T> std::string FirstWire(Sample<T> const& value) {
    return FirstWire(value, [](T const& item) { return Put(item); });
}
inline std::string Encode(Snapshot const& value, Frozen const& frozen, std::uint32_t pid,
    std::uint64_t begin, std::uint64_t end) {
    Require(!value.invalid && begin <= end, "observation capture invalid");
    auto cancel = [](Context const& item) { return Cancellation(item); };
    auto api = [](ApiSample const& item) {
        Require((item.returned.state != 2 && item.awaited.state != 2) || item.head.state == 2, "API phase binding");
        return Array({FirstWire(item.head), OnceWire(item.returned), OnceWire(item.awaited)});
    };
    Require(value.api[2].awaited.state == 0, "launch has no new await");
    Require(value.passed.state != 2 || value.began.state == 2, "finalizer reason dependency");
    Require(value.began.state != 2 || value.winner.state == 2, "finalizer began dependency");
    return Object({
        {"v", "1"}, {"pid", Native(pid)}, {"span", Array({Number(begin), Number(end)})},
        {"c", Array({FirstWire(value.cancellation[0], cancel), FirstWire(value.cancellation[1], cancel)})},
        {"r", OnceWire(value.reason)},
        {"a", Array({frozen.first ? Put(*frozen.first) : "null", frozen.minimum ? Put(*frozen.minimum) : "null"})},
        {"f", Array({FirstWire(value.attempts[0]), FirstWire(value.attempts[1]), FirstWire(value.attempts[2]),
            FirstWire(value.attempts[3]), FirstWire(value.attempts[4])})},
        {"w", Array({OnceWire(value.winner, [](Finish const& item) { return Winner(item); }),
            OnceWire(value.began), OnceWire(value.passed)})},
        {"i", frozen.operation ? Put(*frozen.operation) : "null"},
        {"x", Array({api(value.api[0]), api(value.api[1]), api(value.api[2])})},
        {"journal_status", "null"}
    });
}
}
