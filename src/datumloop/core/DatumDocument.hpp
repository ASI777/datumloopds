#pragma once

#include "datumloop/geometry/GeometryTypes.hpp"

#include <TopoDS_Shape.hxx>

#include <cstdint>
#include <string>
#include <vector>

namespace datumloop {

struct DatumObject {
    std::string id;
    std::string name;
    TopoDS_Shape shape;
    ShapeMetrics metrics;
    Vector3 position{0.0, 0.0, 0.0};
};

class DatumDocument final {
public:
    [[nodiscard]] const DatumObject& createBox(std::string name, const Vector3& dimensions);
    [[nodiscard]] const std::vector<DatumObject>& objects() const noexcept { return objects_; }
    [[nodiscard]] const DatumObject* object(const std::string& id) const noexcept;
    [[nodiscard]] bool setPosition(const std::string& id, const Vector3& position) noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

private:
    std::vector<DatumObject> objects_;
    std::uint64_t revision_ = 0;
};

}  // namespace datumloop
