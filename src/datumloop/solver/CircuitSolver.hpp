#pragma once

#include "datumloop/electronics/ElectronicsModel.hpp"

#include <map>
#include <string>
#include <vector>

namespace datumloop {

struct CircuitComponentResult {
    std::string componentId;
    double voltageDropV = 0.0;
    double currentA = 0.0;
    double powerW = 0.0;
};

struct ThermalSourcePlaceholder {
    std::string componentId;
    double powerW = 0.0;
    bool coupledToThermalSolver = false;
    std::string status = "Power estimate only; no thermal field was solved.";
};

struct CircuitSimulationResult {
    std::string backend;
    std::string analysis;
    std::map<std::string, double> netVoltagesV;
    std::vector<CircuitComponentResult> components;
    std::vector<ThermalSourcePlaceholder> thermalSourcePlaceholders;
};

class ICircuitSolver {
public:
    virtual ~ICircuitSolver() = default;
    [[nodiscard]] virtual CircuitSimulationResult solveDcOperatingPoint(const CircuitDocument& circuit) const = 0;
};

}  // namespace datumloop
