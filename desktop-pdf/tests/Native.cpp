#include "../Job.h"
#include <iostream>

using namespace pdf;
template<typename F> void Reject(F function) {
    bool rejected = false;
    try { function(); } catch (...) { rejected = true; }
    Require(rejected, "expected rejection");
}
int main() {
    try {
        init_apartment(apartment_type::multi_threaded);
        std::string wire = R"({"schema":"4d-pdf-printer.worker-status","version":1,"worker_version":"0.1.0","result":"ok","exit_code":0,"stage":"complete","error":null,"document":null,"page":null,"documents":1,"pages_completed":1,"output_bytes":100,"output_published":true,"cleanup_complete":true})";
        Require(WorkerSuccess(wire, 0) == 100, "real WinRT worker success validation");
        auto escaped = wire;
        escaped.replace(escaped.find("\"schema\""), 8, "\"\\u0073chema\"");
        Require(WorkerSuccess(escaped, 0) == 100, "equivalent JSON key encoding");
        for (auto exit : {24ul, 71ul, 0xc0000005ul}) Reject([&] { WorkerSuccess(wire, exit); });
        for (auto key : {L"schema", L"version", L"worker_version", L"stage", L"error", L"document",
            L"page", L"documents", L"pages_completed", L"output_bytes", L"output_published", L"cleanup_complete"}) {
            auto r = Parse(wire);
            r.Remove(key);
            Reject([&] { WorkerSuccess(to_string(r.Stringify()), 0); });
        }
        auto r = Parse(wire); r.Insert(L"extra", JsonValue::CreateBooleanValue(true));
        Reject([&] { WorkerSuccess(to_string(r.Stringify()), 0); });
        r = Parse(wire); Text(r, L"result", L"info");
        Reject([&] { WorkerSuccess(to_string(r.Stringify()), 0); });
        r = Parse(wire); Number(r, L"output_bytes", 0);
        Reject([&] { WorkerSuccess(to_string(r.Stringify()), 0); });
        r = Parse(wire); r.Insert(L"cleanup_complete", JsonValue::CreateBooleanValue(false));
        Reject([&] { WorkerSuccess(to_string(r.Stringify()), 0); });
        Reject([] { Parse(R"({"v":01})"); });
        Reject([] { Parse(R"({"v":true,"v":false})"); });
        std::wstring id(32, L'a'), job(32, L'b');
        auto registered = Record(id, Mode::Silent, L"registered");
        Text(registered, L"destination", L"C:\\Users\\example\\a.pdf");
        auto began = Now(); Number(registered, L"expires", began + 1);
        Number(registered, L"timeout", DefaultCompletionMs);
        auto submitted = Record(id, Mode::Silent, L"submitted");
        Number(submitted, L"began", began); Number(submitted, L"deadline", Deadline(began, DefaultCompletionMs));
        Require(SubmissionDeadline(submitted, registered, id) == began + DefaultCompletionMs, "submission timeline");
        Number(submitted, L"deadline", began + 2 * DefaultCompletionMs);
        Reject([&] { SubmissionDeadline(submitted, registered, id); });
        auto binding = Record(id, Mode::Silent, L"binding"); Text(binding, L"job", job);
        Require(BoundJob(binding, id) == job, "claim binding");
        Reject([&] { BoundJob(binding, job); });
        std::cout << "Native WinRT production record/status checks passed; NOT activation/rendering/printing proof.\n";
        return 0;
    } catch (...) { Failure(L"native record/status test", to_hresult()); return 1; }
}
