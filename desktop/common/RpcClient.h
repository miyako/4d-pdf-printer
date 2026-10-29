#pragma once
#include "Common.h"

namespace desktop
{
    JsonObject Call(Operation operation, JsonObject const& request = JsonObject{});
    bool WaitReady(std::chrono::milliseconds timeout);
}
