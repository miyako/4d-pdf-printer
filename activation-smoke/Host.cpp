#include "Process.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR arguments, int)
{
    wchar_t const* stage = L"host strict --test-id parsing";
    try
    {
        auto id = smoke::ParseId(arguments, L"--test-id ");
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        stage = L"host ApplicationData.Current.LocalCacheFolder";
        auto folder = smoke::Folder(id).get();
        auto entry = smoke::Record(id, L"host-entry");
        stage = L"host GetProcessTimes";
        smoke::Text(entry, L"creationTime", std::to_wstring(smoke::Creation(GetCurrentProcess())));
        stage = L"host publish host.json";
        smoke::Publish(folder, L"host.json", entry).get();
        stage = L"host observation-ready deadline";
        auto deadline = GetTickCount64() + 10000;
        while (GetTickCount64() < deadline)
        {
            if (folder.TryGetItemAsync(L"ready.json").get())
            {
                smoke::Read(folder, L"ready.json", id, L"observation-ready");
                return 0;
            }
            Sleep(25);
        }
        throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
    }
    catch (...)
    {
        smoke::Failure(stage, winrt::to_hresult());
        return 1;
    }
}
