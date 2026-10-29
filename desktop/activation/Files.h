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
        explicit Security(Context const& context, Diagnostic* diagnostic = nullptr)
        {
            Step(diagnostic, L"prepare.security", L"package-sid");
            PSID package = nullptr;
            Point(diagnostic, L"DeriveAppContainerSidFromAppContainerName", L"win32");
            auto result = DeriveAppContainerSidFromAppContainerName(context.family.c_str(), &package);
            DerivedSidResult(diagnostic, static_cast<uint32_t>(result), package != nullptr);
            CheckResult(result, diagnostic);
            packageSid = CopySid(package, diagnostic, diagnostic ? &diagnostic->derivation.output : nullptr);
            FreeSid(package);
            LPWSTR userText = nullptr, packageText = nullptr;
            Step(diagnostic, L"prepare.security", L"user-sid");
            Point(diagnostic, L"ConvertSidToStringSidW", L"win32");
            Check(ConvertSidToStringSidW(const_cast<uint8_t*>(context.identity.user.data()), &userText) != FALSE, diagnostic);
            Point(diagnostic, L"string-allocation", L"cpp");
            std::wstring user(userText); LocalFree(userText);
            Step(diagnostic, L"prepare.security", L"package-sid");
            Point(diagnostic, L"ConvertSidToStringSidW", L"win32");
            Check(ConvertSidToStringSidW(packageSid.data(), &packageText) != FALSE, diagnostic);
            Point(diagnostic, L"string-allocation", L"cpp");
            std::wstring sid(packageText); LocalFree(packageText);
            Step(diagnostic, L"prepare.security", L"descriptor");
            auto text = L"O:" + user + L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;" + user +
                L")(A;OICI;FA;;;" + sid + L")S:(ML;OICI;NW;;;LW)";
            Point(diagnostic, L"ConvertStringSecurityDescriptorToSecurityDescriptorW", L"win32");
            Check(ConvertStringSecurityDescriptorToSecurityDescriptorW(text.c_str(), SDDL_REVISION_1,
                &descriptor, nullptr) != FALSE, diagnostic);
        }
        ~Security() { if (descriptor) LocalFree(descriptor); }
        Security(Security const&) = delete;
        Security& operator=(Security const&) = delete;
        SECURITY_ATTRIBUTES Attributes() const { return {sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE}; }
    };
    inline void InspectHandle(HANDLE file, Context const& context, Security const& security, bool directory,
        Diagnostic* diagnostic = nullptr)
    {
        FILE_ATTRIBUTE_TAG_INFO attributes{};
        Point(diagnostic, L"GetFileInformationByHandleEx", L"win32");
        Check(GetFileInformationByHandleEx(file, FileAttributeTagInfo, &attributes, sizeof(attributes)) != FALSE, diagnostic);
        Guard(!(attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT), E_ACCESSDENIED, diagnostic, L"no-reparse");
        Guard(!!(attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == directory,
            E_ACCESSDENIED, diagnostic, L"object-type");
        PSID owner = nullptr;
        PACL acl = nullptr;
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        Point(diagnostic, L"GetSecurityInfo", L"win32");
        auto error = GetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
            &owner, nullptr, &acl, nullptr, &descriptor);
        if (error)
        {
            ErrorCode(diagnostic, error, L"win32");
            throw hresult_error(HRESULT_FROM_WIN32(error));
        }
        struct Free { PSECURITY_DESCRIPTOR value; ~Free() { LocalFree(value); } } free{descriptor};
        Guard(owner && EqualSid(owner, const_cast<uint8_t*>(context.identity.user.data())),
            E_ACCESSDENIED, diagnostic, L"owner");
        Guard(acl != nullptr, E_ACCESSDENIED, diagnostic, L"dacl-present");
        SECURITY_DESCRIPTOR_CONTROL control{};
        DWORD revision = 0;
        Point(diagnostic, L"GetSecurityDescriptorControl", L"win32");
        Check(GetSecurityDescriptorControl(descriptor, &control, &revision) != FALSE, diagnostic);
        if (directory) Guard((control & SE_DACL_PROTECTED) != 0, E_ACCESSDENIED, diagnostic, L"dacl-protected");
        Point(diagnostic, L"buffer-allocation", L"cpp");
        auto system = std::vector<uint8_t>(SECURITY_MAX_SID_SIZE);
        DWORD bytes = static_cast<DWORD>(system.size());
        Point(diagnostic, L"CreateWellKnownSid", L"win32");
        Check(CreateWellKnownSid(WinLocalSystemSid, nullptr, system.data(), &bytes) != FALSE, diagnostic);
        bool userSeen = false, packageSeen = false, systemSeen = false;
        Guard(acl->AceCount == 3, E_ACCESSDENIED, diagnostic, L"ace-count");
        for (DWORD index = 0; index < acl->AceCount; ++index)
        {
            void* entry = nullptr;
            Point(diagnostic, L"GetAce", L"win32");
            Check(GetAce(acl, index, &entry) != FALSE, diagnostic);
            auto ace = static_cast<ACCESS_ALLOWED_ACE*>(entry);
            Guard(ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE, E_ACCESSDENIED, diagnostic, L"ace-type");
            Guard((ace->Mask & FILE_ALL_ACCESS) == FILE_ALL_ACCESS, E_ACCESSDENIED, diagnostic, L"ace-mask");
            PSID sid = &ace->SidStart;
            if (EqualSid(sid, const_cast<uint8_t*>(context.identity.user.data()))) userSeen = true;
            else if (EqualSid(sid, const_cast<uint8_t*>(security.packageSid.data()))) packageSeen = true;
            else if (EqualSid(sid, system.data())) systemSeen = true;
            else Guard(false, E_ACCESSDENIED, diagnostic, L"ace-principal");
        }
        Guard(userSeen && packageSeen && systemSeen, E_ACCESSDENIED, diagnostic, L"ace-principals-complete");
    }
    inline desktop::handle OpenDirectory(std::filesystem::path const& path, Context const& context, Security const& security,
        Diagnostic* diagnostic = nullptr)
    {
        Point(diagnostic, L"CreateFileW", L"win32", L"open-directory");
        HANDLE file = CreateFileW(path.c_str(), READ_CONTROL | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        Check(file != INVALID_HANDLE_VALUE, diagnostic);
        desktop::handle owned(file);
        InspectHandle(file, context, security, true, diagnostic);
        return owned;
    }
    inline void NewDirectory(std::filesystem::path const& path, Security const& security, Diagnostic* diagnostic = nullptr)
    {
        auto attributes = security.Attributes();
        Point(diagnostic, L"CreateDirectoryW", L"win32");
        Check(CreateDirectoryW(path.c_str(), &attributes) != FALSE, diagnostic);
    }
    inline void Atomic(std::filesystem::path const& root, wchar_t const* name, JsonObject const& value,
        Security const& security, Diagnostic* diagnostic = nullptr)
    {
        Point(diagnostic, L"JsonObject.Stringify/UTF8", L"winrt");
        auto bytes = to_string(value.Stringify());
        Guard(bytes.size() <= RecordLimit, HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE), diagnostic, L"record-size");
        Point(diagnostic, L"path-allocation", L"cpp");
        auto target = root / name;
        auto pending = root / (std::wstring(name) + L".pending");
        auto attributes = security.Attributes();
        Point(diagnostic, L"CreateFileW", L"win32", L"create-pending");
        HANDLE file = CreateFileW(pending.c_str(), GENERIC_WRITE, 0, &attributes, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        Check(file != INVALID_HANDLE_VALUE, diagnostic);
        desktop::handle owned(file);
        DWORD written = 0;
        Point(diagnostic, L"WriteFile", L"win32");
        Check(WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) != FALSE, diagnostic);
        Guard(written == bytes.size(), E_FAIL, diagnostic, L"write-complete");
        Point(diagnostic, L"FlushFileBuffers", L"win32");
        Check(FlushFileBuffers(file) != FALSE, diagnostic);
        owned.reset();
        Point(diagnostic, L"MoveFileExW", L"win32");
        Check(MoveFileExW(pending.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE, diagnostic);
    }
    inline JsonObject Read(std::filesystem::path const& root, wchar_t const* name,
        Context const& context, Security const& security, Diagnostic* diagnostic = nullptr)
    {
        Point(diagnostic, L"CreateFileW", L"win32", L"read-record");
        HANDLE file = CreateFileW((root / name).c_str(), GENERIC_READ | READ_CONTROL, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        Check(file != INVALID_HANDLE_VALUE, diagnostic);
        desktop::handle owned(file);
        InspectHandle(file, context, security, false, diagnostic);
        LARGE_INTEGER size{};
        Point(diagnostic, L"GetFileSizeEx", L"win32");
        Check(GetFileSizeEx(file, &size) != FALSE, diagnostic);
        Guard(size.QuadPart > 0 && size.QuadPart <= static_cast<LONGLONG>(RecordLimit),
            E_INVALIDARG, diagnostic, L"record-size");
        Point(diagnostic, L"buffer-allocation", L"cpp");
        std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
        DWORD count = 0;
        Point(diagnostic, L"ReadFile", L"win32");
        Check(ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) != FALSE, diagnostic);
        Guard(count == bytes.size(), E_FAIL, diagnostic, L"read-complete");
        Point(diagnostic, L"JsonObject.Parse/UTF8", L"winrt");
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
