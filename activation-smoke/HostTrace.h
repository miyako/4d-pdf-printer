#pragma once
#include <windows.h>
#include <appmodel.h>
#include <shlobj.h>
#include <cstdio>
#include <string>
#include <string_view>

namespace smoke
{
    struct HostTrace
    {
        HANDLE file = INVALID_HANDLE_VALUE;
        DWORD error = ERROR_SUCCESS, bytes = 0;
        static constexpr DWORD Limit = 8192;
        ~HostTrace() { if (file != INVALID_HANDLE_VALUE) CloseHandle(file); }
        HostTrace() = default;
        HostTrace(HostTrace const&) = delete;
        HostTrace& operator=(HostTrace const&) = delete;

        void Fail(wchar_t const* api, DWORD code) noexcept
        {
            if (!error) error = code;
            wchar_t message[256]{};
            swprintf_s(message, L"PSA smoke HOST TRACE FAILURE: %s rawCode=0x%08lX (primary host error unchanged)\n", api, code);
            OutputDebugStringW(message);
        }
        bool OpenPath(wchar_t const* path) noexcept
        {
            file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
            if (file == INVALID_HANDLE_VALUE) { Fail(L"CreateFileW(CREATE_NEW)", GetLastError()); return false; }
            return true;
        }
        bool Open()
        {
            wchar_t family[PACKAGE_FAMILY_NAME_MAX_LENGTH]{}; UINT32 count = ARRAYSIZE(family);
            LONG status = GetCurrentPackageFamilyName(&count, family);
            if (status != ERROR_SUCCESS) { Fail(L"GetCurrentPackageFamilyName", static_cast<DWORD>(status)); return false; }
            if (!std::wstring_view(family).starts_with(L"Miyako.PsaActivationSmoke_"))
            { Fail(L"unexpected smoke package family", ERROR_INVALID_DATA); return false; }
            PWSTR local = nullptr;
            HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_NO_PACKAGE_REDIRECTION, nullptr, &local);
            if (FAILED(hr))
            {
                if (local) CoTaskMemFree(local);
                Fail(L"SHGetKnownFolderPath(no package redirection)", static_cast<DWORD>(hr)); return false;
            }
            std::wstring cache(local); CoTaskMemFree(local);
            cache += L"\\Packages\\" + std::wstring(family) + L"\\LocalCache";
            DWORD attributes = GetFileAttributesW(cache.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES)
            { Fail(L"GetFileAttributesW(existing LocalCache)", GetLastError()); return false; }
            if (!(attributes & FILE_ATTRIBUTE_DIRECTORY))
            { Fail(L"LocalCache is not directory", ERROR_DIRECTORY); return false; }
            FILETIME now{}; GetSystemTimeAsFileTime(&now);
            wchar_t name[96]{};
            swprintf_s(name, L"\\host-startup-%lu-%08lX%08lX.txt", GetCurrentProcessId(),
                now.dwHighDateTime, now.dwLowDateTime);
            return OpenPath((cache + name).c_str());
        }
        bool Write(std::string_view text) noexcept
        {
            if (error || file == INVALID_HANDLE_VALUE)
            { Fail(L"trace sink unavailable", error ? error : ERROR_INVALID_HANDLE); return false; }
            if (text.size() > Limit - bytes)
            { Fail(L"trace size bound", ERROR_FILE_TOO_LARGE); return false; }
            DWORD written{};
            if (!WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr))
            { Fail(L"WriteFile(trace)", GetLastError()); return false; }
            bytes += written;
            if (written != text.size()) { Fail(L"WriteFile(trace short write)", ERROR_WRITE_FAULT); return false; }
            return true;
        }
        bool Stage(char const* stage, HRESULT primary = S_OK, DWORD win32 = ERROR_SUCCESS) noexcept
        {
            char line[256]{};
            int length = std::snprintf(line, sizeof(line), "stage=%s primaryHresult=0x%08lX win32Snapshot=%lu\n",
                stage, static_cast<unsigned long>(primary), win32);
            if (length < 0 || static_cast<size_t>(length) >= sizeof(line))
            { Fail(L"trace stage formatting bound", ERROR_INSUFFICIENT_BUFFER); return false; }
            return Write(std::string_view(line, static_cast<size_t>(length)));
        }
        struct FailureReport { HRESULT primary; DWORD sinkError; int exitCode; };
        FailureReport ReportFailure(char const* stage, HRESULT primary, DWORD win32) noexcept
        {
            char message[384]{};
            std::snprintf(message, sizeof(message), "PSA smoke host: stage=%.128s primaryHresult=0x%08lX win32Snapshot=%lu\n",
                stage, static_cast<unsigned long>(primary), win32);
            OutputDebugStringA(message);
            DWORD written{};
            DWORD length = static_cast<DWORD>(std::char_traits<char>::length(message));
            if (!WriteFile(GetStdHandle(STD_ERROR_HANDLE), message, length, &written, nullptr) || written != length)
                OutputDebugStringA("PSA smoke host: stderr unavailable/write failed; primary diagnostic sent to debugger\n");
            Stage(stage, primary, win32);
            return {primary, error, 1};
        }
        bool Arguments(char const* label, wchar_t const* value)
        {
            std::string line(label); line += "Utf16Hex=";
            size_t index = 0;
            for (; index < 256 && value[index]; ++index)
            {
                char hex[5]{}; sprintf_s(hex, "%04X", static_cast<unsigned>(value[index]));
                line += hex;
            }
            line += value[index] ? " truncated=true\n" : " truncated=false\n";
            return Write(line);
        }
    };
}
