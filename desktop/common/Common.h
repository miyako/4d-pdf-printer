#pragma once
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
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Security.Cryptography.Core.h>

namespace desktop
{
    using winrt::Windows::Data::Json::JsonObject;
    using winrt::Windows::Data::Json::JsonValue;
    inline constexpr wchar_t AutomationUri[] = L"4d-pdf-prototype:automation";
    inline constexpr wchar_t InteractiveUri[] = L"4d-pdf-prototype:interactive";
    inline constexpr wchar_t TitlePrefix[] = L"4DPDF1:";
    enum Operation : unsigned long
    {
        Ping = 1, PrepareGate = 2, StatusGate = 3, CancelGate = 4,
        ClaimGate = 5, FinishGate = 6, WorkflowPing = 7
    };

    struct CloseHandleDeleter
    {
        void operator()(void* value) const noexcept { if (value) CloseHandle(value); }
    };
    using handle = std::unique_ptr<void, CloseHandleDeleter>;

    inline void Check(bool success)
    {
        if (!success) winrt::throw_last_error();
    }

    inline void CheckRpc(RPC_STATUS code)
    {
        if (code != RPC_S_OK) throw winrt::hresult_error(HRESULT_FROM_WIN32(code));
    }

    inline std::vector<uint8_t> TokenData(HANDLE token, TOKEN_INFORMATION_CLASS kind)
    {
        DWORD bytes = 0;
        if (GetTokenInformation(token, kind, nullptr, 0, &bytes) ||
            GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0)
            throw winrt::hresult_error(E_ACCESSDENIED);
        std::vector<uint8_t> data(bytes);
        Check(GetTokenInformation(token, kind, data.data(), bytes, &bytes) != FALSE);
        return data;
    }

    inline std::vector<uint8_t> CopySid(PSID sid)
    {
        if (!sid || !IsValidSid(sid)) throw winrt::hresult_error(E_ACCESSDENIED);
        std::vector<uint8_t> result(GetLengthSid(sid));
        Check(::CopySid(static_cast<DWORD>(result.size()), result.data(), sid) != FALSE);
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

    inline Identity ReadIdentity(HANDLE token)
    {
        Identity result;
        auto user = TokenData(token, TokenUser);
        result.user = CopySid(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid);
        auto session = TokenData(token, TokenSessionId);
        result.session = *reinterpret_cast<DWORD*>(session.data());
        auto container = TokenData(token, TokenIsAppContainer);
        result.appContainer = *reinterpret_cast<DWORD*>(container.data()) != 0;
        if (result.appContainer)
        {
            auto sid = TokenData(token, TokenAppContainerSid);
            result.container = CopySid(reinterpret_cast<TOKEN_APPCONTAINER_INFORMATION*>(sid.data())->TokenAppContainer);
        }
        auto type = TokenData(token, TokenType);
        result.impersonation = *reinterpret_cast<TOKEN_TYPE*>(type.data()) == TokenImpersonation;
        if (result.impersonation)
        {
            auto level = TokenData(token, TokenImpersonationLevel);
            result.level = *reinterpret_cast<SECURITY_IMPERSONATION_LEVEL*>(level.data());
        }
        return result;
    }

    inline Identity CurrentIdentity()
    {
        HANDLE token = nullptr;
        Check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) != FALSE);
        handle owned(token);
        return ReadIdentity(token);
    }

    inline std::wstring RandomToken()
    {
        using namespace winrt::Windows::Security::Cryptography;
        return std::wstring(CryptographicBuffer::EncodeToHexString(CryptographicBuffer::GenerateRandom(32)));
    }

    inline std::wstring Hash(std::wstring const& text)
    {
        using namespace winrt::Windows::Security::Cryptography;
        using namespace winrt::Windows::Security::Cryptography::Core;
        auto algorithm = HashAlgorithmProvider::OpenAlgorithm(HashAlgorithmNames::Sha256());
        return std::wstring(CryptographicBuffer::EncodeToHexString(algorithm.HashData(
            CryptographicBuffer::ConvertStringToBinary(text, BinaryStringEncoding::Utf8))));
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
