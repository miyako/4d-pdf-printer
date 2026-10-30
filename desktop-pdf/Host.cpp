#include "WorkerProcess.h"

using namespace pdf;
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR raw, int) {
    OutputDebugStringW(L"Desktop PDF Integration: fixed host entry, before COM\n");
    StorageFolder job{nullptr};
    std::wstring id, stage = L"activation";
    Mode mode = Mode::SaveAs;
    bool accepted = false;
    JsonObject worker{nullptr};
    try {
        auto activation = ParseActivation(raw); id = activation.id; mode = activation.mode;
        init_apartment(apartment_type::multi_threaded);
        auto root = Root().get();
        job = Folder(L"Jobs", id).get();
        auto path = fs::path(std::wstring(job.Path()));
        auto jobParents = SafeParents(path / L"request.json", true, true);
        // A late/duplicate launch never reopens an accepted job.
        job.CreateFolderAsync(L"accepted", CreationCollisionOption::FailIfExists).get();
        accepted = true;
        auto request = Read(job, L"request.json").get();
        Validate(request, id, mode, L"request");
        Fields(request, {L"schema", L"version", L"id", L"mode", L"kind", L"session",
            L"registration", L"deadline", L"timeout", L"destination", L"sourceBytes"});
        Require(!request.GetNamedString(L"session").empty(), "missing workflow session");
        auto deadline = Integer(request, L"deadline"), timeout = Integer(request, L"timeout");
        auto now = Now();
        Require(timeout > 0 && timeout <= 86400000 && deadline > now &&
            deadline - now <= timeout, "job deadline");
        auto tickDeadline = GetTickCount64() + Remaining(now, deadline, timeout);
        std::wstring registration(request.GetNamedString(L"registration"));
        fs::path registrationPath;
        if (mode == Mode::Silent) {
            Require(Id(registration), "silent registration id");
            auto folder = Folder(L"Registrations", registration).get();
            registrationPath = fs::path(std::wstring(folder.Path()));
            auto registered = Read(folder, L"registered.json").get();
            auto submitted = Read(folder, L"submitted.json").get();
            auto claim = folder.GetFolderAsync(L"claim").get();
            auto binding = Read(claim, L"binding.json").get();
            Require(BoundJob(binding, registration) == id &&
                registered.GetNamedString(L"destination") == request.GetNamedString(L"destination") &&
                Integer(registered, L"timeout") == timeout &&
                SubmissionDeadline(submitted, registered, registration) == deadline,
                "silent binding mismatch");
        } else Require(registration.empty(), "interactive registration forbidden");
        auto exists = [](fs::path const& p) {
            if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
            auto error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND) return false;
            throw_hresult(HRESULT_FROM_WIN32(error));
        };
        auto stop = [&] {
            return Expired(Now(), deadline) || GetTickCount64() >= tickDeadline ||
                exists(path / L"cancel.json") ||
                (!registrationPath.empty() && exists(registrationPath / L"cancel.json"));
        };
        stage = L"serialization";
        Handle slot;
        while (!stop()) {
            slot = Handle(CreateFileW((fs::path(std::wstring(root.Path())) / L"slot.lock").c_str(),
                GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
            if (slot) break;
            auto error = GetLastError();
            if (error != ERROR_SHARING_VIOLATION) throw_hresult(HRESULT_FROM_WIN32(error));
            Sleep(25);
        }
        if (stop()) throw hresult_canceled();
        fs::path destination(std::wstring(request.GetNamedString(L"destination")));
        Require(_wcsicmp(destination.extension().c_str(), L".pdf") == 0, "PDF destination required");
        auto parents = SafeParents(destination, mode == Mode::SaveAs);
        stage = L"runtime";
        auto runtime = ConfiguredRuntime(root);
        auto source = OpenRead(path / L"source.oxps");
        LARGE_INTEGER size{}; Check(GetFileSizeEx(source.value, &size));
        Require(size.QuadPart > 0 && static_cast<std::uint64_t>(size.QuadPart) ==
            Integer(request, L"sourceBytes") && static_cast<std::uint64_t>(size.QuadPart) <= CaptureBytes,
            "source length");
        stage = L"worker";
        worker = Record(id, mode, L"worker");
        auto bytes = Render(runtime, path / L"source.oxps", path / L"worker.pdf", path, stop, worker);
        Publish(job, L"worker.json", worker).get();
        auto digest = Hash(path / L"worker.pdf");
        if (mode == Mode::SaveAs) {
            auto ready = Record(id, mode, L"ready");
            Number(ready, L"bytes", bytes); Text(ready, L"sha256", digest);
            Publish(job, L"ready.json", ready).get();
            stage = L"interactive-publication";
            while (!exists(path / L"publication.json")) {
                if (exists(path / L"publication-unknown.json"))
                    throw std::runtime_error("interactive commit outcome unknown");
                if (stop()) throw hresult_canceled();
                Sleep(25);
            }
            auto publication = Read(job, L"publication.json").get();
            Validate(publication, id, mode, L"published");
            Require(Integer(publication, L"bytes") == bytes &&
                publication.GetNamedString(L"sha256") == digest, "publication binding");
        } else {
            stage = L"silent-publication";
            PublishNew(path / L"worker.pdf", destination, job, id, mode, stop);
        }
        stage = L"cleanup";
        source.Close();
        Check(DeleteFileW((path / L"source.oxps").c_str()));
        Check(DeleteFileW((path / L"worker.pdf").c_str()));
        auto result = Record(id, mode, L"result");
        Text(result, L"outcome", L"published"); Text(result, L"stage", L"complete");
        Number(result, L"bytes", bytes); Text(result, L"sha256", digest);
        Publish(job, L"result.json", result).get();
        return 0;
    } catch (...) {
        auto hr = to_hresult(); Failure(stage.c_str(), hr);
        if (job && accepted) {
            try {
                if (worker && !Exists(job, L"worker.json").get()) Publish(job, L"worker.json", worker).get();
                auto result = Record(id, mode, L"result");
                bool uncertain = Exists(job, L"publish-intent.json").get() ||
                    Exists(job, L"publication.json").get() ||
                    Exists(job, L"publication-unknown.json").get() ||
                    (mode == Mode::SaveAs && Exists(job, L"ready.json").get());
                Text(result, L"outcome", uncertain ? L"indeterminate" :
                    hr == hresult_canceled().code() ? L"canceled-or-expired" : L"failed");
                Text(result, L"stage", stage); Number(result, L"hresult", static_cast<std::uint32_t>(hr.value));
                Publish(job, L"result.json", result).get();
            } catch (...) { Failure(L"terminal record failure", to_hresult()); }
        }
        return 1;
    }
}
