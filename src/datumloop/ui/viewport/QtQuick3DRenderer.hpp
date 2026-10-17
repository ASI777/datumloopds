#pragma once

#include "datumloop/renderer/ViewportRenderer.hpp"

#include <QPointer>

namespace datumloop {

class ExactShapeGeometry;

// Transitional display adapter: forwards OCCT-derived triangles to the
// Qt Quick 3D geometry item. It never owns or edits the exact OCCT shape.
class QtQuick3DRenderer final : public IViewportRenderer {
public:
    explicit QtQuick3DRenderer(ExactShapeGeometry* geometry,
                               ExactShapeGeometry* analysisGeometry = nullptr);

    [[nodiscard]] const char* backendName() const noexcept override;
    void setDisplayMesh(const TriangleMesh& mesh) override;
    void setAnalysisDisplayMesh(const TriangleMesh& mesh);

private:
    QPointer<ExactShapeGeometry> geometry_;
    QPointer<ExactShapeGeometry> analysisGeometry_;
};

}  // namespace datumloop
