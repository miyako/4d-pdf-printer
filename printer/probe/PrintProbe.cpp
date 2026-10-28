#include <windows.h>
#include <winspool.h>
#include <objbase.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    struct Options
    {
        std::wstring queue, output, job, log;
        bool interactive = false, noninteractive = false, service = false;
        unsigned timeout = 60, delay = 0;
    };

    unsigned Number(std::wstring const& value, unsigned maximum)
    {
        if (value.empty() || value.find_first_not_of(L"0123456789") != std::wstring::npos)
            throw std::invalid_argument("invalid numeric option");
        auto number = std::stoul(value);
        if (number > maximum) throw std::invalid_argument("numeric option out of range");
        return static_cast<unsigned>(number);
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
            else
            {
                if (i + 1 == args.size()) throw std::invalid_argument("missing option value");
                auto const& value = args[++i];
                if (arg == L"--queue") result.queue = value;
                else if (arg == L"--output") result.output = value;
                else if (arg == L"--job-name") result.job = value;
                else if (arg == L"--log") result.log = value;
                else if (arg == L"--timeout-seconds") result.timeout = Number(value, 300);
                else if (arg == L"--delay-seconds") result.delay = Number(value, 86400);
                else throw std::invalid_argument("unknown option");
            }
        }
        if (result.queue.empty() || result.interactive == result.noninteractive || result.timeout == 0)
            throw std::invalid_argument("require queue, exactly one interaction mode, positive timeout");
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

    void Log(char const* event, DWORD error = 0, int jobId = 0)
    {
        DWORD sessionId = MAXDWORD;
        ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);
        FILETIME now{};
        GetSystemTimeAsFileTime(&now);
        ULARGE_INTEGER timestamp{};
        timestamp.LowPart = now.dwLowDateTime;
        timestamp.HighPart = now.dwHighDateTime;
        std::string line = "{\"schema\":\"psa-probe-v1\",\"event\":\"" + std::string(event) +
            "\",\"win32Error\":" + std::to_string(error) + ",\"spoolerJobId\":" +
            std::to_string(jobId) + ",\"processId\":" + std::to_string(GetCurrentProcessId()) +
            ",\"windowsSessionId\":" + std::to_string(sessionId) +
            ",\"utcFileTime\":" + std::to_string(timestamp.QuadPart) +
            ",\"outputSupplied\":" + (options.output.empty() ? "false" : "true") + "}\n";
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
            throw std::runtime_error(stage);
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
        Log("probe-started");
        // A separate watchdog bounds synchronous GDI/driver calls that may ignore AbortProc.
        std::thread watchdog([]
        {
            auto limit = std::chrono::steady_clock::now() +
                std::chrono::seconds(options.timeout + options.delay);
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
            for (unsigned i = 0; i < options.delay * 10 && !stopRequested; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
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
            int job = StartDocW(dc, &doc);
            Check(job > 0, "startdoc-failed");
            started = true;
            Log("startdoc-returned", 0, job);
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
            try { Log("probe-failed"); }
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
        auto reject = [&checked](std::vector<std::wstring> args)
        {
            try { Parse(args); }
            catch (std::exception const&) { ++checked; return; }
            throw std::runtime_error("invalid arguments were accepted");
        };
        reject({});
        reject({L"--queue", L"q", L"--interactive", L"--noninteractive"});
        reject({L"--queue", L"q", L"--interactive", L"--output", L"C:\\x.oxps"});
        reject({L"--queue", L"q", L"--noninteractive", L"--output", L"C:\\x.pdf"});
        reject({L"--queue", L"q", L"--noninteractive", L"--output", L"x.oxps"});
        reject({L"--queue", L"q", L"--noninteractive", L"--timeout-seconds", L"0"});
        reject({L"--queue", L"q", L"--noninteractive", L"--timeout-seconds", L"-1"});
        reject({L"--queue", L"q", L"--noninteractive", L"--service"});
        reject({L"--queue", L"q", L"--noninteractive", L"--unknown", L"x"});
        auto valid = Parse({L"--queue", L"q", L"--noninteractive",
            L"--output", L"C:\\synthetic.oxps", L"--job-name", L"synthetic-token"});
        if (checked != 9 || valid.job != L"synthetic-token" || !valid.noninteractive)
            throw std::runtime_error("argument assertions failed");
        std::puts("10 argument cases passed; no printer accessed.");
        return 0;
    }
}

int wmain(int argc, wchar_t** argv)
{
    try
    {
        if (argc == 2 && std::wstring(argv[1]) == L"--self-test") return SelfTest();
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
            " [--log ABSOLUTE.jsonl] [--service] [--delay-seconds 0..86400]\n", error.what());
        return 2;
    }
}
