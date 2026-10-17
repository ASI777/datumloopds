#pragma once

#include "datumloop/geometry/GeometryTypes.hpp"

#include <TopoDS_Shape.hxx>

namespace datumloop {

class OcctKernel final {
public:
    [[nodiscard]] static TopoDS_Shape box(const Vector3& dimensions);
    [[nodiscard]] static ShapeMetrics measure(const TopoDS_Shape& shape);
    [[nodiscard]] static bool isValid(const TopoDS_Shape& shape);
    [[nodiscard]] static TriangleMesh tessellate(const TopoDS_Shape& shape,
                                                 double linearDeflection = 0.15,
                                                 double angularDeflection = 0.35);
};

}  // namespace datumloop
