#pragma once
#include "Contract.h"
#include <aclapi.h>
#include <sddl.h>
#include <userenv.h>
#include <filesystem>

namespace activation
{
    struct Security
    {
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        std::vector<uint8_t> packageSid;
        explicit Security(Context const& context)
        {
            PSID package = nullptr;
            check_hresult(DeriveAppContainerSidFromAppContainerName(context.family.c_str(), &package));
            packageSid = CopySid(package);
            FreeSid(package);
            LPWSTR userText = nullptr, packageText = nullptr;
            Check(ConvertSidToStringSidW(const_cast<uint8_t*>(context.identity.user.data()), &userText) != FALSE);
            std::wstring user(userText); LocalFree(userText);
            Check(ConvertSidToStringSidW(packageSid.data(), &packageText) != FALSE);
            std::wstring sid(packageText); LocalFree(packageText);
            auto text = L"O:" + user + L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;" + user +
                L")(A;OICI;FA;;;" + sid + L")S:(ML;OICI;NW;;;LW)";
            Check(ConvertStringSecurityDescriptorToSecurityDescriptorW(text.c_str(), SDDL_REVISION_1,
                &descriptor, nullptr) != FALSE);
        }
        ~Security() { if (descriptor) LocalFree(descriptor); }
        Security(Security const&) = delete;
        Security& operator=(Security const&) = delete;
        SECURITY_ATTRIBUTES Attributes() const { return {sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE}; }
    };
    inline void InspectHandle(HANDLE file, Context const& context, Security const& security, bool directory)
    {
        FILE_ATTRIBUTE_TAG_INFO attributes{};
        Check(GetFileInformationByHandleEx(file, FileAttributeTagInfo, &attributes, sizeof(attributes)) != FALSE);
        Require(!(attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
            !!(attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == directory, E_ACCESSDENIED);
        PSID owner = nullptr;
        PACL acl = nullptr;
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        auto error = GetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
            &owner, nullptr, &acl, nullptr, &descriptor);
        if (error) throw hresult_error(HRESULT_FROM_WIN32(error));
        struct Free { PSECURITY_DESCRIPTOR value; ~Free() { LocalFree(value); } } free{descriptor};
        Require(owner && EqualSid(owner, const_cast<uint8_t*>(context.identity.user.data())) && acl, E_ACCESSDENIED);
        SECURITY_DESCRIPTOR_CONTROL control{};
        DWORD revision = 0;
        Check(GetSecurityDescriptorControl(descriptor, &control, &revision) != FALSE);
        if (directory) Require((control & SE_DACL_PROTECTED) != 0, E_ACCESSDENIED);
        auto system = std::vector<uint8_t>(SECURITY_MAX_SID_SIZE);
        DWORD bytes = static_cast<DWORD>(system.size());
        Check(CreateWellKnownSid(WinLocalSystemSid, nullptr, system.data(), &bytes) != FALSE);
        bool userSeen = false, packageSeen = false, systemSeen = false;
        Require(acl->AceCount == 3, E_ACCESSDENIED);
        for (DWORD index = 0; index < acl->AceCount; ++index)
        {
            void* entry = nullptr;
            Check(GetAce(acl, index, &entry) != FALSE);
            auto ace = static_cast<ACCESS_ALLOWED_ACE*>(entry);
            Require(ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE &&
                (ace->Mask & FILE_ALL_ACCESS) == FILE_ALL_ACCESS, E_ACCESSDENIED);
            PSID sid = &ace->SidStart;
            if (EqualSid(sid, const_cast<uint8_t*>(context.identity.user.data()))) userSeen = true;
            else if (EqualSid(sid, const_cast<uint8_t*>(security.packageSid.data()))) packageSeen = true;
            else if (EqualSid(sid, system.data())) systemSeen = true;
            else throw hresult_access_denied();
        }
        Require(userSeen && packageSeen && systemSeen, E_ACCESSDENIED);
    }
    inline handle OpenDirectory(std::filesystem::path const& path, Context const& context, Security const& security)
    {
        HANDLE file = CreateFileW(path.c_str(), READ_CONTROL | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        Check(file != INVALID_HANDLE_VALUE);
        handle owned(file);
        InspectHandle(file, context, security, true);
        return owned;
    }
    inline void NewDirectory(std::filesystem::path const& path, Security const& security)
    {
        auto attributes = security.Attributes();
        Check(CreateDirectoryW(path.c_str(), &attributes) != FALSE);
    }
    inline void Atomic(std::filesystem::path const& root, wchar_t const* name, JsonObject const& value,
        Security const& security)
    {
        auto bytes = to_string(value.Stringify());
        Require(bytes.size() <= RecordLimit, HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
        auto target = root / name;
        auto pending = root / (std::wstring(name) + L".pending");
        auto attributes = security.Attributes();
        HANDLE file = CreateFileW(pending.c_str(), GENERIC_WRITE, 0, &attributes, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        Check(file != INVALID_HANDLE_VALUE);
        handle owned(file);
        DWORD written = 0;
        Check(WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) != FALSE);
        Require(written == bytes.size(), E_FAIL);
        Check(FlushFileBuffers(file) != FALSE);
        owned.reset();
        Check(MoveFileExW(pending.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE);
    }
    inline JsonObject Read(std::filesystem::path const& root, wchar_t const* name,
        Context const& context, Security const& security)
    {
        HANDLE file = CreateFileW((root / name).c_str(), GENERIC_READ | READ_CONTROL, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        Check(file != INVALID_HANDLE_VALUE);
        handle owned(file);
        InspectHandle(file, context, security, false);
        LARGE_INTEGER size{};
        Check(GetFileSizeEx(file, &size) != FALSE);
        Require(size.QuadPart > 0 && size.QuadPart <= static_cast<LONGLONG>(RecordLimit), E_INVALIDARG);
        std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
        DWORD count = 0;
        Check(ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) != FALSE);
        Require(count == bytes.size(), E_FAIL);
        return JsonObject::Parse(to_hstring(bytes));
    }
    inline bool Exists(std::filesystem::path const& root, wchar_t const* name)
    {
        auto attributes = GetFileAttributesW((root / name).c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES) return true;
        auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return false;
        throw hresult_error(HRESULT_FROM_WIN32(error));
    }
    inline uint64_t Creation(HANDLE process)
    {
        FILETIME created{}, exited{}, kernel{}, user{};
        Check(GetProcessTimes(process, &created, &exited, &kernel, &user) != FALSE);
        return (static_cast<uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    }
    inline std::wstring CreationText(HANDLE process) { return std::to_wstring(Creation(process)); }
    inline void ValidateCallback(JsonObject const& callback, JsonObject const& request,
        Context const& context, std::wstring const& id)
    {
        Validate(request, context, id, L"request", Now());
        Validate(callback, context, id, L"callback", Now());
        Require(callback.GetNamedBoolean(L"appcontainer") &&
            callback.GetNamedString(L"endpoint") == Endpoint &&
            callback.GetNamedString(L"request_hash") == Hash(std::wstring(request.Stringify())) &&
            callback.GetNamedString(L"title_hash") == Hash(std::wstring(Prefix) + id) &&
            !callback.GetNamedString(L"workflow_hash").empty() &&
            Integer(callback, L"expires_ms") <= Integer(request, L"expires_ms"), E_ACCESSDENIED);
    }
    inline int Host(Context const& context, std::filesystem::path const& base, std::wstring const& id)
    {
        Require(Token(id));
        Security security(context);
        auto baseLock = OpenDirectory(base, context, security);
        auto root = base / id;
        auto lock = OpenDirectory(root, context, security);
        auto request = Read(root, L"request.json", context, security);
        auto callback = Read(root, L"callback.json", context, security);
        ValidateCallback(callback, request, context, id);
        Require(!Exists(root, L"closed.json"), HRESULT_FROM_WIN32(ERROR_CANCELLED));
        auto entry = Bound(context, id, L"host-entry", Integer(callback, L"created_ms"), Integer(callback, L"expires_ms"));
        Number(entry, L"pid", GetCurrentProcessId());
        Text(entry, L"creation_filetime", CreationText(GetCurrentProcess()));
        Text(entry, L"workflow_hash", std::wstring(callback.GetNamedString(L"workflow_hash")));
        Atomic(root, L"host-entry.json", entry, security);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(HostWaitMs);
        while (!Exists(root, L"observing.json"))
        {
            Require(Now() < Integer(callback, L"expires_ms") && !Exists(root, L"closed.json") &&
                std::chrono::steady_clock::now() < deadline, HRESULT_FROM_WIN32(ERROR_TIMEOUT));
            Sleep(25);
        }
        auto observing = Read(root, L"observing.json", context, security);
        Validate(observing, context, id, L"observing", Now());
        Require(Integer(observing, L"pid") == GetCurrentProcessId() &&
            observing.GetNamedString(L"creation_filetime") == CreationText(GetCurrentProcess()) &&
            !Exists(root, L"closed.json") && Now() < Integer(callback, L"expires_ms"), E_ACCESSDENIED);
        auto ack = Bound(context, id, L"ack", Integer(callback, L"created_ms"), Integer(callback, L"expires_ms"));
        Number(ack, L"pid", GetCurrentProcessId());
        Text(ack, L"creation_filetime", CreationText(GetCurrentProcess()));
        Text(ack, L"workflow_hash", std::wstring(callback.GetNamedString(L"workflow_hash")));
        Number(ack, L"ack_ms", Now());
        Atomic(root, L"ack.json", ack, security);
        return 0;
    }
}
