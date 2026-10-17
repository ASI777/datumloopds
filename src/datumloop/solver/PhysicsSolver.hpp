#pragma once

#include "datumloop/knowledge/EquationRegistry.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace datumloop {

struct DimensionalScalar {
    double value = 0.0;
    UnitDimension dimension;
};

struct SolverRequest {
    std::string requestId;
    std::string equationDefinitionId;
    std::string designRevision;
    ModelMaturity minimumMaturity = ModelMaturity::E3;
    double absoluteTolerance = 1e-9;
    double relativeTolerance = 1e-6;
    std::vector<std::pair<std::string, DimensionalScalar>> inputs;

    void validate(const EquationRegistry& registry) const;
};

struct TransientSolverRequest {
    SolverRequest model;
    double startTimeS = 0.0;
    double endTimeS = 0.0;
    double timeStepS = 0.0;

    void validate(const EquationRegistry& registry) const;
};

enum class SolverResultStatus { Succeeded, Failed, Cancelled, NotConverged };

struct SolverResultField {
    std::string name;
    UnitDimension dimension;
    std::optional<double> scalarValue;
    std::string datasetReference;
};

struct SolverResultProvenance {
    std::string equationDefinitionId;
    std::string equationDefinitionRevision;
    ModelMaturity equationMaturity = ModelMaturity::E0;
    std::string materialModelId;
    std::string materialModelVersion;
    std::string backendId;
    std::string backendVersion;
    std::string meshDescription;
    std::string elementFamily;
    int polynomialOrder = 0;
    double absoluteTolerance = 0.0;
    double relativeTolerance = 0.0;
    bool converged = false;
    std::string benchmarkId;
    std::string benchmarkStatus;
    std::string inputRevision;
};

struct PhysicsSolverResult {
    std::string requestId;
    std::string solverId;
    std::string solverVersion;
    SolverResultStatus status = SolverResultStatus::Failed;
    std::vector<SolverResultField> fields;
    std::vector<std::string> provenance;
    std::optional<SolverResultProvenance> verificationProvenance;
    std::string detail;

    void validateFor(const SolverRequest& request) const;
};

class ISolver {
public:
    virtual ~ISolver() = default;
    [[nodiscard]] virtual std::string solverId() const = 0;
    [[nodiscard]] virtual std::string solverVersion() const = 0;
    [[nodiscard]] virtual bool supportsEquation(const std::string& equationId) const noexcept = 0;
};

class ISteadySolver : public ISolver {
public:
    [[nodiscard]] virtual PhysicsSolverResult solveSteady(const SolverRequest& request) const = 0;
};

class ITransientSolver : public ISolver {
public:
    [[nodiscard]] virtual PhysicsSolverResult solveTransient(const TransientSolverRequest& request) const = 0;
};

}  // namespace datumloop
