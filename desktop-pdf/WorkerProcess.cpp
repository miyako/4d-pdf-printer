#include "WorkerProcess.h"
#include <aclapi.h>
#include <sddl.h>
#include <shlobj.h>
#include <array>
#include <cwctype>
#include <memory>
#include <utility>
#include <winrt/Windows.Security.Cryptography.Core.h>

namespace pdf {
#if defined(_M_ARM64)
constexpr WORD TargetMachine = IMAGE_FILE_MACHINE_ARM64;
constexpr wchar_t Architecture[] = L"arm64";
#elif defined(_M_X64)
constexpr WORD TargetMachine = IMAGE_FILE_MACHINE_AMD64;
constexpr wchar_t Architecture[] = L"x64";
#else
#error Native x64 or ARM64 required
#endif
namespace {
fs::path Profile() {
    PWSTR raw{};
    check_hresult(SHGetKnownFolderPath(FOLDERID_Profile, KF_FLAG_NO_PACKAGE_REDIRECTION, nullptr, &raw));
    fs::path path(raw); CoTaskMemFree(raw);
    return path;
}
void PrivateDirectory(fs::path const& path) {
    Handle token;
    HANDLE raw{};
    Check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)); token = Handle(raw);
    DWORD size{};
    GetTokenInformation(token.value, TokenUser, nullptr, 0, &size);
    Require(size > 0, "token user size");
    std::vector<unsigned char> data(size);
    Check(GetTokenInformation(token.value, TokenUser, data.data(), size, &size));
    auto user = reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid;
    PACL acl{}; PSECURITY_DESCRIPTOR descriptor{};
    auto error = GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
        nullptr, nullptr, &acl, nullptr, &descriptor);
    if (error != ERROR_SUCCESS) throw_hresult(HRESULT_FROM_WIN32(error));
    struct Free { PSECURITY_DESCRIPTOR p; ~Free() { LocalFree(p); } } free{descriptor};
    Require(acl != nullptr, "unrestricted directory ACL");
    for (DWORD i = 0; i != acl->AceCount; ++i) {
        void* rawAce{}; Check(GetAce(acl, i, &rawAce));
        auto header = static_cast<ACE_HEADER*>(rawAce);
        if (header->AceFlags & INHERIT_ONLY_ACE) continue;
        if (header->AceType == ACCESS_DENIED_ACE_TYPE) continue;
        Require(header->AceType == ACCESS_ALLOWED_ACE_TYPE, "unsupported directory access ACE");
        auto ace = static_cast<ACCESS_ALLOWED_ACE*>(rawAce);
        auto sid = reinterpret_cast<PSID>(&ace->SidStart);
        try {
            Require(EqualSid(sid, user) || IsWellKnownSid(sid, WinLocalSystemSid) ||
                IsWellKnownSid(sid, WinBuiltinAdministratorsSid), "directory grants access to other principals");
        } catch (std::runtime_error const&) {
            auto primary = std::current_exception();
            std::unique_ptr<std::runtime_error> detail;
            try {
                PWSTR numericSid{};
                auto converted = ConvertSidToStringSidW(sid, &numericSid);
                Free sidOwner{numericSid};
                Check(converted);
                Require(numericSid != nullptr, "missing diagnostic numeric SID");
                auto message = "directory grants access to other principals; directory=" +
                    to_string(hstring(path.native())) + "; sid=" + to_string(hstring(numericSid)) +
                    "; ace_type=" + std::to_string(header->AceType) +
                    "; ace_flags=" + std::to_string(header->AceFlags) +
                    "; access_mask=" + std::to_string(ace->Mask);
                detail = std::make_unique<std::runtime_error>(message);
            } catch (...) { Failure(L"directory rejection diagnostic unavailable", to_hresult()); }
            if (detail) throw *detail;
            std::rethrow_exception(primary);
        }
    }
}
std::wstring Lower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return text;
}
JsonObject FileJson(fs::path const& path) {
    return Read(StorageFolder::GetFolderFromPathAsync(path.parent_path().native()).get(),
        hstring(path.filename().native()), RuntimeRecordBytes).get();
}
std::uint64_t Size(HANDLE file) {
    LARGE_INTEGER size{}; Check(GetFileSizeEx(file, &size));
    Require(size.QuadPart > 0, "empty file");
    return static_cast<std::uint64_t>(size.QuadPart);
}
struct Pipe {
    Handle read, write;
    std::string bytes;
    Pipe() {
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE a{}, b{}; Check(CreatePipe(&a, &b, &sa, 0));
        read = Handle(a); write = Handle(b);
        Check(SetHandleInformation(read.value, HANDLE_FLAG_INHERIT, 0));
    }
    void Drain() {
        for (;;) {
            DWORD available{};
            if (!PeekNamedPipe(read.value, nullptr, 0, nullptr, &available, nullptr)) {
                if (GetLastError() == ERROR_BROKEN_PIPE) return;
                throw_last_error();
            }
            if (!available) return;
            std::array<char, 4096> buffer{}; DWORD count{};
            Check(ReadFile(read.value, buffer.data(), std::min<DWORD>(available, 4096), &count, nullptr));
            Require(count > 0 && bytes.size() + count <= RecordBytes, "worker channel limit");
            bytes.append(buffer.data(), count);
        }
    }
};
DWORD Active(HANDLE job) {
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
    Check(QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &info, sizeof(info), nullptr));
    return info.ActiveProcesses;
}
void CheckDisk(fs::path const& root) {
    std::uint64_t total = 0;
    std::error_code error;
    fs::recursive_directory_iterator entry(root, error), end;
    if (error) throw_hresult(HRESULT_FROM_WIN32(error.value()));
    while (entry != end) {
        auto status = entry->symlink_status(error);
        if (error && error != std::errc::no_such_file_or_directory)
            throw_hresult(HRESULT_FROM_WIN32(error.value()));
        if (!error) {
            Require(!fs::is_symlink(status), "redirected worker staging");
            if (fs::is_regular_file(status)) {
                auto bytes = entry->file_size(error);
                if (error && error != std::errc::no_such_file_or_directory)
                    throw_hresult(HRESULT_FROM_WIN32(error.value()));
                if (!error) {
                    Require(bytes <= CaptureBytes && total <= 2 * CaptureBytes - bytes, "job disk limit");
                    total += bytes;
                }
            }
        }
        error.clear();
        entry.increment(error);
        if (error == std::errc::no_such_file_or_directory) {
            // Normal worker publication removes its temporary directory during sampling.
            return;
        }
        if (error) throw_hresult(HRESULT_FROM_WIN32(error.value()));
    }
}
struct Child {
    Handle process, thread, job;
    bool reaped = false, assigned = false;
    void Reap(bool terminate) {
        if (!process) return;
        if (terminate) {
            auto stopped = assigned ? TerminateJobObject(job.value, ERROR_CANCELLED) :
                TerminateProcess(process.value, ERROR_CANCELLED);
            if (!stopped) {
                auto error = GetLastError();
                if (WaitForSingleObject(process.value, 5000) != WAIT_OBJECT_0)
                    throw_hresult(HRESULT_FROM_WIN32(error));
            }
        }
        auto deadline = GetTickCount64() + 5000;
        Require(WaitForSingleObject(process.value, 5000) == WAIT_OBJECT_0, "worker exit not observed");
        while (job && Active(job.value) != 0 && GetTickCount64() < deadline) Sleep(10);
        Require(!job || Active(job.value) == 0, "surviving worker children");
        reaped = true;
    }
    ~Child() {
        if (process && !reaped) {
            try { Reap(true); }
            catch (...) { Failure(L"worker cleanup/exit observation", to_hresult()); }
        }
    }
};
}
Handle OpenRead(fs::path const& path) {
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    Check(static_cast<bool>(file));
    BY_HANDLE_FILE_INFORMATION info{}; Check(GetFileInformationByHandle(file.value, &info));
    Require(!(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)), "nonregular file");
    return file;
}
std::wstring Hash(HANDLE file) {
    LARGE_INTEGER zero{}; Check(SetFilePointerEx(file, zero, nullptr, FILE_BEGIN));
    auto hash = Windows::Security::Cryptography::Core::HashAlgorithmProvider::OpenAlgorithm(L"SHA256").CreateHash();
    std::array<std::uint8_t, 65536> data{}; DWORD count{};
    do {
        Check(ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &count, nullptr));
        if (count) hash.Append(Windows::Security::Cryptography::CryptographicBuffer::CreateFromByteArray(
            array_view<std::uint8_t const>(data.data(), data.data() + count)));
    } while (count);
    return std::wstring(Windows::Security::Cryptography::CryptographicBuffer::EncodeToHexString(hash.GetValueAndReset()));
}
std::wstring Hash(fs::path const& path) { auto f = OpenRead(path); return Hash(f.value); }
std::vector<Handle> SafeParents(fs::path const& target, bool existingAllowed, bool packageStorage) {
    auto text = target.native();
    Require(LocalPath(text) && target == target.lexically_normal(),
        "absolute normal local path required");
    auto profile = Profile().native();
    Require(text.size() > profile.size() + 1 &&
        _wcsnicmp(text.c_str(), profile.c_str(), profile.size()) == 0 && text[profile.size()] == L'\\',
        "prototype destinations/runtime must be under current profile");
    Require(GetDriveTypeW(target.root_path().c_str()) == DRIVE_FIXED, "fixed local volume required");
    std::vector<Handle> result;
    auto parent = target.parent_path();
    std::vector<fs::path> paths;
    for (auto path = parent; !path.empty() && path != path.root_path(); path = path.parent_path()) paths.push_back(path);
    for (auto it = paths.rbegin(); it != paths.rend(); ++it) {
        Handle directory(CreateFileW(it->c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        Check(static_cast<bool>(directory));
        BY_HANDLE_FILE_INFORMATION info{}; Check(GetFileInformationByHandle(directory.value, &info));
        Require((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE |
                FILE_ATTRIBUTE_RECALL_ON_OPEN | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS)), "redirected/provider parent");
        if (it->native().size() == profile.size()) PrivateDirectory(*it);
        else if (!packageStorage && it->native().size() > profile.size()) PrivateDirectory(*it);
        result.push_back(std::move(directory));
    }
    Require(LocalProvider(StorageFolder::GetFolderFromPathAsync(parent.native()).get().Provider()),
        "nonlocal parent provider unsupported");
    auto attributes = GetFileAttributesW(target.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES)
        Require(existingAllowed && !(attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT |
            FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_OPEN |
            FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS)), "destination exists/unsupported");
    else Require(GetLastError() == ERROR_FILE_NOT_FOUND, "destination inaccessible");
    return result;
}
void Machine(HANDLE file) {
    IMAGE_DOS_HEADER dos{}; DWORD count{}; LARGE_INTEGER offset{};
    Check(SetFilePointerEx(file, offset, nullptr, FILE_BEGIN));
    Check(ReadFile(file, &dos, sizeof(dos), &count, nullptr));
    Require(count == sizeof(dos) && dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew >= 64, "invalid PE");
    offset.QuadPart = dos.e_lfanew; Check(SetFilePointerEx(file, offset, nullptr, FILE_BEGIN));
    DWORD signature{}; IMAGE_FILE_HEADER header{};
    Check(ReadFile(file, &signature, sizeof(signature), &count, nullptr));
    Require(count == sizeof(signature) && signature == IMAGE_NT_SIGNATURE, "PE signature");
    Check(ReadFile(file, &header, sizeof(header), &count, nullptr));
    Require(count == sizeof(header) && header.Machine == TargetMachine, "runtime architecture mismatch");
}
Runtime InspectRuntime(fs::path const& root) {
    USHORT process{}, native{};
    Check(IsWow64Process2(GetCurrentProcess(), &process, &native));
    Require(process == IMAGE_FILE_MACHINE_UNKNOWN && native == TargetMachine, "native runtime host required");
    Runtime runtime; runtime.root = root;
    runtime.locks = SafeParents(root / L"runtime-imports.json", true);
    for (auto name : {L"runtime-imports.json", L"native-test-receipt.json", L"build-provenance.json"})
        runtime.locks.push_back(OpenRead(root / name));
    runtime.inventory = Hash(root / L"runtime-imports.json");
    runtime.receipt = Hash(root / L"native-test-receipt.json");
    runtime.provenance = Hash(root / L"build-provenance.json");
    auto inventory = FileJson(root / L"runtime-imports.json");
    auto receipt = FileJson(root / L"native-test-receipt.json");
    Require(inventory.GetNamedString(L"schema") == L"4d-pdf-printer.pe-inventory" &&
        Integer(inventory, L"version") == 2 && inventory.GetNamedString(L"architecture") == Architecture &&
        receipt.GetNamedString(L"architecture") == Architecture &&
        Lower(std::wstring(receipt.GetNamedString(L"os_architecture"))) == Architecture &&
        receipt.GetNamedString(L"result") == L"passed" &&
        receipt.GetNamedString(L"worker") == L"pdf-render-worker.exe", "native runtime receipt/inventory");
    std::set<std::wstring> files;
    for (auto const& item : inventory.GetNamedArray(L"files")) {
        auto entry = item.GetObject();
        std::wstring name(entry.GetNamedString(L"file"));
        Require(!name.empty() && name.find_first_of(L"/\\:") == name.npos &&
            files.insert(Lower(name)).second &&
            (name == L"pdf-render-worker.exe" || Lower(fs::path(name).extension().native()) == L".dll"),
            "runtime member");
        auto file = OpenRead(root / name);
        Machine(file.value);
        Require(Hash(file.value) == Lower(std::wstring(entry.GetNamedString(L"sha256"))) &&
            Size(file.value) == Integer(entry, L"bytes"), "runtime drift");
        runtime.locks.push_back(std::move(file));
    }
    Require(files.contains(L"pdf-render-worker.exe") &&
        Hash(root / L"pdf-render-worker.exe") == Lower(std::wstring(receipt.GetNamedString(L"worker_sha256"))),
        "worker/receipt mismatch");
    for (auto const& item : fs::directory_iterator(root)) {
        auto extension = Lower(item.path().extension().native());
        if (extension == L".dll" || extension == L".exe")
            Require(files.contains(Lower(item.path().filename().native())), "unlisted runtime binary");
    }
    return runtime;
}
Runtime ConfiguredRuntime(StorageFolder const& root) {
    auto config = Read(root, L"runtime.json").get();
    Fields(config, {L"root", L"inventory", L"receipt", L"provenance"});
    auto runtime = InspectRuntime(fs::path(std::wstring(config.GetNamedString(L"root"))));
    Require(runtime.inventory == config.GetNamedString(L"inventory") && runtime.receipt == config.GetNamedString(L"receipt") &&
        runtime.provenance == config.GetNamedString(L"provenance"), "configured runtime changed");
    return runtime;
}
std::uint64_t Render(Runtime const& runtime, fs::path const& source, fs::path const& output,
    fs::path const& work, std::function<bool()> stop, JsonObject const& observation) {
    Text(observation, L"state", L"preparing");
    observation.Insert(L"exitObserved", JsonValue::CreateBooleanValue(false));
    Child child; Pipe out, err;
    child.job = Handle(CreateJobObjectW(nullptr, nullptr)); Check(static_cast<bool>(child.job));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
        JOB_OBJECT_LIMIT_JOB_MEMORY | JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_PROCESS_TIME;
    limits.BasicLimitInformation.ActiveProcessLimit = 1;
    limits.BasicLimitInformation.PerProcessUserTimeLimit.QuadPart = 300ll * 10000000;
    limits.JobMemoryLimit = 1024ull * 1024 * 1024;
    Check(SetInformationJobObject(child.job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)));
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr));
    Check(static_cast<bool>(input));
    SIZE_T size{};
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<unsigned char> buffer(size);
    auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
    Check(InitializeProcThreadAttributeList(list, 1, 0, &size));
    struct Delete { LPPROC_THREAD_ATTRIBUTE_LIST p; ~Delete() { DeleteProcThreadAttributeList(p); } } cleanup{list};
    HANDLE handles[]{input.value, out.write.value, err.write.value};
    Check(UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), nullptr, nullptr));
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input.value; startup.StartupInfo.hStdOutput = out.write.value;
    startup.StartupInfo.hStdError = err.write.value; startup.lpAttributeList = list;
    auto executable = runtime.root / L"pdf-render-worker.exe";
    auto command = Quote(executable.native()) + L" --protocol-version 1 --input " + Quote(source.native()) +
        L" --output " + Quote(output.native());
    wchar_t system[32768]{}; auto length = GetEnvironmentVariableW(L"SystemRoot", system, 32768);
    Require(length > 0 && length < 32768, "SystemRoot");
    std::wstring environment = L"PATH=" + std::wstring(system) + L"\\System32";
    environment += L'\0'; environment += L"SystemRoot=" + std::wstring(system);
    environment += L'\0'; environment += L"TEMP=" + work.native();
    environment += L'\0'; environment += L"TMP=" + work.native();
    environment += L'\0'; environment += L'\0';
    Require(!stop(), "canceled/expired before worker creation");
    PROCESS_INFORMATION process{};
    Check(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
        environment.data(), work.c_str(), &startup.StartupInfo, &process));
    child.process = Handle(process.hProcess); child.thread = Handle(process.hThread);
    Text(observation, L"state", L"suspended");
    try {
        Check(AssignProcessToJobObject(child.job.value, child.process.value));
        child.assigned = true;
        Require(ResumeThread(child.thread.value) != MAXDWORD, "worker resume");
        Text(observation, L"state", L"running");
        out.write.Close(); err.write.Close(); input.Close();
        for (;;) {
            out.Drain(); err.Drain();
            auto wait = WaitForSingleObject(child.process.value, 25);
            if (wait == WAIT_OBJECT_0) break;
            Require(wait == WAIT_TIMEOUT, "worker wait failed");
            CheckDisk(work);
            if (stop()) throw hresult_canceled();
        }
        child.Reap(false);
        observation.Insert(L"exitObserved", JsonValue::CreateBooleanValue(true));
        Number(observation, L"activeProcesses", 0);
        out.Drain(); err.Drain();
    } catch (...) {
        auto primary = std::current_exception();
        child.Reap(true);
        observation.Insert(L"exitObserved", JsonValue::CreateBooleanValue(true));
        Number(observation, L"activeProcesses", 0);
        DWORD exit{}; Check(GetExitCodeProcess(child.process.value, &exit));
        Number(observation, L"exitCode", exit);
        Text(observation, L"state", L"stopped-or-channel-failed");
        std::rethrow_exception(primary);
    }
    DWORD exit{}; Check(GetExitCodeProcess(child.process.value, &exit));
    Number(observation, L"exitCode", exit);
    Number(observation, L"stdoutBytes", out.bytes.size());
    Number(observation, L"stderrBytes", err.bytes.size());
    Text(observation, L"state", L"validating-status");
    auto bytes = WorkerSuccess(out.bytes, exit);
    auto pdf = OpenRead(output);
    Require(Size(pdf.value) == bytes, "PDF/status size mismatch");
    Text(observation, L"state", L"validated");
    return bytes;
}
void PublishNew(fs::path const& staged, fs::path const& destination, StorageFolder const& job,
    std::wstring_view id, Mode mode, std::function<bool()> stop) {
    auto parents = SafeParents(destination);
    auto transaction = destination.parent_path() / (L".4dpdf-" + std::wstring(id));
    Check(CreateDirectoryW(transaction.c_str(), nullptr));
    auto pending = transaction / L"output.pdf";
    // The directory is deliberately retained on uncertainty; never sweep by prefix.
    Check(CopyFileW(staged.c_str(), pending.c_str(), TRUE));
    Handle file(CreateFileW(pending.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    Check(static_cast<bool>(file)); Check(FlushFileBuffers(file.value));
    auto intent = Record(id, mode, L"publish-intent");
    Text(intent, L"sha256", Hash(file.value)); Number(intent, L"bytes", Size(file.value));
    BY_HANDLE_FILE_INFORMATION info{}; Check(GetFileInformationByHandle(file.value, &info));
    Number(intent, L"volume", info.dwVolumeSerialNumber);
    Number(intent, L"fileIndexHigh", info.nFileIndexHigh); Number(intent, L"fileIndexLow", info.nFileIndexLow);
    Publish(job, L"publish-intent.json", intent).get(); file.Close();
    if (stop()) throw hresult_canceled();
    Check(MoveFileExW(pending.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH));
    auto published = Record(id, mode, L"published");
    Publish(job, L"publication.json", published).get();
    Check(RemoveDirectoryW(transaction.c_str()));
}
JsonObject Reconcile(StorageFolder const& job, std::wstring_view id, Mode mode) {
    auto report = Record(id, mode, L"reconciliation");
    Text(report, L"printOutcome", L"unknown");
    if (Exists(job, L"completion.json").get()) {
        auto completion = Read(job, L"completion.json").get();
        Validate(completion, id, mode, L"completion");
        Text(report, L"printOutcome", completion.GetNamedString(L"outcome"));
    }
    Text(report, L"fileState", L"no-publish-intent");
    if (!Exists(job, L"publish-intent.json").get()) return report;
    auto intent = Read(job, L"publish-intent.json").get();
    Validate(intent, id, mode, L"publish-intent");
    if (mode == Mode::Silent)
        Fields(intent, {L"schema", L"version", L"id", L"mode", L"kind", L"bytes", L"sha256",
            L"volume", L"fileIndexHigh", L"fileIndexLow"});
    else Fields(intent, {L"schema", L"version", L"id", L"mode", L"kind", L"bytes", L"sha256"});
    auto request = Read(job, L"request.json").get(); Validate(request, id, mode, L"request");
    fs::path destination(std::wstring(request.GetNamedString(L"destination")));
    auto parents = SafeParents(destination, true);
    auto attributes = GetFileAttributesW(destination.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        Require(GetLastError() == ERROR_FILE_NOT_FOUND, "reconciliation target inaccessible");
        Text(report, L"fileState", L"absent-now"); return report;
    }
    auto file = OpenRead(destination);
    Text(report, L"fileState", L"unattributed");
    LARGE_INTEGER size{}; Check(GetFileSizeEx(file.value, &size));
    if (size.QuadPart < 0 || static_cast<std::uint64_t>(size.QuadPart) != Integer(intent, L"bytes") ||
        Hash(file.value) != intent.GetNamedString(L"sha256"))
        return report;
    Text(report, L"fileState", L"matching-bytes-not-attributed");
    if (mode == Mode::Silent) {
        BY_HANDLE_FILE_INFORMATION info{}; Check(GetFileInformationByHandle(file.value, &info));
        if ((info.nFileIndexHigh || info.nFileIndexLow) &&
            info.dwVolumeSerialNumber == Integer(intent, L"volume") &&
            info.nFileIndexHigh == Integer(intent, L"fileIndexHigh") &&
            info.nFileIndexLow == Integer(intent, L"fileIndexLow"))
            Text(report, L"fileState", L"confirmed-output");
    }
    // Read-only observation never changes the original print acknowledgement.
    return report;
}
}
