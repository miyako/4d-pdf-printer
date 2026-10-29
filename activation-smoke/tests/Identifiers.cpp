#include "../Contract.h"
#include <iostream>
#include <cstdio>
#include <thread>
#include <chrono>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <cerrno>
#endif

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--fixture")
    {
#ifdef _WIN32
        for (auto stream : {stdout, stderr})
        {
            int descriptor = _fileno(stream);
            int error = errno;
            if (descriptor == -1)
            {
                std::fprintf(stderr, "case=fixture assertion-line=%d API=_fileno stream=%s errno=%d\n",
                    __LINE__, stream == stdout ? "stdout" : "stderr", error);
                return 1;
            }
            int previous = _setmode(descriptor, _O_BINARY);
            error = errno;
            if (previous == -1)
            {
                std::fprintf(stderr, "case=fixture assertion-line=%d API=_setmode(_O_BINARY) stream=%s errno=%d\n",
                    __LINE__, stream == stdout ? "stdout" : "stderr", error);
                return 1;
            }
        }
#endif
        constexpr char text[] = "{\"message\":\"\xe6\x97\xa5\xe6\x9c\xac\"}\n";
        constexpr char error[] = "fixture stderr \xc3\xa9\n";
        return std::fwrite(text, 1, sizeof(text) - 1, stdout) == sizeof(text) - 1 &&
            std::fwrite(error, 1, sizeof(error) - 1, stderr) == sizeof(error) - 1 ? 0 : 1;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--fail")
    { std::fputs("primary fixture API failure\n", stderr); return 7; }
    if (argc == 2 && std::string_view(argv[1]) == "--sleep")
    { std::this_thread::sleep_for(std::chrono::milliseconds(500)); return 0; }
    constexpr auto id = L"0123456789abcdef0123456789abcdef";
    if (!smoke::ValidId(id) || smoke::ValidId(L"") || smoke::ValidId(L"../bad") ||
        smoke::ValidId(L"0123456789ABCDEF0123456789abcdef"))
    { std::cerr << "case=identifier line=" << __LINE__ << " strict lowercase hex assertion\n"; return 1; }
    if (smoke::ParseId(std::wstring(L"--test-id ") + id, L"--test-id ") != id ||
        smoke::ParseId(std::wstring(smoke::TitlePrefix) + id, smoke::TitlePrefix) != id)
    { std::cerr << "case=argument/title line=" << __LINE__ << " correlation assertion\n"; return 1; }
    for (auto bad : {L"--test-id 0123456789abcdef0123456789abcdef extra",
                    L"--test-id  0123456789abcdef0123456789abcdef",
                    L"--test-id ../0123456789abcdef0123456789",
                    L"--other 0123456789abcdef0123456789abcdef"})
    {
        try { smoke::ParseId(bad, L"--test-id "); }
        catch (std::invalid_argument const&) { continue; }
        std::cerr << "case=argument rejection line=" << __LINE__ << " accepted malformed argument\n"; return 1;
    }
    return 0;
}
