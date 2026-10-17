#include "datumloop/ui/viewport/QtQuick3DRenderer.hpp"

#include "datumloop/ui/ExactShapeGeometry.hpp"

#include <stdexcept>

namespace datumloop {

QtQuick3DRenderer::QtQuick3DRenderer(ExactShapeGeometry* geometry) : geometry_(geometry)
{
    if (geometry_ == nullptr) {
        throw std::invalid_argument("Qt Quick 3D renderer requires a display geometry target");
    }
}

const char* QtQuick3DRenderer::backendName() const noexcept
{
    return "Qt Quick 3D";
}

void QtQuick3DRenderer::setDisplayMesh(const TriangleMesh& mesh)
{
    if (geometry_.isNull()) {
        throw std::runtime_error("Qt Quick 3D display geometry target is no longer available");
    }
    geometry_->setMesh(mesh);
}

}  // namespace datumloop
