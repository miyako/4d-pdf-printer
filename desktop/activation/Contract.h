#pragma once
#include "Protocol.h"
#include "../common/Common.h"
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.FileProperties.h>

namespace activation
{
    using namespace desktop;
    using namespace winrt;
    using namespace Windows::Storage;
    inline void Require(bool value, HRESULT error = E_INVALIDARG)
    { if (!value) throw hresult_error(error); }
    inline uint64_t Now()
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    }
    inline void Number(JsonObject const& value, wchar_t const* key, uint64_t number)
    { value.Insert(key, JsonValue::CreateNumberValue(static_cast<double>(number))); }
    inline void Flag(JsonObject const& value, wchar_t const* key, bool flag)
    { value.Insert(key, JsonValue::CreateBooleanValue(flag)); }
    inline JsonObject Record(wchar_t const* stage)
    {
        JsonObject value;
        Text(value, L"schema", L"4d-pdf-printer.activation-probe");
        Number(value, L"version", 1); Text(value, L"stage", stage);
#if defined(_M_ARM64)
        Text(value, L"process_architecture", L"arm64");
#elif defined(_M_X64)
        Text(value, L"process_architecture", L"x64");
#endif
        Number(value, L"process_id", GetCurrentProcessId());
        Flag(value, L"pdfProduced", false); Flag(value, L"activationGatePassed", false);
        return value;
    }
    inline uint64_t Integer(JsonObject const& value, wchar_t const* key)
    {
        auto item = value.GetNamedValue(key);
        Require(item.ValueType() == Windows::Data::Json::JsonValueType::Number);
        auto number = item.GetNumber();
        Require(number >= 0 && number <= 9007199254740991.0 &&
            static_cast<double>(static_cast<uint64_t>(number)) == number);
        return static_cast<uint64_t>(number);
    }
    inline std::wstring UserHash(Identity const& identity)
    {
        std::wstring bytes;
        constexpr wchar_t hex[] = L"0123456789abcdef";
        for (auto byte : identity.user) { bytes += hex[byte >> 4]; bytes += hex[byte & 15]; }
        return Hash(bytes);
    }
    struct Context
    {
        Identity identity;
        std::wstring userHash, packageHash, family;
        StorageFolder cache{nullptr};
    };
    inline Context Current(bool appContainer)
    {
        Context context;
        context.identity = CurrentIdentity();
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
        if (!appContainer)
        {
            HANDLE token = nullptr;
            Check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) != FALSE);
            handle owned(token);
            auto elevation = TokenData(token, TokenElevation);
            Require(reinterpret_cast<TOKEN_ELEVATION*>(elevation.data())->TokenIsElevated == 0, E_ACCESSDENIED);
            USHORT processMachine = 0, nativeMachine = 0;
            Check(IsWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine) != FALSE);
            Require(processMachine == IMAGE_FILE_MACHINE_UNKNOWN, E_ACCESSDENIED);
        }
#endif
        auto package = Windows::ApplicationModel::Package::Current().Id();
        Require(package.Name() == L"Miyako.DesktopPdfPrototype" &&
            package.Publisher() == L"CN=DesktopPdfPrototypeDev" &&
            context.identity.appContainer == appContainer && context.identity.session != 0,
            E_ACCESSDENIED);
        context.family = std::wstring(package.FamilyName());
        context.userHash = UserHash(context.identity);
        context.packageHash = Hash(std::wstring(package.FullName()));
        context.cache = ApplicationData::Current().LocalCacheFolder();
        return context;
    }
    inline JsonObject Bound(Context const& context, std::wstring const& id, wchar_t const* kind,
        uint64_t created, uint64_t expires)
    {
        Require(Token(id));
        auto value = Record(kind);
        Text(value, L"kind", kind); Text(value, L"job_id", id);
        Text(value, L"user_hash", context.userHash); Text(value, L"package_hash", context.packageHash);
        Number(value, L"session", context.identity.session);
        Number(value, L"created_ms", created); Number(value, L"expires_ms", expires);
        return value;
    }
    inline void Validate(JsonObject const& value, Context const& context,
        std::wstring const& id, wchar_t const* kind, uint64_t now)
    {
        Require(Token(id) && value.GetNamedString(L"schema") == L"4d-pdf-printer.activation-probe" &&
            Integer(value, L"version") == 1 && value.GetNamedString(L"kind") == kind &&
            value.GetNamedString(L"job_id") == id && value.GetNamedString(L"user_hash") == context.userHash &&
            value.GetNamedString(L"package_hash") == context.packageHash &&
            Integer(value, L"session") == context.identity.session &&
            Fresh(Integer(value, L"created_ms"), Integer(value, L"expires_ms"), now), E_ACCESSDENIED);
    }
}
