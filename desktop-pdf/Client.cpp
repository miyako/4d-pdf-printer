#include "WorkerProcess.h"
#include <iostream>

using namespace pdf;
namespace {
JsonObject Registered(StorageFolder const& folder, std::wstring const& id) {
    auto r = Read(folder, L"registered.json").get();
    RegisteredRecord(r, id);
    return r;
}
void Print(JsonObject const& r) { std::cout << to_string(r.Stringify()) << '\n'; }
}
int wmain(int argc, wchar_t** argv) {
    try {
        init_apartment(apartment_type::multi_threaded);
        Require(argc >= 3, "expected configure-runtime/register/submitted/status/wait/cancel and operand");
        std::wstring command(argv[1]);
        if (command == L"configure-runtime") {
            Require(argc == 3, "configure-runtime requires fixed absolute runtime root");
            auto runtime = InspectRuntime(fs::path(argv[2]));
            JsonObject r; Text(r, L"root", runtime.root.native());
            Text(r, L"inventory", runtime.inventory); Text(r, L"receipt", runtime.receipt);
            Text(r, L"provenance", runtime.provenance);
            Publish(Root().get(), L"runtime.json", r).get();
            Print(r); return 0;
        }
        if (command == L"reconcile") {
            Require(argc == 4 && Id(argv[3]), "reconcile requires exact mode and job id");
            auto mode = ParseMode(argv[2]); std::wstring id(argv[3]);
            auto report = Reconcile(Folder(L"Jobs", id).get(), id, mode);
            Print(report);
            auto state = report.GetNamedString(L"fileState");
            return report.GetNamedString(L"printOutcome") == L"succeeded" &&
                (state == L"confirmed-output" || state == L"matching-bytes-not-attributed") ? 0 : 2;
        }
        if (command == L"register") {
            Require(argc >= 3 && argc <= 5, "register requires destination, optional completion/registration milliseconds");
            fs::path destination(argv[2]);
            auto parents = SafeParents(destination);
            Require(_wcsicmp(destination.extension().c_str(), L".pdf") == 0, "PDF destination required");
            auto timeout = argc >= 4 ? Duration(argv[3]) : DefaultCompletionMs;
            auto expiry = argc == 5 ? Duration(argv[4]) : DefaultRegistrationMs;
            auto id = RandomId(); auto folder = Folder(L"Registrations", id, true).get();
            auto r = Record(id, Mode::Silent, L"registered");
            Text(r, L"destination", destination.native()); Number(r, L"timeout", timeout);
            Number(r, L"expires", Deadline(Now(), expiry));
            Publish(folder, L"registered.json", r).get();
            auto response = Record(id, Mode::Silent, L"registration");
            Text(response, L"selector", std::wstring(TitlePrefix) + id); Print(response); return 0;
        }
        Require(argc == 3 && Id(argv[2]), "registration id required");
        std::wstring id(argv[2]); auto folder = Folder(L"Registrations", id).get();
        auto registered = Registered(folder, id);
        if (command == L"cancel") {
            if (!Exists(folder, L"cancel.json").get())
                Publish(folder, L"cancel.json", Record(id, Mode::Silent, L"cancel")).get();
            Print(Record(id, Mode::Silent, L"cancel-requested")); return 0;
        }
        if (command == L"submitted") {
            Require(!Expired(Now(), Integer(registered, L"expires")) &&
                !Exists(folder, L"cancel.json").get() && !Exists(folder, L"claim").get(), "registration unavailable");
            auto r = Record(id, Mode::Silent, L"submitted");
            auto began = Now(); Number(r, L"began", began);
            Number(r, L"deadline", Deadline(began, Integer(registered, L"timeout")));
            Publish(folder, L"submitted.json", r).get(); Print(r); return 0;
        }
        Require(command == L"wait" || command == L"status", "unknown client command");
        auto submission = Read(folder, L"submitted.json").get();
        auto deadline = SubmissionDeadline(submission, registered, id);
        auto tickEnd = GetTickCount64() + Remaining(Now(), deadline, Integer(registered, L"timeout"));
        for (;;) {
            if (Exists(folder, L"claim").get()) {
                auto claim = folder.GetFolderAsync(L"claim").get();
                if (Exists(claim, L"binding.json").get()) {
                    auto binding = Read(claim, L"binding.json").get();
                    auto jobId = BoundJob(binding, id);
                    auto job = Folder(L"Jobs", jobId).get();
                    // This is the PSA's acknowledgement, not just a saved-file record.
                    if (Exists(job, L"completion.json").get()) {
                        auto result = Read(job, L"completion.json").get();
                        Validate(result, jobId, Mode::Silent, L"completion");
                        Fields(result, {L"schema", L"version", L"id", L"mode", L"kind", L"outcome", L"stage", L"hresult"});
                        Print(result);
                        return result.GetNamedString(L"outcome") == L"succeeded" ? 0 : 1;
                    }
                }
            }
            if (Expired(Now(), deadline) || GetTickCount64() >= tickEnd) {
                if (!Exists(folder, L"cancel.json").get())
                    Publish(folder, L"cancel.json", Record(id, Mode::Silent, L"cancel")).get();
                auto result = Record(id, Mode::Silent, L"caller-timeout");
                Text(result, L"outcome", L"indeterminate"); Print(result); return 2;
            }
            if (command == L"status") {
                Print(Record(id, Mode::Silent, L"pending")); return 3;
            }
            Sleep(25);
        }
    } catch (...) {
        auto hr = to_hresult(); Failure(L"client", hr);
        JsonObject r; Text(r, L"outcome", L"error"); Number(r, L"hresult", static_cast<std::uint32_t>(hr.value));
        Print(r); return 1;
    }
}
