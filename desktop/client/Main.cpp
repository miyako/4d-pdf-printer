#include "../common/RpcClient.h"
#include <winspool.h>
#include <cstdio>
#include <thread>
#include <atomic>
#include <barrier>
#include <objbase.h>
#include "../../printer/probe/Evidence.h"

extern "C" unsigned long DesktopGatePendingCalls(void);

using namespace desktop;
using namespace winrt;
using namespace Windows::ApplicationModel;

namespace
{
    void Submit(std::wstring const& title, JsonObject const& report)
    {
        Text(report, L"stage", L"gdi-create-dc");
        auto dc = CreateDCW(L"WINSPOOL", L"4D PDF Prototype (Automation)", nullptr, nullptr);
        Check(dc != nullptr);
        struct Dc { HDC value; ~Dc() { DeleteDC(value); } } owned{dc};
        DOCINFOW doc{};
        doc.cbSize = sizeof(doc);
        doc.lpszDocName = title.c_str();
        Text(report, L"stage", L"gdi-start-doc");
        auto start = evidence::CallStartDoc([&] { return StartDocW(dc, &doc); });
        report.Insert(L"startDocReturn", JsonValue::CreateNumberValue(start.rawReturn));
        report.Insert(L"startDocLastError", JsonValue::CreateNumberValue(start.lastError));
        if (!start.Succeeded())
            throw hresult_error(start.lastError ? HRESULT_FROM_WIN32(start.lastError) : E_FAIL);
        Text(report, L"stage", L"gdi-print");
        try
        {
            if (StartPage(dc) <= 0) winrt::throw_last_error();
            constexpr wchar_t text[] = L"Original synthetic desktop handoff gate. NO PDF produced.";
            Check(TextOutW(dc, 120, 120, text, static_cast<int>(std::size(text) - 1)) != FALSE);
            Check(Rectangle(dc, 120, 240, 1000, 1000) != FALSE);
            if (EndPage(dc) <= 0 || EndDoc(dc) <= 0) winrt::throw_last_error();
        }
        catch (...) { AbortDoc(dc); throw; }
    }

    void Require(bool condition)
    {
        if (!condition) throw hresult_error(HRESULT_FROM_WIN32(ERROR_ASSERTION_FAILURE));
    }

    JsonObject Credentials(JsonObject const& prepared)
    {
        JsonObject result;
        Text(result, L"title", std::wstring(prepared.GetNamedString(L"title")));
        Text(result, L"statusSecret", std::wstring(prepared.GetNamedString(L"statusSecret")));
        return result;
    }

    void TransportChecks(JsonObject const& report)
    {
        auto before = Call(Ping);
        JsonObject stall;
        stall.Insert(L"milliseconds", JsonValue::CreateNumberValue(5000));
        auto start = std::chrono::steady_clock::now();
        try { Call(StallGate, stall); throw hresult_error(E_UNEXPECTED); }
        catch (hresult_error const& error)
        {
            if (error.code() != HRESULT_FROM_WIN32(ERROR_TIMEOUT)) throw;
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        auto during = Call(Ping);
        Require(during.GetNamedNumber(L"stallsStarted") > before.GetNamedNumber(L"stallsStarted"));
        Require(during.GetNamedNumber(L"stallsCompleted") == before.GetNamedNumber(L"stallsCompleted"));
        Require(elapsed >= 1500 && elapsed < 4000);
        report.Insert(L"inFlightTimeoutMilliseconds", JsonValue::CreateNumberValue(static_cast<double>(elapsed)));
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (Call(Ping).GetNamedNumber(L"stallsCompleted") > before.GetNamedNumber(L"stallsCompleted")) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        Require(Call(Ping).GetNamedNumber(L"stallsCompleted") > before.GetNamedNumber(L"stallsCompleted"));
        std::barrier ready(9);
        std::atomic<unsigned> capped{0}, timedOut{0}, unexpected{0};
        std::vector<std::thread> threads;
        for (unsigned i = 0; i < 9; ++i)
        {
            threads.emplace_back([&]
            {
                init_apartment();
                JsonObject input;
                input.Insert(L"milliseconds", JsonValue::CreateNumberValue(5000));
                ready.arrive_and_wait();
                try { Call(StallGate, input); ++unexpected; }
                catch (hresult_error const& error)
                {
                    if (error.code() == HRESULT_FROM_WIN32(ERROR_TOO_MANY_CMDS)) ++capped;
                    else if (error.code() == HRESULT_FROM_WIN32(ERROR_TIMEOUT)) ++timedOut;
                    else ++unexpected;
                }
                uninit_apartment();
            });
        }
        for (auto& thread : threads) thread.join();
        Require(capped > 0 && timedOut > 0 && unexpected == 0);
        report.Insert(L"pendingCapRejections", JsonValue::CreateNumberValue(capped.load()));
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        while (DesktopGatePendingCalls() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        Require(DesktopGatePendingCalls() == 0);
        std::this_thread::sleep_for(std::chrono::seconds(4));
        for (unsigned delay : {500u, 1900u, 2100u})
        {
            JsonObject input;
            input.Insert(L"milliseconds", JsonValue::CreateNumberValue(delay));
            bool completed = false;
            try { Call(StallGate, input); completed = true; }
            catch (hresult_error const& error)
            {
                if (error.code() != HRESULT_FROM_WIN32(ERROR_TIMEOUT)) throw;
            }
            if (delay == 500) Require(completed);
            report.Insert(L"race" + to_hstring(delay), JsonValue::CreateStringValue(completed ? L"completed" : L"timed-out-uncertain"));
        }
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        while (DesktopGatePendingCalls() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        Require(DesktopGatePendingCalls() == 0);
        Require(Call(Ping).GetNamedBoolean(L"ready"));
        report.Insert(L"pendingRecoveryVerified", JsonValue::CreateBooleanValue(true));
    }
}

int wmain(int argc, wchar_t** argv)
{
    JsonObject report{nullptr};
    try
    {
        init_apartment();
        report = Record(L"arguments");
        if (argc == 2 && std::wstring(argv[1]) == L"--self-test")
        {
            Require(IsTitle(TitlePrefix + std::wstring(64, L'a')));
            Require(!IsTitle(TitlePrefix + std::wstring(63, L'a')));
            Require(!IsTitle(TitlePrefix + std::wstring(64, L'G')));
            Require(IsToken(RandomToken()));
            Text(report, L"stage", L"self-test-passed");
        }
        else
        {
            if (argc != 2 || (std::wstring(argv[1]) != L"--gate" &&
                std::wstring(argv[1]) != L"--psa-bootstrap")) throw hresult_invalid_argument();
            Text(report, L"stage", L"caller-context");
            auto identity = CurrentIdentity();
            report.Insert(L"callerAppContainer", JsonValue::CreateBooleanValue(identity.appContainer));
            report.Insert(L"callerSession", JsonValue::CreateNumberValue(identity.session));
            report.Insert(L"expectedPackage", JsonValue::CreateBooleanValue(
                Package::Current().Id().Name() == L"Miyako.DesktopPdfPrototype"));
            APTTYPE apartment{};
            APTTYPEQUALIFIER qualifier{};
            check_hresult(CoGetApartmentType(&apartment, &qualifier));
            report.Insert(L"apartmentType", JsonValue::CreateNumberValue(static_cast<int>(apartment)));
            report.Insert(L"apartmentQualifier", JsonValue::CreateNumberValue(static_cast<int>(qualifier)));
            report.Insert(L"processArchitecture", JsonValue::CreateStringValue(to_hstring(evidence::Architecture)));
            auto context = std::string("{\"version\":1") +
                evidence::Context(L"Miyako.DesktopPdfPrototype", L"CN=DesktopPdfPrototypeDev") + "}";
            report.Insert(L"submissionContext", JsonObject::Parse(to_hstring(context)));
            if (std::wstring(argv[1]) == L"--psa-bootstrap")
            {
                Submit(TitlePrefix + RandomToken(), report);
                Text(report, L"stage", L"psa-bootstrap-readiness");
                Require(WaitReady(std::chrono::seconds(15)));
                Text(report, L"stage", L"psa-bootstrap-transport-checks");
                TransportChecks(report);
                Text(report, L"stage", L"psa-bootstrap-verified-no-pdf");
                std::puts(to_string(report.Stringify()).c_str());
                return 0;
            }
            report.Insert(L"launchResultAvailable", JsonValue::CreateBooleanValue(false));
            Text(report, L"stage", L"coordinator-launch-create");
            auto operation = FullTrustProcessLauncher::LaunchFullTrustProcessForAppWithArgumentsAsync(L"Printer", L"--serve");
            Text(report, L"stage", L"coordinator-launch-wait-get-results");
            auto launch = operation.get();
            Text(report, L"stage", L"coordinator-launch-result");
            report.Insert(L"launchResultAvailable", JsonValue::CreateBooleanValue(true));
            report.Insert(L"launchResult", JsonValue::CreateNumberValue(static_cast<int>(launch.LaunchResult())));
            report.Insert(L"launchExtendedError", JsonValue::CreateNumberValue(launch.ExtendedError().value));
            if (launch.LaunchResult() != FullTrustLaunchResult::Success)
                throw hresult_error(launch.ExtendedError().value < 0 ? launch.ExtendedError() : hresult(E_FAIL));
            Require(WaitReady(std::chrono::seconds(10)));
            Text(report, L"stage", L"in-flight-rpc-deadlines");
            TransportChecks(report);
            Text(report, L"stage", L"method-authorization");
            try { Call(WorkflowPing); throw hresult_error(E_UNEXPECTED); }
            catch (hresult_error const& error)
            {
                if (error.code() != E_ACCESSDENIED) throw;
                report.Insert(L"workflowMethodDenied", JsonValue::CreateBooleanValue(true));
            }
            auto first = Credentials(Call(PrepareGate));
            auto second = Credentials(Call(PrepareGate));
            auto canceled = Credentials(Call(PrepareGate));
            Call(CancelGate, canceled);
            Text(report, L"stage", L"reverse-order-print");
            Submit(std::wstring(second.GetNamedString(L"title")), report);
            Submit(std::wstring(first.GetNamedString(L"title")), report);
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
            bool done = false;
            while (std::chrono::steady_clock::now() < deadline)
            {
                auto a = Call(StatusGate, first);
                auto b = Call(StatusGate, second);
                done = a.GetNamedBoolean(L"gateCompleted") && b.GetNamedBoolean(L"gateCompleted");
                if (done) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            Require(done);
            report.Insert(L"outOfOrderClaimsVerified", JsonValue::CreateBooleanValue(true));
            Text(report, L"stage", L"replay-print");
            Submit(std::wstring(second.GetNamedString(L"title")), report);
            Submit(std::wstring(canceled.GetNamedString(L"title")), report);
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            bool replay = false;
            while (std::chrono::steady_clock::now() < deadline)
            {
                replay = Call(StatusGate, second).GetNamedNumber(L"replaysRejected") >= 1 &&
                    Call(StatusGate, canceled).GetNamedNumber(L"canceledClaimsRejected") >= 1;
                if (replay) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            Require(replay);
            Require(!Call(StatusGate, canceled).GetNamedBoolean(L"claimed"));
            report.Insert(L"replayRejected", JsonValue::CreateBooleanValue(true));
            report.Insert(L"canceledClaimRejected", JsonValue::CreateBooleanValue(true));
            Text(report, L"stage", L"gate-passed-no-pdf");
        }
        std::puts(to_string(report.Stringify()).c_str());
        return 0;
    }
    catch (...)
    {
        if (report)
        {
            report.Insert(L"hresult", JsonValue::CreateNumberValue(to_hresult().value));
            std::puts(to_string(report.Stringify()).c_str());
        }
        else std::puts("{\"schema\":\"4d-pdf-printer.desktop-gate\",\"version\":1,\"stage\":\"initialization-failed\",\"pdfProduced\":false}");
        return 1;
    }
}
