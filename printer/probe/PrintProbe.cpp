#include <windows.h>
#include <winspool.h>
#include <objbase.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "Evidence.h"

namespace
{
    struct Options
    {
        std::wstring queue, output, job, log;
        bool interactive = false, noninteractive = false, service = false, diagnoseContext = false;
        unsigned timeout = 60, delay = 0;
    };

    unsigned Number(std::wstring const& value, unsigned minimum, unsigned maximum, char const* option)
    {
        std::string error = std::string(option) + " must be an integer in " +
            std::to_string(minimum) + ".." + std::to_string(maximum);
        if (value.empty()) throw std::invalid_argument(error);
        unsigned number = 0;
        for (wchar_t digit : value)
        {
            if (digit < L'0' || digit > L'9') throw std::invalid_argument(error);
            unsigned next = static_cast<unsigned>(digit - L'0');
            if (number > maximum / 10 || (number == maximum / 10 && next > maximum % 10))
                throw std::invalid_argument(error);
            number = number * 10 + next;
        }
        if (number < minimum) throw std::invalid_argument(error);
        return number;
    }

    std::chrono::seconds WatchdogBudget(Options const& value)
    {
        return std::chrono::seconds(value.delay + value.timeout);
    }

    Options Parse(std::vector<std::wstring> const& args)
    {
        Options result;
        for (size_t i = 0; i < args.size(); ++i)
        {
            auto const& arg = args[i];
            if (arg == L"--interactive") result.interactive = true;
            else if (arg == L"--noninteractive") result.noninteractive = true;
            else if (arg == L"--service") result.service = true;
            else if (arg == L"--diagnose-context") result.diagnoseContext = true;
            else
            {
                if (i + 1 == args.size()) throw std::invalid_argument("missing option value");
                auto const& value = args[++i];
                if (arg == L"--queue") result.queue = value;
                else if (arg == L"--output") result.output = value;
                else if (arg == L"--job-name") result.job = value;
                else if (arg == L"--log") result.log = value;
                else if (arg == L"--timeout-seconds")
                    result.timeout = Number(value, 1, 300, "--timeout-seconds");
                else if (arg == L"--delay-seconds")
                    result.delay = Number(value, 0, 86400, "--delay-seconds");
                else throw std::invalid_argument("unknown option");
            }
        }
        if (result.queue.empty() || result.interactive == result.noninteractive)
            throw std::invalid_argument("require queue and exactly one interaction mode");
        if (result.service && (!result.noninteractive || result.log.empty()))
            throw std::invalid_argument("service requires noninteractive mode and log");
        if (!result.output.empty())
        {
            std::filesystem::path path(result.output);
            if (!path.is_absolute() || path.extension() != L".oxps")
                throw std::invalid_argument("output must be an absolute .oxps path");
        }
        if (!result.log.empty() && !std::filesystem::path(result.log).is_absolute())
            throw std::invalid_argument("log must be absolute");
        if (result.interactive && !result.output.empty())
            throw std::invalid_argument("interactive baseline must omit output");
        if (result.job.empty())
        {
            GUID id{};
            if (FAILED(CoCreateGuid(&id))) throw std::runtime_error("CoCreateGuid failed");
            wchar_t text[40]{};
            if (!StringFromGUID2(id, text, 40)) throw std::runtime_error("GUID formatting failed");
            result.job = L"psa-probe-" + std::wstring(text);
        }
        return result;
    }

    Options options;
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> done{false};
    SERVICE_STATUS_HANDLE serviceHandle = nullptr;
    std::string executableHash, jobHash;
    std::mutex logMutex;

    void Log(char const* event, DWORD error = 0, int jobId = 0, std::string const& fields = {})
    {
        std::string binary, title;
        {
            std::scoped_lock guard(logMutex);
            binary = executableHash;
            title = jobHash;
        }
        DWORD sessionId = MAXDWORD;
        ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);
        FILETIME now{};
        GetSystemTimeAsFileTime(&now);
        ULARGE_INTEGER timestamp{};
        timestamp.LowPart = now.dwLowDateTime;
        timestamp.HighPart = now.dwHighDateTime;
        std::string line = "{\"schema\":\"psa-probe-v2\",\"probeVersion\":\"" +
            std::string(evidence::Version) + "\",\"processArchitecture\":\"" + evidence::Architecture +
            "\",\"executableSha256\":\"" + binary + "\",\"jobTitleSha256\":\"" + title +
            "\",\"serviceMode\":" + (options.service ? "true" : "false") +
            ",\"event\":\"" + std::string(event) +
            "\",\"win32Error\":" + std::to_string(error) + ",\"spoolerJobId\":" +
            std::to_string(jobId) + ",\"processId\":" + std::to_string(GetCurrentProcessId()) +
            ",\"windowsSessionId\":" + std::to_string(sessionId) +
            ",\"utcFileTime\":" + std::to_string(timestamp.QuadPart) +
            ",\"timeoutSeconds\":" + std::to_string(options.timeout) +
            ",\"delaySeconds\":" + std::to_string(options.delay) +
            ",\"watchdogBudgetSeconds\":" + std::to_string(WatchdogBudget(options).count()) +
            ",\"outputSupplied\":" + (options.output.empty() ? "false" : "true") + fields + "}\n";
        if (options.log.empty()) std::fputs(line.c_str(), stdout);
        else
        {
            std::ofstream file(std::filesystem::path(options.log), std::ios::app | std::ios::binary);
            file << line;
            file.flush();
            if (!file) throw std::runtime_error("cannot persist evidence log");
        }
    }

    void Check(bool success, char const* stage)
    {
        if (!success)
        {
            DWORD error = GetLastError();
            Log(stage, error);
            throw evidence::Failure(stage, error);
        }
    }

    BOOL CALLBACK Abort(HDC, int) { return !stopRequested.load(); }

    void ServiceState(DWORD state, DWORD code = NO_ERROR)
    {
        SERVICE_STATUS status{};
        status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
        status.dwCurrentState = state;
        status.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP : 0;
        status.dwWin32ExitCode = code;
        if (!SetServiceStatus(serviceHandle, &status))
            throw std::runtime_error("SetServiceStatus failed");
    }

    int Print()
    {
        // A separate watchdog bounds synchronous GDI/driver calls that may ignore AbortProc.
        auto limit = std::chrono::steady_clock::now() + WatchdogBudget(options);
        std::thread watchdog([limit]
        {
            while (!done)
            {
                if (stopRequested || std::chrono::steady_clock::now() >= limit)
                {
                    try
                    {
                        Log("timeout-or-stop");
                        if (serviceHandle) ServiceState(SERVICE_STOPPED, ERROR_TIMEOUT);
                    }
                    catch (...) { std::fputs("PrintProbe: watchdog evidence/status failed\n", stderr); }
                    // This is the disposable submitter, never the spooler or PSA host.
                    TerminateProcess(GetCurrentProcess(), ERROR_TIMEOUT);
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
        int result = 0;
        HDC dc = nullptr;
        bool started = false;
        try
        {
            auto binary = evidence::ExecutableHash();
            auto title = evidence::JobHash(options.job);
            {
                std::scoped_lock guard(logMutex);
                executableHash = std::move(binary);
                jobHash = std::move(title);
            }
            Log("probe-started");
            for (unsigned i = 0; i < options.delay * 10 && !stopRequested; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (options.diagnoseContext) Log("submission-context", 0, 0, evidence::Context());
            if (!options.output.empty() && std::filesystem::exists(options.output))
                throw std::runtime_error("refuse existing diagnostic destination");
            HANDLE printer = nullptr;
            Check(OpenPrinterW(options.queue.data(), &printer, nullptr) != FALSE, "queue-open-failed");
            Check(ClosePrinter(printer) != FALSE, "queue-close-failed");
            dc = CreateDCW(L"WINSPOOL", options.queue.c_str(), nullptr, nullptr);
            Check(dc != nullptr, "create-dc-failed");
            Check(SetAbortProc(dc, Abort) > 0, "abort-proc-failed");
            DOCINFOW doc{};
            doc.cbSize = sizeof(doc);
            doc.lpszDocName = options.job.c_str();
            doc.lpszOutput = options.output.empty() ? nullptr : options.output.c_str();
            Log("startdoc-enter");
            auto start = evidence::CallStartDoc([&] { return StartDocW(dc, &doc); });
            if (!start.Succeeded())
            {
                Log("startdoc-failed", start.lastError, 0, start.Fields());
                throw evidence::Failure("startdoc-failed", start.lastError);
            }
            int job = start.rawReturn;
            started = true;
            Log("startdoc-returned", 0, job, start.Fields());
            Check(StartPage(dc) > 0, "startpage-failed");
            std::wstring text = L"Synthetic PSA feasibility page; no customer data.";
            Check(TextOutW(dc, 120, 120, text.c_str(), static_cast<int>(text.size())) != FALSE, "textout-failed");
            Check(Rectangle(dc, 120, 220, 1000, 1000) != FALSE, "rectangle-failed");
            Check(EndPage(dc) > 0, "endpage-failed");
            Check(EndDoc(dc) > 0, "enddoc-failed");
            started = false;
            Log("submitted-not-workflow-success", 0, job);
        }
        catch (std::exception const& error)
        {
            std::fprintf(stderr, "PrintProbe: %s\n", error.what());
            auto failure = dynamic_cast<evidence::Failure const*>(&error);
            DWORD code = failure && failure->code ? failure->code : ERROR_GEN_FAILURE;
            try
            {
                std::string fields = failure ?
                    ",\"failureStage\":\"" + std::string(failure->what()) + "\"" : "";
                Log("probe-failed", code, 0, fields);
            }
            catch (...) { std::fputs("PrintProbe: evidence log failed\n", stderr); }
            result = 1;
        }
        if (started && AbortDoc(dc) <= 0)
        {
            try { Log("abortdoc-failed", GetLastError()); }
            catch (...) { std::fputs("PrintProbe: abort evidence failed\n", stderr); }
            result = 1;
        }
        if (dc && !DeleteDC(dc)) result = 1;
        done = true;
        watchdog.join();
        return result;
    }

    void WINAPI Control(DWORD control)
    {
        if (control == SERVICE_CONTROL_STOP) stopRequested = true;
    }

    void WINAPI ServiceMain(DWORD, wchar_t**)
    {
        serviceHandle = RegisterServiceCtrlHandlerW(L"PsaPrintProbe", Control);
        if (!serviceHandle) return;
        try
        {
            ServiceState(SERVICE_RUNNING);
            int result = Print();
            ServiceState(SERVICE_STOPPED, result == 0 ? NO_ERROR : ERROR_GEN_FAILURE);
        }
        catch (std::exception const& error)
        {
            std::fprintf(stderr, "PrintProbe service: %s\n", error.what());
            SERVICE_STATUS status{SERVICE_WIN32_OWN_PROCESS, SERVICE_STOPPED, 0,
                ERROR_GEN_FAILURE, 0, 0, 0};
            SetServiceStatus(serviceHandle, &status);
        }
    }

    int SelfTest()
    {
        unsigned checked = 0;
        auto reject = [&checked](std::vector<std::wstring> args, char const* expectedError = nullptr)
        {
            try { Parse(args); }
            catch (std::exception const& error)
            {
                if (expectedError && std::string(error.what()).find(expectedError) == std::string::npos)
                    throw std::runtime_error("incorrect timing error");
                ++checked;
                return;
            }
            throw std::runtime_error("invalid arguments were accepted");
        };
        reject({});
        reject({L"--queue", L"q", L"--interactive", L"--noninteractive"});
        reject({L"--queue", L"q", L"--interactive", L"--output", L"C:\\x.oxps"});
        reject({L"--queue", L"q", L"--noninteractive", L"--output", L"C:\\x.pdf"});
        reject({L"--queue", L"q", L"--noninteractive", L"--output", L"x.oxps"});
        reject({L"--queue", L"q", L"--noninteractive", L"--timeout-seconds", L"0"},
            "--timeout-seconds must be an integer in 1..300");
        reject({L"--queue", L"q", L"--noninteractive", L"--timeout-seconds", L"-1"},
            "--timeout-seconds must be an integer in 1..300");
        reject({L"--queue", L"q", L"--noninteractive", L"--service"});
        reject({L"--queue", L"q", L"--noninteractive", L"--unknown", L"x"});
        reject({L"--queue", L"q", L"--noninteractive", L"--timeout-seconds", L"301"},
            "--timeout-seconds must be an integer in 1..300");
        reject({L"--queue", L"q", L"--noninteractive", L"--timeout-seconds", L"999999999999999999999"},
            "--timeout-seconds must be an integer in 1..300");
        reject({L"--queue", L"q", L"--noninteractive", L"--delay-seconds", L"-1"},
            "--delay-seconds must be an integer in 0..86400");
        reject({L"--queue", L"q", L"--noninteractive", L"--delay-seconds", L"86401"},
            "--delay-seconds must be an integer in 0..86400");
        reject({L"--queue", L"q", L"--noninteractive", L"--delay-seconds", L"999999999999999999999"},
            "--delay-seconds must be an integer in 0..86400");
        reject({L"--queue", L"q", L"--noninteractive", L"--delay-seconds", L"1.5"},
            "--delay-seconds must be an integer in 0..86400");
        auto valid = Parse({L"--queue", L"q", L"--noninteractive",
            L"--output", L"C:\\synthetic.oxps", L"--job-name", L"synthetic-token"});
        if (checked != 15 || valid.job != L"synthetic-token" || !valid.noninteractive ||
            WatchdogBudget(valid) != std::chrono::seconds(60))
            throw std::runtime_error("argument assertions failed");
        auto delayed = Parse({L"--service", L"--noninteractive", L"--queue",
            L"PSA Diagnostic Capture (no file UI)", L"--output",
            L"C:\\PsaLab\\evidence\\synthetic-service.oxps", L"--job-name", L"synthetic-service",
            L"--log", L"C:\\PsaLab\\evidence\\service.jsonl",
            L"--timeout-seconds", L"90", L"--delay-seconds", L"120"});
        auto start = std::chrono::steady_clock::time_point{};
        auto submission = start + std::chrono::seconds(delayed.delay);
        auto deadline = start + WatchdogBudget(delayed);
        if (!delayed.service || delayed.timeout != 90 || delayed.delay != 120 ||
            deadline != start + std::chrono::seconds(210) ||
            deadline - submission != std::chrono::seconds(90))
            throw std::runtime_error("documented service command must retain 90 seconds after its 120-second delay");
        for (auto const& values : {std::pair{L"1", L"0"}, std::pair{L"300", L"86400"},
            std::pair{L"240", L"120"}})
        {
            auto timing = Parse({L"--queue", L"q", L"--noninteractive", L"--timeout-seconds",
                values.first, L"--delay-seconds", values.second});
            if (WatchdogBudget(timing) - std::chrono::seconds(timing.delay) !=
                std::chrono::seconds(timing.timeout))
                throw std::runtime_error("delay must be added to, not deducted from, submission allowance");
        }
        std::puts("20 argument/timing cases passed; service delay 120 + timeout 90 = budget 210; no printer accessed.");
        auto diagnostic = Parse({L"--queue", L"q", L"--noninteractive", L"--diagnose-context"});
        if (!diagnostic.diagnoseContext || valid.diagnoseContext)
            throw std::runtime_error("context must be opt-in");
        for (auto raw : {-1, 0, 42})
        {
            SetLastError(ERROR_ACCESS_DENIED);
            auto startDoc = evidence::CallStartDoc([raw]
            {
                if (GetLastError() != ERROR_SUCCESS)
                    throw std::runtime_error("stale LastError not cleared");
                SetLastError(ERROR_SPL_NO_STARTDOC);
                return raw;
            });
            if (startDoc.rawReturn != raw || startDoc.lastError != ERROR_SPL_NO_STARTDOC ||
                startDoc.Succeeded() != (raw > 0) ||
                startDoc.Fields().find("\"startDocReturn\":" + std::to_string(raw)) == std::string::npos)
                throw std::runtime_error("raw StartDoc evidence changed");
        }
        auto noError = evidence::CallStartDoc([] { return -1; });
        if (noError.lastError != 0 || noError.Succeeded())
            throw std::runtime_error("missing LastError must remain an unsuccessful raw return");
        if (evidence::JobHash(L"synthetic-service-01") !=
            "d42fd077c5d93441b33f956cd37cced22a406c9bfd04107402d2bab07de35737" ||
            evidence::JobHash(L"\u00e9") !=
            "4a99557e4033c3539de2eb65472017cad5f9557f7a0625a09f1c3f6e2ba69c4c")
            throw std::runtime_error("job hash must match task UTF-8 SHA256");
        std::puts("Context opt-in, 4 raw StartDoc/error cases and 2 UTF-8 SHA256 vectors passed; no printer accessed.");
        return 0;
    }
}

int wmain(int argc, wchar_t** argv)
{
    try
    {
        if (argc == 2 && std::wstring(argv[1]) == L"--self-test") return SelfTest();
        if (argc == 2 && std::wstring(argv[1]) == L"--version")
        {
            std::printf("PrintProbe %s (%s); evidence schema psa-probe-v2\n",
                evidence::Version, evidence::Architecture);
            return 0;
        }
        options = Parse(std::vector<std::wstring>(argv + 1, argv + argc));
        if (options.service)
        {
            SERVICE_TABLE_ENTRYW table[] = {
                {const_cast<LPWSTR>(L"PsaPrintProbe"), ServiceMain}, {nullptr, nullptr}};
            if (!StartServiceCtrlDispatcherW(table))
            {
                std::fprintf(stderr, "SCM dispatcher failed: %lu\n", GetLastError());
                return 1;
            }
            return 0;
        }
        return Print();
    }
    catch (std::exception const& error)
    {
        std::fprintf(stderr, "PrintProbe: %s\n"
            "Usage: PrintProbe --queue NAME (--interactive | --noninteractive)"
            " [--output ABSOLUTE.oxps] [--job-name TOKEN] [--timeout-seconds 1..300]"
            " [--log ABSOLUTE.jsonl] [--service] [--delay-seconds 0..86400] [--diagnose-context]\n"
            "PrintProbe --version | --self-test\n"
            "Watchdog budget = delay + timeout; timeout is not reduced by the startup delay.\n", error.what());
        return 2;
    }
}
