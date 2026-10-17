#pragma once

#include "datumloop/core/DatumDocument.hpp"
#include "datumloop/numerics/AnalysisMesh.hpp"

#include <cstdint>

namespace datumloop {

struct LinearElasticitySolverSettings {
    double absoluteTolerance = 1e-12;
    double relativeTolerance = 1e-10;
    std::uint32_t maximumIterations = 20000;
    // Required for arbitrary tetrahedral parts. Must lie in material, not a hole.
    // Records the normal stress along the longest CAD bounding-box axis.
    std::optional<Vector3> sectionStressSamplePositionM;
};

// Serial MFEM H1/Q1 (hex) or H1/P1 (tet) solution of the static, small-strain isotropic elasticity
// equation on an explicit, independently checked CAD-derived AnalysisMesh.
class MfemLinearElasticitySolver final {
public:
    [[nodiscard]] std::string solverId() const;
    [[nodiscard]] std::string solverVersion() const;

    [[nodiscard]] StructuralAnalysisRun solve(
        const DatumDocument& document,
        const StructuralAnalysisStudy& study,
        const AnalysisMesh& mesh,
        const LinearElasticitySolverSettings& settings = {}) const;
};

}  // namespace datumloop
