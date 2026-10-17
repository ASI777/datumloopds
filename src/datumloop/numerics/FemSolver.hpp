#pragma once

#include "datumloop/numerics/CantileverBeamFEM.hpp"
#include "datumloop/solver/PhysicsSolver.hpp"

#include <string>

namespace datumloop {

struct FemSolverResult {
    std::string solverId;
    std::string solverVersion;
    int elementCount = 0;
    double tipDisplacementM = 0.0;
    double analyticalTipDisplacementM = 0.0;
    double relativeError = 0.0;
    double strainEnergyJ = 0.0;
    double relativeResidual = 0.0;
    double executionTimeSeconds = 0.0;
    bool converged = false;
    SolverResultProvenance provenance;

    void validate() const;
};

// Narrow first backend boundary for the shared cantilever verification problem.
// MFEM details stay out of the DatumLoop Core and problem interfaces.
class IFemSolver {
public:
    virtual ~IFemSolver() = default;
    [[nodiscard]] virtual std::string solverId() const = 0;
    [[nodiscard]] virtual std::string solverVersion() const = 0;
    [[nodiscard]] virtual FemSolverResult solveCantilever(const CantileverBeamModel& model,
                                                          int elementCount) const = 0;
};

class DatumReferenceFemSolver final : public IFemSolver {
public:
    [[nodiscard]] std::string solverId() const override;
    [[nodiscard]] std::string solverVersion() const override;
    [[nodiscard]] FemSolverResult solveCantilever(const CantileverBeamModel& model,
                                                  int elementCount) const override;
};

[[nodiscard]] CantileverReducedModel fitCantileverReducedModel(
    const IFemSolver& highFidelitySolver,
    const CantileverBeamModel& model,
    int trainingElementCount,
    int validationElementCount,
    const std::vector<double>& trainingLoadsNPerM,
    const std::vector<double>& heldOutLoadsNPerM);

}  // namespace datumloop
