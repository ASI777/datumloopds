#pragma once

#include "datumloop/core/DatumDocument.hpp"
#include "datumloop/numerics/AnalysisMesh.hpp"

#include <cstdint>

namespace datumloop {

struct LinearElasticitySolverSettings {
    double absoluteTolerance = 1e-12;
    double relativeTolerance = 1e-10;
    std::uint32_t maximumIterations = 20000;
};

// Serial MFEM H1/Q1 solution of the static, small-strain isotropic elasticity
// equation on the explicit CAD-derived AnalysisMesh. The first CAD mesher is
// intentionally limited to axis-aligned rectangular OCCT solids.
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
