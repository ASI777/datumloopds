#pragma once

#include "datumloop/geometry/GeometryTypes.hpp"

namespace datumloop {

// Renderer adapters receive only disposable display geometry. Exact CAD shapes
// stay owned by DatumDocument and are never reconstructed from this interface.
class IViewportRenderer {
public:
    virtual ~IViewportRenderer() = default;

    [[nodiscard]] virtual const char* backendName() const noexcept = 0;
    virtual void setDisplayMesh(const TriangleMesh& mesh) = 0;
};

}  // namespace datumloop
