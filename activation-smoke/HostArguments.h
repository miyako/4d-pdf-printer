#pragma once
#include "Contract.h"

namespace smoke
{
    inline constexpr wchar_t HostActivationPrefix[] = L"/InvokerPRAID: Printer --test-id ";
    inline std::wstring ParseHostArguments(std::wstring_view arguments)
    {
        return ParseId(arguments, arguments.starts_with(HostActivationPrefix) ?
            std::wstring_view(HostActivationPrefix) : std::wstring_view(L"--test-id "));
    }
}
