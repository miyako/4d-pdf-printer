#include "Protocol.h"
#include "../common/Diagnostic.h"
#include <cstdio>
#include <stdexcept>
namespace
{
    void Expect(bool value) { if (!value) throw std::runtime_error("probe-unit-assertion"); }
    void ProtocolTests()
    {
        std::wstring id(64, L'a');
        Expect(activation::Token(id) && !activation::Token(std::wstring(63, L'a')) &&
            !activation::Token(std::wstring(64, L'G')) && !activation::Token(L"../job"));
        Expect(activation::JobFromTitle(std::wstring(activation::Prefix) + id) == id);
        Expect(activation::JobFromTitle(L"ordinary title").empty());
        Expect(activation::Fresh(100, 30100, 100) && activation::Fresh(100, 30100, 30099));
        Expect(!activation::Fresh(100, 30100, 30100) && !activation::Fresh(100, 30100, 99) &&
            !activation::Fresh(100, 30101, 100) && !activation::Fresh(100, 100, 100));
    }
    void DiagnosticCoreTests()
    {
        desktop::Diagnostic first, second;
        desktop::Step(&first, L"current.token", L"token-user");
        desktop::Point(&first, L"GetTokenInformation", L"win32", L"value-query");
        desktop::ErrorCode(&first, 5, L"win32");
        Expect(first.hasCode && first.code == 5 && !second.hasCode);
        desktop::Point(&first, L"validation", L"validation", L"owner");
        Expect(!first.hasCode && std::wstring_view(first.reason) == L"owner" &&
            std::wstring_view(second.stage) == L"control");
    }
}
#ifndef _WIN32
int main()
{
    try { ProtocolTests(); DiagnosticCoreTests(); std::puts("Portable protocol/diagnostic isolation cases passed; NOT actual PSA activation."); return 0; }
    catch (...) { std::fputs("Portable activation protocol failure.\n", stderr); return 1; }
}
#else
#include "Controller.h"
#include <objbase.h>
#include <array>

namespace
{
    activation::Context UnitContext()
    {
        activation::Context context;
        context.identity = desktop::CurrentIdentity();
        context.family = L"Miyako.DesktopPdfPrototype.Unit";
        context.userHash = activation::UserHash(context.identity);
        context.packageHash = desktop::Hash(context.family);
        return context;
    }
    std::wstring Executable()
    {
        std::array<wchar_t, 32768> name{};
        DWORD length = GetModuleFileNameW(nullptr, name.data(), static_cast<DWORD>(name.size()));
        activation::Require(length > 0 && length < name.size());
        return {name.data(), length};
    }
    void NativeDiagnosticTests(activation::Context const& context)
    {
        using namespace activation;
        Diagnostic diagnostic;
        Step(&diagnostic, L"current.token", L"process-token");
        Point(&diagnostic, L"OpenProcessToken", L"win32");
        SetLastError(ERROR_ACCESS_DENIED);
        bool rejected = false;
        try { Check(false, &diagnostic); }
        catch (hresult_error const& error) { rejected = error.code() == E_ACCESSDENIED; }
        Require(rejected && diagnostic.hasCode && diagnostic.code == ERROR_ACCESS_DENIED);
        auto report = activation::Record(L"control-arguments");
        DiagnosticRecord(report, diagnostic);
        auto metadata = report.GetNamedObject(L"error_context");
        Require(metadata.GetNamedString(L"origin") == L"win32" && Integer(metadata, L"code") == ERROR_ACCESS_DENIED);
        rejected = false;
        try { Guard(false, E_ACCESSDENIED, &diagnostic, L"owner"); }
        catch (hresult_error const& error) { rejected = error.code() == E_ACCESSDENIED; }
        Require(rejected && !diagnostic.hasCode && std::wstring_view(diagnostic.origin) == L"validation" &&
            std::wstring_view(diagnostic.reason) == L"owner");
        for (auto kind : {TokenUser, TokenSessionId, TokenIsAppContainer, TokenAppContainerSid,
            TokenType, TokenImpersonationLevel, TokenElevation})
        {
            rejected = false;
            try { TokenData(INVALID_HANDLE_VALUE, kind, &diagnostic); }
            catch (hresult_error const& error) { rejected = error.code() == E_ACCESSDENIED; }
            Require(rejected && diagnostic.hasCode && diagnostic.code == ERROR_INVALID_HANDLE &&
                std::wstring_view(diagnostic.object) == TokenRole(kind) &&
                std::wstring_view(diagnostic.reason) == L"token-size-probe");
        }
        rejected = false;
        try { desktop::CopySid(nullptr, &diagnostic); }
        catch (hresult_error const& error) { rejected = error.code() == E_ACCESSDENIED; }
        Require(rejected && !diagnostic.hasCode && std::wstring_view(diagnostic.reason) == L"sid-validity");
        Point(&diagnostic, L"DeriveAppContainerSidFromAppContainerName", L"win32");
        rejected = false;
        try { CheckResult(E_INVALIDARG, &diagnostic); }
        catch (hresult_error const& error) { rejected = error.code() == E_INVALIDARG; }
        Require(rejected && diagnostic.hasCode && diagnostic.code == static_cast<uint32_t>(E_INVALIDARG) &&
            std::wstring_view(diagnostic.codeKind) == L"hresult");
        Security security(context);
        Step(&diagnostic, L"prepare.base", L"base");
        rejected = false;
        try { InspectHandle(INVALID_HANDLE_VALUE, context, security, true, &diagnostic); }
        catch (hresult_error const& error) { rejected = error.code() == HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE); }
        Require(rejected && diagnostic.code == ERROR_INVALID_HANDLE &&
            std::wstring_view(diagnostic.api) == L"GetFileInformationByHandleEx");
        JsonObject value;
        Text(value, L"created_ms", L"not-a-number");
        Step(&diagnostic, L"prepare.created", L"existing-request");
        rejected = false;
        try { Integer(value, L"created_ms", &diagnostic); }
        catch (hresult_error const& error) { rejected = error.code() == E_INVALIDARG; }
        Require(rejected && std::wstring_view(diagnostic.reason) == L"integer-type");
        value.Insert(L"created_ms", JsonValue::CreateNumberValue(-1));
        rejected = false;
        try { Integer(value, L"created_ms", &diagnostic); }
        catch (hresult_error const& error) { rejected = error.code() == E_INVALIDARG; }
        Require(rejected && std::wstring_view(diagnostic.reason) == L"integer-range");
    }
    void StateCompatibilityTest(std::filesystem::path const& base, activation::Context context,
        activation::Security const& security)
    {
        using namespace activation;
        auto cache = base / L"compatibility-cache";
        NewDirectory(cache, security);
        context.cache = StorageFolder::GetFolderFromPathAsync(cache.wstring()).get();
        context.packageHash = Hash(L"new-full-package-name-0.2.1.0-unit");
        auto previousContext = context;
        previousContext.packageHash = Hash(L"old-full-package-name-0.2.0.0-unit");
        auto folder = Base(context);
        NewDirectory(folder, security);
        auto oldId = RandomToken();
        auto oldRoot = folder / oldId;
        NewDirectory(oldRoot, security);
        auto now = Now();
        auto expired = Bound(previousContext, oldId, L"request", now - 60000, now - 30000);
        Atomic(oldRoot, L"request.json", expired, security);
        Diagnostic diagnostic;
        auto newId = Prepare(context, &diagnostic);
        Require(Read(oldRoot, L"request.json", context, security).Stringify() == expired.Stringify());
        bool busy = false;
        try { Prepare(context, &diagnostic); }
        catch (hresult_error const& error) { busy = error.code() == HRESULT_FROM_WIN32(ERROR_BUSY); }
        Require(busy && std::wstring_view(diagnostic.reason) == L"request-not-fresh");
        auto liveNow = Now();
        auto oldLive = Bound(previousContext, oldId, L"request", liveNow, liveNow + LifetimeMs);
        Validate(oldLive, previousContext, oldId, L"request", liveNow);
        bool replayRejected = false;
        try { Validate(oldLive, context, oldId, L"request", Now()); }
        catch (hresult_error const& error) { replayRejected = error.code() == E_ACCESSDENIED; }
        Require(replayRejected);
        context.cache = nullptr; previousContext.cache = nullptr;
        for (auto const& id : {oldId, newId})
        {
            std::filesystem::remove(folder / id / L"request.json");
            std::filesystem::remove(folder / id);
        }
        std::filesystem::remove(folder / L"admission.lock");
        std::filesystem::remove(folder); std::filesystem::remove(cache);
    }
}
int wmain(int count, wchar_t** args)
{
    using namespace activation;
    std::filesystem::path base;
    try
    {
        init_apartment();
        ProtocolTests();
        DiagnosticCoreTests();
        auto context = UnitContext();
        if (count == 4 && std::wstring_view(args[1]) == L"--controlled-unit-child")
            return Host(context, args[2], args[3]);
        Require(count == 1);
        NativeDiagnosticTests(context);
        Security security(context);
        GUID guid{};
        check_hresult(CoCreateGuid(&guid));
        wchar_t text[40]{};
        Require(StringFromGUID2(guid, text, 40) > 0);
        std::array<wchar_t, MAX_PATH> temporary{};
        DWORD length = GetTempPathW(static_cast<DWORD>(temporary.size()), temporary.data());
        Require(length > 0 && length < temporary.size());
        base = std::filesystem::path(temporary.data()) / (L"activation-probe-unit-" + std::wstring(text));
        NewDirectory(base, security);
        StateCompatibilityTest(base, context, security);
        auto id = RandomToken();
        auto root = base / id;
        NewDirectory(root, security);
        auto baseLock = OpenDirectory(base, context, security);
        auto jobLock = OpenDirectory(root, context, security);
        auto now = Now();
        auto request = Bound(context, id, L"request", now, now + LifetimeMs);
        Text(request, L"unicode_unit", L"\u65E5\u672C\u8A9E \u00E9");
        Atomic(root, L"request.json", request, security);
        auto copy = Read(root, L"request.json", context, security);
        Validate(copy, context, id, L"request", now);
        bool duplicate = false;
        Diagnostic fileDiagnostic;
        Step(&fileDiagnostic, L"prepare.request", L"new-request");
        try { Atomic(root, L"request.json", request, security, &fileDiagnostic); }
        catch (hresult_error const& error)
        {
            duplicate = fileDiagnostic.hasCode &&
                error.code() == HRESULT_FROM_WIN32(fileDiagnostic.code) &&
                std::wstring_view(fileDiagnostic.api) == L"MoveFileExW";
        }
        Require(duplicate && Read(root, L"request.json", context, security).Stringify() == copy.Stringify());
        auto wrong = copy;
        Number(wrong, L"session", context.identity.session + 1);
        bool rejected = false;
        try { Validate(wrong, context, id, L"request", now); } catch (hresult_error const&) { rejected = true; }
        Require(rejected);
        rejected = false;
        try { Validate(request, context, id, L"request", now + LifetimeMs); }
        catch (hresult_error const&) { rejected = true; }
        Require(rejected);
        auto callback = Bound(context, id, L"callback", now, now + CallbackMs);
        Flag(callback, L"appcontainer", true); Text(callback, L"endpoint", activation::Endpoint);
        Text(callback, L"request_hash", Hash(std::wstring(request.Stringify())));
        Text(callback, L"title_hash", Hash(std::wstring(Prefix) + id));
        Text(callback, L"workflow_hash", Hash(L"controlled-unit-not-an-actual-PSA-callback"));
        Atomic(root, L"callback.json", callback, security);
        auto exe = Executable();
        std::wstring command = L"\"" + exe + L"\" --controlled-unit-child \"" + base.wstring() + L"\" " + id;
        desktop::handle job(CreateJobObjectW(nullptr, nullptr));
        Check(job != nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        Check(SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)) != FALSE);
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        Check(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != FALSE);
        desktop::handle child(process.hProcess), thread(process.hThread);
        if (!AssignProcessToJobObject(job.get(), child.get()))
        {
            auto error = GetLastError();
            TerminateProcess(child.get(), ERROR_CANCELLED); WaitForSingleObject(child.get(), 5000);
            throw hresult_error(HRESULT_FROM_WIN32(error));
        }
        Require(ResumeThread(thread.get()) != MAXDWORD);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!Exists(root, L"host-entry.json"))
        {
            Require(WaitForSingleObject(child.get(), 0) == WAIT_TIMEOUT &&
                std::chrono::steady_clock::now() < deadline, E_FAIL);
            Sleep(10);
        }
        auto entry = Read(root, L"host-entry.json", context, security);
        Require(Integer(entry, L"pid") == process.dwProcessId &&
            entry.GetNamedString(L"creation_filetime") == CreationText(child.get()));
        auto observing = Bound(context, id, L"observing", now, now + CallbackMs);
        Number(observing, L"pid", process.dwProcessId);
        Text(observing, L"creation_filetime", CreationText(child.get()));
        Atomic(root, L"observing.json", observing, security);
        Require(WaitForSingleObject(child.get(), 5000) == WAIT_OBJECT_0);
        DWORD exit = MAXDWORD;
        Check(GetExitCodeProcess(child.get(), &exit) != FALSE);
        Require(exit == 0);
        Validate(Read(root, L"ack.json", context, security), context, id, L"ack", Now());
        bool replay = false;
        try { Host(context, base, id); } catch (hresult_error const&) { replay = true; }
        Require(replay);
        auto unprotected = base / L"unprotected-unit";
        Check(CreateDirectoryW(unprotected.c_str(), nullptr) != FALSE);
        rejected = false;
        Step(&fileDiagnostic, L"prepare.existing", L"existing-job");
        try { auto unsafe = OpenDirectory(unprotected, context, security, &fileDiagnostic); }
        catch (hresult_error const&) { rejected = true; }
        Require(rejected && std::wstring_view(fileDiagnostic.origin) == L"validation" &&
            std::wstring_view(fileDiagnostic.reason) == L"dacl-protected" && !fileDiagnostic.hasCode);
        std::filesystem::remove(unprotected);
        auto waitingId = RandomToken();
        auto waitingRoot = base / waitingId;
        NewDirectory(waitingRoot, security);
        auto waitingNow = Now();
        auto waitingRequest = Bound(context, waitingId, L"request", waitingNow, waitingNow + LifetimeMs);
        auto waitingCallback = Bound(context, waitingId, L"callback", waitingNow, waitingNow + CallbackMs);
        Flag(waitingCallback, L"appcontainer", true);
        Text(waitingCallback, L"endpoint", activation::Endpoint);
        Text(waitingCallback, L"request_hash", Hash(std::wstring(waitingRequest.Stringify())));
        Text(waitingCallback, L"title_hash", Hash(std::wstring(Prefix) + waitingId));
        Text(waitingCallback, L"workflow_hash", Hash(L"unit-no-observer-not-a-PSA-callback"));
        Atomic(waitingRoot, L"request.json", waitingRequest, security);
        Atomic(waitingRoot, L"callback.json", waitingCallback, security);
        command = L"\"" + exe + L"\" --controlled-unit-child \"" + base.wstring() + L"\" " + waitingId;
        PROCESS_INFORMATION waitingProcess{};
        Check(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup, &waitingProcess) != FALSE);
        desktop::handle waitingChild(waitingProcess.hProcess), waitingThread(waitingProcess.hThread);
        if (!AssignProcessToJobObject(job.get(), waitingChild.get()))
        {
            auto error = GetLastError();
            TerminateProcess(waitingChild.get(), ERROR_CANCELLED); WaitForSingleObject(waitingChild.get(), 5000);
            throw hresult_error(HRESULT_FROM_WIN32(error));
        }
        Require(ResumeThread(waitingThread.get()) != MAXDWORD);
        Require(WaitForSingleObject(waitingChild.get(), 6000) == WAIT_OBJECT_0);
        Check(GetExitCodeProcess(waitingChild.get(), &exit) != FALSE);
        Require(exit == 1 && Exists(waitingRoot, L"host-entry.json") && !Exists(waitingRoot, L"ack.json"));
        auto wrongCallback = JsonObject::Parse(callback.Stringify());
        Text(wrongCallback, L"request_hash", L"wrong");
        rejected = false;
        try { ValidateCallback(wrongCallback, request, context, id); }
        catch (hresult_error const&) { rejected = true; }
        Require(rejected);
        jobLock.reset(); baseLock.reset(); job.reset();
        for (auto const& entryFile : std::filesystem::directory_iterator(root)) std::filesystem::remove(entryFile.path());
        for (auto const& entryFile : std::filesystem::directory_iterator(waitingRoot)) std::filesystem::remove(entryFile.path());
        std::filesystem::remove(root); std::filesystem::remove(waitingRoot); std::filesystem::remove(base); base.clear();
        std::puts("Protected files, Unicode, duplicate/replay, expiry/session/request mismatch, unprotected ACL rejection: passed.");
        std::puts("Diagnostic API/validation HRESULT preservation and retained old-version admission/replay cases: passed.");
        std::puts("Fake UnitContext/temp files; NOT a real nonadmin packaged Current/AppContainer test.");
        std::puts("Controlled child ack+exit0 and missing-observer bounded exit1/no-ack: passed. NOT actual PSA activation.");
        return 0;
    }
    catch (...)
    {
        std::fprintf(stderr, "Activation probe unit failed HRESULT=0x%08X; no PSA/printing performed.\n",
            static_cast<unsigned>(to_hresult().value));
        return 1;
    }
}
#endif
