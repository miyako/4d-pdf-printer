#include "Fixtures.h"
#include "../common/WorkerJob.h"
#include "../../printer/probe/Evidence.h"
#include <psapi.h>
#include <cstdio>
#include <map>
#include <set>
#include <array>

using namespace proof;

namespace
{
    struct Options
    {
        std::wstring command;
        std::map<std::wstring, std::wstring> values;
        bool control = false;
        std::wstring Get(wchar_t const* name) const
        {
            auto found = values.find(name);
            Require(found != values.end() && !found->second.empty(), "arguments", ERROR_INVALID_PARAMETER);
            return found->second;
        }
    };
    Options Parse(int count, wchar_t** args)
    {
        Require(count >= 2, "arguments", ERROR_INVALID_PARAMETER);
        Options options;
        options.command = args[1];
        Require(options.command == L"--self-test" || options.command == L"--fixtures" ||
            options.command == L"--print" || options.command == L"--repeat" ||
            options.command == L"--runtime-check", "command", ERROR_INVALID_PARAMETER);
        std::map<std::wstring, std::set<std::wstring>> supported{
            {L"--self-test", {}},
            {L"--fixtures", {L"--directory", L"--report"}},
            {L"--print", {L"--manifest", L"--id", L"--queue", L"--output", L"--report"}},
            {L"--repeat", {L"--directory", L"--report", L"--manifest", L"--runtime", L"--verbose-log"}},
            {L"--runtime-check", {L"--runtime", L"--report"}}
        };
        auto const& allowed = supported.at(options.command);
        for (int index = 2; index < count; ++index)
        {
            std::wstring name(args[index]);
            if (name == L"--control")
            {
                Require(options.command == L"--print" && !options.control, "control-option");
                options.control = true;
                continue;
            }
            Require(allowed.contains(name) && index + 1 < count, "arguments", ERROR_INVALID_PARAMETER);
            Require(options.values.emplace(name, args[++index]).second, "duplicate-option");
        }
        return options;
    }

    void SelfTest()
    {
        Require(Quote(L"") == L"\"\"" && Quote(L"a b") == L"\"a b\"" &&
            Quote(L"x\\") == L"\"x\\\\\"" && Quote(L"a\"b") == L"\"a\\\"b\"", "quote-test");
        auto status = JsonObject::Parse(LR"({"schema":"4d-pdf-printer.worker-status","version":1,"worker_version":"0.1.0","result":"ok","exit_code":0,"stage":"complete","error":null,"document":null,"page":null,"documents":1,"pages_completed":2,"output_bytes":6115,"output_published":true,"cleanup_complete":true})");
        Require(ValidSuccess(status, 0), "status-test");
        Require(!ValidSuccess(status, 24) && !ValidSuccess(status, 71), "exit-test");
        status.Insert(L"result", JsonValue::CreateStringValue(L"info"));
        Require(!ValidSuccess(status, 0), "info-test");
        status.Insert(L"result", JsonValue::CreateStringValue(L"ok"));
        Flag(status, L"cleanup_complete", false);
        Require(!ValidSuccess(status, 0), "cleanup-test");
        Flag(status, L"cleanup_complete", true);
        Number(status, L"version", 2);
        Require(!ValidSuccess(status, 0), "version-test");
        Number(status, L"version", 1);
        auto good = winrt::to_string(status.Stringify()) + "\n";
        Require(ParseSuccess(good, 0).GetNamedString(L"result") == L"ok", "framing-positive");
        for (auto const& bad : {std::string{}, std::string("\n"), std::string("{\n"),
            good + good, std::string("\xef\xbb\xbf") + good, good.substr(0, good.size() - 1)})
        {
            bool rejected = false;
            try { ParseSuccess(bad, 0); }
            catch (Failure const&) { rejected = true; }
            catch (winrt::hresult_error const&) { rejected = true; }
            Require(rejected, "framing-negative");
        }
        status.Insert(L"extra", JsonValue::CreateBooleanValue(true));
        Require(!ValidSuccess(status, 0), "extra-field-test");
        std::array<wchar_t, MAX_PATH> temporary{}, filename{};
        DWORD length = GetTempPathW(static_cast<DWORD>(temporary.size()), temporary.data());
        Require(length > 0 && length < temporary.size(), "hash-temp-path");
        Check(GetTempFileNameW(temporary.data(), L"pft", 0, filename.data()) != 0, "hash-temp-name");
        struct DeleteTemporary { wchar_t* name; ~DeleteTemporary() { DeleteFileW(name); } } deleteTemporary{filename.data()};
        Handle file(CreateFileW(filename.data(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
            OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE | FILE_ATTRIBUTE_TEMPORARY, nullptr));
        Check(file.get() != INVALID_HANDLE_VALUE, "hash-temp-open");
        DWORD written = 0;
        Check(WriteFile(file.get(), "abc", 3, &written, nullptr), "hash-temp-write");
        Require(written == 3, "hash-temp-write");
        for (unsigned round = 0; round < 16; ++round)
            Require(Hash(file.get()) == L"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                "hash-contract");
    }

    uint64_t PrivateBytes()
    {
        PROCESS_MEMORY_COUNTERS_EX memory{};
        Check(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
            sizeof(memory)), "observer-memory");
        return memory.PrivateUsage;
    }
    DWORD Handles()
    {
        DWORD count = 0;
        Check(GetProcessHandleCount(GetCurrentProcess(), &count), "observer-handles");
        return count;
    }

    JsonObject Repeat(Options const& options, JsonObject report)
    {
        auto runtime = LoadRuntime(options.Get(L"--runtime"));
        auto input = ReadJson(options.Get(L"--manifest"));
        Require(input.GetNamedString(L"schema") == L"4d-pdf-printer.proof-inputs" &&
            input.GetNamedNumber(L"version") == 1, "input-manifest");
        auto inputs = input.GetNamedArray(L"inputs");
        Require(inputs.Size() == 3, "input-count");
        constexpr wchar_t const* ids[] = {L"text", L"vectors", L"mixed"};
        for (uint32_t index = 0; index < inputs.Size(); ++index)
            Require(inputs.GetObjectAt(index).GetNamedString(L"id") == ids[index], "input-order");
        auto root = std::filesystem::path(options.Get(L"--directory"));
        ValidatePath(root, false);
        Check(CreateDirectoryW(root.c_str(), nullptr), "repeat-directory");
        Trace trace(options.values.contains(L"--verbose-log") ?
            std::filesystem::path(options.Get(L"--verbose-log")) : std::filesystem::path{});
        JsonArray results, faults;
        auto run = [&](uint32_t index, std::wstring const& name, WorkerOptions settings = {})
        {
            auto source = inputs.GetObjectAt(index);
            auto job = root / name;
            auto value = RunWorker(runtime, std::wstring(source.GetNamedString(L"input")), job, trace, settings);
            Text(value, L"fixture_id", std::wstring(source.GetNamedString(L"id")));
            Text(value, L"job_name", name);
            return value;
        };
        auto warm = run(0, L"warm-up");
        report.Insert(L"warm_up", warm);
        Require(warm.GetNamedString(L"result") == L"ok" && warm.GetNamedBoolean(L"reaped"), "warm-up");
        uint64_t beforeBytes = PrivateBytes();
        DWORD beforeHandles = Handles();
        bool passed = true;
        std::set<std::wstring> processInstances;
        for (uint32_t index = 0; index < 30; ++index)
        {
            auto value = run(index % 3, L"job-" + std::to_wstring(index));
            bool clean = value.GetNamedString(L"result") == L"ok" && value.GetNamedBoolean(L"reaped");
            if (clean)
            {
                auto key = std::to_wstring(static_cast<uint64_t>(value.GetNamedNumber(L"pid"))) +
                    L":" + std::wstring(value.GetNamedString(L"creation_filetime"));
                Require(processInstances.insert(key).second, "worker-instance-reused");
                auto job = root / std::wstring(value.GetNamedString(L"job_name"));
                uint64_t unexpected = 0;
                for (auto const& entry : std::filesystem::directory_iterator(job))
                    if (entry.path().filename() != L"output.pdf") ++unexpected;
                Number(value, L"unaccounted_staging_entries", unexpected);
                clean = unexpected == 0;
            }
            Number(value, L"observer_private_bytes_after_reap", PrivateBytes());
            Number(value, L"observer_handles_after_reap", Handles());
            results.Append(value);
            if (!clean) { passed = false; break; }
        }
        report.Insert(L"jobs", results);
        Number(report, L"observer_baseline_private_bytes", beforeBytes);
        Number(report, L"observer_baseline_handles", beforeHandles);
        Number(report, L"observer_final_private_bytes", PrivateBytes());
        Number(report, L"observer_final_handles", Handles());
        passed = passed && results.Size() == 30 && Handles() <= beforeHandles + 4 &&
            PrivateBytes() <= beforeBytes + 16 * 1024 * 1024;
        report.Insert(L"faults", faults);
        if (passed)
        {
            for (auto kind : {L"deadline", L"cancel", L"forced-termination"})
            {
                WorkerOptions fault;
                if (std::wstring(kind) == L"deadline") fault.deadline = std::chrono::milliseconds(1);
                else fault.fault = std::wstring(kind) == L"cancel" ? WorkerOptions::Cancel : WorkerOptions::Terminate;
                auto value = run(1, L"fault-" + std::wstring(kind), fault);
                bool observed = value.GetNamedString(L"result") == L"stopped" &&
                    value.GetNamedBoolean(L"reaped") && value.GetNamedString(L"stop_reason") == kind;
                Flag(value, L"fault_observed", observed);
                Text(value, L"fault_scope", L"active-process; not proof of render stage or native AV");
                faults.Append(value);
                if (observed && value.GetNamedBoolean(L"created_by_supervisor"))
                {
                    auto job = root / (L"fault-" + std::wstring(kind));
                    ValidatePath(job);
                    std::filesystem::remove_all(job);
                    Flag(value, L"private_experiment_cleanup_complete", !std::filesystem::exists(job));
                }
                passed = passed && observed;
            }
        }
        report.Insert(L"faults", faults);
        Flag(report, L"verbose_truncated", trace.truncated);
        Text(report, L"stage", L"worker-experiment-complete");
        Text(report, L"result", passed ? L"passed" : L"failed");
        Flag(report, L"psa_pipeline_tested", false);
        Flag(report, L"manual_fidelity_review_required", true);
        return report;
    }
}

int wmain(int argc, wchar_t** argv)
{
    JsonObject report{nullptr};
    std::filesystem::path output;
    try
    {
        winrt::init_apartment();
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        auto options = Parse(argc, argv);
        report = Record(L"arguments");
        Text(report, L"result", L"error");
        Text(report, L"process_architecture", winrt::to_hstring(evidence::Architecture).c_str());
        if (options.values.contains(L"--report")) output = options.Get(L"--report");
        USHORT processMachine = 0, nativeMachine = 0;
        Check(IsWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine), "native-context");
        Require(processMachine == IMAGE_FILE_MACHINE_UNKNOWN &&
            nativeMachine ==
#if defined(_M_ARM64)
            IMAGE_FILE_MACHINE_ARM64,
#else
            IMAGE_FILE_MACHINE_AMD64,
#endif
            "native-architecture", ERROR_NOT_SUPPORTED);
        report.Insert(L"caller_context", JsonObject::Parse(winrt::to_hstring(
            std::string("{\"version\":1") + evidence::Context() + "}")));
        Text(report, L"executable_sha256", winrt::to_hstring(evidence::ExecutableHash()).c_str());
        if (options.command == L"--self-test")
        {
            Require(argc == 2, "self-test-arguments");
            SelfTest();
            Text(report, L"stage", L"self-test-passed");
            Text(report, L"result", L"passed");
        }
        else
        {
            Require(!output.empty() && !std::filesystem::exists(output), "new-report");
            ValidatePath(output, false);
            if (options.command == L"--runtime-check")
            {
                auto runtime = LoadRuntime(options.Get(L"--runtime"));
                Text(report, L"runtime_inventory_sha256", runtime.inventoryHash);
                Number(report, L"pinned_file_count", runtime.files.size());
                Flag(report, L"os_ownership_attested", false);
                Text(report, L"stage", L"runtime-preflight-not-conversion");
                Text(report, L"result", L"passed");
            }
            else if (options.command == L"--fixtures") report = CreateFixtures(options.Get(L"--directory"));
            else if (options.command == L"--print")
                report = PrintFixture(options.Get(L"--manifest"), options.Get(L"--id"),
                    options.Get(L"--queue"), options.Get(L"--output"), options.control);
            else report = Repeat(options, report);
            report.Insert(L"caller_context", JsonObject::Parse(winrt::to_hstring(
                std::string("{\"version\":1") + evidence::Context() + "}")));
            Text(report, L"executable_sha256", winrt::to_hstring(evidence::ExecutableHash()).c_str());
            Text(report, L"process_architecture", winrt::to_hstring(evidence::Architecture).c_str());
            WriteNew(output, report);
        }
        auto result = report.GetNamedString(L"result", L"fixtures");
        std::puts(result == L"error" || result == L"failed" ?
            "PDF proof failed; read the private structured report." :
            "PDF proof observation recorded; not a usable printer.");
        return result == L"error" || result == L"failed" ? 1 : 0;
    }
    catch (...)
    {
        auto code = winrt::to_hresult();
        try { throw; }
        catch (Failure const& error)
        {
            if (report) { Text(report, L"stage", winrt::to_hstring(error.stage).c_str());
                Text(report, L"error_domain", winrt::to_hstring(error.domain).c_str());
                Number(report, L"native_error", error.code); }
        }
        catch (std::filesystem::filesystem_error const& error)
        {
            if (report) { Text(report, L"stage", L"proof-filesystem");
                Number(report, L"native_error", static_cast<DWORD>(error.code().value())); }
        }
        catch (...) { if (report) Number(report, L"hresult", static_cast<DWORD>(code.value)); }
        if (report)
        {
            Text(report, L"result", L"error");
            auto stage = winrt::to_string(report.GetNamedString(L"stage", L"unknown"));
            auto native = static_cast<DWORD>(report.GetNamedNumber(L"native_error", 0));
            auto hresult = static_cast<DWORD>(report.GetNamedNumber(L"hresult", 0));
            std::fprintf(stderr, "PDF proof error: stage=%s native=0x%08lX hresult=0x%08lX\n",
                stage.c_str(), static_cast<unsigned long>(native), static_cast<unsigned long>(hresult));
            if (!output.empty() && !std::filesystem::exists(output))
            {
                try { WriteNew(output, report); }
                catch (...) { std::fputs("PDF proof: terminal report could not be written.\n", stderr); }
            }
        }
        std::fputs("PDF proof failed; no PDF pipeline success is established.\n", stderr);
        return 1;
    }
}
