#pragma once
#include "../common/Trace.h"

namespace proof
{
    JsonObject CreateFixtures(std::filesystem::path const& directory);
    JsonObject PrintFixture(std::filesystem::path const& manifest, std::wstring const& id,
        std::wstring const& queue, std::filesystem::path const& output, bool control);
}
