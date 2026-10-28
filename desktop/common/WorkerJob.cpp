#include "WorkerJob.h"
#include <psapi.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <thread>
#include <cstdio>
#include <winrt/Windows.Foundation.h>

namespace proof
{
    namespace
    {
        constexpr WORD Machine =
#if defined(_M_ARM64)
            IMAGE_FILE_MACHINE_ARM64;
#elif defined(_M_X64)
            IMAGE_FILE_MACHINE_AMD64;
#else
#error Matching native x64 or ARM64 is required.
#endif
        std::wstring Lower(std::wstring text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](wchar_t ch)
            {
                return ch >= L'A' && ch <= L'Z' ? static_cast<wchar_t>(ch + L'a' - L'A') : ch;
            });
            return text;
        }
        bool IsNull(winrt::Windows::Data::Json::IJsonValue const& value)
        {
            return value.ValueType() == winrt::Windows::Data::Json::JsonValueType::Null;
        }
        void CheckMachine(HANDLE file)
        {
            IMAGE_DOS_HEADER dos{};
            DWORD read = 0;
            Check(ReadFile(file, &dos, sizeof(dos), &read, nullptr), "pe-read");
            Require(read == sizeof(dos) && dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew >= 64, "pe-dos");
            LARGE_INTEGER position{};
            position.QuadPart = dos.e_lfanew;
            Check(SetFilePointerEx(file, position, nullptr, FILE_BEGIN), "pe-seek");
            DWORD signature = 0;
            IMAGE_FILE_HEADER header{};
            Check(ReadFile(file, &signature, sizeof(signature), &read, nullptr), "pe-signature");
            Require(read == sizeof(signature) && signature == IMAGE_NT_SIGNATURE, "pe-signature");
            Check(ReadFile(file, &header, sizeof(header), &read, nullptr), "pe-header");
            Require(read == sizeof(header) && header.Machine == Machine, "pe-machine");
        }
        bool Integer(JsonObject const& status, wchar_t const* field)
        {
            auto value = status.GetNamedValue(field);
            if (value.ValueType() != winrt::Windows::Data::Json::JsonValueType::Number) return false;
            double number = value.GetNumber();
            return std::isfinite(number) && number >= 0 && number <= 9007199254740991.0 && std::floor(number) == number;
        }
        struct Pipe
        {
            Handle read, write;
            std::string bytes;
            explicit Pipe()
            {
                SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
                HANDLE in = nullptr, out = nullptr;
                Check(CreatePipe(&in, &out, &security, 0), "pipe-create");
                read.reset(in); write.reset(out);
                Check(SetHandleInformation(read.get(), HANDLE_FLAG_INHERIT, 0), "pipe-private");
            }
            bool Drain()
            {
                for (;;)
                {
                    DWORD available = 0;
                    if (!PeekNamedPipe(read.get(), nullptr, 0, nullptr, &available, nullptr))
                    {
                        DWORD error = GetLastError();
                        if (error == ERROR_BROKEN_PIPE) return true;
                        throw Failure("pipe-peek", error);
                    }
                    if (!available) return false;
                    std::array<char, 4096> buffer{};
                    DWORD readBytes = 0;
                    Check(ReadFile(read.get(), buffer.data(),
                        std::min<DWORD>(available, static_cast<DWORD>(buffer.size())), &readBytes, nullptr), "pipe-read");
                    Require(bytes.size() + readBytes <= 65536, "status-channel-limit", ERROR_BUFFER_OVERFLOW);
                    bytes.append(buffer.data(), readBytes);
                }
            }
        };
        struct ProcessGuard
        {
            Handle process, thread, job;
            bool Stop()
            {
                if (!process.get()) return true;
                bool terminated = true;
                if (job.get()) terminated = TerminateJobObject(job.get(), ERROR_CANCELLED) != FALSE;
                if (WaitForSingleObject(process.get(), 0) != WAIT_OBJECT_0)
                    terminated = TerminateProcess(process.get(), ERROR_CANCELLED) != FALSE && terminated;
                bool waited = WaitForSingleObject(process.get(), 5000) == WAIT_OBJECT_0;
                if (job.get())
                {
                    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION value{};
                    if (!QueryInformationJobObject(job.get(), JobObjectBasicAccountingInformation,
                        &value, sizeof(value), nullptr) || value.ActiveProcesses != 0) return false;
                }
                return waited && terminated;
            }
            ~ProcessGuard()
            {
                if (process.get() && WaitForSingleObject(process.get(), 0) == WAIT_TIMEOUT)
                {
                    if (!Stop()) std::fputs("PDF proof: process cleanup/reap incomplete.\n", stderr);
                }
            }
        };
        uint64_t Active(HANDLE job)
        {
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION value{};
            Check(QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &value,
                sizeof(value), nullptr), "job-accounting");
            return value.ActiveProcesses;
        }
    }

    Runtime LoadRuntime(std::filesystem::path const& root)
    {
        ValidatePath(root);
        Runtime runtime{root, Hash(root / L"runtime-imports.json"), {}};
        auto manifest = ReadJson(root / L"runtime-imports.json");
        Require(manifest.GetNamedString(L"schema") == L"4d-pdf-printer.pe-inventory" &&
            manifest.GetNamedNumber(L"version") == 2, "runtime-schema");
        auto receipt = ReadJson(root / L"native-test-receipt.json");
        auto architecture = Machine == IMAGE_FILE_MACHINE_ARM64 ? L"arm64" : L"x64";
        auto os = receipt.GetNamedString(L"os_architecture");
        std::wstring osName = Lower(std::wstring(os));
        Require(receipt.GetNamedString(L"result") == L"passed" &&
            receipt.GetNamedString(L"architecture") == architecture &&
            osName == architecture, "native-receipt");
        auto tests = receipt.GetNamedArray(L"tests");
        std::set<std::wstring> testNames;
        for (auto const& value : tests) testNames.emplace(value.GetString());
        Require(testNames.contains(L"worker-contract") && testNames.contains(L"output-transactions"), "native-tests");
        OpenRead(root / L"build-provenance.json");
        std::set<std::wstring> names;
        for (auto const& entry : manifest.GetNamedArray(L"files"))
        {
            auto record = entry.GetObject();
            std::wstring name(record.GetNamedString(L"file"));
            Require(!name.empty() && std::filesystem::path(name).filename() == name &&
                name.find_first_of(L"/\\:") == std::wstring::npos, "runtime-name");
            std::wstring normalized = Lower(name);
            Require(names.insert(normalized).second, "runtime-duplicate");
            auto path = root / name;
            auto file = OpenRead(path);
            CheckMachine(file.get());
            auto digest = Hash(file.get());
            Require(digest == record.GetNamedString(L"sha256"), "runtime-hash");
            LARGE_INTEGER size{};
            Check(GetFileSizeEx(file.get(), &size), "runtime-size");
            Require(static_cast<double>(size.QuadPart) == record.GetNamedNumber(L"bytes"), "runtime-size");
            if (normalized != L"pdf-render-worker.exe")
                Require(path.extension() == L".dll" && !IsNull(record.GetNamedValue(L"package_file_list")),
                    "runtime-owner");
            runtime.files.push_back({path, digest});
        }
        Require(names.contains(L"pdf-render-worker.exe") &&
            Hash(root / L"pdf-render-worker.exe") == receipt.GetNamedString(L"worker_sha256"), "worker-receipt");
        for (auto const& entry : std::filesystem::directory_iterator(root))
        {
            auto extension = entry.path().extension().wstring();
            extension = Lower(extension);
            if (extension != L".dll" && extension != L".exe") continue;
            auto name = entry.path().filename().wstring();
            name = Lower(name);
            Require(names.contains(name), "runtime-uninventoried");
        }
        auto notices = manifest.GetNamedArray(L"notices");
        Require(notices.Size() > 0, "runtime-notices");
        for (auto const& entry : notices)
        {
            auto value = entry.GetObject();
            auto name = std::filesystem::path(std::wstring(value.GetNamedString(L"file")));
            Require(!name.has_root_path() && name.begin() != name.end(), "notice-path");
            for (auto const& part : name) Require(part != L"..", "notice-path");
            auto path = root / name;
            auto digest = Hash(path);
            Require(digest == value.GetNamedString(L"sha256"), "notice-hash");
            runtime.files.push_back({path, digest});
        }
        for (auto name : {L"build-provenance.json", L"native-test-receipt.json"})
            runtime.files.push_back({root / name, Hash(root / name)});
        return runtime;
    }

    bool ValidSuccess(JsonObject const& status, DWORD exit)
    {
        try
        {
            constexpr wchar_t const* fields[] = {L"schema", L"version", L"worker_version", L"result",
                L"exit_code", L"stage", L"error", L"document", L"page", L"documents",
                L"pages_completed", L"output_bytes", L"output_published", L"cleanup_complete"};
            if (status.Size() != std::size(fields)) return false;
            for (auto key : fields) if (!status.HasKey(key)) return false;
            if (exit != 0 || !Integer(status, L"version") || status.GetNamedNumber(L"version") != 1 ||
                status.GetNamedString(L"schema") != L"4d-pdf-printer.worker-status" ||
                status.GetNamedString(L"worker_version").empty() ||
                status.GetNamedString(L"result") != L"ok" ||
                !Integer(status, L"exit_code") || status.GetNamedNumber(L"exit_code") != exit ||
                status.GetNamedString(L"stage") != L"complete" ||
                !IsNull(status.GetNamedValue(L"error")) ||
                !IsNull(status.GetNamedValue(L"document")) || !IsNull(status.GetNamedValue(L"page")) ||
                !Integer(status, L"documents") || status.GetNamedNumber(L"documents") < 1 ||
                !Integer(status, L"pages_completed") || status.GetNamedNumber(L"pages_completed") < 1 ||
                !Integer(status, L"output_bytes") || status.GetNamedNumber(L"output_bytes") < 1 ||
                !status.GetNamedBoolean(L"output_published") || !status.GetNamedBoolean(L"cleanup_complete")) return false;
            return true;
        }
        catch (winrt::hresult_error const&) { return false; }
    }

    JsonObject ParseSuccess(std::string const& bytes, DWORD exit)
    {
        Require(!bytes.empty() && bytes.size() <= 65536 && bytes.back() == '\n' &&
            bytes.find('\n') == bytes.size() - 1 && !bytes.starts_with("\xef\xbb\xbf"),
            "worker-status-framing");
        auto status = JsonObject::Parse(winrt::to_hstring(bytes));
        Require(ValidSuccess(status, exit), "worker-status");
        return status;
    }

    JsonObject RunWorker(Runtime const& runtime, std::filesystem::path const& input,
        std::filesystem::path const& jobRoot, Trace& trace, WorkerOptions options)
    {
        auto result = Record(L"worker-preflight");
        Text(result, L"result", L"error");
        Flag(result, L"destination_published", false);
        auto start = std::chrono::steady_clock::now();
        ProcessGuard process;
        std::vector<Handle> runtimeLocks;
        Handle source;
        try
        {
        source = OpenRead(input);
        LARGE_INTEGER inputSize{};
        Check(GetFileSizeEx(source.get(), &inputSize), "input-size");
        Require(inputSize.QuadPart > 0 && inputSize.QuadPart <= 256ll * 1024 * 1024, "input-limit");
        Number(result, L"input_bytes", static_cast<uint64_t>(inputSize.QuadPart));
        Text(result, L"input_sha256", Hash(source.get()));
        Text(result, L"runtime_inventory_sha256", runtime.inventoryHash);
        Require(Hash(runtime.root / L"runtime-imports.json") == runtime.inventoryHash, "inventory-changed");
        for (auto const& entry : runtime.files)
        {
            auto file = OpenRead(entry.path);
            Require(Hash(file.get()) == entry.sha256, "runtime-changed");
            runtimeLocks.push_back(std::move(file));
        }
        ValidatePath(jobRoot, false);
        Check(CreateDirectoryW(jobRoot.c_str(), nullptr), "job-root-create");
        Flag(result, L"created_by_supervisor", true);
        auto output = jobRoot / L"output.pdf";
        Pipe stdoutPipe, stderrPipe;
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        Handle nullInput(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        Check(nullInput.get() != INVALID_HANDLE_VALUE, "stdin");
        process.job.reset(CreateJobObjectW(nullptr, nullptr));
        Check(process.job.get() != nullptr, "job-create");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_JOB_MEMORY;
        limits.JobMemoryLimit = 1024ull * 1024 * 1024;
        Check(SetInformationJobObject(process.job.get(), JobObjectExtendedLimitInformation,
            &limits, sizeof(limits)), "job-limit");
        SIZE_T attributesSize = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attributesSize);
        std::vector<unsigned char> attributes(attributesSize);
        auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
        Check(InitializeProcThreadAttributeList(list, 1, 0, &attributesSize), "attribute-list");
        struct DeleteAttributes { LPPROC_THREAD_ATTRIBUTE_LIST list; ~DeleteAttributes() { DeleteProcThreadAttributeList(list); } } deleteAttributes{list};
        HANDLE inherit[] = {nullInput.get(), stdoutPipe.write.get(), stderrPipe.write.get()};
        Check(UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit,
            sizeof(inherit), nullptr, nullptr), "handle-whitelist");
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = nullInput.get();
        startup.StartupInfo.hStdOutput = stdoutPipe.write.get();
        startup.StartupInfo.hStdError = stderrPipe.write.get();
        startup.lpAttributeList = list;
        auto executable = runtime.root / L"pdf-render-worker.exe";
        std::wstring command = Quote(executable.native()) + L" --protocol-version 1 --input " +
            Quote(input.native()) + L" --output " + Quote(output.native());
        std::array<wchar_t, 32768> system{};
        DWORD systemLength = GetEnvironmentVariableW(L"SystemRoot", system.data(), static_cast<DWORD>(system.size()));
        Require(systemLength > 0 && systemLength < system.size(), "system-root");
        std::wstring environment = L"PATH=" + std::wstring(system.data()) + L"\\System32";
        environment.push_back(0);
        environment += L"SystemRoot=" + std::wstring(system.data()); environment.push_back(0);
        environment += L"TEMP=" + jobRoot.native(); environment.push_back(0);
        environment += L"TMP=" + jobRoot.native(); environment.push_back(0); environment.push_back(0);
        PROCESS_INFORMATION info{};
        Check(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
            environment.data(), jobRoot.c_str(), &startup.StartupInfo, &info), "worker-create");
        process.process.reset(info.hProcess); process.thread.reset(info.hThread);
        Check(AssignProcessToJobObject(process.job.get(), process.process.get()), "job-assign-before-resume");
        FILETIME created{}, exited{}, kernel{}, user{};
        Check(GetProcessTimes(process.process.get(), &created, &exited, &kernel, &user), "process-times");
        Number(result, L"pid", info.dwProcessId);
        ULARGE_INTEGER creation{};
        creation.LowPart = created.dwLowDateTime; creation.HighPart = created.dwHighDateTime;
        Text(result, L"creation_filetime", std::to_wstring(creation.QuadPart));
        trace.Event(L"worker-start", result);
        Check(ResumeThread(process.thread.get()) != MAXDWORD, "worker-resume");
        stdoutPipe.write.reset(); stderrPipe.write.reset(); nullInput.reset();
        bool stopped = false;
        std::chrono::steady_clock::time_point stopDecision{};
        std::wstring reason;
        uint64_t peakPrivate = 0, peakWorkingSet = 0, peakHandles = 0;
        auto resumed = std::chrono::steady_clock::now();
        try
        {
            for (;;)
            {
                stdoutPipe.Drain(); stderrPipe.Drain();
                DWORD wait = WaitForSingleObject(process.process.get(), 10);
                if (wait == WAIT_OBJECT_0) break;
                if (wait == WAIT_FAILED) throw Failure("worker-wait", GetLastError());
                PROCESS_MEMORY_COUNTERS_EX memory{};
                if (!GetProcessMemoryInfo(process.process.get(),
                    reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
                {
                    DWORD error = GetLastError();
                    if (WaitForSingleObject(process.process.get(), 0) == WAIT_OBJECT_0) break;
                    throw Failure("worker-memory", error);
                }
                peakPrivate = std::max<uint64_t>(peakPrivate, memory.PrivateUsage);
                peakWorkingSet = std::max<uint64_t>(peakWorkingSet, memory.WorkingSetSize);
                DWORD handles = 0;
                if (!GetProcessHandleCount(process.process.get(), &handles))
                {
                    DWORD error = GetLastError();
                    if (WaitForSingleObject(process.process.get(), 0) == WAIT_OBJECT_0) break;
                    throw Failure("worker-handles", error);
                }
                peakHandles = std::max<uint64_t>(peakHandles, handles);
                auto elapsed = std::chrono::steady_clock::now() - resumed;
                uint64_t diskBytes = 0;
                for (auto const& entry : std::filesystem::recursive_directory_iterator(jobRoot))
                {
                    ValidatePath(entry.path());
                    if (entry.is_regular_file()) diskBytes += entry.file_size();
                }
                Require(diskBytes <= 256ull * 1024 * 1024, "output-growth", ERROR_FILE_TOO_LARGE);
                if (elapsed >= options.deadline) reason = L"deadline";
                else if (options.fault != WorkerOptions::None && elapsed >= std::chrono::milliseconds(10))
                    reason = options.fault == WorkerOptions::Cancel ? L"cancel" : L"forced-termination";
                if (!reason.empty())
                {
                    trace.Event(L"worker-stop-decision");
                    stopDecision = std::chrono::steady_clock::now();
                    Check(TerminateJobObject(process.job.get(), ERROR_CANCELLED), "worker-terminate");
                    Require(WaitForSingleObject(process.process.get(), 5000) == WAIT_OBJECT_0, "worker-reap", ERROR_TIMEOUT);
                    Number(result, L"stop_to_reap_ms", static_cast<uint64_t>(std::chrono::duration_cast<
                        std::chrono::milliseconds>(std::chrono::steady_clock::now() - stopDecision).count()));
                    stopped = true;
                    break;
                }
            }
            stdoutPipe.Drain(); stderrPipe.Drain();
        }
        catch (...)
        {
            Check(TerminateJobObject(process.job.get(), ERROR_CANCELLED), "channel-terminate");
            Require(WaitForSingleObject(process.process.get(), 5000) == WAIT_OBJECT_0, "channel-reap", ERROR_TIMEOUT);
            throw;
        }
        auto reapDeadline = (stopped ? stopDecision : std::chrono::steady_clock::now()) + std::chrono::seconds(5);
        while (Active(process.job.get()) && std::chrono::steady_clock::now() < reapDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        Require(Active(process.job.get()) == 0, "surviving-child", ERROR_TIMEOUT);
        if (stopped) Number(result, L"stop_to_reap_ms", static_cast<uint64_t>(std::chrono::duration_cast<
            std::chrono::milliseconds>(std::chrono::steady_clock::now() - stopDecision).count()));
        DWORD exitCode = 0;
        Check(GetExitCodeProcess(process.process.get(), &exitCode), "worker-exit");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION accounting{};
        Check(QueryInformationJobObject(process.job.get(), JobObjectExtendedLimitInformation,
            &accounting, sizeof(accounting), nullptr), "job-peak");
        Number(result, L"exit_code", exitCode);
        Check(GetProcessTimes(process.process.get(), &created, &exited, &kernel, &user), "worker-cpu");
        ULARGE_INTEGER kernelTime{}, userTime{};
        kernelTime.LowPart = kernel.dwLowDateTime; kernelTime.HighPart = kernel.dwHighDateTime;
        userTime.LowPart = user.dwLowDateTime; userTime.HighPart = user.dwHighDateTime;
        Number(result, L"cpu_100ns", kernelTime.QuadPart + userTime.QuadPart);
        Number(result, L"sampled_peak_private_bytes", peakPrivate);
        Number(result, L"sampled_peak_working_set", peakWorkingSet);
        Number(result, L"sampled_peak_handles", peakHandles);
        Number(result, L"kernel_peak_committed_bytes", accounting.PeakJobMemoryUsed);
        Number(result, L"active_processes_after_reap", Active(process.job.get()));
        Flag(result, L"reaped", true);
        Number(result, L"stderr_bytes", stderrPipe.bytes.size());
        Number(result, L"elapsed_ms", static_cast<uint64_t>(std::chrono::duration_cast<
            std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count()));
        if (stopped)
        {
            Text(result, L"result", L"stopped");
            Text(result, L"stop_reason", reason);
            Flag(result, L"worker_private_output_exists", std::filesystem::exists(output));
        }
        else
        {
            auto status = ParseSuccess(stdoutPipe.bytes, exitCode);
            result.Insert(L"worker_status", status);
            auto file = OpenRead(output);
            LARGE_INTEGER size{};
            Check(GetFileSizeEx(file.get(), &size), "output-size");
            Require(size.QuadPart > 0 && size.QuadPart <= 256ll * 1024 * 1024 &&
                static_cast<double>(size.QuadPart) == status.GetNamedNumber(L"output_bytes"), "output-size");
            Number(result, L"output_bytes", static_cast<uint64_t>(size.QuadPart));
            Text(result, L"output_sha256", Hash(file.get()));
            Text(result, L"result", L"ok");
        }
        Text(result, L"stage", L"worker-reaped");
        trace.Event(L"worker-reaped", result);
        return result;
        }
        catch (Failure const& error)
        {
            bool reaped = process.Stop();
            Text(result, L"stage", winrt::to_hstring(error.stage).c_str());
            Text(result, L"error_domain", winrt::to_hstring(error.domain).c_str());
            Number(result, L"native_error", error.code);
            Flag(result, L"reaped", reaped);
            Flag(result, L"reconciliation_required", result.GetNamedBoolean(L"created_by_supervisor", false));
            Text(result, L"result", L"error");
            return result;
        }
        catch (winrt::hresult_error const& error)
        {
            bool reaped = process.Stop();
            Text(result, L"stage", L"worker-json");
            Text(result, L"error_domain", L"hresult");
            Number(result, L"native_error", static_cast<DWORD>(error.code().value));
            Flag(result, L"reaped", reaped);
            Flag(result, L"reconciliation_required", result.GetNamedBoolean(L"created_by_supervisor", false));
            Text(result, L"result", L"error");
            return result;
        }
        catch (std::filesystem::filesystem_error const& error)
        {
            bool reaped = process.Stop();
            Text(result, L"stage", L"worker-filesystem");
            Number(result, L"native_error", static_cast<DWORD>(error.code().value()));
            Flag(result, L"reaped", reaped);
            Flag(result, L"reconciliation_required", result.GetNamedBoolean(L"created_by_supervisor", false));
            Text(result, L"result", L"error");
            return result;
        }
    }
}
