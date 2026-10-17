#pragma once

#include "datumloop/core/DatumTopologyReference.hpp"

#include <TopoDS_Shape.hxx>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace datumloop {

enum class DatumTopologyResolutionStatus {
    Resolved,
    OwnerMismatch,
    UnsupportedFeature,
    NotFound,
    Ambiguous,
};

struct DatumTopologyResolveOptions {
    double maximumRelativeMeasureError = 0.20;
    double maximumCentroidDistanceRatio = 0.10;
    double maximumScore = 0.25;
    double ambiguityTolerance = 1e-8;
};

struct DatumTopologyResolution {
    DatumTopologyResolutionStatus status = DatumTopologyResolutionStatus::NotFound;
    TopoDS_Shape subshape;
    std::size_t transientIndex = 0;
    double score = 0.0;
};

struct DatumTopologyHitResolution {
    DatumTopologyResolutionStatus status = DatumTopologyResolutionStatus::NotFound;
    std::string referenceId;
    std::size_t transientIndex = 0;
    double distanceMm = 0.0;
};

class DatumTopologyResolver final {
public:
    // Captures faces first, then edges and vertices. Parent IDs describe
    // immediate topology adjacency; enumeration hints are diagnostic only.
    [[nodiscard]] static std::vector<DatumTopologyReference> captureAll(
        const std::string& objectId,
        const TopoDS_Shape& shape,
        std::optional<std::string> featureId = std::nullopt);

    // Uses topology and geometric signatures. It never resolves a tie by
    // FaceN/EdgeN ordering; ambiguous matches remain unresolved.
    [[nodiscard]] static DatumTopologyResolution resolve(
        const DatumTopologyReference& reference,
        const std::string& currentObjectId,
        const TopoDS_Shape& currentShape,
        const DatumTopologyResolveOptions& options = {});

    // Maps an exact renderer hit in object-local millimetres to a unique
    // persisted reference. Transient ordering is used only to pair candidates
    // created during this call with their OCCT subshapes, never to identify a
    // stored reference.
    [[nodiscard]] static DatumTopologyHitResolution resolveHitAtPoint(
        const std::vector<DatumTopologyReference>& references,
        const std::string& objectId,
        const TopoDS_Shape& shape,
        DatumSubshapeType subshapeType,
        const Vector3& localPointMm,
        const std::optional<Vector3>& localNormal = std::nullopt,
        double toleranceMm = 1.0,
        const DatumTopologyResolveOptions& options = {});
};

}  // namespace datumloop
