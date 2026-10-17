#pragma once

#include "datumloop/geometry/GeometryTypes.hpp"

#include <QQuick3DGeometry>

namespace datumloop {

class ExactShapeGeometry : public QQuick3DGeometry {
    Q_OBJECT
public:
    explicit ExactShapeGeometry(QQuick3DObject* parent = nullptr);
    void setMesh(const TriangleMesh& mesh);
};

}  // namespace datumloop
