#pragma once
#include <windows.h>
#include <appmodel.h>
#include <bcrypt.h>
#include <ntsecapi.h>
#include <sddl.h>
#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace evidence
{
    constexpr char Version[] = "2";
#if defined(_M_ARM64)
    constexpr char Architecture[] = "arm64";
#elif defined(_M_X64)
    constexpr char Architecture[] = "x64";
#else
#error Unsupported PrintProbe architecture
#endif

    struct Failure : std::runtime_error
    {
        DWORD code;
        Failure(char const* stage, DWORD value) : std::runtime_error(stage), code(value) {}
    };

    inline void Status(NTSTATUS status, char const* stage)
    {
        if (status < 0) throw Failure(stage, LsaNtStatusToWinError(status));
    }

    class Sha256
    {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
    public:
        Sha256()
        {
            Status(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0),
                "hash-provider-failed");
            auto status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
            if (status < 0)
            {
                BCryptCloseAlgorithmProvider(algorithm, 0);
                algorithm = nullptr;
                Status(status, "hash-create-failed");
            }
        }
        Sha256(Sha256 const&) = delete;
        Sha256& operator=(Sha256 const&) = delete;
        ~Sha256()
        {
            if (hash) BCryptDestroyHash(hash);
            if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        }
        void Add(unsigned char* bytes, ULONG size)
        {
            Status(BCryptHashData(hash, bytes, size, 0), "hash-data-failed");
        }
        std::string Finish()
        {
            std::array<unsigned char, 32> bytes{};
            Status(BCryptFinishHash(hash, bytes.data(), static_cast<ULONG>(bytes.size()), 0),
                "hash-finish-failed");
            std::string result;
            for (auto byte : bytes)
            {
                result += "0123456789abcdef"[byte >> 4];
                result += "0123456789abcdef"[byte & 15];
            }
            return result;
        }
    };

    inline std::string JobHash(std::wstring const& job)
    {
        int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, job.data(),
            static_cast<int>(job.size()), nullptr, 0, nullptr, nullptr);
        if (!size) throw Failure("job-utf8-size-failed", GetLastError());
        std::vector<unsigned char> bytes(static_cast<size_t>(size));
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, job.data(),
            static_cast<int>(job.size()), reinterpret_cast<char*>(bytes.data()), size, nullptr, nullptr))
            throw Failure("job-utf8-failed", GetLastError());
        Sha256 hash;
        hash.Add(bytes.data(), static_cast<ULONG>(bytes.size()));
        return hash.Finish();
    }

    struct CloseFile
    {
        void operator()(void* handle) const { CloseHandle(handle); }
    };
    using Handle = std::unique_ptr<void, CloseFile>;

    inline std::string ExecutableHash()
    {
        std::vector<wchar_t> path(32768);
        DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!length || length >= path.size())
            throw Failure("executable-path-failed", length ? ERROR_INSUFFICIENT_BUFFER : GetLastError());
        HANDLE file = CreateFileW(path.data(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE) throw Failure("executable-open-failed", GetLastError());
        Handle owned(file);
        Sha256 hash;
        std::array<unsigned char, 65536> bytes{};
        for (;;)
        {
            DWORD read = 0;
            if (!ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr))
                throw Failure("executable-read-failed", GetLastError());
            if (!read) break;
            hash.Add(bytes.data(), read);
        }
        return hash.Finish();
    }

    struct StartDocResult
    {
        int rawReturn;
        DWORD lastError;
        bool Succeeded() const { return rawReturn > 0; }
        std::string Fields() const
        {
            return ",\"startDocReturn\":" + std::to_string(rawReturn) +
                ",\"startDocLastError\":" + std::to_string(lastError);
        }
    };

    template<typename Call>
    StartDocResult CallStartDoc(Call call)
    {
        // StartDoc does not promise a meaningful LastError for every return.
        SetLastError(ERROR_SUCCESS);
        int raw = call();
        DWORD error = GetLastError();
        return {raw, error};
    }

    inline std::string Context()
    {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
            throw Failure("context-token-open-failed", GetLastError());
        Handle owned(token);
        DWORD size = 0;
        TOKEN_STATISTICS statistics{};
        if (!GetTokenInformation(token, TokenStatistics, &statistics, sizeof(statistics), &size))
            throw Failure("context-token-statistics-failed", GetLastError());
        DWORD session = 0;
        if (!GetTokenInformation(token, TokenSessionId, &session, sizeof(session), &size))
            throw Failure("context-token-session-failed", GetLastError());
        PSECURITY_LOGON_SESSION_DATA data = nullptr;
        NTSTATUS status = LsaGetLogonSessionData(&statistics.AuthenticationId, &data);
        std::string fields = ",\"tokenSessionId\":" + std::to_string(session) +
            ",\"logonQueryNtStatus\":" + std::to_string(static_cast<LONG>(status)) +
            ",\"logonType\":" + (status >= 0 && data ? std::to_string(data->LogonType) : "null");
        if (data) LsaFreeReturnBuffer(data);

        GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        DWORD error = GetLastError();
        if (error != ERROR_INSUFFICIENT_BUFFER) throw Failure("context-user-size-failed", error);
        std::vector<unsigned char> user(size);
        if (!GetTokenInformation(token, TokenUser, user.data(), size, &size))
            throw Failure("context-user-failed", GetLastError());
        LPWSTR sid = nullptr;
        if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid, &sid))
            throw Failure("context-sid-failed", GetLastError());
        HKEY hive = nullptr;
        LSTATUS hiveStatus = RegOpenKeyExW(HKEY_USERS, sid, 0, KEY_QUERY_VALUE, &hive);
        LocalFree(sid);
        if (hive) RegCloseKey(hive);
        fields += ",\"profileHiveOpenWin32Error\":" + std::to_string(hiveStatus);

        PACKAGE_ID id{};
        std::wstring name = L"Miyako.PsaFeasibility", publisher = L"CN=PsaFeasibilityDev";
        id.name = name.data();
        id.publisher = publisher.data();
        UINT32 length = 0;
        LONG result = PackageFamilyNameFromId(&id, &length, nullptr);
        if (result != ERROR_INSUFFICIENT_BUFFER) throw Failure("context-family-size-failed", result);
        std::vector<wchar_t> family(length);
        result = PackageFamilyNameFromId(&id, &length, family.data());
        if (result != ERROR_SUCCESS) throw Failure("context-family-failed", result);
        UINT32 count = 0;
        length = 0;
        result = FindPackagesByPackageFamily(family.data(), PACKAGE_FILTER_HEAD,
            &count, nullptr, &length, nullptr, nullptr);
        // This sizing call supplies a count, not package readiness or activation evidence.
        bool counted = result == ERROR_SUCCESS || result == ERROR_INSUFFICIENT_BUFFER;
        fields += ",\"packageQueryWin32Error\":" + std::to_string(result) +
            ",\"currentUserPackageCount\":" + (counted ? std::to_string(count) : "null");
        return fields;
    }
}
