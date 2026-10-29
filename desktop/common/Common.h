#pragma once
#include "Diagnostic.h"
#include <windows.h>
#include <rpc.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Security.Cryptography.Core.h>

namespace desktop
{
    using winrt::Windows::Data::Json::JsonObject;
    using winrt::Windows::Data::Json::JsonValue;
    inline constexpr wchar_t AutomationUri[] = L"miyako-pdf-prototype:automation";
    inline constexpr wchar_t InteractiveUri[] = L"miyako-pdf-prototype:interactive";
    inline constexpr wchar_t TitlePrefix[] = L"4DPDF1:";
    enum Operation : unsigned long
    {
        Ping = 1, PrepareGate = 2, StatusGate = 3, CancelGate = 4,
        ClaimGate = 5, FinishGate = 6, WorkflowPing = 7, StallGate = 8,
        WorkflowStallGate = 9
    };

    struct CloseHandleDeleter
    {
        void operator()(void* value) const noexcept { if (value) CloseHandle(value); }
    };
    using handle = std::unique_ptr<void, CloseHandleDeleter>;

    inline void Check(bool success, Diagnostic* diagnostic = nullptr)
    {
        if (!success)
        {
            auto error = GetLastError();
            ErrorCode(diagnostic, error, L"win32");
            winrt::throw_hresult(HRESULT_FROM_WIN32(error));
        }
    }

    inline void CheckRpc(RPC_STATUS code)
    {
        if (code != ERROR_SUCCESS) throw winrt::hresult_error(HRESULT_FROM_WIN32(code));
    }

    inline wchar_t const* TokenRole(TOKEN_INFORMATION_CLASS kind) noexcept
    {
        switch (kind)
        {
        case TokenUser: return L"token-user";
        case TokenSessionId: return L"token-session";
        case TokenIsAppContainer: return L"token-container-flag";
        case TokenAppContainerSid: return L"token-container-sid";
        case TokenType: return L"token-type";
        case TokenImpersonationLevel: return L"token-impersonation";
        case TokenElevation: return L"token-elevation";
        default: return L"token-information";
        }
    }
    inline std::vector<uint8_t> TokenData(HANDLE token, TOKEN_INFORMATION_CLASS kind,
        Diagnostic* diagnostic = nullptr)
    {
        DWORD bytes = 0;
        Step(diagnostic, L"current.token", TokenRole(kind));
        Point(diagnostic, L"GetTokenInformation", L"win32", L"size-query");
        auto succeeded = GetTokenInformation(token, kind, nullptr, 0, &bytes);
        auto error = succeeded ? ERROR_SUCCESS : GetLastError();
        if (succeeded || error != ERROR_INSUFFICIENT_BUFFER || bytes == 0)
        {
            Point(diagnostic, L"GetTokenInformation", L"validation", L"token-size-probe");
            if (!succeeded) ErrorCode(diagnostic, error, L"win32");
            throw winrt::hresult_error(E_ACCESSDENIED);
        }
        Point(diagnostic, L"buffer-allocation", L"cpp");
        std::vector<uint8_t> data(bytes);
        Point(diagnostic, L"GetTokenInformation", L"win32", L"value-query");
        Check(GetTokenInformation(token, kind, data.data(), bytes, &bytes) != FALSE, diagnostic);
        return data;
    }

    inline std::vector<uint8_t> CopySid(PSID sid, Diagnostic* diagnostic = nullptr)
    {
        Point(diagnostic, L"IsValidSid", L"validation", L"sid-validity");
        if (!sid || !IsValidSid(sid)) throw winrt::hresult_error(E_ACCESSDENIED);
        Point(diagnostic, L"GetLengthSid", L"win32");
        auto length = GetLengthSid(sid);
        Point(diagnostic, L"buffer-allocation", L"cpp");
        std::vector<uint8_t> result(length);
        Point(diagnostic, L"CopySid", L"win32");
        Check(::CopySid(static_cast<DWORD>(result.size()), result.data(), sid) != FALSE, diagnostic);
        return result;
    }

    struct Identity
    {
        std::vector<uint8_t> user, container;
        DWORD session = MAXDWORD;
        bool appContainer = false;
        bool impersonation = false;
        SECURITY_IMPERSONATION_LEVEL level = SecurityAnonymous;
    };

    inline Identity ReadIdentity(HANDLE token, Diagnostic* diagnostic = nullptr)
    {
        Identity result;
        auto user = TokenData(token, TokenUser, diagnostic);
        result.user = CopySid(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid, diagnostic);
        auto session = TokenData(token, TokenSessionId, diagnostic);
        result.session = *reinterpret_cast<DWORD*>(session.data());
        auto container = TokenData(token, TokenIsAppContainer, diagnostic);
        result.appContainer = *reinterpret_cast<DWORD*>(container.data()) != 0;
        if (result.appContainer)
        {
            auto sid = TokenData(token, TokenAppContainerSid, diagnostic);
            result.container = CopySid(reinterpret_cast<TOKEN_APPCONTAINER_INFORMATION*>(sid.data())->TokenAppContainer, diagnostic);
        }
        auto type = TokenData(token, TokenType, diagnostic);
        result.impersonation = *reinterpret_cast<TOKEN_TYPE*>(type.data()) == TokenImpersonation;
        if (result.impersonation)
        {
            auto level = TokenData(token, TokenImpersonationLevel, diagnostic);
            result.level = *reinterpret_cast<SECURITY_IMPERSONATION_LEVEL*>(level.data());
        }
        return result;
    }

    inline Identity CurrentIdentity(Diagnostic* diagnostic = nullptr)
    {
        HANDLE token = nullptr;
        Step(diagnostic, L"current.token", L"process-token");
        Point(diagnostic, L"OpenProcessToken", L"win32");
        Check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) != FALSE, diagnostic);
        handle owned(token);
        return ReadIdentity(token, diagnostic);
    }

    inline std::wstring RandomToken(Diagnostic* diagnostic = nullptr)
    {
        using namespace winrt::Windows::Security::Cryptography;
        Point(diagnostic, L"CryptographicBuffer.GenerateRandom", L"winrt");
        auto bytes = CryptographicBuffer::GenerateRandom(32);
        Point(diagnostic, L"CryptographicBuffer.EncodeToHexString", L"winrt");
        auto encoded = CryptographicBuffer::EncodeToHexString(bytes);
        Point(diagnostic, L"string-allocation", L"cpp");
        auto value = std::wstring(encoded);
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch)
        {
            return ch >= L'A' && ch <= L'F' ? static_cast<wchar_t>(ch + (L'a' - L'A')) : ch;
        });
        return value;
    }

    inline std::wstring Hash(std::wstring const& text, Diagnostic* diagnostic = nullptr)
    {
        using namespace winrt::Windows::Security::Cryptography;
        using namespace winrt::Windows::Security::Cryptography::Core;
        Point(diagnostic, L"HashAlgorithmNames.Sha256", L"winrt");
        auto name = HashAlgorithmNames::Sha256();
        Point(diagnostic, L"HashAlgorithmProvider.OpenAlgorithm", L"winrt");
        auto algorithm = HashAlgorithmProvider::OpenAlgorithm(name);
        Point(diagnostic, L"CryptographicBuffer.ConvertStringToBinary", L"winrt");
        auto bytes = CryptographicBuffer::ConvertStringToBinary(text, BinaryStringEncoding::Utf8);
        Point(diagnostic, L"HashAlgorithmProvider.HashData", L"winrt");
        auto hash = algorithm.HashData(bytes);
        Point(diagnostic, L"CryptographicBuffer.EncodeToHexString", L"winrt");
        auto encoded = CryptographicBuffer::EncodeToHexString(hash);
        Point(diagnostic, L"string-allocation", L"cpp");
        return std::wstring(encoded);
    }

    inline bool IsToken(std::wstring const& value)
    {
        return value.size() == 64 && std::all_of(value.begin(), value.end(), [](wchar_t ch)
        {
            return (ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'f');
        });
    }

    inline bool IsTitle(std::wstring const& title)
    {
        return title.starts_with(TitlePrefix) && IsToken(title.substr(std::size(TitlePrefix) - 1));
    }

    inline std::wstring Endpoint(Identity const& identity)
    {
        std::wstring user;
        constexpr wchar_t hex[] = L"0123456789abcdef";
        for (auto byte : identity.user)
        {
            user += hex[byte >> 4];
            user += hex[byte & 15];
        }
        auto family = winrt::Windows::ApplicationModel::Package::Current().Id().FamilyName();
        return L"4DPdfGate.v1." + Hash(std::wstring(family) + L":" + user +
            L":" + std::to_wstring(identity.session));
    }

    inline JsonObject Record(wchar_t const* stage)
    {
        JsonObject result;
        result.Insert(L"schema", JsonValue::CreateStringValue(L"4d-pdf-printer.desktop-gate"));
        result.Insert(L"version", JsonValue::CreateNumberValue(1));
        result.Insert(L"stage", JsonValue::CreateStringValue(stage));
        result.Insert(L"pdfProduced", JsonValue::CreateBooleanValue(false));
        return result;
    }

    inline void Text(JsonObject const& value, wchar_t const* name, std::wstring const& text)
    {
        value.Insert(name, JsonValue::CreateStringValue(text));
    }
}
