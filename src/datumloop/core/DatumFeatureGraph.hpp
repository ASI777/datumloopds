#pragma once

#include "datumloop/geometry/GeometryTypes.hpp"

#include <TopoDS_Shape.hxx>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace datumloop {

enum class DatumFeatureType {
    Box,
    Cylinder,
    Sphere,
    Cone,
    Translate,
    BooleanUnion,
    BooleanCut,
    BooleanIntersection,
};

enum class DatumFeatureStatus {
    Succeeded,
    Failed,
};

[[nodiscard]] std::string datumFeatureTypeName(DatumFeatureType type);
[[nodiscard]] std::optional<DatumFeatureType> datumFeatureTypeFromName(const std::string& name);
[[nodiscard]] std::string datumFeatureStatusName(DatumFeatureStatus status);

struct DatumFeature {
    std::string id;
    std::string ownerObjectId;
    DatumFeatureType type = DatumFeatureType::Box;
    std::vector<std::string> inputFeatureIds;
    std::map<std::string, double> parameters;
    std::string kernelBackend = "occt";
    DatumFeatureStatus status = DatumFeatureStatus::Failed;
    std::string error = "Feature has not been recomputed.";
    // Exact generated B-Rep. Display tessellation is derived separately.
    TopoDS_Shape generatedShape;
};

// An append-ordered DAG. Inputs must refer to earlier features, which makes
// evaluation deterministic and prevents cycles without a separate scheduler.
class DatumFeatureGraph final {
public:
    DatumFeatureGraph() = default;
    DatumFeatureGraph(std::string ownerObjectId,
                      std::vector<DatumFeature> features,
                      std::optional<std::string> outputFeatureId);

    [[nodiscard]] const std::string& ownerObjectId() const noexcept { return ownerObjectId_; }
    [[nodiscard]] const std::vector<DatumFeature>& features() const noexcept { return features_; }
    [[nodiscard]] const std::optional<std::string>& outputFeatureId() const noexcept { return outputFeatureId_; }
    [[nodiscard]] const TopoDS_Shape& outputShape() const noexcept { return outputShape_; }
    [[nodiscard]] bool outputSucceeded() const noexcept { return !outputShape_.IsNull(); }
    [[nodiscard]] const DatumFeature* feature(const std::string& id) const noexcept;

    void recompute();

private:
    std::string ownerObjectId_;
    std::vector<DatumFeature> features_;
    std::optional<std::string> outputFeatureId_;
    TopoDS_Shape outputShape_;
};

}  // namespace datumloop
