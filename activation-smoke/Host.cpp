#include "Process.h"
#include "HostTrace.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR arguments, int)
{
    OutputDebugStringW(L"PSA smoke: wWinMain entry (before independent trace path resolution)\n");
    smoke::HostTrace trace;
    char const* stage = "Win32 startup trace open";
    try
    {
        if (!trace.Open()) return 1;
        bool traced = trace.Stage("wWinMain entry") &&
            trace.Arguments("wWinMainArguments", arguments) &&
            trace.Arguments("GetCommandLineW", GetCommandLineW());
        if (!traced) return 1;
        stage = "strict --test-id parsing"; trace.Stage(stage);
        auto id = smoke::ParseId(arguments, L"--test-id ");
        stage = "init_apartment(MTA)"; trace.Stage(stage);
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        stage = "ApplicationData.Current.LocalCacheFolder"; trace.Stage(stage);
        auto folder = smoke::Folder(id).get();
        auto entry = smoke::Record(id, L"host-entry");
        stage = "GetProcessTimes"; trace.Stage(stage);
        smoke::Text(entry, L"creationTime", std::to_wstring(smoke::Creation(GetCurrentProcess())));
        stage = "publish host.json"; trace.Stage(stage);
        smoke::Publish(folder, L"host.json", entry).get();
        stage = "observation-ready deadline"; trace.Stage(stage);
        auto deadline = GetTickCount64() + 10000;
        while (GetTickCount64() < deadline)
        {
            if (folder.TryGetItemAsync(L"ready.json").get())
            {
                smoke::Read(folder, L"ready.json", id, L"observation-ready");
                return trace.Stage("normal return0") && !trace.error ? 0 : 1;
            }
            Sleep(25);
        }
        throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
    }
    catch (...)
    {
        DWORD win32 = GetLastError();
        auto primary = winrt::to_hresult();
        return trace.ReportFailure(stage, primary.value, win32).exitCode;
    }
}
