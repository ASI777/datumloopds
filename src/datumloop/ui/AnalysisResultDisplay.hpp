#pragma once

#include "datumloop/engineering/StructuralAnalysis.hpp"
#include "datumloop/geometry/GeometryTypes.hpp"

#include <string>

namespace datumloop {

enum class AnalysisResultField {
    DisplacementMagnitude,
    VonMisesStress,
    EquivalentStrain,
};

struct AnalysisResultDisplay {
    TriangleMesh mesh;
    std::string fieldName;
    std::string units;
    double minimum = 0.0;
    double maximum = 0.0;
};

// Builds disposable colored boundary triangles from the persisted FE mesh and
// its field arrays. Coordinates remain derived display data in CAD millimetres.
[[nodiscard]] AnalysisResultDisplay buildAnalysisResultDisplay(
    const StructuralAnalysisRun& run,
    AnalysisResultField field,
    double deformationScale = 1.0);

}  // namespace datumloop
