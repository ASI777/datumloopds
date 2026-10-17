#pragma once

#include "datumloop/renderer/ViewportRenderer.hpp"

#include <QPointer>

namespace datumloop {

class ExactShapeGeometry;

// Transitional display adapter: forwards OCCT-derived triangles to the
// Qt Quick 3D geometry item. It never owns or edits the exact OCCT shape.
class QtQuick3DRenderer final : public IViewportRenderer {
public:
    explicit QtQuick3DRenderer(ExactShapeGeometry* geometry);

    [[nodiscard]] const char* backendName() const noexcept override;
    void setDisplayMesh(const TriangleMesh& mesh) override;

private:
    QPointer<ExactShapeGeometry> geometry_;
};

}  // namespace datumloop
