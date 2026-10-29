#pragma once
#include <windows.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

namespace proof
{
    using winrt::Windows::Data::Json::JsonObject;
    using winrt::Windows::Data::Json::JsonArray;
    using winrt::Windows::Data::Json::JsonValue;
    struct Failure : std::exception
    {
        char const* stage;
        DWORD code;
        char const* domain;
        Failure(char const* where, DWORD value, char const* kind = "win32") :
            stage(where), code(value), domain(kind) {}
        char const* what() const noexcept override { return stage; }
    };
    inline void Require(bool value, char const* stage, DWORD code = ERROR_INVALID_DATA)
    {
        if (!value) throw Failure(stage, code);
    }
    inline void Check(BOOL value, char const* stage)
    {
        if (!value) throw Failure(stage, GetLastError());
    }
    class Handle
    {
        HANDLE value_ = nullptr;
    public:
        Handle() = default;
        explicit Handle(HANDLE value) : value_(value) {}
        ~Handle() { reset(); }
        Handle(Handle const&) = delete;
        Handle& operator=(Handle const&) = delete;
        Handle(Handle&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
        Handle& operator=(Handle&& other) noexcept
        {
            if (this != &other) { reset(); value_ = std::exchange(other.value_, nullptr); }
            return *this;
        }
        HANDLE get() const { return value_; }
        void reset(HANDLE value = nullptr)
        {
            if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
            value_ = value;
        }
    };
    inline void Number(JsonObject const& json, wchar_t const* key, uint64_t value)
    {
        json.Insert(key, JsonValue::CreateNumberValue(static_cast<double>(value)));
    }
    inline void Text(JsonObject const& json, wchar_t const* key, std::wstring const& value)
    {
        json.Insert(key, JsonValue::CreateStringValue(value));
    }
    inline void Flag(JsonObject const& json, wchar_t const* key, bool value)
    {
        json.Insert(key, JsonValue::CreateBooleanValue(value));
    }
    JsonObject Record(wchar_t const* stage);
    void ValidatePath(std::filesystem::path const& path, bool mustExist = true);
    Handle OpenRead(std::filesystem::path const& path);
    std::wstring Hash(HANDLE file);
    std::wstring Hash(std::filesystem::path const& path);
    JsonObject ReadJson(std::filesystem::path const& path);
    void WriteNew(std::filesystem::path const& path, JsonObject const& json);
    std::wstring Quote(std::wstring const& argument);

    class Trace
    {
        Handle file_;
        uint64_t bytes_ = 0;
        std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
    public:
        bool truncated = false;
        explicit Trace(std::filesystem::path const& path = {});
        void Event(wchar_t const* stage, JsonObject json = {});
    };
}
