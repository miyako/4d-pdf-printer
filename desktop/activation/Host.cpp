#include "Files.h"
#include <shellapi.h>
#include <optional>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    using namespace activation;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    std::wstring id;
    std::optional<Context> context;
    try
    {
        init_apartment();
        int count = 0;
        auto args = CommandLineToArgvW(GetCommandLineW(), &count);
        Check(args != nullptr);
        struct Free { LPWSTR* value; ~Free() { LocalFree(value); } } free{args};
        Require(count == 3 && std::wstring_view(args[1]) == L"--job" && Token(args[2]));
        id = args[2];
        context = Current(false);
        auto base = std::filesystem::path(std::wstring(context->cache.Path())) / Folder;
        return Host(*context, base, id);
    }
    catch (...)
    {
        auto code = to_hresult();
        try
        {
            if (context && Token(id))
            {
                Security security(*context);
                auto base = std::filesystem::path(std::wstring(context->cache.Path())) / Folder;
                auto baseLock = OpenDirectory(base, *context, security);
                auto root = base / id;
                auto lock = OpenDirectory(root, *context, security);
                auto error = activation::Record(L"host-rejected-or-failed");
                Number(error, L"hresult", static_cast<uint32_t>(code.value));
                Atomic(root, L"host-error.json", error, security);
            }
        }
        catch (...) { OutputDebugStringW(L"PSA activation probe host error record could not be published.\n"); }
        wchar_t message[120]{};
        swprintf_s(message, L"PSA activation probe host rejected/failed: HRESULT=0x%08X\n",
            static_cast<unsigned>(code.value));
        OutputDebugStringW(message);
        return 1;
    }
}
