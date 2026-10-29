#include "../Process.h"
#include <iostream>

using namespace smoke;
#define EXPECT(condition) do { if (!(condition)) { \
    std::cerr << "case=" << test << " assertion-line=" << __LINE__ \
        << " API=" #condition " last-error=" << GetLastError() << "\n"; return 1; } } while (false)

int wmain(int argc, wchar_t** argv)
{
    char const* test = "initialization";
    try
    {
        if (argc == 2 && std::wstring_view(argv[1]) == L"--sleep") { Sleep(500); return 0; }
        init_apartment(apartment_type::multi_threaded);
        std::wstring id = L"0123456789abcdef0123456789abcdef";
        test = "record JSON roundtrip/type/ID validation";
        auto record = Record(id, L"host-entry");
        Text(record, L"creationTime", L"123");
        auto parsed = JsonObject::Parse(record.Stringify()); Validate(parsed, id, L"host-entry");
        EXPECT(Pid(parsed) == GetCurrentProcessId());
        EXPECT(ParseCreation(parsed.GetNamedString(L"creationTime")) == 123);
        bool rejected = false;
        try { Validate(parsed, L"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", L"host-entry"); }
        catch (hresult_error const&) { rejected = true; }
        EXPECT(rejected);
        for (auto bad : {L"", L"-1", L"1x", L"0", L"18446744073709551616"})
        {
            rejected = false;
            try { ParseCreation(bad); } catch (std::exception const&) { rejected = true; }
            catch (hresult_error const&) { rejected = true; }
            EXPECT(rejected);
        }
        Number(parsed, L"pid", 1.5); rejected = false;
        try { Pid(parsed); } catch (hresult_error const&) { rejected = true; }
        EXPECT(rejected);
        rejected = false;
        try { JsonObject::Parse(L"{broken"); } catch (hresult_error const&) { rejected = true; }
        EXPECT(rejected);
        test = "CreateProcessW fixed test child";
        auto self = Image(GetCurrentProcess());
        auto command = L"\"" + self + L"\" --sleep";
        STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
        EXPECT(CreateProcessW(self.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process));
        Handle child(process.hProcess), thread(process.hThread);
        test = "exact image/creation time then controlled timeout/exit0";
        auto created = Creation(child.value);
        VerifyProcess(child.value, self, created);
        rejected = false;
        try { VerifyProcess(child.value, self, created + 1); } catch (hresult_error const&) { rejected = true; }
        EXPECT(rejected);
        rejected = false;
        try { VerifyProcess(child.value, self + L".wrong", created); } catch (hresult_error const&) { rejected = true; }
        EXPECT(rejected);
        DWORD code{};
        EXPECT(!ObserveExit(child.value, 0, code));
        EXPECT(ObserveExit(child.value, 5000, code)); EXPECT(code == 0);
        std::cout << "native parsing/process cases passed; unpackaged tests do NOT prove PSA activation/storage\n";
        return 0;
    }
    catch (...)
    {
        auto hr = to_hresult();
        std::cerr << "case=" << test << " boundary-line=" << __LINE__ << " HRESULT=" << hr.value << "\n";
        Failure(L"native test boundary (case above)", hr); return 1;
    }
}
