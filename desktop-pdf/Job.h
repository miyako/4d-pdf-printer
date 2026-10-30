#pragma once
#include "Contract.h"
#include <windows.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.FileProperties.h>
#include <winrt/Windows.Storage.Streams.h>

namespace pdf {
using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Data::Json;
using namespace Windows::Storage;
using namespace Windows::Storage::Streams;
inline bool LocalProvider(StorageProvider const& provider) {
    auto id = provider.Id();
    return _wcsicmp(id.c_str(), L"Computer") == 0 || _wcsicmp(id.c_str(), L"Local") == 0;
}
inline std::uint64_t Now() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}
inline std::wstring RandomId() {
    return std::wstring(Windows::Security::Cryptography::CryptographicBuffer::EncodeToHexString(
        Windows::Security::Cryptography::CryptographicBuffer::GenerateRandom(16)));
}
inline void Text(JsonObject const& r, wchar_t const* key, std::wstring_view value) {
    r.Insert(key, JsonValue::CreateStringValue(hstring(value)));
}
inline void Number(JsonObject const& r, wchar_t const* key, std::uint64_t value) {
    Require(value <= 9007199254740991ull, "JSON integer overflow");
    r.Insert(key, JsonValue::CreateNumberValue(static_cast<double>(value)));
}
inline std::uint64_t Integer(JsonObject const& r, wchar_t const* key) {
    auto n = r.GetNamedNumber(key);
    Require(std::isfinite(n) && n >= 0 && n <= 9007199254740991.0 && std::floor(n) == n, "JSON integer");
    return static_cast<std::uint64_t>(n);
}
inline void Fields(JsonObject const& r, std::initializer_list<wchar_t const*> names) {
    Require(r.Size() == names.size(), "unexpected JSON fields");
    for (auto name : names) Require(r.HasKey(name), "missing JSON field");
}
inline JsonObject Record(std::wstring_view id, Mode mode, std::wstring_view kind) {
    Require(Id(id), "record id");
    JsonObject r;
    Text(r, L"schema", L"4d-pdf-printer.desktop"); Number(r, L"version", 1);
    Text(r, L"id", id); Text(r, L"mode", Name(mode)); Text(r, L"kind", kind);
    return r;
}
inline void Validate(JsonObject const& r, std::wstring_view id, Mode mode, std::wstring_view kind) {
    Require(r.GetNamedString(L"schema") == L"4d-pdf-printer.desktop" &&
        Integer(r, L"version") == 1 && r.GetNamedString(L"id") == id &&
        r.GetNamedString(L"mode") == Name(mode) && r.GetNamedString(L"kind") == kind, "record binding");
}
inline JsonObject Parse(std::string_view wire, std::size_t limit = RecordBytes) {
    JsonWire(wire, limit).Check();
    return JsonObject::Parse(to_hstring(wire));
}
inline void Failure(wchar_t const* stage, hresult code) noexcept {
    wchar_t text[256]{};
    swprintf_s(text, L"Desktop PDF Integration: %s HRESULT=0x%08X\n", stage,
        static_cast<unsigned>(code.value));
    OutputDebugStringW(text);
#ifndef PDF_APP
    fwprintf(stderr, L"%s", text);
#endif
}
inline IAsyncOperation<StorageFolder> Root() {
    auto package = Windows::ApplicationModel::Package::Current().Id();
    Require(package.Name() == L"Miyako.DesktopPdfIntegration" &&
        package.Publisher() == L"CN=DesktopPdfPrototypeDev", "integration package identity");
    co_return co_await ApplicationData::Current().LocalCacheFolder().CreateFolderAsync(
        L"DesktopPdf", CreationCollisionOption::OpenIfExists);
}
inline IAsyncOperation<StorageFolder> Folder(wchar_t const* group, std::wstring id, bool create = false) {
    Require(Id(id), "folder id");
    auto root = co_await Root();
    auto parent = co_await root.CreateFolderAsync(group, CreationCollisionOption::OpenIfExists);
    if (create) co_return co_await parent.CreateFolderAsync(id, CreationCollisionOption::FailIfExists);
    co_return co_await parent.GetFolderAsync(id);
}
inline IAsyncAction Publish(StorageFolder folder, hstring name, JsonObject record) {
    auto bytes = to_string(record.Stringify());
    JsonWire(bytes).Check();
    auto file = co_await folder.CreateFileAsync(L"pending-" + hstring(RandomId()), CreationCollisionOption::FailIfExists);
    co_await FileIO::WriteTextAsync(file, record.Stringify(), UnicodeEncoding::Utf8);
    co_await file.RenameAsync(name, NameCollisionOption::FailIfExists);
}
inline IAsyncOperation<JsonObject> Read(StorageFolder folder, hstring name, std::uint64_t limit = RecordBytes) {
    auto file = co_await folder.GetFileAsync(name);
    auto properties = co_await file.GetBasicPropertiesAsync();
    Require(limit <= RuntimeRecordBytes && properties.Size() > 0 && properties.Size() <= limit, "record size");
    co_return Parse(to_string(co_await FileIO::ReadTextAsync(file)), static_cast<std::size_t>(limit));
}
inline IAsyncOperation<bool> Exists(StorageFolder folder, hstring name) {
    co_return (co_await folder.TryGetItemAsync(name)) != nullptr;
}
inline void RegisteredRecord(JsonObject const& r, std::wstring_view id) {
    Validate(r, id, Mode::Silent, L"registered");
    Fields(r, {L"schema", L"version", L"id", L"mode", L"kind", L"destination", L"expires", L"timeout"});
    Require(LocalPath(std::wstring_view(r.GetNamedString(L"destination"))) && Integer(r, L"expires") > 0 &&
        Integer(r, L"timeout") > 0 && Integer(r, L"timeout") <= 86400000, "registration values");
}
inline std::uint64_t SubmissionDeadline(JsonObject const& submitted, JsonObject const& registered,
    std::wstring_view id) {
    RegisteredRecord(registered, id);
    Validate(submitted, id, Mode::Silent, L"submitted");
    Fields(submitted, {L"schema", L"version", L"id", L"mode", L"kind", L"began", L"deadline"});
    auto began = Integer(submitted, L"began");
    auto deadline = Deadline(began, Integer(registered, L"timeout"));
    Require(began < Integer(registered, L"expires") && began <= Now() &&
        deadline == Integer(submitted, L"deadline"), "invalid submission timeline");
    return deadline;
}
inline std::wstring BoundJob(JsonObject const& binding, std::wstring_view registration) {
    Validate(binding, registration, Mode::Silent, L"binding");
    Fields(binding, {L"schema", L"version", L"id", L"mode", L"kind", L"job"});
    std::wstring id(binding.GetNamedString(L"job"));
    Require(Id(id), "bound job id");
    return id;
}
inline IAsyncOperation<std::uint64_t> Copy(IInputStream input, IOutputStream output,
    std::function<bool()> stop, std::function<void(IBuffer const&)> observe = {}) {
    Buffer buffer(65536);
    std::uint64_t total = 0;
    for (;;) {
        if (stop()) throw hresult_canceled();
        auto read = co_await input.ReadAsync(buffer, buffer.Capacity(), InputStreamOptions::Partial);
        if (stop()) throw hresult_canceled();
        if (!read.Length()) break;
        total += read.Length();
        Require(total <= CaptureBytes, "copy byte limit");
        if (observe) observe(read);
        Require(co_await output.WriteAsync(read) == read.Length(), "short write");
    }
    Require(total > 0 && co_await output.FlushAsync(), "empty copy/flush failure");
    co_return total;
}
inline std::uint64_t WorkerSuccess(std::string_view wire, unsigned long exit) {
    auto r = Parse(wire);
    Fields(r, {L"schema", L"version", L"worker_version", L"result", L"exit_code", L"stage",
        L"error", L"document", L"page", L"documents", L"pages_completed", L"output_bytes",
        L"output_published", L"cleanup_complete"});
    Require(exit == 0 && Integer(r, L"exit_code") == exit &&
        r.GetNamedString(L"schema") == L"4d-pdf-printer.worker-status" &&
        Integer(r, L"version") == 1 && !r.GetNamedString(L"worker_version").empty() &&
        r.GetNamedString(L"result") == L"ok" && r.GetNamedString(L"stage") == L"complete" &&
        r.GetNamedValue(L"error").ValueType() == JsonValueType::Null &&
        r.GetNamedValue(L"document").ValueType() == JsonValueType::Null &&
        r.GetNamedValue(L"page").ValueType() == JsonValueType::Null &&
        Integer(r, L"documents") > 0 && Integer(r, L"pages_completed") > 0 &&
        r.GetNamedBoolean(L"output_published") && r.GetNamedBoolean(L"cleanup_complete"),
        "worker failed/indeterminate; staging requires reconciliation");
    auto size = Integer(r, L"output_bytes");
    Require(size > 0 && size <= CaptureBytes, "worker PDF size");
    return size;
}
}
