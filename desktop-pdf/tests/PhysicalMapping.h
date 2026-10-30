#pragma once
#include <limits>
#include <optional>

namespace proof
{
    struct PhysicalMapping
    {
        int windowX, windowY, viewportX, viewportY;
    };

    inline std::optional<PhysicalMapping> MappingFromReference(
        int pixelsX, int pixelsY, int millimetersX, int millimetersY)
    {
        constexpr int maximumMillimeters = std::numeric_limits<int>::max() / 100;
        if (pixelsX <= 0 || pixelsY <= 0 || millimetersX <= 0 || millimetersY <= 0 ||
            millimetersX > maximumMillimeters || millimetersY > maximumMillimeters)
            return std::nullopt;
        return PhysicalMapping{millimetersX * 100, millimetersY * 100, pixelsX, pixelsY};
    }
}
