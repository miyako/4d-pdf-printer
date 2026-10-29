#pragma once
#include <string>
#include <string_view>
#include <stdexcept>

namespace smoke
{
    inline constexpr wchar_t Queue[] = L"4D PDF Activation Smoke (No PDF)";
    inline constexpr wchar_t TitlePrefix[] = L"PSA activation smoke ";
    inline bool ValidId(std::wstring_view id)
    {
        return id.size() == 32 && id.find_first_not_of(L"0123456789abcdef") == id.npos;
    }
    inline std::wstring ParseId(std::wstring_view text, std::wstring_view prefix)
    {
        if (!text.starts_with(prefix) || !ValidId(text.substr(prefix.size())))
            throw std::invalid_argument("strict test-ID argument/title parsing");
        return std::wstring(text.substr(prefix.size()));
    }
}
