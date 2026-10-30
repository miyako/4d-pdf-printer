#include "../WorkerProcess.h"
#include <fstream>
#include <iterator>
#include <iostream>

using namespace pdf;
namespace {
void Write(fs::path const& path, std::string const& bytes) {
    Handle f(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, 0, nullptr));
    Check(static_cast<bool>(f)); DWORD written{};
    Check(WriteFile(f.value, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr));
    Require(written == bytes.size(), "fixture short write"); Check(FlushFileBuffers(f.value));
}
void ChildAttempt(wchar_t const* image, bool breakaway) {
    auto command = Quote(image) + L" --descendant";
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION p{};
    if (!CreateProcessW(image, command.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW | (breakaway ? CREATE_BREAKAWAY_FROM_JOB : 0), nullptr, nullptr, &startup, &p)) {
        auto error = GetLastError();
        Require(error == ERROR_ACCESS_DENIED || error == ERROR_NOT_ENOUGH_QUOTA, "unexpected child creation error");
        return;
    }
    Handle process(p.hProcess), thread(p.hThread);
    if (WaitForSingleObject(process.value, 5000) != WAIT_OBJECT_0) {
        Check(TerminateProcess(process.value, ERROR_CANCELLED));
        Require(WaitForSingleObject(process.value, 5000) == WAIT_OBJECT_0, "escaped fixture child not reaped");
        throw std::runtime_error("fixture descendant escaped process limit/breakaway policy");
    }
}
}
int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    fs::path work;
    try {
        if (argc == 2 && std::wstring_view(argv[1]) == L"--descendant") { Sleep(INFINITE); return 1; }
        init_apartment(apartment_type::multi_threaded);
        Require(argc == 7 && std::wstring_view(argv[1]) == L"--protocol-version" &&
            std::wstring_view(argv[2]) == L"1" && std::wstring_view(argv[3]) == L"--input" &&
            std::wstring_view(argv[5]) == L"--output", "fixture CLI");
        fs::path input(argv[4]), output(argv[6]); work = input.parent_path();
        std::ifstream stream(input, std::ios::binary);
        Require(static_cast<bool>(stream), "fixture input");
        std::string bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>{});
        auto config = Parse(bytes); auto scenario = config.GetNamedString(L"case");
        FILETIME created{}, exited{}, kernel{}, user{};
        Check(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user));
        JsonObject ready; Number(ready, L"pid", GetCurrentProcessId());
        Number(ready, L"createdHigh", created.dwHighDateTime); Number(ready, L"createdLow", created.dwLowDateTime);
        Write(work / L"started.pending", to_string(ready.Stringify()));
        Check(MoveFileExW((work / L"started.pending").c_str(), (work / L"started.json").c_str(), 0));
        auto end = GetTickCount64() + 15000;
        while (!fs::exists(work / L"release")) {
            Require(GetTickCount64() < end, "fixture rendezvous expired"); Sleep(10);
        }
        if (scenario == L"deadline" || scenario == L"cancel" || scenario == L"killclose") Sleep(INFINITE);
        if (scenario == L"crash") {
            RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
            return 2;
        }
        if (scenario == L"descendant" || scenario == L"breakaway") ChildAttempt(argv[0], scenario == L"breakaway");
        if (scenario == L"handles") {
            BY_HANDLE_FILE_INFORMATION info{};
            auto sentinel = reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(Integer(config, L"sentinel")));
            bool same = GetFileInformationByHandle(sentinel, &info) &&
                info.dwVolumeSerialNumber == Integer(config, L"volume") &&
                info.nFileIndexHigh == Integer(config, L"indexHigh") && info.nFileIndexLow == Integer(config, L"indexLow");
            Require(!same, "unrelated sentinel FILE ID inherited (not merely a reused handle value)");
            wchar_t value[2]{};
            Require(GetEnvironmentVariableW(L"DESKTOP_PDF_FIXTURE_SENTINEL", value, 2) == 0 &&
                GetLastError() == ERROR_ENVVAR_NOT_FOUND, "unexpected parent environment inherited");
            char byte{}; DWORD read{};
            Check(ReadFile(GetStdHandle(STD_INPUT_HANDLE), &byte, 1, &read, nullptr));
            Require(read == 0, "stdin was not the supplied NUL");
            std::cerr << "Controlled fixture stderr; NOT renderer evidence.\n" << std::flush;
        }
        Write(output, "Controlled fault-worker bytes; NOT A PDF OR REAL RENDERER.\n");
        JsonObject status;
        Text(status, L"schema", L"4d-pdf-printer.worker-status"); Number(status, L"version", 1);
        Text(status, L"worker_version", L"controlled-fault-fixture-NOT-renderer");
        Text(status, L"result", L"ok"); Number(status, L"exit_code", 0); Text(status, L"stage", L"complete");
        for (auto key : {L"error", L"document", L"page"}) status.Insert(key, JsonValue::CreateNullValue());
        Number(status, L"documents", 1); Number(status, L"pages_completed", 1);
        Number(status, L"output_bytes", scenario == L"mismatch" ? 1 : fs::file_size(output));
        status.Insert(L"output_published", JsonValue::CreateBooleanValue(true));
        status.Insert(L"cleanup_complete", JsonValue::CreateBooleanValue(true));
        bytes = to_string(status.Stringify());
        if (scenario == L"malformed") bytes = "{";
        if (scenario == L"duplicate") { bytes.pop_back(); bytes += ",\"version\":1}"; }
        if (scenario == L"trailing") bytes += "{}";
        if (scenario == L"missing") bytes.clear();
        if (scenario == L"channel") {
            std::cout << std::string(RecordBytes + 1, 'x') << std::flush;
            Sleep(INFINITE);
        }
        std::cout << bytes << std::flush;
        return scenario == L"exit24" ? 24 : scenario == L"exit71" ? 71 : 0;
    } catch (...) {
        Failure(L"controlled fault fixture (NOT renderer)", to_hresult());
        std::string message = "Nonstandard controlled fixture failure";
        try { throw; }
        catch (hresult_error const& error) { message = to_string(error.message()); }
        catch (std::exception const& error) { message = error.what(); }
        catch (...) { std::cerr << message << '\n'; }
        if (!work.empty()) {
            try { Write(work / L"fixture-error.txt", message); }
            catch (...) { Failure(L"additional fixture diagnostic-write failure", to_hresult()); }
        }
        return 2;
    }
}
