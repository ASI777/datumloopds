#pragma once

#include "datumloop/geometry/GeometryTypes.hpp"
#include "datumloop/core/DatumTopologyReference.hpp"

#include <TopoDS_Shape.hxx>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace datumloop {

using DatumPropertyValue = std::variant<bool, std::int64_t, double, std::string>;
using DatumPropertyMap = std::map<std::string, DatumPropertyValue>;

struct DatumObject {
    std::string id;
    std::string name;
    std::optional<std::string> parentId;
    DatumPropertyMap properties;
    TopoDS_Shape shape;
    ShapeMetrics metrics;
    Vector3 position{0.0, 0.0, 0.0};
};

class DatumDocument final {
public:
    explicit DatumDocument(std::string name = "Untitled Mechanism", std::string projectId = {});

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::string& projectId() const noexcept { return projectId_; }
    [[nodiscard]] bool isDirty() const noexcept { return dirty_; }
    void markSaved() noexcept { dirty_ = false; }
    void setName(std::string name);
    void clear(std::string name = "Untitled Mechanism");
    void replaceWith(DatumDocument replacement);

    [[nodiscard]] const DatumObject& createBox(std::string name, const Vector3& dimensions);
    [[nodiscard]] const DatumObject& addObject(std::string id,
                                              std::string name,
                                              TopoDS_Shape shape,
                                              const Vector3& position = {0.0, 0.0, 0.0},
                                              std::optional<std::string> parentId = std::nullopt);
    [[nodiscard]] const std::vector<DatumObject>& objects() const noexcept { return objects_; }
    [[nodiscard]] const std::vector<DatumTopologyReference>& topologyReferences() const noexcept
    {
        return topologyReferences_;
    }
    [[nodiscard]] const DatumObject* object(const std::string& id) const noexcept;
    [[nodiscard]] bool setPosition(const std::string& id, const Vector3& position) noexcept;
    void setProperty(const std::string& id, std::string key, DatumPropertyValue value);
    [[nodiscard]] const DatumTopologyReference& addTopologyReference(DatumTopologyReference reference);
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

private:
    std::string name_;
    std::string projectId_;
    std::vector<DatumObject> objects_;
    std::vector<DatumTopologyReference> topologyReferences_;
    std::uint64_t revision_ = 0;
    bool dirty_ = true;
};

}  // namespace datumloop
