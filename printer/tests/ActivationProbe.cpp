#include <windows.h>
#include <cstdio>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.ApplicationModel.Background.h>

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2) return 2;
    HMODULE module = LoadLibraryW(argv[1]);
    if (!module) return 3;
    int result = 0;
    try
    {
        winrt::init_apartment();
        using GetFactory = HRESULT(__stdcall*)(void*, void**);
        auto entry = reinterpret_cast<GetFactory>(GetProcAddress(module, "DllGetActivationFactory"));
        if (!entry) throw winrt::hresult_error(E_NOINTERFACE);
        winrt::hstring name = L"PsaDiagnostic.Task";
        winrt::Windows::Foundation::IActivationFactory factory{nullptr};
        winrt::check_hresult(entry(winrt::get_abi(name), winrt::put_abi(factory)));
        auto task = factory.ActivateInstance<winrt::Windows::ApplicationModel::Background::IBackgroundTask>();
        if (winrt::get_class_name(task) != name) throw winrt::hresult_error(E_FAIL);
        winrt::hstring invalid = L"PsaDiagnostic.Invalid";
        winrt::Windows::Foundation::IActivationFactory missing{nullptr};
        if (entry(winrt::get_abi(invalid), winrt::put_abi(missing)) != CLASS_E_CLASSNOTAVAILABLE || missing)
            throw winrt::hresult_error(E_FAIL);
        std::puts("Factory activation passed; Run not called, no printer accessed.");
    }
    catch (...)
    {
        std::fprintf(stderr, "Factory probe failed: 0x%08X\n",
            static_cast<unsigned int>(winrt::to_hresult()));
        result = 1;
    }
    FreeLibrary(module);
    return result;
}
