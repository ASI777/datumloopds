#pragma once

#include "datumloop/solver/CircuitSolver.hpp"

#include <QString>

namespace datumloop {

class NgspiceProcessCircuitSolver final : public ICircuitSolver {
public:
    explicit NgspiceProcessCircuitSolver(QString workerExecutable, int timeoutMilliseconds = 10000);

    [[nodiscard]] CircuitSimulationResult solveDcOperatingPoint(const CircuitDocument& circuit) const override;

private:
    QString workerExecutable_;
    int timeoutMilliseconds_ = 10000;
};

}  // namespace datumloop
