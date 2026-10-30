#include "../Process.h"
#include "../HostTrace.h"
#include <iostream>

using namespace smoke;
#define EXPECT(condition) do { if (!(condition)) { \
    std::cerr << "case=" << test << " assertion-line=" << __LINE__ \
        << " API=" #condition " last-error=" << GetLastError() << "\n"; return 1; } } while (false)

int wmain(int argc, wchar_t** argv)
{
    char const* test = "initialization";
    try
    {
        if (argc == 2 && std::wstring_view(argv[1]) == L"--sleep") { Sleep(500); return 0; }
        if (argc == 2 && std::wstring_view(argv[1]) == L"--hold") { Sleep(30000); return 0; }
        if (argc == 2 && std::wstring_view(argv[1]) == L"--negative") return 1;
        init_apartment(apartment_type::multi_threaded);
        std::wstring id = L"0123456789abcdef0123456789abcdef";
        test = "record JSON roundtrip/type/ID validation";
        auto record = Record(id, L"host-entry");
        Text(record, L"creationTime", L"123");
        auto parsed = JsonObject::Parse(record.Stringify()); Validate(parsed, id, L"host-entry");
        EXPECT(Pid(parsed) == GetCurrentProcessId());
        EXPECT(ParseCreation(parsed.GetNamedString(L"creationTime")) == 123);
        bool rejected = false;
        try { Validate(parsed, L"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", L"host-entry"); }
        catch (hresult_error const&) { rejected = true; }
        EXPECT(rejected);
        for (auto bad : {L"", L"-1", L"1x", L"0", L"18446744073709551616"})
        {
            rejected = false;
            try { ParseCreation(bad); } catch (std::exception const&) { rejected = true; }
            catch (hresult_error const&) { rejected = true; }
            EXPECT(rejected);
        }
        Number(parsed, L"pid", 1.5); rejected = false;
        try { Pid(parsed); } catch (hresult_error const&) { rejected = true; }
        EXPECT(rejected);
        rejected = false;
        try { JsonObject::Parse(L"{broken"); } catch (hresult_error const&) { rejected = true; }
        EXPECT(rejected);
        test = "CreateProcessW fixed test child";
        auto self = Image(GetCurrentProcess());
        auto command = L"\"" + self + L"\" --sleep";
        STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
        EXPECT(CreateProcessW(self.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process));
        Handle child(process.hProcess), thread(process.hThread);
        test = "exact image/creation time then controlled timeout/exit0";
        auto created = Creation(child.value);
        VerifyProcess(child.value, self, created);
        rejected = false;
        try { VerifyProcess(child.value, self, created + 1); } catch (hresult_error const&) { rejected = true; }
        EXPECT(rejected);
        rejected = false;
        try { VerifyProcess(child.value, self + L".wrong", created); } catch (hresult_error const&) { rejected = true; }
        EXPECT(rejected);
        DWORD code{};
        EXPECT(!ObserveExit(child.value, 0, code));
        EXPECT(ObserveExit(child.value, 5000, code)); EXPECT(code == 0);
        test = "production cleanup preserves observed normal exit";
        auto normal = FinishOwnedSubmitter(child.value, 0);
        EXPECT(normal.GetNamedBoolean(L"cleanupSucceeded") && normal.GetNamedBoolean(L"exitObserved"));
        EXPECT(!normal.GetNamedBoolean(L"terminationAttempted"));
        command = L"\"" + self + L"\" --negative";
        EXPECT(CreateProcessW(self.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process));
        Handle negative(process.hProcess), negativeThread(process.hThread);
        test = "production cleanup nonzero GDI-like exit is not cleanup failure";
        auto nonzero = FinishOwnedSubmitter(negative.value, 5000);
        EXPECT(nonzero.GetNamedBoolean(L"cleanupSucceeded") && nonzero.GetNamedBoolean(L"exitObserved"));
        EXPECT(nonzero.GetNamedNumber(L"exitCode") == 1 && !nonzero.GetNamedBoolean(L"terminationAttempted"));
        command = L"\"" + self + L"\" --hold";
        EXPECT(CreateProcessW(self.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process));
        Handle held(process.hProcess), heldThread(process.hThread);
        test = "production cleanup termination failure and bounded unobserved exit";
        Handle restricted(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process.dwProcessId));
        auto denied = restricted.value ? FinishOwnedSubmitter(restricted.value, 0, 20) : JsonObject{};
        test = "production cleanup actual TerminateProcess and observed exit";
        auto terminated = FinishOwnedSubmitter(held.value, 0, 2000);
        EXPECT(restricted.value != nullptr);
        EXPECT(terminated.GetNamedBoolean(L"cleanupSucceeded") && terminated.GetNamedBoolean(L"exitObserved"));
        EXPECT(terminated.GetNamedBoolean(L"terminationAttempted"));
        EXPECT(terminated.GetNamedNumber(L"exitCode") == ERROR_TIMEOUT);
        EXPECT(!denied.GetNamedBoolean(L"cleanupSucceeded") && !denied.GetNamedBoolean(L"exitObserved"));
        EXPECT(denied.GetNamedBoolean(L"terminationAttempted"));
        EXPECT(denied.GetNamedNumber(L"terminationHresult") == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));
        test = "independent host trace pre-parse malformed arguments/stage failure";
        wchar_t temp[MAX_PATH + 1]{};
        auto length = GetTempPathW(ARRAYSIZE(temp), temp);
        EXPECT(length > 0 && length < ARRAYSIZE(temp));
        auto path = std::wstring(temp) + L"smoke-host-trace-" + std::to_wstring(GetCurrentProcessId()) +
            L"-" + std::to_wstring(GetTickCount64()) + L".txt";
        struct RemoveTrace
        {
            std::wstring path;
            bool owned = false;
            ~RemoveTrace()
            {
                if (owned && !DeleteFileW(path.c_str()))
                    Failure(L"DeleteFileW controlled host trace fixture", HRESULT_FROM_WIN32(GetLastError()));
            }
        } cleanup{path};
        HRESULT primary = E_ACCESSDENIED;
        {
            HostTrace trace;
            EXPECT(trace.OpenPath(path.c_str()));
            cleanup.owned = true;
            EXPECT(trace.Stage("wWinMain entry"));
            EXPECT(trace.Arguments("wWinMainArguments", L"/unexpected \"malformed\"\n"));
            rejected = false;
            try { ParseId(L"/unexpected \"malformed\"\n", L"--test-id "); }
            catch (std::invalid_argument const&) { rejected = true; }
            EXPECT(rejected);
            EXPECT(trace.Stage("injected stage failure", primary, ERROR_ACCESS_DENIED));
            HostTrace collision;
            EXPECT(!collision.OpenPath(path.c_str()));
            EXPECT(collision.error == ERROR_FILE_EXISTS);
            EXPECT(trace.Arguments("bounded", std::wstring(300, L'x').c_str()));
            EXPECT(!trace.Write(std::string(HostTrace::Limit, 'x')));
            EXPECT(trace.error == ERROR_FILE_TOO_LARGE);
            auto failure = trace.ReportFailure("secondary sink failure", primary, ERROR_ACCESS_DENIED);
            EXPECT(failure.primary == E_ACCESSDENIED && failure.exitCode == 1);
            EXPECT(failure.sinkError == ERROR_FILE_TOO_LARGE && trace.error == ERROR_FILE_TOO_LARGE);
        }
        auto opened = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        EXPECT(opened != INVALID_HANDLE_VALUE);
        Handle traceFile(opened);
        char traceBytes[HostTrace::Limit + 1]{}; DWORD read{};
        EXPECT(ReadFile(traceFile.value, traceBytes, HostTrace::Limit, &read, nullptr));
        std::string contents(traceBytes, read);
        EXPECT(contents.starts_with("stage=wWinMain entry"));
        EXPECT(contents.find("wWinMainArgumentsUtf16Hex=002F0075") != contents.npos);
        EXPECT(contents.find("stage=injected stage failure primaryHresult=0x80070005 win32Snapshot=5") != contents.npos);
        EXPECT(contents.find("truncated=true") != contents.npos);
        EXPECT(read < HostTrace::Limit);
        test = "independent unavailable trace sink retains primary failure";
        HostTrace missing;
        EXPECT(!missing.OpenPath((path + L"\\missing.txt").c_str()));
        auto sinkError = missing.error;
        auto failure = missing.ReportFailure("primary error retained", E_ACCESSDENIED, ERROR_ACCESS_DENIED);
        EXPECT(sinkError != ERROR_SUCCESS && failure.primary == E_ACCESSDENIED && failure.exitCode == 1);
        EXPECT(failure.sinkError == sinkError && missing.error == sinkError);
        std::cout << "native parsing/process cases passed; unpackaged tests do NOT prove PSA activation/storage\n";
        return 0;
    }
    catch (...)
    {
        auto hr = to_hresult();
        std::cerr << "case=" << test << " boundary-line=" << __LINE__ << " HRESULT=" << hr.value << "\n";
        Failure(L"native test boundary (case above)", hr); return 1;
    }
}
