#include "../Contract.h"
#include "PhysicalMapping.h"
#include <iostream>

using namespace pdf;
template<typename F> void Reject(F function) {
    bool rejected = false;
    try { function(); } catch (std::exception const&) { rejected = true; }
    Require(rejected, "expected rejection");
}
int main() {
    try {
        std::wstring id(32, L'a');
        for (auto mode : {Mode::SaveAs, Mode::Silent}) {
            auto arguments = L"--mode " + Name(mode) + L" --job " + id;
            auto bare = ParseActivation(arguments);
            auto wrapped = ParseActivation(L"/InvokerPRAID: " + Name(mode) + L" " + arguments);
            Require(bare.id == id && wrapped.id == id && bare.mode == mode && wrapped.mode == mode, "activation binding");
            auto other = mode == Mode::SaveAs ? L"Silent" : L"SaveAs";
            Reject([&] { ParseActivation(L"/InvokerPRAID: " + std::wstring(other) + L" " + arguments); });
            for (auto suffix : {L" ", L"\n", L" --extra", L"f"})
                Reject([&] { ParseActivation(arguments + suffix); });
        }
        for (auto prefix : {L"Printer", L"Client", L"saveas", L"SaveAs.exe", L"Unknown"})
            Reject([&] { ParseActivation(L"/InvokerPRAID: " + std::wstring(prefix) + L" --mode SaveAs --job " + id); });
        Reject([&] { ParseActivation(L"--mode SaveAs --job " + std::wstring(32, L'A')); });
        Reject([&] { ParseActivation(L"--test-id " + id); });
        Require(Registration(L"4dpdf:" + id) == id, "registration selector");
        Reject([] { Registration(L"normal application job title"); });
        Reject([&] { Registration(L"4dpdf:" + id + L" "); });
        Require(Duration(L"300000") == DefaultCompletionMs && Duration(L"86400000") == 86400000, "duration");
        for (auto value : {L"", L"0", L"-1", L"1x", L"86400001", L"18446744073709551616"})
            Reject([&] { Duration(value); });
        auto deadline = Deadline(1000, DefaultCompletionMs);
        Require(deadline == 301000 && !Expired(300999, deadline) && Expired(301000, deadline), "absolute deadline");
        // Moving through capture/serialization does not create another completion budget.
        Require(deadline - 200000 == 101000 && Expired(301001, deadline), "no phase reset");
        Require(Remaining(200000, deadline, DefaultCompletionMs) == 101000 &&
            Remaining(0, deadline, DefaultCompletionMs) == DefaultCompletionMs &&
            Remaining(301001, deadline, DefaultCompletionMs) == 0, "bounded monotonic observation");
        Reject([] { Deadline(9007199254740991ull, 1); });
        Require(Quote(L"") == L"\"\"" && Quote(L"a b") == L"\"a b\"" &&
            Quote(L"x\\") == L"\"x\\\\\"" && Quote(L"a\"b") == L"\"a\\\"b\"", "Windows quoting");
        for (auto path : {L"C:\\Users\\me\\a.pdf", L"D:\\user data\\unicode-\u03a9.pdf"})
            Require(LocalPath(path), "normal local path");
        for (auto path : {L"relative.pdf", L"\\\\server\\a.pdf", L"\\\\?\\C:\\a.pdf", L"C:/a.pdf",
            L"C:\\x\\..\\a.pdf", L"C:\\x\\a.pdf:ads", L"C:\\x\\a. ", L"C:\\x\\CON.pdf",
            L"C:\\x\\com1.pdf", L"C:\\x\\", L"C:\\x\\\\a.pdf", L"C:\\x\\a\n.pdf"})
            Require(!LocalPath(path), "unsafe path accepted");
        Require(!LocalPath(std::wstring_view(L"C:\\x\\a\0b.pdf", 13)) &&
            !LocalPath(L"C:\\x\\COM\u00b9.pdf"), "device/control path");
        for (auto text : {R"({"a":1,"b":{"a":2}})", R"({"\u0061":1})", R"({"a":[true,null,"escaped\"value"]})", " {\"ok\":true}\n"})
            JsonWire(text).Check();
        for (auto text : {"", "{}", R"({"a":1,"a":2})", R"({"a":{"b":1,"b":2}})",
            R"({"a":1,"\u0061":2})", R"({"a":1}{"b":2})", R"({"a":"bad\q"})", R"({"a":1,})"}) {
            if (std::string_view(text) == "{}") { JsonWire(text).Check(); continue; }
            Reject([&] { JsonWire(text).Check(); });
        }
        Reject([] { JsonWire(std::string(RecordBytes + 1, ' ')).Check(); });
        auto physical = proof::MappingFromReference(2880, 1800, 762, 476);
        Require(physical && physical->windowX == 76200 && physical->windowY == 47600 &&
            physical->viewportX == 2880 && physical->viewportY == 1800, "reference DC geometry");
        for (int bad : {0, -1, std::numeric_limits<int>::min()})
            Require(!proof::MappingFromReference(bad, 1800, 762, 476) &&
                !proof::MappingFromReference(2880, bad, 762, 476) &&
                !proof::MappingFromReference(2880, 1800, bad, 476), "bad reference metrics");
        Require(!proof::MappingFromReference(1, 1, std::numeric_limits<int>::max(), 1), "physical overflow");
        std::cout << "Portable production contracts and reference-metric guards passed; NOT Windows/EMF/printing evidence.\n";
        return 0;
    } catch (std::exception const& error) { std::cerr << error.what() << '\n'; return 1; }
}
