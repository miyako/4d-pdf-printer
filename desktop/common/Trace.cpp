#include "Trace.h"
#include <bcrypt.h>
#include <array>
#include <vector>
#include <winrt/Windows.Foundation.h>

namespace proof
{
    JsonObject Record(wchar_t const* stage)
    {
        JsonObject value;
        Text(value, L"schema", L"4d-pdf-printer.product-proof");
        Number(value, L"version", 1);
        Text(value, L"stage", stage);
        Flag(value, L"productUsable", false);
        return value;
    }

    void ValidatePath(std::filesystem::path const& path, bool mustExist)
    {
        Require(path.is_absolute() && path.native().size() < 30000 &&
            path.native().find(L'\0') == std::wstring::npos &&
            path.root_name().native().size() == 2, "path", ERROR_NOT_SUPPORTED);
        auto root = path.root_path().native();
        Require(GetDriveTypeW(root.c_str()) == DRIVE_FIXED, "volume", ERROR_NOT_SUPPORTED);
        auto current = path.root_path();
        auto relative = path.lexically_normal().relative_path();
        for (auto const& component : relative)
        {
            Require(component != L".." && component != L"." &&
                component.native().find(L':') == std::wstring::npos, "path");
            current /= component;
            DWORD attributes = GetFileAttributesW(current.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES)
            {
                DWORD error = GetLastError();
                if (!mustExist && current == path.lexically_normal() &&
                    (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)) return;
                throw Failure("path", error);
            }
            Require(!(attributes & FILE_ATTRIBUTE_REPARSE_POINT), "reparse", ERROR_NOT_SUPPORTED);
        }
    }

    Handle OpenRead(std::filesystem::path const& path)
    {
        ValidatePath(path);
        Handle result(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        Check(result.get() != INVALID_HANDLE_VALUE, "open-input");
        BY_HANDLE_FILE_INFORMATION info{};
        Check(GetFileInformationByHandle(result.get(), &info), "input-identity");
        Require(!(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)),
            "input-kind");
        return result;
    }

    std::wstring Hash(HANDLE file)
    {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        std::vector<UCHAR> state;
        auto checked = [](NTSTATUS code)
        {
            if (code != 0) throw Failure("sha256", static_cast<DWORD>(code), "ntstatus");
        };
        checked(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0));
        struct Close
        {
            BCRYPT_ALG_HANDLE& algorithm;
            BCRYPT_HASH_HANDLE& hash;
            ~Close() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
        } close{algorithm, hash};
        DWORD length = 0, actual = 0;
        checked(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&length), sizeof(length), &actual, 0));
        state.resize(length);
        checked(BCryptCreateHash(algorithm, &hash, state.data(), length, nullptr, 0, 0));
        LARGE_INTEGER zero{};
        Check(SetFilePointerEx(file, zero, nullptr, FILE_BEGIN), "hash-seek");
        std::array<UCHAR, 65536> buffer{};
        DWORD read = 0;
        for (;;)
        {
            Check(ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr), "hash-read");
            if (!read) break;
            checked(BCryptHashData(hash, buffer.data(), read, 0));
        }
        std::array<UCHAR, 32> digest{};
        checked(BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0));
        Check(SetFilePointerEx(file, zero, nullptr, FILE_BEGIN), "hash-rewind");
        constexpr wchar_t hex[] = L"0123456789abcdef";
        std::wstring result;
        for (auto value : digest) { result += hex[value >> 4]; result += hex[value & 15]; }
        return result;
    }

    std::wstring Hash(std::filesystem::path const& path) { auto file = OpenRead(path); return Hash(file.get()); }

    JsonObject ReadJson(std::filesystem::path const& path)
    {
        auto file = OpenRead(path);
        LARGE_INTEGER size{};
        Check(GetFileSizeEx(file.get(), &size), "json-size");
        Require(size.QuadPart > 0 && size.QuadPart <= 1024 * 1024, "json-limit");
        std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
        DWORD read = 0;
        Check(ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr), "json-read");
        Require(read == bytes.size(), "json-short-read");
        if (bytes.starts_with("\xef\xbb\xbf")) bytes.erase(0, 3);
        return JsonObject::Parse(winrt::to_hstring(bytes));
    }

    void WriteNew(std::filesystem::path const& path, JsonObject const& json)
    {
        ValidatePath(path, false);
        Handle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr));
        Check(file.get() != INVALID_HANDLE_VALUE, "report-create");
        auto bytes = winrt::to_string(json.Stringify()) + "\n";
        Require(bytes.size() <= 4 * 1024 * 1024, "report-limit");
        DWORD written = 0;
        Check(WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr),
            "report-write");
        Require(written == bytes.size(), "report-short-write");
        Check(FlushFileBuffers(file.get()), "report-flush");
    }

    std::wstring Quote(std::wstring const& argument)
    {
        std::wstring result = L"\"";
        size_t slashes = 0;
        for (auto ch : argument)
        {
            if (ch == L'\\') { ++slashes; continue; }
            if (ch == L'"') result.append(slashes * 2 + 1, L'\\');
            else result.append(slashes, L'\\');
            result += ch;
            slashes = 0;
        }
        result.append(slashes * 2, L'\\');
        return result + L"\"";
    }

    Trace::Trace(std::filesystem::path const& path)
    {
        if (path.empty()) return;
        ValidatePath(path, false);
        file_.reset(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr));
        Check(file_.get() != INVALID_HANDLE_VALUE, "trace-create");
    }

    void Trace::Event(wchar_t const* stage, JsonObject json)
    {
        if (!file_.get()) return;
        json = JsonObject::Parse(json.Stringify());
        Text(json, L"stage", stage);
        Number(json, L"elapsed_ms", static_cast<uint64_t>(std::chrono::duration_cast<
            std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_).count()));
        FILETIME time{};
        GetSystemTimeAsFileTime(&time);
        ULARGE_INTEGER utc{};
        utc.LowPart = time.dwLowDateTime; utc.HighPart = time.dwHighDateTime;
        Text(json, L"utc_filetime", std::to_wstring(utc.QuadPart));
        auto bytes = winrt::to_string(json.Stringify()) + "\n";
        if (bytes_ + bytes.size() > 1024 * 1024) { truncated = true; return; }
        DWORD written = 0;
        Check(WriteFile(file_.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr), "trace-write");
        Require(written == bytes.size(), "trace-short-write");
        bytes_ += written;
    }
}
