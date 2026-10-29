#include "../proof/PhysicalMapping.h"
#include <cstdio>
#include <initializer_list>
#include <stdexcept>

namespace
{
    void Expect(bool value, char const* message)
    {
        if (!value) throw std::runtime_error(message);
    }

    void CheckMetrics()
    {
        auto mapping = proof::MappingFromReference(2880, 1800, 762, 476);
        Expect(mapping && mapping->windowX == 76200 && mapping->windowY == 47600 &&
            mapping->viewportX == 2880 && mapping->viewportY == 1800, "observed-reference-mapping");
        auto other = proof::MappingFromReference(1920, 1080, 320, 180);
        Expect(other && other->windowX == 32000 && other->windowY == 18000 &&
            other->viewportX == 1920 && other->viewportY == 1080, "different-reference-mapping");
        for (int bad : {0, -1, std::numeric_limits<int>::min()})
        {
            Expect(!proof::MappingFromReference(bad, 1800, 762, 476), "invalid-reference-pixels-x");
            Expect(!proof::MappingFromReference(2880, bad, 762, 476), "invalid-reference-pixels-y");
            Expect(!proof::MappingFromReference(2880, 1800, bad, 476), "invalid-reference-mm-x");
            Expect(!proof::MappingFromReference(2880, 1800, 762, bad), "invalid-reference-mm-y");
        }
        constexpr int maximum = std::numeric_limits<int>::max() / 100;
        auto boundary = proof::MappingFromReference(1, 1, maximum, maximum);
        Expect(boundary && boundary->windowX == maximum * 100, "reference-overflow-boundary");
        Expect(!proof::MappingFromReference(1, 1, maximum + 1, 1) &&
            !proof::MappingFromReference(1, 1, 1, maximum + 1), "reference-mm-overflow");
    }
}

#ifndef _WIN32
int main()
{
    try { CheckMetrics(); std::puts("Portable metric guards passed; NOT actual Windows EMF evidence."); return 0; }
    catch (std::exception const& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
#else
#include "../proof/Fixtures.h"
#include <objbase.h>
#include <array>
#include <exception>
#include <memory>
#include <vector>

using namespace proof;

namespace
{
    struct DeleteEmf { void operator()(std::remove_pointer_t<HENHMETAFILE>* value) const { if (value) DeleteEnhMetaFile(value); } };
    using Emf = std::unique_ptr<std::remove_pointer_t<HENHMETAFILE>, DeleteEmf>;

    template<typename T> T const& RecordAs(ENHMETARECORD const* record)
    {
        Require(record->nSize >= sizeof(T), "fixture-test-record-size");
        return *reinterpret_cast<T const*>(record);
    }

    struct PageRecords
    {
        ENHMETAHEADER header{};
        SIZEL window{}, viewport{};
        DWORD mapMode = 0;
        unsigned rectangles = 0, ellipses = 0, lines = 0, gradients = 0, alpha = 0, images = 0;
        std::vector<std::wstring> text;
        std::exception_ptr error;

        void CheckMapping() const
        {
            auto reference = MappingFromReference(header.szlDevice.cx, header.szlDevice.cy,
                header.szlMillimeters.cx, header.szlMillimeters.cy);
            Require(reference.has_value() && mapMode == MM_ANISOTROPIC &&
                window.cx > 0 && window.cy > 0 && viewport.cx > 0 && viewport.cy > 0,
                "fixture-test-map-mode");
            Require(static_cast<int64_t>(window.cx) * reference->viewportX ==
                static_cast<int64_t>(viewport.cx) * reference->windowX &&
                static_cast<int64_t>(window.cy) * reference->viewportY ==
                static_cast<int64_t>(viewport.cy) * reference->windowY, "fixture-test-header-map-scale");
        }
        void CheckBounds(RECTL const& bounds) const
        {
            auto reference = MappingFromReference(header.szlDevice.cx, header.szlDevice.cy,
                header.szlMillimeters.cx, header.szlMillimeters.cy);
            Require(reference.has_value(), "fixture-test-header-metrics");
            Require(bounds.left >= 0 && bounds.top >= 0 && bounds.right >= bounds.left &&
                bounds.bottom >= bounds.top &&
                static_cast<int64_t>(bounds.right) * reference->windowX <=
                    static_cast<int64_t>(header.rclFrame.right) * reference->viewportX &&
                static_cast<int64_t>(bounds.bottom) * reference->windowY <=
                    static_cast<int64_t>(header.rclFrame.bottom) * reference->viewportY,
                "fixture-test-content-outside-page");
        }
    };

    int CALLBACK Inspect(HDC, HANDLETABLE*, ENHMETARECORD const* record, int, LPARAM parameter)
    {
        auto& page = *reinterpret_cast<PageRecords*>(parameter);
        try
        {
            switch (record->iType)
            {
            case EMR_SETMAPMODE: page.mapMode = RecordAs<EMRSETMAPMODE>(record).iMode; break;
            case EMR_SETWINDOWEXTEX: page.window = RecordAs<EMRSETWINDOWEXTEX>(record).szlExtent; break;
            case EMR_SETVIEWPORTEXTEX: page.viewport = RecordAs<EMRSETVIEWPORTEXTEX>(record).szlExtent; break;
            case EMR_EXTTEXTOUTW:
            {
                page.CheckMapping();
                auto const& value = RecordAs<EMREXTTEXTOUTW>(record);
                page.CheckBounds(value.rclBounds);
                Require(!(value.emrtext.fOptions & ETO_GLYPH_INDEX) &&
                    value.emrtext.offString % alignof(wchar_t) == 0 &&
                    value.emrtext.offString >= sizeof(EMREXTTEXTOUTW) &&
                    static_cast<uint64_t>(value.emrtext.offString) +
                        static_cast<uint64_t>(value.emrtext.nChars) * sizeof(wchar_t) <= record->nSize,
                    "fixture-test-text-record");
                auto text = reinterpret_cast<wchar_t const*>(
                    reinterpret_cast<unsigned char const*>(record) + value.emrtext.offString);
                page.text.emplace_back(text, value.emrtext.nChars);
                break;
            }
            case EMR_RECTANGLE: page.CheckMapping(); ++page.rectangles; break;
            case EMR_ELLIPSE: page.CheckMapping(); ++page.ellipses; break;
            case EMR_LINETO: page.CheckMapping(); ++page.lines; break;
            case EMR_GRADIENTFILL: page.CheckMapping(); ++page.gradients; break;
            case EMR_ALPHABLEND: page.CheckMapping(); ++page.alpha; break;
            case EMR_STRETCHDIBITS: page.CheckMapping(); ++page.images; break;
            default: break;
            }
            return 1;
        }
        catch (...) { page.error = std::current_exception(); return 0; }
    }

    JsonObject InspectPage(std::filesystem::path const& path, std::wstring const& id, int number)
    {
        Emf metafile(GetEnhMetaFileW(path.c_str()));
        Check(metafile != nullptr, "fixture-test-open");
        PageRecords page;
        Require(GetEnhMetaFileHeader(metafile.get(), sizeof(page.header), &page.header) == sizeof(page.header) &&
            page.header.dSignature == ENHMETA_SIGNATURE &&
            page.header.rclFrame.left == 0 && page.header.rclFrame.top == 0 &&
            page.header.rclFrame.right == 21000 && page.header.rclFrame.bottom == 29700,
            "fixture-test-frame");
        BOOL enumerated = EnumEnhMetaFile(nullptr, metafile.get(), Inspect, &page, nullptr);
        if (page.error) std::rethrow_exception(page.error);
        Check(enumerated, "fixture-test-enumerate");
        page.CheckMapping();
        page.CheckBounds(page.header.rclBounds);
        std::wstring heading = L"Original PDF product proof: " + id + L" page " + std::to_wstring(number);
        Require(!page.text.empty() && page.text.front() == heading, "fixture-test-full-heading");
        int bottom = 0;
        if (id == L"text")
        {
            Require(page.text.size() == 21 && page.rectangles == 1, "fixture-test-text-content");
            for (size_t index = 1; index < page.text.size(); ++index)
                Require(page.text[index] == L"Selectable text 0123456789 \u03a9 \u03b1 \u0416",
                    "fixture-test-full-body-text");
            bottom = 18000;
        }
        else if (id == L"vectors")
        {
            Require(page.text.size() == 2 && page.text[1] == L"Vector clipping and gradient" &&
                page.ellipses == 9 && page.lines == 8 && page.gradients == 1 && page.alpha == 1,
                "fixture-test-vector-content");
            bottom = 24000;
        }
        else
        {
            Require(page.text.size() == 2 && page.text[1] == L"Original image and selectable text" &&
                page.images == 1 && page.rectangles == 1 && page.ellipses == 1,
                "fixture-test-mixed-content");
            bottom = 22000;
        }
        // Inclusive device bounds can round the logical edge by a reference pixel.
        Require((static_cast<int64_t>(page.header.rclBounds.bottom) + 2) * page.window.cy >=
            static_cast<int64_t>(bottom) * page.viewport.cy, "fixture-test-lower-content-bounds");
        JsonObject observation;
        Text(observation, L"id", id); Number(observation, L"page", static_cast<uint64_t>(number));
        Number(observation, L"reference_pixels_x", page.header.szlDevice.cx);
        Number(observation, L"reference_pixels_y", page.header.szlDevice.cy);
        Number(observation, L"reference_mm_x", page.header.szlMillimeters.cx);
        Number(observation, L"reference_mm_y", page.header.szlMillimeters.cy);
        Number(observation, L"window_x", page.window.cx); Number(observation, L"window_y", page.window.cy);
        Number(observation, L"viewport_x", page.viewport.cx); Number(observation, L"viewport_y", page.viewport.cy);
        Number(observation, L"content_right_pixels", page.header.rclBounds.right);
        Number(observation, L"content_bottom_pixels", page.header.rclBounds.bottom);
        Number(observation, L"text_records", page.text.size());
        Flag(observation, L"full_heading", true); Flag(observation, L"intended_graphics", true);
        Flag(observation, L"physical_bounds_inside_frame", true);
        return observation;
    }
}

int wmain()
{
    std::filesystem::path root;
    JsonObject receipt{nullptr};
    int result = 1;
    try
    {
        winrt::init_apartment();
        USHORT process = 0, native = 0;
        Check(IsWow64Process2(GetCurrentProcess(), &process, &native), "fixture-test-native");
        Require(process == IMAGE_FILE_MACHINE_UNKNOWN && native ==
#if defined(_M_ARM64)
            IMAGE_FILE_MACHINE_ARM64,
#else
            IMAGE_FILE_MACHINE_AMD64,
#endif
            "fixture-test-native");
        CheckMetrics();
        std::array<wchar_t, MAX_PATH> temporary{};
        DWORD length = GetTempPathW(static_cast<DWORD>(temporary.size()), temporary.data());
        Require(length > 0 && length < temporary.size(), "fixture-test-temporary");
        GUID id{};
        winrt::check_hresult(CoCreateGuid(&id));
        std::array<wchar_t, 40> name{};
        Require(StringFromGUID2(id, name.data(), static_cast<int>(name.size())) > 0, "fixture-test-guid");
        auto candidate = std::filesystem::path(temporary.data()) / (L"pdf-fixture-test-" + std::wstring(name.data()));
        Require(!std::filesystem::exists(candidate), "fixture-test-new-directory");
        auto generated = CreateFixtures(candidate);
        root = candidate;
        receipt = JsonObject{};
        Text(receipt, L"schema", L"4d-pdf-printer.fixture-generation-unit");
        Number(receipt, L"version", 1); Flag(receipt, L"printing_performed", false);
        Flag(receipt, L"productUsable", false);
        JsonArray pages;
        for (auto const& value : generated.GetNamedArray(L"fixtures"))
        {
            auto fixture = value.GetObject();
            auto files = fixture.GetNamedArray(L"pages");
            auto hashes = fixture.GetNamedArray(L"source_sha256");
            Require(files.Size() == 2 && hashes.Size() == 2, "fixture-test-page-count");
            for (uint32_t index = 0; index < files.Size(); ++index)
            {
                auto path = root / std::wstring(files.GetStringAt(index));
                Require(Hash(path) == hashes.GetStringAt(index), "fixture-test-manifest-hash");
                pages.Append(InspectPage(path, std::wstring(fixture.GetNamedString(L"id")),
                    static_cast<int>(index + 1)));
            }
        }
        Require(pages.Size() == 6, "fixture-test-six-pages");
        receipt.Insert(L"pages", pages); Text(receipt, L"result", L"passed");
        result = 0;
    }
    catch (Failure const& error) { std::fprintf(stderr, "Fixture unit failed: %s\n", error.stage); }
    catch (std::exception const& error) { std::fprintf(stderr, "Fixture unit failed: %s\n", error.what()); }
    catch (...) { std::fputs("Fixture unit failed: Windows API error.\n", stderr); }
    if (!root.empty())
    {
        try
        {
            for (auto const& entry : std::filesystem::directory_iterator(root))
                Require(std::filesystem::remove(entry.path()), "fixture-test-file-cleanup");
            Require(std::filesystem::remove(root), "fixture-test-directory-cleanup");
        }
        catch (...) { std::fputs("Fixture unit temporary cleanup failed.\n", stderr); return 1; }
    }
    if (result == 0)
    {
        Flag(receipt, L"cleanup_complete", true);
        std::puts(winrt::to_string(receipt.Stringify()).c_str());
    }
    return result;
}
#endif
