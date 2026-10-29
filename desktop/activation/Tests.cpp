#include "Protocol.h"
#include "../common/Diagnostic.h"
#include <cstdio>
#include <source_location>
#include <string>
namespace
{
    struct UnitAssertion { uint_least32_t line; };
    void Expect(bool value, std::source_location where = std::source_location::current())
    { if (!value) throw UnitAssertion{where.line()}; }
    struct UnitCase
    {
        char const* name = "initialization";
        uint_least32_t line = 0;
        desktop::Diagnostic diagnostic;
        void At(char const* value, bool reset = true, std::source_location where = std::source_location::current())
        {
            name = value; line = where.line();
            if (reset) diagnostic = {};
        }
    };
    int FormatFailure(char* output, size_t capacity, UnitCase const& test, char const* kind,
        uint_least32_t assertionLine = 0, uint32_t hresult = 0)
    {
        auto const& diagnostic = test.diagnostic;
        char assertion[48]{}, error[32]{};
        if (assertionLine) std::snprintf(assertion, sizeof(assertion), " assertion_line=%u",
            static_cast<unsigned>(assertionLine));
        if (hresult) std::snprintf(error, sizeof(error), " HRESULT=0x%08X", static_cast<unsigned>(hresult));
        return std::snprintf(output, capacity,
            "Activation probe unit failure kind=%s case=%s case_line=%u%s%s"
            " context_stage=%ls api=%ls object=%ls origin=%ls reason=%ls has_code=%u code=%u code_kind=%ls"
            "; no PSA/printing performed.\n",
            kind, test.name, static_cast<unsigned>(test.line), assertion, error,
            diagnostic.stage, diagnostic.api, diagnostic.object,
            diagnostic.origin, diagnostic.reason, static_cast<unsigned>(diagnostic.hasCode),
            diagnostic.hasCode ? static_cast<unsigned>(diagnostic.code) : 0u, diagnostic.codeKind);
    }
    void ReportFailure(FILE* output, UnitCase const& test, char const* kind,
        uint_least32_t assertionLine = 0, uint32_t hresult = 0)
    {
        char buffer[2048]{};
        auto length = FormatFailure(buffer, sizeof(buffer), test, kind, assertionLine, hresult);
        if (length < 0 || static_cast<size_t>(length) >= sizeof(buffer))
            std::fputs("Unit failure detail formatting failed; ", output);
        std::fputs(buffer, output);
    }
    void ObservabilityTests()
    {
        UnitCase test;
        test.At("observability.assertion");
        desktop::Point(&test.diagnostic, L"OpenProcessToken", L"win32");
        desktop::ErrorCode(&test.diagnostic, 5, L"win32");
        char buffer[2048]{};
        std::string text;
        bool caught = false;
        auto expectedLine = std::source_location::current().line() + 1;
        try { Expect(false); }
        catch (UnitAssertion const& error)
        {
            caught = true;
            Expect(error.line == expectedLine);
            auto length = FormatFailure(buffer, sizeof(buffer), test, "assertion", error.line);
            Expect(length > 0 && static_cast<size_t>(length) < sizeof(buffer));
            text = buffer;
            Expect(text.find("HRESULT=") == std::string::npos);
        }
        Expect(caught);
        auto length = FormatFailure(buffer, sizeof(buffer), test, "production-exception", 0, 0x80070005);
        Expect(length > 0 && static_cast<size_t>(length) < sizeof(buffer));
        text += buffer;
        Expect(text.find("kind=assertion case=observability.assertion") != std::string::npos &&
            text.find("assertion_line=" + std::to_string(expectedLine)) != std::string::npos &&
            text.find("kind=production-exception") != std::string::npos &&
            text.find("HRESULT=0x80070005") != std::string::npos &&
            text.find("api=OpenProcessToken") != std::string::npos &&
            text.find("origin=win32") != std::string::npos &&
            text.find("code=5 code_kind=win32") != std::string::npos &&
            text.find(__FILE__) == std::string::npos);
        test.At("observability.reset");
        Expect(!test.diagnostic.hasCode && std::wstring_view(test.diagnostic.api) == L"none");
    }
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
    UnitCase test;
    try
    {
        test.At("protocol"); ProtocolTests();
        test.At("diagnostic-isolation"); DiagnosticCoreTests();
        test.At("observability"); ObservabilityTests();
        std::puts("Portable protocol/diagnostic isolation/failure-observability cases passed; NOT actual PSA activation.");
        return 0;
    }
    catch (UnitAssertion const& error) { ReportFailure(stderr, test, "assertion", error.line); return 1; }
    catch (...) { ReportFailure(stderr, test, "unexpected-exception"); return 1; }
}
#else
#include "Controller.h"
#include <objbase.h>
#include <array>

namespace
{
    activation::Context UnitContext(desktop::Diagnostic* diagnostic)
    {
        activation::Context context;
        context.identity = desktop::CurrentIdentity(diagnostic);
        context.family = L"Miyako.DesktopPdfPrototype.Unit";
        desktop::Step(diagnostic, L"current.hash", L"identity-hash");
        context.userHash = activation::UserHash(context.identity, diagnostic);
        context.packageHash = desktop::Hash(context.family, diagnostic);
        return context;
    }
    std::wstring Executable()
    {
        std::array<wchar_t, 32768> name{};
        DWORD length = GetModuleFileNameW(nullptr, name.data(), static_cast<DWORD>(name.size()));
        Expect(length > 0 && length < name.size());
        return {name.data(), length};
    }
    constexpr std::array TokenKinds{TokenUser, TokenSessionId, TokenIsAppContainer, TokenAppContainerSid,
        TokenType, TokenImpersonationLevel, TokenElevation};
    struct TokenQuery { BOOL returned; DWORD error, required; };
    TokenQuery DirectTokenQuery(HANDLE token, TOKEN_INFORMATION_CLASS kind, void* buffer, DWORD capacity)
    {
        DWORD required = 0;
        auto returned = GetTokenInformation(token, kind, buffer, capacity, &required);
        auto error = returned ? ERROR_SUCCESS : GetLastError();
        return {returned, error, required};
    }
    void QueryEvidence(char const* context, TOKEN_INFORMATION_CLASS kind, DWORD capacity, TokenQuery const& query)
    {
        std::printf("Token query context=%s class=%ls input_bytes=%u returned=%u error=%u required_bytes=%u\n",
            context, desktop::TokenRole(kind), static_cast<unsigned>(capacity), static_cast<unsigned>(query.returned != FALSE),
            static_cast<unsigned>(query.error), static_cast<unsigned>(query.required));
        std::fflush(stdout);
    }
    bool SizeProbeAdmitted(TokenQuery const& query)
    { return !query.returned && query.error == ERROR_INSUFFICIENT_BUFFER && query.required != 0; }
    void InvalidTokenTests(UnitCase& test)
    {
        using namespace activation;
        auto& diagnostic = test.diagnostic;
        for (auto kind : TokenKinds)
        {
            test.At("diagnostic.token-error-reference");
            if (kind == TokenElevation)
            {
                TOKEN_ELEVATION value{};
                auto reference = DirectTokenQuery(INVALID_HANDLE_VALUE, kind, &value,
                    static_cast<DWORD>(sizeof(value)));
                QueryEvidence("invalid.fixed", kind, static_cast<DWORD>(sizeof(value)), reference);
                Expect(!reference.returned);
                bool rejected = false;
                try { TokenData(INVALID_HANDLE_VALUE, kind, &diagnostic); }
                catch (hresult_error const& error) { rejected = error.code() == HRESULT_FROM_WIN32(reference.error); }
                Expect(rejected && diagnostic.hasCode && diagnostic.code == reference.error &&
                    std::wstring_view(diagnostic.object) == TokenRole(kind) &&
                    std::wstring_view(diagnostic.api) == L"GetTokenInformation" &&
                    std::wstring_view(diagnostic.origin) == L"win32" &&
                    std::wstring_view(diagnostic.reason) == L"value-query");
                continue;
            }
            auto reference = DirectTokenQuery(INVALID_HANDLE_VALUE, kind, nullptr, 0);
            QueryEvidence("invalid.zero", kind, 0, reference);
            Expect(!reference.returned);
            auto admitted = SizeProbeAdmitted(reference);
            auto expected = reference;
            if (admitted)
            {
                std::vector<uint8_t> buffer(reference.required);
                expected = DirectTokenQuery(INVALID_HANDLE_VALUE, kind, buffer.data(), reference.required);
                QueryEvidence("invalid.value", kind, reference.required, expected);
                Expect(!expected.returned);
            }
            auto expectedHresult = admitted ? HRESULT_FROM_WIN32(expected.error) : E_ACCESSDENIED;
            bool rejected = false;
            try { TokenData(INVALID_HANDLE_VALUE, kind, &diagnostic); }
            catch (hresult_error const& error) { rejected = error.code() == expectedHresult; }
            Expect(rejected && diagnostic.hasCode && diagnostic.code == expected.error &&
                std::wstring_view(diagnostic.object) == TokenRole(kind) &&
                std::wstring_view(diagnostic.reason) == (admitted ? L"value-query" : L"token-size-probe"));
        }
    }
    void ElevationSizeTests(UnitCase& test)
    {
        auto& diagnostic = test.diagnostic;
        auto exact = static_cast<DWORD>(sizeof(TOKEN_ELEVATION));
        for (DWORD size : std::array<DWORD, 4>{0, exact - 1, exact + 1, MAXDWORD})
        {
            test.At("elevation.returned-size-rejected");
            TOKEN_ELEVATION value{};
            bool rejected = false;
            try { desktop::TokenElevationData(value, size, &diagnostic); }
            catch (winrt::hresult_error const& error) { rejected = error.code() == E_ACCESSDENIED; }
            Expect(rejected && !diagnostic.hasCode && std::wstring_view(diagnostic.origin) == L"validation" &&
                std::wstring_view(diagnostic.reason) == L"token-fixed-size");
        }
        for (DWORD flag : std::array<DWORD, 3>{0, 1, MAXDWORD})
        {
            test.At("elevation.exact-size-dword-preserved");
            TOKEN_ELEVATION value{flag};
            auto data = desktop::TokenElevationData(value, exact, &diagnostic);
            Expect(data.size() == sizeof(value) &&
                reinterpret_cast<TOKEN_ELEVATION const*>(data.data())->TokenIsElevated == flag);
        }
    }
    DWORD ReferenceCapacity(TOKEN_INFORMATION_CLASS kind, DWORD required)
    {
        switch (kind)
        {
        case TokenSessionId: case TokenIsAppContainer: return static_cast<DWORD>(sizeof(DWORD));
        case TokenType: return static_cast<DWORD>(sizeof(TOKEN_TYPE));
        case TokenImpersonationLevel: return static_cast<DWORD>(sizeof(SECURITY_IMPERSONATION_LEVEL));
        case TokenElevation: return static_cast<DWORD>(sizeof(TOKEN_ELEVATION));
        default: return required;
        }
    }
    void ValidTokenTests(UnitCase& test)
    {
        using namespace activation;
        auto& diagnostic = test.diagnostic;
        test.At("valid-token.open");
        Step(&diagnostic, L"current.token", L"process-token");
        Point(&diagnostic, L"OpenProcessToken", L"win32");
        HANDLE token = nullptr;
        Check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) != FALSE, &diagnostic);
        desktop::handle owned(token);
        TOKEN_TYPE type{};
        auto typeReference = DirectTokenQuery(token, TokenType, &type, static_cast<DWORD>(sizeof(type)));
        QueryEvidence("valid.primary-type", TokenType, static_cast<DWORD>(sizeof(type)), typeReference);
        Expect(typeReference.returned && typeReference.required == sizeof(type) && type == TokenPrimary);
        for (auto kind : TokenKinds)
        {
            test.At("valid-token.direct-reference");
            Step(&diagnostic, L"current.token", TokenRole(kind));
            auto size = DirectTokenQuery(token, kind, nullptr, 0);
            QueryEvidence("valid.zero", kind, 0, size);
            auto capacity = ReferenceCapacity(kind, size.required);
            Expect(capacity != 0);
            std::vector<uint8_t> reference(capacity);
            TOKEN_ELEVATION elevation{};
            void* buffer = kind == TokenElevation ? static_cast<void*>(&elevation) : reference.data();
            auto value = DirectTokenQuery(token, kind, buffer, capacity);
            QueryEvidence("valid.value", kind, capacity, value);
            if (!value.returned)
            {
                Expect(kind == TokenImpersonationLevel);
                test.At("valid-token.primary-impersonation-rejected");
                auto admitted = SizeProbeAdmitted(size);
                auto expectedError = admitted ? value.error : size.error;
                auto expectedHresult = admitted ? HRESULT_FROM_WIN32(expectedError) : E_ACCESSDENIED;
                bool rejected = false;
                try { TokenData(token, kind, &diagnostic); }
                catch (hresult_error const& error) { rejected = error.code() == expectedHresult; }
                Expect(rejected && diagnostic.hasCode && diagnostic.code == expectedError &&
                    std::wstring_view(diagnostic.object) == TokenRole(kind) &&
                    std::wstring_view(diagnostic.reason) == (admitted ? L"value-query" : L"token-size-probe"));
                continue;
            }
            Expect(value.required != 0 && value.required <= capacity);
            test.At("valid-token.helper");
            auto data = TokenData(token, kind, &diagnostic);
            Expect(data.size() == value.required);
            if (kind == TokenUser)
            {
                Expect(data.size() >= sizeof(TOKEN_USER));
                auto actual = reinterpret_cast<TOKEN_USER const*>(data.data());
                auto expected = reinterpret_cast<TOKEN_USER const*>(reference.data());
                Expect(actual->User.Attributes == expected->User.Attributes &&
                    IsValidSid(actual->User.Sid) && IsValidSid(expected->User.Sid) &&
                    EqualSid(actual->User.Sid, expected->User.Sid));
            }
            else if (kind == TokenAppContainerSid)
            {
                Expect(data.size() >= sizeof(TOKEN_APPCONTAINER_INFORMATION));
                auto actual = reinterpret_cast<TOKEN_APPCONTAINER_INFORMATION const*>(data.data())->TokenAppContainer;
                auto expected = reinterpret_cast<TOKEN_APPCONTAINER_INFORMATION const*>(reference.data())->TokenAppContainer;
                Expect((!actual && !expected) ||
                    (actual && expected && IsValidSid(actual) && IsValidSid(expected) && EqualSid(actual, expected)));
            }
            else if (kind == TokenElevation)
            {
                Expect(data.size() == sizeof(TOKEN_ELEVATION) &&
                    reinterpret_cast<TOKEN_ELEVATION const*>(data.data())->TokenIsElevated == elevation.TokenIsElevated);
            }
            else Expect(std::equal(data.begin(), data.end(), reference.begin()));
        }
    }
    void NativeDiagnosticTests(activation::Context const& context, UnitCase& test)
    {
        using namespace activation;
        auto& diagnostic = test.diagnostic;
        test.At("diagnostic.win32-error");
        Step(&diagnostic, L"current.token", L"process-token");
        Point(&diagnostic, L"OpenProcessToken", L"win32");
        SetLastError(ERROR_ACCESS_DENIED);
        bool rejected = false;
        try { Check(false, &diagnostic); }
        catch (hresult_error const& error) { rejected = error.code() == E_ACCESSDENIED; }
        Expect(rejected && diagnostic.hasCode && diagnostic.code == ERROR_ACCESS_DENIED);
        test.At("diagnostic.serialization", false);
        auto report = activation::Record(L"control-arguments");
        DiagnosticRecord(report, diagnostic);
        auto metadata = report.GetNamedObject(L"error_context");
        Expect(metadata.GetNamedString(L"origin") == L"win32" && Integer(metadata, L"code") == ERROR_ACCESS_DENIED);
        test.At("diagnostic.validation-error");
        rejected = false;
        try { Guard(false, E_ACCESSDENIED, &diagnostic, L"owner"); }
        catch (hresult_error const& error) { rejected = error.code() == E_ACCESSDENIED; }
        Expect(rejected && !diagnostic.hasCode && std::wstring_view(diagnostic.origin) == L"validation" &&
            std::wstring_view(diagnostic.reason) == L"owner");
        test.At("diagnostic.sid-validity");
        rejected = false;
        try { desktop::CopySid(nullptr, &diagnostic); }
        catch (hresult_error const& error) { rejected = error.code() == E_ACCESSDENIED; }
        Expect(rejected && !diagnostic.hasCode && std::wstring_view(diagnostic.reason) == L"sid-validity");
        test.At("diagnostic.hresult-error");
        Point(&diagnostic, L"DeriveAppContainerSidFromAppContainerName", L"win32");
        rejected = false;
        try { CheckResult(E_INVALIDARG, &diagnostic); }
        catch (hresult_error const& error) { rejected = error.code() == E_INVALIDARG; }
        Expect(rejected && diagnostic.hasCode && diagnostic.code == static_cast<uint32_t>(E_INVALIDARG) &&
            std::wstring_view(diagnostic.codeKind) == L"hresult");
        test.At("diagnostic.invalid-handle-security");
        Security security(context, &diagnostic);
        test.At("diagnostic.invalid-handle");
        Step(&diagnostic, L"prepare.base", L"base");
        rejected = false;
        try { InspectHandle(INVALID_HANDLE_VALUE, context, security, true, &diagnostic); }
        catch (hresult_error const& error) { rejected = error.code() == HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE); }
        Expect(rejected && diagnostic.code == ERROR_INVALID_HANDLE &&
            std::wstring_view(diagnostic.api) == L"GetFileInformationByHandleEx");
        test.At("diagnostic.integer-type");
        JsonObject value;
        Text(value, L"created_ms", L"not-a-number");
        Step(&diagnostic, L"prepare.created", L"existing-request");
        rejected = false;
        try { Integer(value, L"created_ms", &diagnostic); }
        catch (hresult_error const& error) { rejected = error.code() == E_INVALIDARG; }
        Expect(rejected && std::wstring_view(diagnostic.reason) == L"integer-type");
        test.At("diagnostic.integer-range");
        value.Insert(L"created_ms", JsonValue::CreateNumberValue(-1));
        rejected = false;
        try { Integer(value, L"created_ms", &diagnostic); }
        catch (hresult_error const& error) { rejected = error.code() == E_INVALIDARG; }
        Expect(rejected && std::wstring_view(diagnostic.reason) == L"integer-range");
    }
    void StateCompatibilityTest(std::filesystem::path const& base, activation::Context context,
        activation::Security const& security, UnitCase& test)
    {
        using namespace activation;
        auto& diagnostic = test.diagnostic;
        test.At("state.cache-directory");
        auto cache = base / L"compatibility-cache";
        NewDirectory(cache, security, &diagnostic);
        test.At("state.cache-folder");
        Point(&diagnostic, L"StorageFolder.GetFolderFromPathAsync", L"winrt");
        context.cache = StorageFolder::GetFolderFromPathAsync(cache.wstring()).get();
        test.At("state.package-hashes");
        context.packageHash = Hash(L"new-full-package-name-0.2.1.0-unit", &diagnostic);
        auto previousContext = context;
        previousContext.packageHash = Hash(L"old-full-package-name-0.2.0.0-unit", &diagnostic);
        test.At("state.old-record");
        Point(&diagnostic, L"StorageFolder.Path", L"winrt");
        auto folder = Base(context);
        NewDirectory(folder, security, &diagnostic);
        auto oldId = RandomToken(&diagnostic);
        auto oldRoot = folder / oldId;
        NewDirectory(oldRoot, security, &diagnostic);
        auto now = Now();
        auto expired = Bound(previousContext, oldId, L"request", now - 60000, now - 30000);
        Atomic(oldRoot, L"request.json", expired, security, &diagnostic);
        test.At("state.expired-admission");
        auto newId = Prepare(context, &diagnostic);
        test.At("state.retained-bytes");
        Expect(Read(oldRoot, L"request.json", context, security, &diagnostic).Stringify() == expired.Stringify());
        test.At("state.fresh-busy");
        bool busy = false;
        try { Prepare(context, &diagnostic); }
        catch (hresult_error const& error) { busy = error.code() == HRESULT_FROM_WIN32(ERROR_BUSY); }
        Expect(busy && std::wstring_view(diagnostic.reason) == L"request-not-fresh");
        test.At("state.old-package-binding");
        Point(&diagnostic, L"Bound/Validate", L"validation");
        auto liveNow = Now();
        auto oldLive = Bound(previousContext, oldId, L"request", liveNow, liveNow + LifetimeMs);
        Validate(oldLive, previousContext, oldId, L"request", liveNow);
        bool replayRejected = false;
        try { Validate(oldLive, context, oldId, L"request", Now()); }
        catch (hresult_error const& error) { replayRejected = error.code() == E_ACCESSDENIED; }
        Expect(replayRejected);
        test.At("state.cleanup");
        Point(&diagnostic, L"filesystem.remove", L"cpp");
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
    UnitCase test;
    auto& diagnostic = test.diagnostic;
    try
    {
        test.At("apartment");
        Point(&diagnostic, L"winrt.init_apartment", L"winrt");
        init_apartment();
        test.At("protocol"); ProtocolTests();
        test.At("diagnostic-isolation"); DiagnosticCoreTests();
        test.At("observability"); ObservabilityTests();
        test.At("unit-context");
        auto context = UnitContext(&diagnostic);
        if (count == 4 && std::wstring_view(args[1]) == L"--controlled-unit-child")
        {
            test.At("controlled-child.host");
            return Host(context, args[2], args[3]);
        }
        test.At("arguments");
        Expect(count == 1);
        InvalidTokenTests(test);
        ElevationSizeTests(test);
        ValidTokenTests(test);
        NativeDiagnosticTests(context, test);
        test.At("files.security");
        Security security(context, &diagnostic);
        test.At("files.temporary-root");
        GUID guid{};
        Point(&diagnostic, L"CoCreateGuid", L"win32");
        check_hresult(CoCreateGuid(&guid));
        wchar_t text[40]{};
        Expect(StringFromGUID2(guid, text, 40) > 0);
        std::array<wchar_t, MAX_PATH> temporary{};
        DWORD length = GetTempPathW(static_cast<DWORD>(temporary.size()), temporary.data());
        Expect(length > 0 && length < temporary.size());
        base = std::filesystem::path(temporary.data()) / (L"activation-probe-unit-" + std::wstring(text));
        NewDirectory(base, security, &diagnostic);
        StateCompatibilityTest(base, context, security, test);
        test.At("files.request-create-read");
        auto id = RandomToken(&diagnostic);
        auto root = base / id;
        NewDirectory(root, security, &diagnostic);
        auto baseLock = OpenDirectory(base, context, security, &diagnostic);
        auto jobLock = OpenDirectory(root, context, security, &diagnostic);
        auto now = Now();
        auto request = Bound(context, id, L"request", now, now + LifetimeMs);
        Text(request, L"unicode_unit", L"\u65E5\u672C\u8A9E \u00E9");
        Atomic(root, L"request.json", request, security, &diagnostic);
        auto copy = Read(root, L"request.json", context, security, &diagnostic);
        Validate(copy, context, id, L"request", now);
        test.At("files.duplicate-no-overwrite");
        bool duplicate = false;
        auto& fileDiagnostic = diagnostic;
        Step(&fileDiagnostic, L"prepare.request", L"new-request");
        try { Atomic(root, L"request.json", request, security, &fileDiagnostic); }
        catch (hresult_error const& error)
        {
            duplicate = fileDiagnostic.hasCode &&
                error.code() == HRESULT_FROM_WIN32(fileDiagnostic.code) &&
                std::wstring_view(fileDiagnostic.api) == L"MoveFileExW";
        }
        Expect(duplicate && Read(root, L"request.json", context, security).Stringify() == copy.Stringify());
        test.At("files.session-mismatch");
        auto wrong = copy;
        Number(wrong, L"session", context.identity.session + 1);
        bool rejected = false;
        try { Validate(wrong, context, id, L"request", now); } catch (hresult_error const&) { rejected = true; }
        Expect(rejected);
        test.At("files.expiry");
        rejected = false;
        try { Validate(request, context, id, L"request", now + LifetimeMs); }
        catch (hresult_error const&) { rejected = true; }
        Expect(rejected);
        test.At("child.callback-create");
        auto callback = Bound(context, id, L"callback", now, now + CallbackMs);
        Flag(callback, L"appcontainer", true); Text(callback, L"endpoint", activation::Endpoint);
        Text(callback, L"request_hash", Hash(std::wstring(request.Stringify())));
        Text(callback, L"title_hash", Hash(std::wstring(Prefix) + id));
        Text(callback, L"workflow_hash", Hash(L"controlled-unit-not-an-actual-PSA-callback"));
        Atomic(root, L"callback.json", callback, security, &diagnostic);
        test.At("child.create-and-resume");
        auto exe = Executable();
        std::wstring command = L"\"" + exe + L"\" --controlled-unit-child \"" + base.wstring() + L"\" " + id;
        Point(&diagnostic, L"CreateJobObjectW", L"win32");
        desktop::handle job(CreateJobObjectW(nullptr, nullptr));
        Check(job != nullptr, &diagnostic);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        Point(&diagnostic, L"SetInformationJobObject", L"win32");
        Check(SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)) != FALSE, &diagnostic);
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        Point(&diagnostic, L"CreateProcessW", L"win32");
        Check(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != FALSE, &diagnostic);
        desktop::handle child(process.hProcess), thread(process.hThread);
        Point(&diagnostic, L"AssignProcessToJobObject", L"win32");
        if (!AssignProcessToJobObject(job.get(), child.get()))
        {
            auto error = GetLastError();
            ErrorCode(&diagnostic, error, L"win32");
            TerminateProcess(child.get(), ERROR_CANCELLED); WaitForSingleObject(child.get(), 5000);
            throw hresult_error(HRESULT_FROM_WIN32(error));
        }
        Expect(ResumeThread(thread.get()) != MAXDWORD);
        test.At("child.host-entry");
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!Exists(root, L"host-entry.json"))
        {
            Expect(WaitForSingleObject(child.get(), 0) == WAIT_TIMEOUT &&
                std::chrono::steady_clock::now() < deadline);
            Sleep(10);
        }
        auto entry = Read(root, L"host-entry.json", context, security, &diagnostic);
        Expect(Integer(entry, L"pid") == process.dwProcessId &&
            entry.GetNamedString(L"creation_filetime") == CreationText(child.get()));
        auto observing = Bound(context, id, L"observing", now, now + CallbackMs);
        Number(observing, L"pid", process.dwProcessId);
        Text(observing, L"creation_filetime", CreationText(child.get()));
        Atomic(root, L"observing.json", observing, security, &diagnostic);
        test.At("child.ack-and-exit");
        Expect(WaitForSingleObject(child.get(), 5000) == WAIT_OBJECT_0);
        DWORD exit = MAXDWORD;
        Point(&diagnostic, L"GetExitCodeProcess", L"win32");
        Check(GetExitCodeProcess(child.get(), &exit) != FALSE, &diagnostic);
        Expect(exit == 0);
        Validate(Read(root, L"ack.json", context, security, &diagnostic), context, id, L"ack", Now());
        test.At("child.replay-rejected");
        bool replay = false;
        try { Host(context, base, id); } catch (hresult_error const&) { replay = true; }
        Expect(replay);
        test.At("files.unprotected-directory-create");
        auto unprotected = base / L"unprotected-unit";
        Point(&diagnostic, L"CreateDirectoryW", L"win32");
        Check(CreateDirectoryW(unprotected.c_str(), nullptr) != FALSE, &diagnostic);
        test.At("files.unprotected-directory-rejected");
        rejected = false;
        Step(&fileDiagnostic, L"prepare.existing", L"existing-job");
        try { auto unsafe = OpenDirectory(unprotected, context, security, &fileDiagnostic); }
        catch (hresult_error const&) { rejected = true; }
        Expect(rejected && std::wstring_view(fileDiagnostic.origin) == L"validation" &&
            std::wstring_view(fileDiagnostic.reason) == L"dacl-protected" && !fileDiagnostic.hasCode);
        test.At("files.unprotected-directory-cleanup");
        Point(&diagnostic, L"filesystem.remove", L"cpp");
        std::filesystem::remove(unprotected);
        test.At("child.missing-observer-records");
        auto waitingId = RandomToken();
        auto waitingRoot = base / waitingId;
        NewDirectory(waitingRoot, security, &diagnostic);
        auto waitingNow = Now();
        auto waitingRequest = Bound(context, waitingId, L"request", waitingNow, waitingNow + LifetimeMs);
        auto waitingCallback = Bound(context, waitingId, L"callback", waitingNow, waitingNow + CallbackMs);
        Flag(waitingCallback, L"appcontainer", true);
        Text(waitingCallback, L"endpoint", activation::Endpoint);
        Text(waitingCallback, L"request_hash", Hash(std::wstring(waitingRequest.Stringify())));
        Text(waitingCallback, L"title_hash", Hash(std::wstring(Prefix) + waitingId));
        Text(waitingCallback, L"workflow_hash", Hash(L"unit-no-observer-not-a-PSA-callback"));
        Atomic(waitingRoot, L"request.json", waitingRequest, security, &diagnostic);
        Atomic(waitingRoot, L"callback.json", waitingCallback, security, &diagnostic);
        test.At("child.missing-observer-start");
        command = L"\"" + exe + L"\" --controlled-unit-child \"" + base.wstring() + L"\" " + waitingId;
        PROCESS_INFORMATION waitingProcess{};
        Point(&diagnostic, L"CreateProcessW", L"win32");
        Check(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup, &waitingProcess) != FALSE, &diagnostic);
        desktop::handle waitingChild(waitingProcess.hProcess), waitingThread(waitingProcess.hThread);
        Point(&diagnostic, L"AssignProcessToJobObject", L"win32");
        if (!AssignProcessToJobObject(job.get(), waitingChild.get()))
        {
            auto error = GetLastError();
            ErrorCode(&diagnostic, error, L"win32");
            TerminateProcess(waitingChild.get(), ERROR_CANCELLED); WaitForSingleObject(waitingChild.get(), 5000);
            throw hresult_error(HRESULT_FROM_WIN32(error));
        }
        Expect(ResumeThread(waitingThread.get()) != MAXDWORD);
        test.At("child.missing-observer-exit-no-ack");
        Expect(WaitForSingleObject(waitingChild.get(), 6000) == WAIT_OBJECT_0);
        Point(&diagnostic, L"GetExitCodeProcess", L"win32");
        Check(GetExitCodeProcess(waitingChild.get(), &exit) != FALSE, &diagnostic);
        Expect(exit == 1 && Exists(waitingRoot, L"host-entry.json") && !Exists(waitingRoot, L"ack.json"));
        test.At("files.callback-request-mismatch");
        auto wrongCallback = JsonObject::Parse(callback.Stringify());
        Text(wrongCallback, L"request_hash", L"wrong");
        rejected = false;
        try { ValidateCallback(wrongCallback, request, context, id); }
        catch (hresult_error const&) { rejected = true; }
        Expect(rejected);
        test.At("files.cleanup");
        Point(&diagnostic, L"filesystem.remove", L"cpp");
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
    catch (UnitAssertion const& error)
    { ReportFailure(stderr, test, "assertion", error.line); return 1; }
    catch (hresult_error const& error)
    { ReportFailure(stderr, test, "production-exception", 0, static_cast<uint32_t>(error.code().value)); return 1; }
    catch (...)
    {
        ReportFailure(stderr, test, "unexpected-exception", 0, static_cast<uint32_t>(to_hresult().value));
        return 1;
    }
}
#endif
