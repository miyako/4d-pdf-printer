#pragma once
#include "Files.h"
#include <appmodel.h>
#include <tlhelp32.h>
#include <array>

namespace activation
{
    inline std::filesystem::path Base(Context const& context)
    { return std::filesystem::path(std::wstring(context.cache.Path())) / Folder; }
    inline std::wstring Prepare(Context const& context)
    {
        Security security(context);
        auto base = Base(context);
        auto attributes = security.Attributes();
        if (!CreateDirectoryW(base.c_str(), &attributes))
        {
            auto error = GetLastError();
            if (error != ERROR_ALREADY_EXISTS) throw hresult_error(HRESULT_FROM_WIN32(error));
        }
        auto lock = OpenDirectory(base, context, security);
        HANDLE admission = CreateFileW((base / L"admission.lock").c_str(),
            GENERIC_READ | GENERIC_WRITE | READ_CONTROL, 0, &attributes, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(admission != INVALID_HANDLE_VALUE);
        handle owned(admission);
        InspectHandle(admission, context, security, false);
        size_t jobs = 0;
        for (auto const& entry : std::filesystem::directory_iterator(base))
        {
            if (entry.path().filename() == L"admission.lock") continue;
            Require(Token(entry.path().filename().wstring()), E_ACCESSDENIED);
            auto previousLock = OpenDirectory(entry.path(), context, security);
            auto previous = Read(entry.path(), L"request.json", context, security);
            Require(!Fresh(Integer(previous, L"created_ms"), Integer(previous, L"expires_ms"), Now()),
                HRESULT_FROM_WIN32(ERROR_BUSY));
            ++jobs;
        }
        Require(jobs < 8, HRESULT_FROM_WIN32(ERROR_TOO_MANY_CMDS));
        auto id = RandomToken();
        auto root = base / id;
        NewDirectory(root, security);
        auto jobLock = OpenDirectory(root, context, security);
        auto now = Now();
        auto request = Bound(context, id, L"request", now, now + LifetimeMs);
        Text(request, L"endpoint", Endpoint);
        Atomic(root, L"request.json", request, security);
        return id;
    }
    inline std::wstring ProcessPath(HANDLE process)
    {
        std::array<wchar_t, 32768> buffer{};
        DWORD length = static_cast<DWORD>(buffer.size());
        Check(QueryFullProcessImageNameW(process, 0, buffer.data(), &length) != FALSE);
        return {buffer.data(), length};
    }
    inline void CheckProcess(HANDLE process, Context const& context, std::filesystem::path const& expected)
    {
        Require(_wcsicmp(ProcessPath(process).c_str(), expected.c_str()) == 0, E_ACCESSDENIED);
        HANDLE token = nullptr;
        Check(OpenProcessToken(process, TOKEN_QUERY, &token) != FALSE);
        handle owned(token);
        auto identity = ReadIdentity(token);
        Require(!identity.appContainer && identity.session == context.identity.session &&
            identity.user == context.identity.user, E_ACCESSDENIED);
        UINT32 size = 0;
        Require(GetPackageFullName(process, &size, nullptr) == ERROR_INSUFFICIENT_BUFFER, E_ACCESSDENIED);
        std::wstring name(size, L'\0');
        auto error = GetPackageFullName(process, &size, name.data());
        if (error) throw hresult_error(HRESULT_FROM_WIN32(error));
        name.resize(size - 1);
        Require(name == Windows::ApplicationModel::Package::Current().Id().FullName(), E_ACCESSDENIED);
    }
    inline uint64_t RemainingHosts(Context const& context, std::filesystem::path const& expected)
    {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        Check(snapshot != INVALID_HANDLE_VALUE);
        handle owned(snapshot);
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        Check(Process32FirstW(snapshot, &entry) != FALSE);
        uint64_t count = 0;
        do
        {
            if (_wcsicmp(entry.szExeFile, expected.filename().c_str()) != 0) continue;
            DWORD session = 0;
            if (!ProcessIdToSessionId(entry.th32ProcessID, &session))
            {
                if (GetLastError() == ERROR_INVALID_PARAMETER) continue;
                winrt::throw_last_error();
            }
            if (session != context.identity.session) continue;
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, entry.th32ProcessID);
            if (!process)
            {
                if (GetLastError() == ERROR_INVALID_PARAMETER) continue;
                winrt::throw_last_error();
            }
            handle processHandle(process);
            if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) continue;
            if (_wcsicmp(ProcessPath(process).c_str(), expected.c_str()) == 0)
            { CheckProcess(process, context, expected); ++count; }
        } while (Process32NextW(snapshot, &entry));
        Require(GetLastError() == ERROR_NO_MORE_FILES, E_FAIL);
        return count;
    }
    inline JsonObject Observe(Context const& context, std::wstring const& id)
    {
        Require(Token(id));
        auto report = Record(L"observer");
        Text(report, L"job_hash", Hash(id));
        Security security(context);
        auto baseLock = OpenDirectory(Base(context), context, security);
        auto root = Base(context) / id;
        auto lock = OpenDirectory(root, context, security);
        auto request = Read(root, L"request.json", context, security);
        Validate(request, context, id, L"request", Now());
        auto expected = std::filesystem::path(std::wstring(
            Windows::ApplicationModel::Package::Current().InstalledLocation().Path())) / L"DesktopPdfProbeHost.exe";
        handle host;
        bool validatedHost = false;
        wchar_t const* stage = L"waiting-for-host-entry";
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(LifetimeMs);
        try
        {
            while (!Exists(root, L"host-entry.json"))
            {
                Require(Now() < Integer(request, L"expires_ms") &&
                    std::chrono::steady_clock::now() < deadline, HRESULT_FROM_WIN32(ERROR_TIMEOUT));
                Sleep(25);
            }
            stage = L"verifying-host-process";
            auto callback = Read(root, L"callback.json", context, security);
            ValidateCallback(callback, request, context, id);
            auto entry = Read(root, L"host-entry.json", context, security);
            Validate(entry, context, id, L"host-entry", Now());
            auto pid = Integer(entry, L"pid");
            Require(pid > 0 && pid <= MAXDWORD, E_INVALIDARG);
            host.reset(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE,
                FALSE, static_cast<DWORD>(pid)));
            Check(host != nullptr);
            CheckProcess(host.get(), context, expected);
            Require(entry.GetNamedString(L"creation_filetime") == CreationText(host.get()) &&
                entry.GetNamedString(L"workflow_hash") == callback.GetNamedString(L"workflow_hash"), E_ACCESSDENIED);
            validatedHost = true;
            auto observing = Bound(context, id, L"observing", Integer(callback, L"created_ms"), Integer(callback, L"expires_ms"));
            Number(observing, L"pid", pid);
            Text(observing, L"creation_filetime", CreationText(host.get()));
            Atomic(root, L"observing.json", observing, security);
            stage = L"waiting-for-host-exit";
            auto waited = WaitForSingleObject(host.get(), 5000);
            Require(waited == WAIT_OBJECT_0, HRESULT_FROM_WIN32(ERROR_TIMEOUT));
            DWORD exit = MAXDWORD;
            Check(GetExitCodeProcess(host.get(), &exit) != FALSE);
            Number(report, L"host_exit_code", exit);
            Flag(report, L"host_exit_observed", true);
            Require(exit == 0, E_FAIL);
            stage = L"verifying-acknowledgment";
            auto ack = Read(root, L"ack.json", context, security);
            Validate(ack, context, id, L"ack", Now());
            Require(Integer(ack, L"pid") == pid &&
                ack.GetNamedString(L"creation_filetime") == CreationText(host.get()) &&
                ack.GetNamedString(L"workflow_hash") == callback.GetNamedString(L"workflow_hash") &&
                Integer(ack, L"ack_ms") < Integer(callback, L"expires_ms"), E_ACCESSDENIED);
            stage = L"waiting-for-callback-completion";
            while (!Exists(root, L"callback-result.json"))
            {
                Require(Now() < Integer(request, L"expires_ms") &&
                    std::chrono::steady_clock::now() < deadline, HRESULT_FROM_WIN32(ERROR_TIMEOUT));
                Sleep(25);
            }
            auto result = Read(root, L"callback-result.json", context, security);
            Validate(result, context, id, L"callback-result", Now());
            Require(result.GetNamedBoolean(L"roundtrip") && result.GetNamedBoolean(L"complete_failed_returned") &&
                result.GetNamedString(L"workflow_hash") == callback.GetNamedString(L"workflow_hash"), E_FAIL);
            Number(report, L"remaining_hosts_at_observation", RemainingHosts(context, expected));
            Require(Integer(report, L"remaining_hosts_at_observation") == 0, E_FAIL);
            Flag(report, L"callback_verified", true); Flag(report, L"ack_verified", true);
            Flag(report, L"activationGatePassed", true);
            Text(report, L"stage", L"actual-callback-host-roundtrip-no-pdf");
        }
        catch (...)
        {
            auto error = to_hresult();
            Text(report, L"stage", stage); Number(report, L"hresult", static_cast<uint32_t>(error.value));
            Flag(report, L"late_launch_not_excluded", true);
            for (auto name : {L"callback-result.json", L"host-error.json"})
            {
                try
                {
                    if (!Exists(root, name)) continue;
                    auto original = Read(root, name, context, security);
                    JsonObject diagnostic;
                    for (auto key : {L"stage", L"hresult", L"launch_result_available", L"launch_result",
                        L"launch_extended_hresult", L"roundtrip", L"complete_failed_returned",
                        L"late_launch_not_excluded", L"completion_hresult"})
                        if (original.HasKey(key)) diagnostic.Insert(key, original.GetNamedValue(key));
                    report.Insert(name == std::wstring_view(L"host-error.json") ? L"host_diagnostic" : L"callback_diagnostic",
                        diagnostic);
                }
                catch (...) { Number(report, L"diagnostic_read_hresult", static_cast<uint32_t>(to_hresult().value)); }
            }
            try
            {
                if (!Exists(root, L"closed.json"))
                    Atomic(root, L"closed.json", Bound(context, id, L"closed",
                        Integer(request, L"created_ms"), Integer(request, L"expires_ms")), security);
            }
            catch (...) { Number(report, L"close_record_hresult", static_cast<uint32_t>(to_hresult().value)); }
            if (validatedHost && WaitForSingleObject(host.get(), 0) != WAIT_OBJECT_0)
            {
                Flag(report, L"known_host_terminated", TerminateProcess(host.get(), ERROR_CANCELLED) != FALSE);
                Flag(report, L"known_host_exit_observed", WaitForSingleObject(host.get(), 5000) == WAIT_OBJECT_0);
            }
        }
        return report;
    }
}
