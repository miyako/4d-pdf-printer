#include "Fixtures.h"
#include "../../printer/probe/Evidence.h"
#include <winspool.h>
#include <array>
#include <algorithm>
#include <memory>

namespace proof
{
    namespace
    {
        struct DeleteDc { void operator()(std::remove_pointer_t<HDC>* value) const { if (value) DeleteDC(value); } };
        struct DeleteEmf { void operator()(std::remove_pointer_t<HENHMETAFILE>* value) const { if (value) DeleteEnhMetaFile(value); } };
        using Dc = std::unique_ptr<std::remove_pointer_t<HDC>, DeleteDc>;
        using Emf = std::unique_ptr<std::remove_pointer_t<HENHMETAFILE>, DeleteEmf>;
        void Line(HDC dc, int x, int y, wchar_t const* value)
        {
            Check(TextOutW(dc, x, y, value, static_cast<int>(wcslen(value))), "fixture-text");
        }
        void Page(std::filesystem::path const& path, std::wstring const& id, int page)
        {
            RECT physical{0, 0, 21000, 29700};
            HDC dc = CreateEnhMetaFileW(nullptr, path.c_str(), &physical,
                L"4D PDF proof\0Original synthetic fixture\0");
            Check(dc != nullptr, "emf-create");
            try
            {
                Check(SetMapMode(dc, MM_ANISOTROPIC) != 0, "mapping");
                Check(SetWindowExtEx(dc, 21000, 29700, nullptr), "mapping");
                Check(SetViewportExtEx(dc, 2100, 2970, nullptr), "mapping");
                HFONT font = CreateFontW(-420, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                    DEFAULT_PITCH, L"Segoe UI");
                Check(font != nullptr, "fixture-font");
                auto previous = SelectObject(dc, font);
                Check(previous && previous != HGDI_ERROR, "fixture-font-select");
                struct FontSelection
                {
                    HDC dc; HFONT font; HGDIOBJ previous;
                    ~FontSelection() { if (dc) SelectObject(dc, previous); if (font) DeleteObject(font); }
                } selection{dc, font, previous};
                wchar_t face[LF_FACESIZE]{};
                Require(GetTextFaceW(dc, LF_FACESIZE, face) > 0 &&
                    std::wstring(face) == L"Segoe UI", "fixture-font-unavailable");
                SetBkMode(dc, TRANSPARENT);
                std::wstring heading = L"Original PDF product proof: " + id +
                    L" page " + std::to_wstring(page);
                Line(dc, 1500, 1500, heading.c_str());
                if (id == L"text")
                {
                    constexpr wchar_t text[] = L"Selectable text 0123456789 \u03a9 \u03b1 \u0416";
                    std::array<WORD, std::size(text) - 1> glyphs{};
                    Require(GetGlyphIndicesW(dc, text, static_cast<int>(glyphs.size()),
                        glyphs.data(), GGI_MARK_NONEXISTING_GLYPHS) != GDI_ERROR, "font-glyph-query");
                    for (auto glyph : glyphs) Require(glyph != 0xffff, "font-glyph-missing");
                    for (int row = 0; row < 20; ++row) Line(dc, 1500, 3000 + row * 650, text);
                    Check(Rectangle(dc, 1500, 17500, 18500, 18000), "fixture-rectangle");
                }
                else if (id == L"vectors")
                {
                    Line(dc, 1500, 2500, L"Vector clipping and gradient");
                    for (int index = 0; index < 8; ++index)
                    {
                        Check(Ellipse(dc, 1600 + index * 1900, 4000,
                            3000 + index * 1900, 6500), "fixture-ellipse");
                        Check(MoveToEx(dc, 1500, 8000 + index * 600, nullptr), "fixture-move");
                        Check(LineTo(dc, 18500, 8500 + index * 600), "fixture-line");
                    }
                    int saved = SaveDC(dc);
                    Require(saved != 0, "fixture-save");
                    Require(IntersectClipRect(dc, 2000, 15000, 17000, 19000) != ERROR, "fixture-clip");
                    Check(Ellipse(dc, 1000, 14000, 19000, 20000), "fixture-clipped-ellipse");
                    Check(RestoreDC(dc, saved), "fixture-restore");
                    TRIVERTEX colors[] = {{2000, 21000, 0xff00, 0, 0, 0},
                        {17000, 24000, 0, 0, 0xff00, 0}};
                    GRADIENT_RECT rectangle{0, 1};
                    Check(GradientFill(dc, colors, 2, &rectangle, 1, GRADIENT_FILL_RECT_H), "fixture-gradient");
                    Dc memory(CreateCompatibleDC(dc));
                    Check(memory != nullptr, "fixture-alpha-dc");
                    BITMAPINFO alpha{};
                    alpha.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    alpha.bmiHeader.biWidth = 8; alpha.bmiHeader.biHeight = -8;
                    alpha.bmiHeader.biPlanes = 1; alpha.bmiHeader.biBitCount = 32;
                    void* raw = nullptr;
                    HBITMAP image = CreateDIBSection(dc, &alpha, DIB_RGB_COLORS, &raw, nullptr, 0);
                    Check(image != nullptr, "fixture-alpha-image");
                    auto old = SelectObject(memory.get(), image);
                    std::fill_n(static_cast<DWORD*>(raw), 64, 0x80800000u);
                    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
                    BOOL blended = AlphaBlend(dc, 5000, 21000, 8000, 3000, memory.get(), 0, 0, 8, 8, blend);
                    DWORD alphaError = blended ? 0 : GetLastError();
                    SelectObject(memory.get(), old);
                    Check(DeleteObject(image), "fixture-alpha-close");
                    if (!blended) throw Failure("fixture-alpha", alphaError);
                }
                else
                {
                    Line(dc, 1500, 2500, L"Original image and selectable text");
                    std::array<DWORD, 64 * 64> pixels{};
                    for (unsigned y = 0; y < 64; ++y)
                        for (unsigned x = 0; x < 64; ++x)
                            pixels[y * 64 + x] = ((x * 4) << 16) | ((y * 4) << 8) | ((x ^ y) * 4);
                    BITMAPINFO bitmap{};
                    bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bitmap.bmiHeader.biWidth = 64;
                    bitmap.bmiHeader.biHeight = -64;
                    bitmap.bmiHeader.biPlanes = 1;
                    bitmap.bmiHeader.biBitCount = 32;
                    bitmap.bmiHeader.biCompression = BI_RGB;
                    Require(StretchDIBits(dc, 2000, 4000, 12000, 10000, 0, 0, 64, 64,
                        pixels.data(), &bitmap, DIB_RGB_COLORS, SRCCOPY) != static_cast<int>(GDI_ERROR), "fixture-image");
                    Check(Rectangle(dc, 2000, 15000, 18000, 16000), "fixture-image-vector");
                    Check(Ellipse(dc, 2000, 18000, 18000, 22000), "fixture-image-vector");
                }
                SelectObject(dc, previous);
                selection.dc = nullptr;
                Check(DeleteObject(font), "fixture-font-close");
                selection.font = nullptr;
                Emf emf(CloseEnhMetaFile(dc));
                dc = nullptr;
                Check(emf != nullptr, "emf-close");
            }
            catch (...)
            {
                if (dc) { Emf emf(CloseEnhMetaFile(dc)); }
                throw;
            }
        }
    }

    JsonObject CreateFixtures(std::filesystem::path const& directory)
    {
        ValidatePath(directory, false);
        Check(CreateDirectoryW(directory.c_str(), nullptr), "fixtures-directory");
        auto report = Record(L"fixtures-created");
        Text(report, L"schema", L"4d-pdf-printer.proof-fixtures");
        JsonArray fixtures;
        for (auto id : {L"text", L"vectors", L"mixed"})
        {
            JsonObject fixture;
            Text(fixture, L"id", id);
            JsonArray pages, hashes, expected;
            for (int page = 1; page <= 2; ++page)
            {
                auto name = std::wstring(id) + L"-" + std::to_wstring(page) + L".emf";
                Page(directory / name, id, page);
                pages.Append(JsonValue::CreateStringValue(name));
                hashes.Append(JsonValue::CreateStringValue(Hash(directory / name)));
                expected.Append(JsonValue::CreateStringValue(L"Original PDF product proof: " +
                    std::wstring(id) + L" page " + std::to_wstring(page)));
            }
            fixture.Insert(L"pages", pages);
            fixture.Insert(L"source_sha256", hashes);
            expected.Append(JsonValue::CreateStringValue(std::wstring(id) == L"text" ?
                L"Selectable text 0123456789 \u03a9 \u03b1 \u0416" : std::wstring(id) == L"vectors" ?
                L"Vector clipping and gradient" : L"Original image and selectable text"));
            fixture.Insert(L"expected_text", expected);
            Number(fixture, L"minimum_vector_paths", std::wstring(id) == L"vectors" ? 6 : 1);
            fixtures.Append(fixture);
        }
        report.Insert(L"fixtures", fixtures);
        WriteNew(directory / L"fixtures.json", report);
        return report;
    }

    JsonObject PrintFixture(std::filesystem::path const& manifest, std::wstring const& id,
        std::wstring const& queue, std::filesystem::path const& output, bool control)
    {
        Require(queue == (control ? L"PSA Diagnostic OXPS (Save As)" : L"Microsoft Print to PDF"), "queue");
        ValidatePath(output, false);
        Require(!std::filesystem::exists(output), "print-output-exists", ERROR_FILE_EXISTS);
        auto source = ReadJson(manifest);
        Require(source.GetNamedString(L"schema") == L"4d-pdf-printer.proof-fixtures", "fixture-schema");
        JsonObject fixture{nullptr};
        for (auto const& entry : source.GetNamedArray(L"fixtures"))
            if (entry.GetObject().GetNamedString(L"id") == id) fixture = entry.GetObject();
        Require(fixture != nullptr, "fixture-id");
        auto report = Record(L"gdi-create-dc");
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        mode.dmFields = DM_PAPERSIZE | DM_ORIENTATION | DM_SCALE;
        mode.dmPaperSize = DMPAPER_A4; mode.dmOrientation = DMORIENT_PORTRAIT; mode.dmScale = 100;
        Dc dc(CreateDCW(L"WINSPOOL", queue.c_str(), nullptr, &mode));
        Check(dc != nullptr, "gdi-create-dc");
        Number(report, L"physical_width_pixels", GetDeviceCaps(dc.get(), PHYSICALWIDTH));
        Number(report, L"physical_height_pixels", GetDeviceCaps(dc.get(), PHYSICALHEIGHT));
        Number(report, L"dpi_x", GetDeviceCaps(dc.get(), LOGPIXELSX));
        Number(report, L"dpi_y", GetDeviceCaps(dc.get(), LOGPIXELSY));
        auto title = L"proof-" + id + L"-" + std::to_wstring(GetTickCount64());
        Text(report, L"title_sha256", winrt::to_hstring(evidence::JobHash(title)).c_str());
        DOCINFOW document{};
        document.cbSize = sizeof(document);
        document.lpszDocName = title.c_str();
        document.lpszOutput = control ? nullptr : output.c_str();
        auto started = evidence::CallStartDoc([&] { return StartDocW(dc.get(), &document); });
        report.Insert(L"start_doc_return", JsonValue::CreateNumberValue(started.rawReturn));
        Number(report, L"start_doc_last_error", started.lastError);
        if (!started.Succeeded())
        {
            Text(report, L"stage", L"gdi-start-doc");
            Text(report, L"result", L"error");
            return report;
        }
        try
        {
            auto pages = fixture.GetNamedArray(L"pages"), hashes = fixture.GetNamedArray(L"source_sha256");
            for (uint32_t index = 0; index < pages.Size(); ++index)
            {
                auto name = std::filesystem::path(std::wstring(pages.GetStringAt(index)));
                Require(name.filename() == name, "fixture-page-name");
                auto path = manifest.parent_path() / name;
                auto pinned = OpenRead(path);
                Require(Hash(pinned.get()) == hashes.GetStringAt(index), "fixture-changed");
                Emf emf(GetEnhMetaFileW(path.c_str()));
                Check(emf != nullptr, "emf-open");
                auto startedPage = evidence::CallStartDoc([&] { return StartPage(dc.get()); });
                Require(startedPage.Succeeded(), "gdi-start-page", startedPage.lastError);
                int x = GetDeviceCaps(dc.get(), PHYSICALOFFSETX), y = GetDeviceCaps(dc.get(), PHYSICALOFFSETY);
                RECT rectangle{-x, -y, GetDeviceCaps(dc.get(), PHYSICALWIDTH) - x,
                    GetDeviceCaps(dc.get(), PHYSICALHEIGHT) - y};
                Check(PlayEnhMetaFile(dc.get(), emf.get(), &rectangle), "emf-replay");
                auto endedPage = evidence::CallStartDoc([&] { return EndPage(dc.get()); });
                Require(endedPage.Succeeded(), "gdi-end-page", endedPage.lastError);
            }
            auto ended = evidence::CallStartDoc([&] { return EndDoc(dc.get()); });
            report.Insert(L"end_doc_return", JsonValue::CreateNumberValue(ended.rawReturn));
            Number(report, L"end_doc_last_error", ended.lastError);
            Require(ended.Succeeded(), "gdi-end-doc", ended.lastError);
            Text(report, L"stage", L"gdi-submitted-not-pipeline-success");
            Text(report, L"result", L"submitted");
        }
        catch (Failure const& error)
        {
            AbortDoc(dc.get());
            Text(report, L"stage", winrt::to_hstring(error.stage).c_str());
            Number(report, L"native_error", error.code);
            Text(report, L"result", L"error");
        }
        catch (...) { AbortDoc(dc.get()); throw; }
        return report;
    }
}
