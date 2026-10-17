#pragma once

#include "datumloop/geometry/GeometryTypes.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace datumloop {

enum class DatumSubshapeType {
    Face,
    Edge,
    Vertex,
};

[[nodiscard]] std::string datumSubshapeTypeName(DatumSubshapeType type);
[[nodiscard]] std::optional<DatumSubshapeType> datumSubshapeTypeFromName(const std::string& name);

struct DatumTopologyNeighborSignature {
    DatumSubshapeType subshapeType = DatumSubshapeType::Face;
    std::string geometryType;
    double measure = 0.0;

    friend bool operator==(const DatumTopologyNeighborSignature&,
                           const DatumTopologyNeighborSignature&) = default;
};

struct DatumTopologyGeometrySignature {
    std::string geometryType;
    // Surface area in mm^2, curve length in mm, or zero for a vertex.
    double measure = 0.0;
    Vector3 centroidMm{0.0, 0.0, 0.0};
    std::optional<Vector3> direction;
    std::vector<DatumTopologyNeighborSignature> adjacent;

    friend bool operator==(const DatumTopologyGeometrySignature&,
                           const DatumTopologyGeometrySignature&) = default;
};

struct DatumTopologyReference {
    std::string id;
    std::string objectId;
    std::optional<std::string> featureId;
    DatumSubshapeType subshapeType = DatumSubshapeType::Face;
    std::string topologySignature;
    DatumTopologyGeometrySignature geometrySignature;
    std::vector<std::string> parentReferenceIds;
    // Diagnostic only. Resolution never trusts a transient enumeration index.
    std::optional<std::uint32_t> transientIndexHint;
};

}  // namespace datumloop
