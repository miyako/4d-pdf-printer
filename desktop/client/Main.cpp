#include "../common/RpcClient.h"
#include <winspool.h>
#include <cstdio>
#include <thread>

using namespace desktop;
using namespace winrt;
using namespace Windows::ApplicationModel;

namespace
{
    void Submit(std::wstring const& title)
    {
        auto dc = CreateDCW(L"WINSPOOL", L"4D PDF Prototype (Automation)", nullptr, nullptr);
        Check(dc != nullptr);
        struct Dc { HDC value; ~Dc() { DeleteDC(value); } } owned{dc};
        DOCINFOW doc{};
        doc.cbSize = sizeof(doc);
        doc.lpszDocName = title.c_str();
        if (StartDocW(dc, &doc) <= 0) winrt::throw_last_error();
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
            Text(report, L"stage", L"self-test-passed");
        }
        else
        {
            if (argc != 2 || std::wstring(argv[1]) != L"--gate") throw hresult_invalid_argument();
            Text(report, L"stage", L"coordinator-launch");
            auto launch = FullTrustProcessLauncher::LaunchFullTrustProcessForAppWithArgumentsAsync(L"Printer", L"--serve").get();
            report.Insert(L"launchResult", JsonValue::CreateNumberValue(static_cast<int>(launch.LaunchResult())));
            report.Insert(L"launchExtendedError", JsonValue::CreateNumberValue(launch.ExtendedError().value));
            if (launch.LaunchResult() != FullTrustLaunchResult::Success)
                throw hresult_error(launch.ExtendedError().value < 0 ? launch.ExtendedError() : hresult(E_FAIL));
            Require(WaitReady(std::chrono::seconds(10)));
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
            Submit(std::wstring(second.GetNamedString(L"title")));
            Submit(std::wstring(first.GetNamedString(L"title")));
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
            Submit(std::wstring(second.GetNamedString(L"title")));
            Submit(std::wstring(canceled.GetNamedString(L"title")));
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
