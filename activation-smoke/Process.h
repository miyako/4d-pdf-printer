#pragma once
#include "Records.h"
#include <limits>

namespace smoke
{
    struct Handle
    {
        HANDLE value{};
        explicit Handle(HANDLE h = nullptr) : value(h) {}
        ~Handle() { if (value) CloseHandle(value); }
        Handle(Handle const&) = delete;
        Handle& operator=(Handle const&) = delete;
    };
    inline uint64_t Creation(HANDLE process)
    {
        FILETIME created{}, exited{}, kernel{}, user{};
        check_bool(GetProcessTimes(process, &created, &exited, &kernel, &user));
        return (uint64_t{created.dwHighDateTime} << 32) | created.dwLowDateTime;
    }
    inline std::wstring Image(HANDLE process)
    {
        wchar_t path[32768]{}; DWORD size = ARRAYSIZE(path);
        check_bool(QueryFullProcessImageNameW(process, 0, path, &size));
        return {path, size};
    }
    inline DWORD Pid(JsonObject const& record)
    {
        auto value = record.GetNamedNumber(L"pid");
        if (!(value >= 1 && value <= (std::numeric_limits<DWORD>::max)()) ||
            value != static_cast<DWORD>(value)) throw hresult_invalid_argument();
        return static_cast<DWORD>(value);
    }
    inline uint64_t ParseCreation(hstring const& text)
    {
        std::wstring value(text);
        if (value.empty() || value.find_first_not_of(L"0123456789") != value.npos)
            throw hresult_invalid_argument();
        size_t used{};
        auto result = std::stoull(value, &used);
        if (!result || used != value.size()) throw hresult_invalid_argument();
        return result;
    }
    inline void VerifyProcess(HANDLE process, std::wstring const& expected, uint64_t creation)
    {
        if (_wcsicmp(Image(process).c_str(), expected.c_str()) != 0 || Creation(process) != creation)
            throw hresult_error(HRESULT_FROM_WIN32(ERROR_INVALID_DATA), L"image/creation-time mismatch");
    }
    inline bool ObserveExit(HANDLE process, DWORD milliseconds, DWORD& code)
    {
        auto wait = WaitForSingleObject(process, milliseconds);
        if (wait == WAIT_TIMEOUT) return false;
        if (wait != WAIT_OBJECT_0) throw hresult_error(HRESULT_FROM_WIN32(GetLastError()), L"WaitForSingleObject");
        check_bool(GetExitCodeProcess(process, &code));
        return true;
    }
}
