#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace pdf {
enum class Mode { SaveAs, Silent };
inline constexpr std::uint64_t DefaultCompletionMs = 300000;
inline constexpr std::uint64_t DefaultRegistrationMs = 600000;
inline constexpr std::uint64_t CaptureBytes = 256ull * 1024 * 1024;
inline constexpr std::uint64_t RecordBytes = 65536;
inline constexpr std::uint64_t RuntimeRecordBytes = 1024 * 1024;
inline constexpr std::wstring_view TitlePrefix = L"4dpdf:";
inline std::wstring Name(Mode mode) { return mode == Mode::SaveAs ? L"SaveAs" : L"Silent"; }
inline void Require(bool value, char const* message) {
    if (!value) throw std::runtime_error(message);
}
inline bool Id(std::wstring_view value) {
    return value.size() == 32 && value.find_first_not_of(L"0123456789abcdef") == value.npos;
}
inline Mode ParseMode(std::wstring_view value) {
    if (value == L"SaveAs") return Mode::SaveAs;
    if (value == L"Silent") return Mode::Silent;
    throw std::runtime_error("invalid mode");
}
struct Activation { Mode mode; std::wstring id; };
inline Activation ParseActivation(std::wstring_view raw) {
    for (auto mode : {Mode::SaveAs, Mode::Silent}) {
        auto arguments = L"--mode " + Name(mode) + L" --job ";
        auto wrapped = L"/InvokerPRAID: " + Name(mode) + L" " + arguments;
        auto prefix = raw.starts_with(wrapped) ? wrapped : arguments;
        if (raw.starts_with(prefix) && Id(raw.substr(prefix.size())))
            return {mode, std::wstring(raw.substr(prefix.size()))};
    }
    throw std::runtime_error("unrecognized activation arguments");
}
inline std::wstring Registration(std::wstring_view title) {
    Require(title.starts_with(TitlePrefix) && Id(title.substr(TitlePrefix.size())),
        "invalid silent selector");
    return std::wstring(title.substr(TitlePrefix.size()));
}
inline std::uint64_t Duration(std::wstring_view text) {
    Require(!text.empty(), "empty duration");
    std::uint64_t result = 0;
    for (auto c : text) {
        Require(c >= L'0' && c <= L'9' && result <= (86400000ull - (c - L'0')) / 10,
            "duration must be 1..86400000 milliseconds");
        result = result * 10 + (c - L'0');
    }
    Require(result > 0, "zero duration");
    return result;
}
inline std::uint64_t Deadline(std::uint64_t submitted, std::uint64_t duration) {
    Require(duration > 0 && duration <= 86400000 && submitted <= 9007199254740991ull - duration,
        "invalid deadline");
    return submitted + duration;
}
inline bool Expired(std::uint64_t now, std::uint64_t deadline) { return now >= deadline; }
inline std::uint64_t Remaining(std::uint64_t now, std::uint64_t deadline, std::uint64_t limit) {
    return deadline > now ? std::min(deadline - now, limit) : 0;
}
inline bool LocalPath(std::wstring_view path) {
    if (path.size() < 4 || !((path[0] >= L'A' && path[0] <= L'Z') ||
        (path[0] >= L'a' && path[0] <= L'z')) || path[1] != L':' || path[2] != L'\\' ||
        path.find_first_of(L"/:*?\"<>|", 2) != path.npos) return false;
    std::size_t start = 3;
    while (start < path.size()) {
        auto end = path.find(L'\\', start);
        if (end == path.npos) end = path.size();
        auto part = path.substr(start, end - start);
        if (part.empty() || part.back() == L'.' || part.back() == L' ' ||
            part.find_first_of(L"\r\n\t") != part.npos) return false;
        for (auto c : part) if (c < 32) return false;
        std::wstring base(part.substr(0, part.find(L'.')));
        for (auto& c : base) if (c >= L'a' && c <= L'z') c = static_cast<wchar_t>(c - (L'a' - L'A'));
        if (base == L"CON" || base == L"PRN" || base == L"AUX" || base == L"NUL" ||
            (base.size() == 4 && (base.starts_with(L"COM") || base.starts_with(L"LPT")) &&
             ((base[3] >= L'1' && base[3] <= L'9') || base[3] == L'\u00b9' ||
                base[3] == L'\u00b2' || base[3] == L'\u00b3'))) return false;
        start = end + 1;
    }
    return path.back() != L'\\';
}
inline std::wstring Quote(std::wstring_view text) {
    std::wstring result = L"\"";
    unsigned slashes = 0;
    for (auto c : text) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(c == L'"' ? 2 * slashes + 1 : slashes, L'\\');
        slashes = 0;
        result += c;
    }
    result.append(2 * slashes, L'\\');
    return result + L"\"";
}

// WinRT's JSON map cannot detect duplicate keys after parsing. Check the wire first.
class JsonWire {
    std::string_view text;
    std::size_t at = 0;
    std::size_t limit;
    void Space() { while (at < text.size() && std::string_view(" \t\r\n").find(text[at]) != text.npos) ++at; }
    char Take() { Require(at < text.size(), "truncated JSON"); return text[at++]; }
    std::string String(bool key) {
        Require(Take() == '"', "JSON string expected");
        std::string value;
        for (;;) {
            auto c = static_cast<unsigned char>(Take());
            if (c == '"') return value;
            Require(c >= 32, "JSON control byte");
            if (c == '\\') {
                char escaped = Take();
                Require(std::string_view("\"\\/bfnrtu").find(escaped) != text.npos, "JSON escape");
                unsigned decoded = 0;
                if (escaped == 'u') {
                    for (int i = 0; i != 4; ++i) {
                        auto digit = Take();
                        auto position = std::string_view("0123456789abcdef").find(digit);
                        if (position == text.npos) position = std::string_view("0123456789ABCDEF").find(digit);
                        Require(position != text.npos, "JSON unicode");
                        decoded = 16 * decoded + static_cast<unsigned>(position);
                    }
                } else if (escaped == '"' || escaped == '\\' || escaped == '/') {
                    decoded = static_cast<unsigned char>(escaped);
                }
                if (key) {
                    Require(decoded >= 32 && decoded < 127, "unsupported JSON key");
                    value += static_cast<char>(decoded);
                }
                continue;
            }
            if (key) Require(c < 127, "unsupported JSON key");
            value += static_cast<char>(c);
        }
    }
    void Value(unsigned depth) {
        Require(depth < 32, "JSON nesting");
        Space();
        Require(at < text.size(), "missing JSON value");
        if (text[at] == '"') { String(false); return; }
        if (text[at] == '{' || text[at] == '[') {
            bool object = Take() == '{';
            char close = object ? '}' : ']';
            std::set<std::string> keys;
            Space();
            if (at < text.size() && text[at] == close) { ++at; return; }
            for (;;) {
                Space();
                if (object) {
                    Require(keys.insert(String(true)).second, "duplicate JSON key");
                    Space(); Require(Take() == ':', "JSON colon");
                }
                Value(depth + 1); Space();
                char next = Take();
                if (next == close) return;
                Require(next == ',', "JSON delimiter");
            }
        }
        auto start = at;
        while (at < text.size() && std::string_view(",]} \t\r\n").find(text[at]) == text.npos) ++at;
        Require(at > start, "JSON scalar");
        // Scalar grammar and UTF-8 are checked by the actual JSON parser.
    }
public:
    explicit JsonWire(std::string_view bytes, std::size_t maximum = RecordBytes) : text(bytes), limit(maximum) {}
    void Check() {
        Require(!text.empty() && text.size() <= limit, "JSON size");
        Space(); Require(at < text.size() && text[at] == '{', "JSON object required");
        Value(0); Space(); Require(at == text.size(), "extra JSON/status bytes");
    }
};
}
