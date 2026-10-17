#pragma once

#include "datumloop/numerics/FemSolver.hpp"

namespace datumloop {

// Serial MFEM implementation of the registered one-dimensional Timoshenko
// cantilever benchmark. MFEM types remain private to this optional adapter.
class MfemCantileverSolver final : public IFemSolver {
public:
    [[nodiscard]] std::string solverId() const override;
    [[nodiscard]] std::string solverVersion() const override;
    [[nodiscard]] FemSolverResult solveCantilever(const CantileverBeamModel& model,
                                                  int elementCount) const override;
};

}  // namespace datumloop
