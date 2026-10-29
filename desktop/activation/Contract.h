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
    inline void Guard(bool value, HRESULT error, Diagnostic* diagnostic, wchar_t const* reason)
    {
        Point(diagnostic, L"validation", L"validation", reason);
        Require(value, error);
    }
    inline void CheckResult(HRESULT result, Diagnostic* diagnostic)
    {
        if (FAILED(result)) ErrorCode(diagnostic, static_cast<uint32_t>(result), L"hresult");
        check_hresult(result);
    }
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
    inline uint64_t Integer(JsonObject const& value, wchar_t const* key, Diagnostic* diagnostic = nullptr)
    {
        Point(diagnostic, L"JsonObject.GetNamedValue", L"winrt");
        auto item = value.GetNamedValue(key);
        Point(diagnostic, L"JsonValue.ValueType", L"winrt");
        auto type = item.ValueType();
        Guard(type == Windows::Data::Json::JsonValueType::Number, E_INVALIDARG, diagnostic, L"integer-type");
        Point(diagnostic, L"JsonValue.GetNumber", L"winrt");
        auto number = item.GetNumber();
        Guard(number >= 0 && number <= 9007199254740991.0 &&
            static_cast<double>(static_cast<uint64_t>(number)) == number, E_INVALIDARG, diagnostic, L"integer-range");
        return static_cast<uint64_t>(number);
    }
    inline std::wstring UserHash(Identity const& identity, Diagnostic* diagnostic = nullptr)
    {
        Point(diagnostic, L"string-allocation", L"cpp");
        std::wstring bytes;
        constexpr wchar_t hex[] = L"0123456789abcdef";
        for (auto byte : identity.user) { bytes += hex[byte >> 4]; bytes += hex[byte & 15]; }
        return Hash(bytes, diagnostic);
    }
    struct Context
    {
        Identity identity;
        std::wstring userHash, packageHash, family;
        StorageFolder cache{nullptr};
    };
    inline Context Current(bool appContainer, Diagnostic* diagnostic = nullptr)
    {
        Context context;
        context.identity = CurrentIdentity(diagnostic);
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
        if (!appContainer)
        {
            HANDLE token = nullptr;
            Step(diagnostic, L"current.token", L"process-token");
            Point(diagnostic, L"OpenProcessToken", L"win32");
            Check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) != FALSE, diagnostic);
            desktop::handle owned(token);
            auto elevation = TokenData(token, TokenElevation, diagnostic);
            Step(diagnostic, L"current.guard", L"process-token");
            Guard(reinterpret_cast<TOKEN_ELEVATION*>(elevation.data())->TokenIsElevated == 0,
                E_ACCESSDENIED, diagnostic, L"not-elevated");
            USHORT processMachine = 0, nativeMachine = 0;
            Point(diagnostic, L"IsWow64Process2", L"win32");
            Check(IsWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine) != FALSE, diagnostic);
            Guard(processMachine == IMAGE_FILE_MACHINE_UNKNOWN, E_ACCESSDENIED, diagnostic, L"native-machine");
        }
#endif
        Step(diagnostic, L"current.package", L"package");
        Point(diagnostic, L"Package.Current", L"winrt");
        auto current = Windows::ApplicationModel::Package::Current();
        Point(diagnostic, L"Package.Id", L"winrt");
        auto package = current.Id();
        Point(diagnostic, L"PackageId.Name", L"winrt");
        auto name = package.Name();
        Guard(name == L"Miyako.DesktopPdfPrototype", E_ACCESSDENIED, diagnostic, L"package-name");
        Point(diagnostic, L"PackageId.Publisher", L"winrt");
        auto publisher = package.Publisher();
        Guard(publisher == L"CN=DesktopPdfPrototypeDev", E_ACCESSDENIED, diagnostic, L"package-publisher");
        Guard(context.identity.appContainer == appContainer, E_ACCESSDENIED, diagnostic, L"container-context");
        Guard(context.identity.session != 0, E_ACCESSDENIED, diagnostic, L"interactive-session");
        Point(diagnostic, L"PackageId.FamilyName", L"winrt");
        context.family = std::wstring(package.FamilyName());
        Step(diagnostic, L"current.hash", L"identity-hash");
        Point(diagnostic, L"UserHash", L"winrt");
        context.userHash = UserHash(context.identity, diagnostic);
        Point(diagnostic, L"PackageId.FullName", L"winrt");
        auto fullName = package.FullName();
        Point(diagnostic, L"string-allocation", L"cpp");
        context.packageHash = Hash(std::wstring(fullName), diagnostic);
        Step(diagnostic, L"current.cache", L"cache");
        Point(diagnostic, L"ApplicationData.Current", L"winrt");
        auto data = ApplicationData::Current();
        Point(diagnostic, L"ApplicationData.LocalCacheFolder", L"winrt");
        context.cache = data.LocalCacheFolder();
        return context;
    }
    inline void DiagnosticRecord(JsonObject const& report, Diagnostic const& diagnostic)
    {
        JsonObject value;
        Text(value, L"stage", diagnostic.stage); Text(value, L"api", diagnostic.api);
        Text(value, L"object", diagnostic.object); Text(value, L"origin", diagnostic.origin);
        Text(value, L"reason", diagnostic.reason);
        if (diagnostic.hasCode)
        {
            Number(value, L"code", diagnostic.code);
            Text(value, L"codeKind", diagnostic.codeKind);
        }
        if (diagnostic.derivation.recorded)
        {
            auto const& source = diagnostic.derivation;
            JsonObject derivation;
            Number(derivation, L"hresult", source.hresult);
            Flag(derivation, L"outputPresent", source.output.present);
            Flag(derivation, L"validityChecked", source.output.checked);
            Flag(derivation, L"valid", source.output.valid);
            value.Insert(L"derivation", derivation);
        }
        report.Insert(L"error_context", value);
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
