#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace datumloop {

using Vector3 = std::array<double, 3>;

struct ShapeMetrics {
    double volume = 0.0;
    double surfaceArea = 0.0;
    Vector3 centerOfMass{0.0, 0.0, 0.0};
    Vector3 minimum{0.0, 0.0, 0.0};
    Vector3 maximum{0.0, 0.0, 0.0};
    bool valid = false;
};

struct MeshVertex {
    std::array<float, 3> position{};
    std::array<float, 3> normal{};
};

struct TriangleMesh {
    std::vector<MeshVertex> vertices;
    Vector3 minimum{0.0, 0.0, 0.0};
    Vector3 maximum{0.0, 0.0, 0.0};

    [[nodiscard]] std::size_t triangleCount() const noexcept
    {
        return vertices.size() / 3;
    }
};

}  // namespace datumloop
