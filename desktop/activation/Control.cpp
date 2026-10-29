#include "Controller.h"
#include "../../printer/probe/Evidence.h"
#include <winspool.h>
#include <cstdio>

namespace
{
    activation::JsonObject Print(std::wstring const& id)
    {
        using namespace activation;
        auto report = activation::Record(L"gdi-create-dc");
        Text(report, L"job_hash", Hash(id));
        auto dc = CreateDCW(L"WINSPOOL", Queue, nullptr, nullptr);
        if (!dc)
        {
            Number(report, L"hresult", static_cast<uint32_t>(HRESULT_FROM_WIN32(GetLastError())));
            return report;
        }
        struct Close { HDC value; ~Close() { DeleteDC(value); } } close{dc};
        auto title = std::wstring(Prefix) + id;
        DOCINFOW document{};
        document.cbSize = sizeof(document); document.lpszDocName = title.c_str();
        auto started = evidence::CallStartDoc([&] { return StartDocW(dc, &document); });
        report.Insert(L"start_doc_return", JsonValue::CreateNumberValue(started.rawReturn));
        Number(report, L"start_doc_last_error", started.lastError);
        Text(report, L"stage", L"gdi-start-doc");
        if (!started.Succeeded()) return report;
        try
        {
            Text(report, L"stage", L"gdi-page");
            Require(StartPage(dc) > 0, E_FAIL);
            constexpr wchar_t text[] = L"Original PSA activation probe. NO PDF IS PRODUCED.";
            Check(TextOutW(dc, 120, 120, text, static_cast<int>(std::size(text) - 1)) != FALSE);
            Require(EndPage(dc) > 0, E_FAIL);
            auto ended = evidence::CallStartDoc([&] { return EndDoc(dc); });
            report.Insert(L"end_doc_return", JsonValue::CreateNumberValue(ended.rawReturn));
            Number(report, L"end_doc_last_error", ended.lastError);
            Require(ended.Succeeded(), E_FAIL);
            Text(report, L"stage", L"submitted-not-printer-success");
        }
        catch (...)
        {
            Number(report, L"hresult", static_cast<uint32_t>(to_hresult().value));
            auto aborted = AbortDoc(dc);
            report.Insert(L"abort_doc_return", JsonValue::CreateNumberValue(aborted));
            return report;
        }
        return report;
    }
}
int wmain(int count, wchar_t** args)
{
    using namespace activation;
    auto report = activation::Record(L"control-arguments");
    Diagnostic diagnostic;
    try
    {
        Step(&diagnostic, L"control.apartment", L"control");
        Point(&diagnostic, L"init_apartment", L"winrt");
        init_apartment();
        auto context = Current(false, &diagnostic);
        if (count == 2 && std::wstring_view(args[1]) == L"--prepare")
        {
            auto id = Prepare(context, &diagnostic);
            Step(&diagnostic, L"control.output", L"control");
            Point(&diagnostic, L"JsonObject.Insert", L"winrt");
            Text(report, L"job_id", id);
            Text(report, L"stage", L"prepared-private-correlation");
            std::puts(to_string(report.Stringify()).c_str());
            return 0;
        }
        Require(count == 3 && Token(args[2]));
        if (std::wstring_view(args[1]) == L"--print")
        {
            Security security(context);
            auto baseLock = OpenDirectory(Base(context), context, security);
            auto job = Base(context) / args[2];
            auto jobLock = OpenDirectory(job, context, security);
            Validate(Read(job, L"request.json", context, security), context, args[2], L"request", Now());
            report = Print(args[2]);
            std::puts(to_string(report.Stringify()).c_str());
            return report.GetNamedString(L"stage") == L"submitted-not-printer-success" ? 0 : 1;
        }
        Require(std::wstring_view(args[1]) == L"--observe");
        report = Observe(context, args[2]);
        std::puts(to_string(report.Stringify()).c_str());
        return report.GetNamedBoolean(L"activationGatePassed") ? 0 : 1;
    }
    catch (...)
    {
        auto error = to_hresult();
        try { throw; }
        catch (std::filesystem::filesystem_error const& failure)
        {
            if (failure.code().category() == std::system_category())
                ErrorCode(&diagnostic, static_cast<uint32_t>(failure.code().value()), L"system");
            else if (failure.code().category() == std::generic_category())
                ErrorCode(&diagnostic, static_cast<uint32_t>(failure.code().value()), L"generic");
        }
        catch (...) {}
        Number(report, L"hresult", static_cast<uint32_t>(error.value));
        DiagnosticRecord(report, diagnostic);
        std::puts(to_string(report.Stringify()).c_str());
        return 1;
    }
}
