#include "Process.h"
#include <objbase.h>
#include <winrt/Windows.ApplicationModel.h>

using namespace smoke;

namespace
{
    void GdiResult(JsonObject const& r, wchar_t const* name, int value, DWORD error)
    {
        JsonObject api; Number(api, L"return", value);
        Number(api, L"lastErrorOnFailure", value <= 0 ? error : 0);
        r.Insert(name, api);
        if (value <= 0) Failure(name, HRESULT_FROM_WIN32(error ? error : ERROR_PRINT_CANCELLED));
    }
    int Submit(std::wstring const& id, wchar_t const*& stage)
    {
        stage = L"submitter ApplicationData.Current.LocalCacheFolder";
        auto folder = Folder(id).get();
        auto record = Record(id, L"gdi-submission");
        stage = L"CreateDCW fixed smoke queue";
        SetLastError(ERROR_SUCCESS);
        HDC dc = CreateDCW(L"WINSPOOL", Queue, nullptr, nullptr);
        DWORD error = GetLastError();
        if (!dc) { Failure(L"CreateDCW fixed smoke queue", HRESULT_FROM_WIN32(error)); return 1; }
        std::wstring title = std::wstring(TitlePrefix) + id;
        DOCINFOW info{sizeof(info)}; info.lpszDocName = title.c_str();
        SetLastError(ERROR_SUCCESS);
        int started = StartDocW(dc, &info); error = GetLastError();
        GdiResult(record, L"StartDocW", started, error);
        int ended = 0;
        if (started > 0)
        {
            SetLastError(ERROR_SUCCESS);
            int page = StartPage(dc); error = GetLastError();
            GdiResult(record, L"StartPage", page, error);
            if (page > 0)
            {
                constexpr wchar_t text[] = L"Original synthetic activation smoke page. No PDF is produced.";
                SetLastError(ERROR_SUCCESS);
                int drawn = TextOutW(dc, 100, 100, text, ARRAYSIZE(text) - 1); error = GetLastError();
                GdiResult(record, L"TextOutW", drawn, error);
                SetLastError(ERROR_SUCCESS);
                int pageEnd = EndPage(dc); error = GetLastError();
                GdiResult(record, L"EndPage", pageEnd, error);
            }
            SetLastError(ERROR_SUCCESS);
            ended = EndDoc(dc); error = GetLastError();
            GdiResult(record, L"EndDoc", ended, error);
        }
        stage = L"DeleteDC";
        if (!DeleteDC(dc)) Failure(stage, HRESULT_FROM_WIN32(GetLastError()));
        stage = L"publish gdi.json";
        Publish(folder, L"gdi.json", record).get();
        return started > 0 && ended > 0 ? 0 : 1;
    }
    int Observe(std::wstring const& id, wchar_t const*& stage)
    {
        stage = L"Package.Current identity/InstalledLocation";
        auto report = Record(id, L"observation");
        Flag(report, L"actualCallbackObserved", false); Text(report, L"launchResult", L"missing");
        Flag(report, L"hostEntryObserved", false); Flag(report, L"hostExitObserved", false);
        Flag(report, L"activationFeasibilityPassed", false);
        StorageFolder folder{nullptr};
        Handle submit;
        bool passed = false;
        try
        {
            auto package = Windows::ApplicationModel::Package::Current();
            auto version = package.Id().Version();
            if (package.Id().Name() != L"Miyako.PsaActivationSmoke" ||
                package.Id().Publisher() != L"CN=DesktopPdfPrototypeDev" ||
                version.Major != 0 || version.Minor != 1 || version.Build != 1 || version.Revision != 0)
                throw hresult_invalid_argument();
            auto installed = std::wstring(package.InstalledLocation().Path());
            auto expected = installed + L"\\SmokeHost.exe";
            stage = L"ApplicationData.Current.LocalCacheFolder fresh run";
            folder = Folder(id, true).get();
            stage = L"CreateProcessW fixed submitter (never host)";
            auto self = installed + L"\\SmokeObserver.exe";
            auto command = L"\"" + self + L"\" --submit " + id;
            STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
            check_bool(CreateProcessW(self.c_str(), command.data(), nullptr, nullptr, FALSE,
                CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process));
            submit.value = process.hProcess; Handle thread(process.hThread);
            auto deadline = GetTickCount64() + 45000;
            Handle host;
            bool launchSeen = false;
            while (GetTickCount64() < deadline)
            {
                stage = L"read actual callback.json";
                if (!report.GetNamedBoolean(L"actualCallbackObserved") && folder.TryGetItemAsync(L"callback.json").get())
                {
                    auto callback = Read(folder, L"callback.json", id, L"callback-result");
                    if (callback.GetNamedString(L"workflowSessionId").empty() ||
                        !callback.GetNamedBoolean(L"actualCallbackObserved")) throw hresult_invalid_argument();
                    Flag(report, L"actualCallbackObserved", true);
                }
                stage = L"read launch.json raw launch result";
                if (!launchSeen && folder.TryGetItemAsync(L"launch.json").get())
                {
                    auto launch = Read(folder, L"launch.json", id, L"callback-result");
                    Text(report, L"launchResult", launch.GetNamedString(L"launchResult"));
                    report.Insert(L"launcher", launch); launchSeen = true;
                }
                stage = L"host entry/OpenProcess/QueryFullProcessImageNameW/GetProcessTimes";
                if (!host.value && folder.TryGetItemAsync(L"host.json").get())
                {
                    auto entry = Read(folder, L"host.json", id, L"host-entry");
                    host.value = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, Pid(entry));
                    check_bool(host.value != nullptr);
                    VerifyProcess(host.value, expected, ParseCreation(entry.GetNamedString(L"creationTime")));
                    report.Insert(L"host", entry); Flag(report, L"hostEntryObserved", true);
                    stage = L"publish ready.json after exact process handle acquired";
                    Publish(folder, L"ready.json", Record(id, L"observation-ready")).get();
                }
                stage = L"WaitForSingleObject/GetExitCodeProcess exact host handle";
                DWORD code{};
                if (host.value && !report.GetNamedBoolean(L"hostExitObserved") && ObserveExit(host.value, 0, code))
                {
                    Flag(report, L"hostExitObserved", true); Number(report, L"hostExitCode", code);
                }
                if (launchSeen && report.GetNamedBoolean(L"hostExitObserved") &&
                    report.GetNamedBoolean(L"actualCallbackObserved"))
                {
                    passed = report.GetNamedString(L"launchResult") == L"Success" &&
                        report.GetNamedNumber(L"hostExitCode") == 0 &&
                        !report.GetNamedObject(L"launcher").GetNamedBoolean(L"loggingFailed");
                    break;
                }
                Sleep(25);
            }
            stage = L"overall observation deadline/pass gate";
            if (!passed) throw hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
        }
        catch (...)
        {
            auto hr = to_hresult(); Failure(stage, hr);
            Text(report, L"failureStage", stage); Number(report, L"hresult", hr.value);
        }
        if (submit.value)
        {
            stage = L"finish exact owned submitter";
            auto cleanup = FinishOwnedSubmitter(submit.value);
            report.Insert(L"submitterCleanup", cleanup);
            if (!cleanup.GetNamedBoolean(L"cleanupSucceeded")) passed = false;
        }
        Flag(report, L"activationFeasibilityPassed", passed);
        stage = L"publish observation.json";
        try { if (folder) Publish(folder, L"observation.json", report).get(); }
        catch (...)
        {
            Failure(stage, to_hresult());
            passed = false; Flag(report, L"activationFeasibilityPassed", false);
            Flag(report, L"observationReportWriteFailed", true);
        }
        stage = L"WriteFile stdout observation JSON";
        auto text = to_string(report.Stringify()) + "\n";
        DWORD written{};
        check_bool(WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text.data(), static_cast<DWORD>(text.size()), &written, nullptr));
        if (written != text.size()) throw hresult_error(E_FAIL);
        return passed ? 0 : 1;
    }
}
int wmain(int argc, wchar_t** argv)
{
    wchar_t const* stage = L"observer/submitter strict arguments";
    try
    {
        if (argc != 3 || !ValidId(argv[2])) throw hresult_invalid_argument();
        init_apartment(apartment_type::multi_threaded);
        if (std::wstring_view(argv[1]) == L"--submit") return Submit(argv[2], stage);
        if (std::wstring_view(argv[1]) == L"--observe") return Observe(argv[2], stage);
        throw hresult_invalid_argument();
    }
    catch (...) { Failure(stage, to_hresult()); return 1; }
}
