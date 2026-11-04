#pragma once
#include <filesystem>
#include <vector>

namespace pdf::tests {
// Test-only local paths: reject Win32 aliases before any filesystem mutation.
inline bool TrialPathText(std::wstring_view path) {
    if (path.size() < 4 || path.size() > 200 ||
        !((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) ||
        path.substr(1, 2) != L":\\" || path.back() == L'\\') return false;
    for (std::size_t begin = 3; begin < path.size();) {
        auto end = path.find(L'\\', begin);
        if (end == std::wstring_view::npos) end = path.size();
        auto part = path.substr(begin, end - begin);
        if (part.empty() || part == L"." || part == L".." || part.back() == L'.' || part.back() == L' ') return false;
        std::wstring stem;
        for (auto c : part) {
            if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                (c >= L'0' && c <= L'9') || c == L'_' || c == L'-' || c == L' ' || c == L'.')) return false;
            if (c >= L'a' && c <= L'z') c = static_cast<wchar_t>(c - (L'a' - L'A'));
            stem += c;
        }
        stem.resize(stem.find(L'.') == std::wstring::npos ? stem.size() : stem.find(L'.'));
        if (stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL" ||
            (stem.size() == 4 && (stem.starts_with(L"COM") || stem.starts_with(L"LPT")) &&
                stem[3] >= L'0' && stem[3] <= L'9')) return false;
        begin = end + 1;
    }
    return true;
}
inline void TrialPathChecks() {
    Require(TrialPathText(L"C:\\private\\fresh-test_01"), "ordinary explicit local trial path");
    for (auto path : {L"", L"C:\\", L"relative", L"C:relative", L"\\\\server\\share\\trial",
        L"\\\\?\\C:\\trial", L"C:/trial", L"C:\\a\\..\\trial", L"C:\\a\\.\\trial",
        L"C:\\a\\\\trial", L"C:\\trial\\", L"C:\\trial.", L"C:\\trial ", L"C:\\trial:stream",
        L"C:\\NUL.txt", L"C:\\COM1", L"C:\\a\\LPT2.log"}) {
        Require(!TrialPathText(path), "reject ambiguous/nonlocal trial path");
    }
}

class FreshTrialRoot {
    std::vector<handle> ancestors;
    void Hold(std::filesystem::path const& ancestor) {
        handle directory(CreateFileW(ancestor.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        check_bool(static_cast<bool>(directory));
        FILE_ATTRIBUTE_TAG_INFO info{};
        check_bool(GetFileInformationByHandleEx(directory.get(), FileAttributeTagInfo, &info, sizeof(info)));
        Require((info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            !(info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT), "plain test root ancestor");
        ancestors.push_back(std::move(directory));
    }
public:
    std::filesystem::path const path;
    explicit FreshTrialRoot(std::wstring value) : path(std::move(value)) {
        Require(TrialPathText(path.wstring()), "explicit absolute local test path");
        Require(GetDriveTypeW(path.root_path().c_str()) == DRIVE_FIXED, "fixed local test drive");
        auto current = path.root_path();
        Hold(current);
        for (auto const& component : path.parent_path().relative_path()) {
            current /= component;
            Hold(current);
        }
        Require(std::filesystem::create_directory(path), "test root must not exist; no retry or reuse");
        Hold(path);
    }
    std::filesystem::path NewCase() const {
        auto child = path / (L"case-" + RandomId());
        Require(std::filesystem::create_directory(child), "fresh retained test case");
        return child;
    }
    // Handles prevent ancestor rename/deletion while this scope lives. Never remove owned trees.
};
}
