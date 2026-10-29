#pragma once
#include <cstdint>

namespace desktop
{
    struct SidValidity
    {
        bool present = false, checked = false, valid = false;
    };
    struct SidDerivation
    {
        bool recorded = false;
        uint32_t hresult = 0;
        SidValidity output;
    };
    struct Diagnostic
    {
        wchar_t const* stage = L"control";
        wchar_t const* object = L"control";
        wchar_t const* api = L"none";
        wchar_t const* origin = L"validation";
        wchar_t const* reason = L"none";
        uint32_t code = 0;
        bool hasCode = false;
        wchar_t const* codeKind = L"none";
        SidDerivation derivation;
    };
    inline void DerivedSidResult(Diagnostic* value, uint32_t result, bool present) noexcept
    {
        if (value) value->derivation = {true, result, {present, false, false}};
    }
    inline void Step(Diagnostic* value, wchar_t const* stage, wchar_t const* object) noexcept
    {
        if (value) { value->stage = stage; value->object = object; }
    }
    inline void Point(Diagnostic* value, wchar_t const* api, wchar_t const* origin,
        wchar_t const* reason = L"none") noexcept
    {
        if (value)
        {
            value->api = api; value->origin = origin; value->reason = reason;
            value->hasCode = false; value->codeKind = L"none";
        }
    }
    inline void ErrorCode(Diagnostic* value, uint32_t code, wchar_t const* kind) noexcept
    {
        if (value) { value->code = code; value->hasCode = true; value->codeKind = kind; }
    }
}
