#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

namespace activation
{
    inline constexpr std::wstring_view Prefix = L"PSAPROBE1:";
    inline constexpr wchar_t Endpoint[] = L"miyako-pdf-prototype:automation";
    inline constexpr wchar_t Queue[] = L"4D PDF Prototype (Automation)";
    inline constexpr wchar_t Folder[] = L"ActivationProbe-v1";
    inline constexpr uint64_t LifetimeMs = 30000;
    inline constexpr uint64_t CallbackMs = 12000;
    inline constexpr uint64_t HostWaitMs = 3000;
    inline constexpr uint64_t RecordLimit = 16384;
    inline bool Token(std::wstring_view text)
    {
        return text.size() == 64 && std::all_of(text.begin(), text.end(), [](wchar_t ch)
        { return (ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'f'); });
    }
    inline bool Fresh(uint64_t created, uint64_t expires, uint64_t now)
    {
        return expires > created && expires - created <= LifetimeMs &&
            now >= created && now < expires;
    }
    inline std::wstring JobFromTitle(std::wstring_view title)
    {
        if (!title.starts_with(Prefix) || !Token(title.substr(Prefix.size()))) return {};
        return std::wstring(title.substr(Prefix.size()));
    }
}
