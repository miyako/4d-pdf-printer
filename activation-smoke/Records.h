#pragma once
#include "Contract.h"
#include <windows.h>
#include <cstdio>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.FileProperties.h>
#include <winrt/Windows.Storage.Streams.h>

namespace smoke
{
    using namespace winrt;
    using namespace Windows::Data::Json;
    using namespace Windows::Storage;
    using namespace Windows::Foundation;
    inline void Text(JsonObject const& r, wchar_t const* key, std::wstring_view value)
    { r.Insert(key, JsonValue::CreateStringValue(hstring(value))); }
    inline void Flag(JsonObject const& r, wchar_t const* key, bool value)
    { r.Insert(key, JsonValue::CreateBooleanValue(value)); }
    inline void Number(JsonObject const& r, wchar_t const* key, double value)
    { r.Insert(key, JsonValue::CreateNumberValue(value)); }
    inline void Failure(wchar_t const* stage, hresult hr) noexcept
    {
        wchar_t message[512]{};
        swprintf_s(message, L"PSA activation smoke: API/stage=%s HRESULT=0x%08X\n",
            stage, static_cast<unsigned>(hr.value));
        OutputDebugStringW(message);
#ifndef SMOKE_APP
        fwprintf(stderr, L"%s", message);
#endif
    }
    inline JsonObject Record(std::wstring_view id, wchar_t const* kind)
    {
        JsonObject r;
        Text(r, L"testId", id); Text(r, L"kind", kind);
        Number(r, L"pid", GetCurrentProcessId()); Flag(r, L"pdfProduced", false);
        return r;
    }
    inline IAsyncOperation<StorageFolder> Folder(std::wstring id, bool fresh = false)
    {
        if (!ValidId(std::wstring_view(id))) throw hresult_invalid_argument();
        wchar_t const* stage = L"ApplicationData.Current.LocalCacheFolder/CreateFolderAsync(ActivationSmoke)";
        try
        {
            auto root = co_await ApplicationData::Current().LocalCacheFolder().CreateFolderAsync(
                L"ActivationSmoke", CreationCollisionOption::OpenIfExists);
            stage = L"LocalCache CreateFolderAsync(test-ID)";
            co_return co_await root.CreateFolderAsync(id, fresh ?
                CreationCollisionOption::FailIfExists : CreationCollisionOption::OpenIfExists);
        }
        catch (...) { Failure(stage, to_hresult()); throw; }
    }
    inline IAsyncAction Publish(StorageFolder folder, hstring name, JsonObject record)
    {
        using namespace Windows::Security::Cryptography;
        auto bytes = CryptographicBuffer::ConvertStringToBinary(record.Stringify(), BinaryStringEncoding::Utf8);
        if (bytes.Length() > 16384) throw hresult_invalid_argument();
        wchar_t const* stage = L"StorageFolder.CreateFileAsync(create-new pending)";
        try
        {
            auto file = co_await folder.CreateFileAsync(name + L".pending", CreationCollisionOption::FailIfExists);
            stage = L"FileIO.WriteBufferAsync(UTF8/no BOM)";
            co_await FileIO::WriteBufferAsync(file, bytes);
            stage = L"StorageFile.RenameAsync(fail-if-exists)";
            co_await file.RenameAsync(name, NameCollisionOption::FailIfExists);
        }
        catch (...) { Failure(stage, to_hresult()); throw; }
    }
    inline void Validate(JsonObject const& r, std::wstring_view id, wchar_t const* kind)
    {
        if (r.GetNamedString(L"testId") != id || r.GetNamedString(L"kind") != kind ||
            r.GetNamedBoolean(L"pdfProduced")) throw hresult_invalid_argument();
    }
    inline JsonObject Read(StorageFolder const& folder, wchar_t const* name,
        std::wstring_view id, wchar_t const* kind)
    {
        auto file = folder.GetFileAsync(name).get();
        auto size = file.GetBasicPropertiesAsync().get().Size();
        if (!size || size > 16384) throw hresult_invalid_argument();
        auto r = JsonObject::Parse(FileIO::ReadTextAsync(file).get());
        Validate(r, id, kind);
        return r;
    }
}
