#pragma once
#include "Contract.h"

namespace pdf::activation {
enum class ReportStage { None, Resolve, Create, Write, Flush, Close, Rename, Complete };
struct ReportResult {
    ReportStage stage{ReportStage::None};
    std::uint32_t error{};
    bool Published() const noexcept { return stage == ReportStage::Complete && error == 0; }
};
template<typename Backend> ReportResult PublishTerminal(Backend& backend, std::string_view bytes) {
    JsonWire(bytes, 4096).Check();
    auto error = backend.Create();
    if (error) return {ReportStage::Create, error};
    std::uint32_t written{};
    error = backend.Write(bytes, written);
    ReportResult result{ReportStage::Write, error ? error : written != bytes.size() ? 29u : 0u};
    if (!result.error) {
        result = {ReportStage::Flush, backend.Flush()};
    }
    auto closeError = backend.Close();
    if (!result.error) result = {ReportStage::Close, closeError};
    if (result.error) return result;
    error = backend.Rename();
    return {error ? ReportStage::Rename : ReportStage::Complete, error};
}
}

#ifdef _WIN32
#include "Job.h"
#include <fileapifromapp.h>

namespace pdf::activation {
inline StorageFolder JournalCache() {
    auto package = Windows::ApplicationModel::Package::Current().Id();
    Require(package.Name() == L"Miyako.DesktopPdfIntegration" &&
        package.Publisher() == L"CN=DesktopPdfPrototypeDev", "integration package identity");
    return ApplicationData::Current().LocalCacheFolder();
}
inline std::wstring TerminalCachePath() {
    return std::wstring(JournalCache().Path());
}
class NativeReportBackend {
    std::wstring parent, partial, final;
    handle file;
public:
    NativeReportBackend(std::wstring root, std::wstring_view id) : parent(std::move(root)) {
        Require(Id(id) && !parent.empty(), "terminal report path binding");
        auto stem = parent + L"\\DesktopPdfJournal-" + std::wstring(id);
        partial = stem + L".partial"; final = stem + L".json";
    }
    std::uint32_t Create() {
        WIN32_FILE_ATTRIBUTE_DATA attributes{};
        if (!GetFileAttributesExFromAppW(parent.c_str(), GetFileExInfoStandard, &attributes)) return GetLastError();
        if (!(attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (attributes.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return ERROR_NOT_SUPPORTED;
        CREATEFILE2_EXTENDED_PARAMETERS options{};
        options.dwSize = sizeof(options); options.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
        options.dwFileFlags = FILE_FLAG_OPEN_REPARSE_POINT;
        auto raw = CreateFile2FromAppW(partial.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE,
            CREATE_NEW, &options);
        if (raw == INVALID_HANDLE_VALUE) return GetLastError();
        file.attach(raw);
        return 0;
    }
    std::uint32_t Write(std::string_view bytes, std::uint32_t& written) {
        DWORD count{};
        auto ok = WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr);
        written = count;
        return ok ? 0u : GetLastError();
    }
    std::uint32_t Flush() { return FlushFileBuffers(file.get()) ? 0u : GetLastError(); }
    std::uint32_t Close() {
        auto raw = file.detach();
        return CloseHandle(raw) ? 0u : GetLastError();
    }
    std::uint32_t Rename() { return MoveFileFromAppW(partial.c_str(), final.c_str()) ? 0u : GetLastError(); }
};
}
#endif
