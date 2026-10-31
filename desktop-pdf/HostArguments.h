#pragma once
#include "Contract.h"

namespace pdf {
inline Activation ParseHostActivation(std::wstring_view raw) {
    constexpr std::wstring_view silent = L"/InvokerPRAID: SaveAs --mode Silent --job ";
    constexpr std::wstring_view owner = L"/InvokerPRAID: SaveAs ";
    if (raw.starts_with(silent)) return ParseActivation(raw.substr(owner.size()));
    return ParseActivation(raw);
}
}
