#include "../WorkerProcess.h"
#include <shlobj.h>
#include <fstream>
#include <iostream>
#include <memory>

using namespace pdf;
namespace {
void Write(fs::path const& path, std::string const& bytes) {
    Handle f(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, 0, nullptr));
    Check(static_cast<bool>(f)); DWORD written{};
    Check(WriteFile(f.value, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr));
    Require(written == bytes.size(), "test short write"); Check(FlushFileBuffers(f.value));
}
struct Area {
    fs::path root;
    std::vector<fs::path> directories;
    std::vector<Handle> processes;
    Area() {
        PWSTR raw{}; check_hresult(SHGetKnownFolderPath(FOLDERID_Profile, KF_FLAG_NO_PACKAGE_REDIRECTION, nullptr, &raw));
        root = fs::path(raw) / (L"Desktop PDF Integration \u30c6\u30b9\u30c8-" + RandomId()); CoTaskMemFree(raw);
        Make(root);
    }
    void Make(fs::path const& path) { Check(CreateDirectoryW(path.c_str(), nullptr)); directories.push_back(path); }
    void StopOwned() {
        for (auto const& p : processes) {
            if (WaitForSingleObject(p.value, 0) != WAIT_OBJECT_0) Check(TerminateProcess(p.value, ERROR_CANCELLED));
            Require(WaitForSingleObject(p.value, 5000) == WAIT_OBJECT_0, "owned process cleanup failed");
        }
    }
    void Cleanup() {
        for (auto const& p : processes) Require(WaitForSingleObject(p.value, 0) == WAIT_OBJECT_0, "test survivor");
        for (auto& p : processes) p.Close();
        // Only inventory exact files inside the directories created by this test, after all exits.
        std::vector<fs::path> files;
        for (auto const& dir : directories) for (auto const& item : fs::directory_iterator(dir)) {
            auto attributes = GetFileAttributesW(item.path().c_str());
            Require(attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT), "unexpected test reparse");
            if (attributes & FILE_ATTRIBUTE_DIRECTORY)
                Require(std::find(directories.begin(), directories.end(), item.path()) != directories.end(), "unowned test directory");
            else files.push_back(item.path());
        }
        for (auto const& file : files) Check(DeleteFileW(file.c_str()));
        for (auto it = directories.rbegin(); it != directories.rend(); ++it) Check(RemoveDirectoryW(it->c_str()));
    }
};
JsonObject Identity(HANDLE process, fs::path const& image) {
    wchar_t path[32768]{}; DWORD size = 32768;
    Check(QueryFullProcessImageNameW(process, 0, path, &size));
    Require(_wcsicmp(path, image.c_str()) == 0, "owned process image mismatch");
    FILETIME created{}, exited{}, kernel{}, user{};
    Check(GetProcessTimes(process, &created, &exited, &kernel, &user));
    JsonObject result; Number(result, L"createdHigh", created.dwHighDateTime);
    Number(result, L"createdLow", created.dwLowDateTime); return result;
}
HANDLE Observe(Area& area, fs::path const& work, fs::path const& image) {
    auto marker = Read(StorageFolder::GetFolderFromPathAsync(work.native()).get(), L"started.json").get();
    auto pid = Integer(marker, L"pid"); Require(pid > 0 && pid <= MAXDWORD, "worker PID");
    Handle p(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid)));
    Check(static_cast<bool>(p));
    Require(WaitForSingleObject(p.value, 0) == WAIT_TIMEOUT, "worker not live at rendezvous");
    auto identity = Identity(p.value, image);
    Require(Integer(identity, L"createdHigh") == Integer(marker, L"createdHigh") &&
        Integer(identity, L"createdLow") == Integer(marker, L"createdLow"), "worker creation-time mismatch");
    auto result = p.value; area.processes.push_back(std::move(p)); return result;
}
Runtime Fixture(Area& area, fs::path const& fixture) {
    Runtime r; r.root = area.root / L"fixture"; area.Make(r.root);
    Check(CopyFileW(fixture.c_str(), (r.root / L"pdf-render-worker.exe").c_str(), TRUE));
    // This does NOT call InspectRuntime or fabricate PR7 inventory/receipt/provenance.
    return r;
}
void Worker(Area& area, fs::path const& fixture, std::wstring const& scenario) {
    auto runtime = Fixture(area, fixture);
    auto work = area.root / L"work"; area.Make(work);
    JsonObject config; Text(config, L"case", scenario);
    Handle sentinel;
    if (scenario == L"handles") {
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        sentinel = Handle(CreateFileW((work / L"sentinel").c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_NEW, 0, nullptr)); Check(static_cast<bool>(sentinel));
        BY_HANDLE_FILE_INFORMATION info{}; Check(GetFileInformationByHandle(sentinel.value, &info));
        Require(info.nFileIndexHigh || info.nFileIndexLow, "sentinel file identity unavailable");
        Number(config, L"sentinel", reinterpret_cast<UINT_PTR>(sentinel.value)); Number(config, L"volume", info.dwVolumeSerialNumber);
        Number(config, L"indexHigh", info.nFileIndexHigh); Number(config, L"indexLow", info.nFileIndexLow);
        Check(SetEnvironmentVariableW(L"DESKTOP_PDF_FIXTURE_SENTINEL", L"parent-only"));
    }
    Write(work / L"source.oxps", to_string(config.Stringify()));
    auto observation = Record(RandomId(), Mode::Silent, L"worker");
    HANDLE exact{}; auto end = GetTickCount64() + 20000; ULONGLONG deadline{};
    auto stop = [&] {
        if (scenario == L"precancel") return true;
        Require(GetTickCount64() < end, "test rendezvous/worker exceeded bounded controller deadline");
        if (!exact && fs::exists(work / L"started.json")) {
            exact = Observe(area, work, runtime.root / L"pdf-render-worker.exe");
            deadline = GetTickCount64() + 200;
            Write(work / L"release", "");
        }
        return exact && (scenario == L"cancel" || (scenario == L"deadline" && GetTickCount64() >= deadline));
    };
    bool rejected = false; std::uint64_t bytes{};
    try { bytes = Render(runtime, work / L"source.oxps", work / L"worker.pdf", work, stop, observation); }
    catch (...) { rejected = true; std::cout << "Render rejection: 0x" << std::hex << to_hresult().value << std::dec << '\n'; }
    if (fs::exists(work / L"fixture-error.txt")) {
        auto folder = StorageFolder::GetFolderFromPathAsync(work.native()).get();
        auto file = folder.GetFileAsync(L"fixture-error.txt").get();
        std::cerr << "Unexpected controlled fixture failure: " << to_string(FileIO::ReadTextAsync(file).get()) << '\n';
        throw std::runtime_error("fixture failed before requested production branch");
    }
    if (scenario == L"handles") Check(SetEnvironmentVariableW(L"DESKTOP_PDF_FIXTURE_SENTINEL", nullptr));
    bool good = scenario == L"success" || scenario == L"handles" || scenario == L"descendant" || scenario == L"breakaway";
    Require(rejected != good, "Render accepted failure or rejected controlled success");
    if (scenario == L"precancel") {
        Require(!exact && !observation.GetNamedBoolean(L"exitObserved") && !fs::exists(work / L"started.json"),
            "pre-cancel fabricated process/exit");
    } else {
        Require(exact && WaitForSingleObject(exact, 0) == WAIT_OBJECT_0, "exact worker exit not observed");
        Require(observation.GetNamedBoolean(L"exitObserved") && Integer(observation, L"activeProcesses") == 0,
            "production reap/zero-active observation absent");
        DWORD exit{}; Check(GetExitCodeProcess(exact, &exit));
        DWORD expected = scenario == L"crash" ? EXCEPTION_ACCESS_VIOLATION : scenario == L"exit24" ? 24 :
            scenario == L"exit71" ? 71 : scenario == L"cancel" || scenario == L"deadline" || scenario == L"channel" ? ERROR_CANCELLED : 0;
        Require(exit == expected && Integer(observation, L"exitCode") == exit, "wrong fixture fault/exit branch");
    }
    if (good) Require(bytes == fs::file_size(work / L"worker.pdf"), "strict v1 output length");
    if (scenario == L"handles") Require(Integer(observation, L"stderrBytes") > 0, "whitelisted stderr not drained");
    if (scenario == L"exit24" || scenario == L"exit71" || scenario == L"mismatch" || scenario == L"malformed" ||
        scenario == L"duplicate" || scenario == L"trailing" || scenario == L"missing")
        Require(fs::file_size(work / L"worker.pdf") > 0, "failure staging was lost");
    Require(!fs::exists(work / L"publication.json"), "Render must not perform final publication");
    std::cout << to_string(observation.Stringify()) << '\n';
}
void KillClose(Area& area, fs::path const& fixture, fs::path const& self) {
    auto runtime = Fixture(area, fixture); auto work = area.root / L"work"; area.Make(work);
    JsonObject config; Text(config, L"case", L"killclose"); Write(work / L"source.oxps", to_string(config.Stringify()));
    auto command = Quote(self.native()) + L" supervise " + Quote(area.root.native());
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION p{};
    Check(CreateProcessW(self.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &p));
    Handle thread(p.hThread); area.processes.emplace_back(p.hProcess);
    auto supervisor = p.hProcess; auto before = Identity(supervisor, self);
    auto end = GetTickCount64() + 20000;
    while (!fs::exists(work / L"started.json")) {
        Require(GetTickCount64() < end && WaitForSingleObject(supervisor, 0) == WAIT_TIMEOUT, "supervisor/fixture rendezvous failed"); Sleep(10);
    }
    auto worker = Observe(area, work, runtime.root / L"pdf-render-worker.exe");
    auto current = Identity(supervisor, self);
    Require(Integer(current, L"createdHigh") == Integer(before, L"createdHigh") &&
        Integer(current, L"createdLow") == Integer(before, L"createdLow"), "supervisor identity changed");
    Check(TerminateProcess(supervisor, ERROR_CANCELLED));
    Require(WaitForSingleObject(supervisor, 5000) == WAIT_OBJECT_0, "test supervisor exit unobserved");
    Require(WaitForSingleObject(worker, 5000) == WAIT_OBJECT_0, "kill-on-close did not stop exact worker");
    std::cout << "Actual Render supervisor death: exact worker exit observed; NOT PSA root reaping proof.\n";
}
void Publication(Area& area, std::wstring const& scenario) {
    auto jobPath = area.root / L"job", destPath = area.root / L"destination";
    area.Make(jobPath); area.Make(destPath);
    auto job = StorageFolder::GetFolderFromPathAsync(jobPath.native()).get();
    auto id = RandomId(); auto staged = jobPath / L"worker.pdf", destination = destPath / L"new.pdf";
    Write(staged, "Controlled publication bytes; NOT A REAL PDF.\n");
    auto request = Record(id, Mode::Silent, L"request"); Text(request, L"destination", destination.native());
    Text(request, L"session", L"controlled-test-NOT-PSA"); Text(request, L"registration", RandomId());
    Number(request, L"deadline", Deadline(Now(), DefaultCompletionMs)); Number(request, L"timeout", DefaultCompletionMs);
    Number(request, L"sourceBytes", fs::file_size(staged));
    Publish(job, L"request.json", request).get();
    auto completion = Record(id, Mode::Silent, L"completion"); Text(completion, L"outcome", L"failed");
    Text(completion, L"stage", L"controlled-fault"); Number(completion, L"hresult", static_cast<std::uint32_t>(E_FAIL));
    Publish(job, L"completion.json", completion).get();
    std::wstring existingHash;
    DWORD existingAttributes{};
    if (scenario == L"existing") {
        Write(destination, "existing"); existingHash = Hash(destination);
        existingAttributes = GetFileAttributesW(destination.c_str());
        Require(existingAttributes != INVALID_FILE_ATTRIBUTES && !(existingAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE |
                FILE_ATTRIBUTE_RECALL_ON_OPEN | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS)),
            "existing fixture must be an ordinary local file");
    }
    if (scenario == L"journal") Publish(job, L"publication.json", Record(id, Mode::Silent, L"fault-injection")).get();
    bool boundary = false, rejected = false, existingReason = false;
    auto stop = [&] {
        boundary = true;
        Require(Exists(job, L"publish-intent.json").get(), "commit boundary before flushed intent");
        if (scenario == L"race") Check(CopyFileW(staged.c_str(), destination.c_str(), TRUE));
        return scenario == L"pubcancel";
    };
    try { PublishNew(staged, destination, job, id, Mode::Silent, stop); }
    catch (...) {
        rejected = true;
        auto primary = std::current_exception(); auto code = to_hresult();
        try {
            std::cerr << "Original publication rejection HRESULT=0x" << std::hex << code.value << std::dec << '\n';
            try { std::rethrow_exception(primary); }
            catch (hresult_error const& error) { std::cerr << to_string(error.message()) << '\n'; }
            catch (std::runtime_error const& error) {
                existingReason = std::string_view(error.what()) == "destination exists/unsupported";
                std::cerr << error.what() << '\n';
            }
            catch (std::exception const& error) { std::cerr << error.what() << '\n'; }
            catch (...) { std::cerr << "Nonstandard original publication exception\n"; }
            if (scenario == L"existing") std::cerr << "Existing fixture attributes=0x" << std::hex << existingAttributes << std::dec << '\n';
            std::cerr.flush();
            Require(std::cerr.good(), "publication diagnostic stream failed");
        } catch (...) {
            Failure(L"original publication diagnostic unavailable", to_hresult());
            std::rethrow_exception(primary);
        }
    }
    if (scenario == L"existing") Require(existingReason, "expected exact existing-file semantic rejection, not an unrelated pre-boundary failure");
    Require(rejected == (scenario != L"publish"), "publication branch outcome");
    Require(boundary == (scenario != L"existing"), "wrong publication failure phase");
    auto transaction = destPath / (L".4dpdf-" + id);
    if (fs::exists(transaction)) area.directories.push_back(transaction);
    auto report = Reconcile(job, id, Mode::Silent);
    Require(report.GetNamedString(L"printOutcome") == L"failed", "reconciliation upgraded print failure");
    if (scenario == L"publish" || scenario == L"journal") {
        Require(Hash(staged) == Hash(destination) && report.GetNamedString(L"fileState") == L"confirmed-output",
            "actual moved-file identity/bytes not confirmed");
    } else if (scenario == L"race") {
        Require(Hash(staged) == Hash(destination) && report.GetNamedString(L"fileState") == L"matching-bytes-not-attributed",
            "equal competitor bytes falsely attributed");
        Require(fs::file_size(transaction / L"output.pdf") > 0, "collision lost owned pending copy");
    } else if (scenario == L"pubcancel") {
        Require(!fs::exists(destination) && report.GetNamedString(L"fileState") == L"absent-now" &&
            fs::file_size(transaction / L"output.pdf") > 0, "cancellation lost staging or published");
    } else Require(Hash(destination) == existingHash && report.GetNamedString(L"fileState") == L"no-publish-intent",
        "existing destination changed");
    auto unknown = completion; Text(unknown, L"outcome", L"saved-or-unknown-acknowledgement");
    Check(DeleteFileW((jobPath / L"completion.json").c_str())); Publish(job, L"completion.json", unknown).get();
    Require(Reconcile(job, id, Mode::Silent).GetNamedString(L"printOutcome") == L"saved-or-unknown-acknowledgement",
        "reconciliation upgraded unknown acknowledgement");
    Require(Hash(staged).size() == 64, "source staging lost");
    std::cout << to_string(report.Stringify()) << '\n';
}
void Validation(Area& area, fs::path const& fixture) {
    auto runtime = Fixture(area, fixture);
    auto file = OpenRead(runtime.root / L"pdf-render-worker.exe"); Machine(file.value);
    auto parents = SafeParents(runtime.root / L"runtime-imports.json", true);
    bool rejected = false;
    try { InspectRuntime(runtime.root); } catch (...) { rejected = true; }
    Require(rejected, "runtime without real provenance accepted");
    for (auto name : {L"runtime-imports.json", L"native-test-receipt.json", L"build-provenance.json"})
        Write(runtime.root / name, "{}");
    rejected = false;
    try { InspectRuntime(runtime.root); } catch (...) { rejected = true; }
    Require(rejected, "invalid, explicitly non-provenance metadata accepted");
    Write(area.root / L"bad.exe", "invalid PE");
    rejected = false;
    try { auto bad = OpenRead(area.root / L"bad.exe"); Machine(bad.value); } catch (...) { rejected = true; }
    Require(rejected, "invalid PE accepted");
}
}
int wmain(int argc, wchar_t** argv) {
    std::unique_ptr<Area> area;
    try {
        init_apartment(apartment_type::multi_threaded);
        Require(argc == 3, "test requires case and fixture/root");
        std::wstring scenario(argv[1]);
        if (scenario == L"supervise") {
            fs::path root(argv[2]); Runtime r; r.root = root / L"fixture";
            auto work = root / L"work"; auto observation = Record(RandomId(), Mode::Silent, L"worker");
            Render(r, work / L"source.oxps", work / L"worker.pdf", work, [] { return false; }, observation);
            throw std::runtime_error("test supervisor unexpectedly returned");
        }
        area = std::make_unique<Area>();
        if (scenario == L"killclose") {
            wchar_t self[32768]{}; auto count = GetModuleFileNameW(nullptr, self, 32768);
            Require(count > 0 && count < 32768, "test image path"); KillClose(*area, argv[2], self);
        } else if (scenario == L"validation") Validation(*area, argv[2]);
        else if (scenario == L"publish" || scenario == L"existing" || scenario == L"race" ||
            scenario == L"pubcancel" || scenario == L"journal") Publication(*area, scenario);
        else Worker(*area, argv[2], scenario);
        area->Cleanup();
        std::cout << "Controlled production process/publication case passed; NOT renderer/PDF/PSA proof.\n"; return 0;
    } catch (...) {
        auto primary = to_hresult(); Failure(L"controlled production test", primary);
        try { throw; }
        catch (hresult_error const& error) { std::cerr << to_string(error.message()) << '\n'; }
        catch (std::exception const& error) { std::cerr << error.what() << '\n'; }
        catch (...) { std::cerr << "Nonstandard primary test failure\n"; }
        if (area) {
            try { area->StopOwned(); } catch (...) { Failure(L"FAILED owned test process cleanup", to_hresult()); }
            std::cerr << "FAILED test evidence preserved at exact owned path: " << to_string(hstring(area->root.native())) << '\n';
        }
        return 1;
    }
}
