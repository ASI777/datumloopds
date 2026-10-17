#pragma once

#include "datumloop/geometry/GeometryTypes.hpp"
#include "datumloop/knowledge/EquationRegistry.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace datumloop {

struct FixedDisplacement {
    std::string id;
    std::string name;
    std::string cadObjectId;
    std::string topologyReferenceId;
    Vector3 displacementM{0.0, 0.0, 0.0};
    std::array<bool, 3> constrainedComponents{true, true, true};

    friend bool operator==(const FixedDisplacement&, const FixedDisplacement&) = default;
};

struct AppliedForce {
    std::string id;
    std::string name;
    std::string cadObjectId;
    std::string topologyReferenceId;
    Vector3 totalForceN{0.0, 0.0, 0.0};

    friend bool operator==(const AppliedForce&, const AppliedForce&) = default;
};

// Positive pressure acts inward, opposite to the outward CAD face normal.
struct AppliedPressure {
    std::string id;
    std::string name;
    std::string cadObjectId;
    std::string topologyReferenceId;
    double pressurePa = 0.0;

    friend bool operator==(const AppliedPressure&, const AppliedPressure&) = default;
};

struct BodyForce {
    std::string id;
    std::string name;
    std::string cadObjectId;
    Vector3 forceDensityNPerM3{0.0, 0.0, 0.0};

    friend bool operator==(const BodyForce&, const BodyForce&) = default;
};

struct InitialCondition {
    std::string id;
    std::string name;
    std::string cadObjectId;
    Vector3 displacementM{0.0, 0.0, 0.0};
    Vector3 velocityMPerS{0.0, 0.0, 0.0};

    friend bool operator==(const InitialCondition&, const InitialCondition&) = default;
};

using BoundaryCondition =
    std::variant<FixedDisplacement, AppliedForce, AppliedPressure, BodyForce, InitialCondition>;

[[nodiscard]] std::string boundaryConditionId(const BoundaryCondition& condition);
[[nodiscard]] std::string boundaryConditionCadObjectId(const BoundaryCondition& condition);
[[nodiscard]] std::optional<std::string> boundaryConditionTopologyReferenceId(
    const BoundaryCondition& condition);
void validateBoundaryCondition(const BoundaryCondition& condition);

struct AnalysisMeshProvenance {
    std::string revisionId;
    std::string generator;
    std::string generatorVersion;
    std::string elementType;
    double requestedMaximumElementSizeM = 0.0;
    std::uint64_t nodeCount = 0;
    std::uint64_t elementCount = 0;
    double minimumScaledJacobian = 0.0;
    double maximumAspectRatio = 0.0;

    friend bool operator==(const AnalysisMeshProvenance&, const AnalysisMeshProvenance&) = default;
};

enum class FieldType {
    Displacement,
    DisplacementMagnitude,
    StrainTensor,
    StressTensor,
    VonMisesStress,
};

enum class FieldLocation {
    Nodes,
    Elements,
};

[[nodiscard]] std::string fieldTypeName(FieldType type);
[[nodiscard]] std::optional<FieldType> fieldTypeFromName(const std::string& name);
[[nodiscard]] std::string fieldLocationName(FieldLocation location);
[[nodiscard]] std::optional<FieldLocation> fieldLocationFromName(const std::string& name);

struct FieldDataset {
    std::string id;
    FieldType fieldType = FieldType::Displacement;
    FieldLocation location = FieldLocation::Nodes;
    std::vector<std::string> components;
    std::string units;
    std::string meshRevisionId;
    std::string solverRunId;
    std::vector<double> values;

    void validate(std::uint64_t nodeCount,
                  std::uint64_t elementCount,
                  const std::string& expectedMeshRevisionId,
                  const std::string& expectedSolverRunId) const;

    friend bool operator==(const FieldDataset&, const FieldDataset&) = default;
};

struct AnalysisRunProvenance {
    std::string solverRunId;
    std::string timestampUtc;
    std::string sourceProjectId;
    std::uint64_t sourceDocumentRevision = 0;
    std::string cadObjectId;
    std::optional<std::string> geometryFeatureId;
    std::string materialDefinitionId;
    std::string materialRevision;
    std::optional<std::string> materialStateId;
    std::optional<double> analysisTemperatureK;
    bool temperatureRangeVerified = false;
    std::string equationDefinitionId;
    ModelMaturity equationMaturity = ModelMaturity::E0;
    std::string solverId;
    std::string solverVersion;
    AnalysisMeshProvenance mesh;
    std::vector<std::string> topologyReferenceIds;
    std::vector<std::string> boundaryConditionIds;
    double absoluteTolerance = 0.0;
    double relativeTolerance = 0.0;
    std::uint32_t maximumIterations = 0;
    std::optional<std::string> agentProposalId;

    friend bool operator==(const AnalysisRunProvenance&, const AnalysisRunProvenance&) = default;
};

struct StructuralAnalysisRun {
    AnalysisRunProvenance provenance;
    bool converged = false;
    std::uint32_t iterations = 0;
    double relativeResidual = 0.0;
    Vector3 appliedForceN{0.0, 0.0, 0.0};
    Vector3 reactionForceN{0.0, 0.0, 0.0};
    double relativeForceBalanceError = 0.0;
    double tipDisplacementM = 0.0;
    double maximumDisplacementM = 0.0;
    // Absolute axial stress at a fixed midspan section point, 75% from the
    // neutral axis toward the positive first transverse axis.
    double sectionAxialStressPa = 0.0;
    Vector3 sectionStressSamplePositionM{0.0, 0.0, 0.0};
    double maximumVonMisesStressPa = 0.0;
    double strainEnergyJ = 0.0;
    std::vector<FieldDataset> fields;

    void validate(const std::vector<std::string>& expectedBoundaryConditionIds) const;

    friend bool operator==(const StructuralAnalysisRun&, const StructuralAnalysisRun&) = default;
};

struct StructuralMeshConvergence {
    std::vector<std::string> solverRunIds;
    double relativeTipDisplacementChange = 0.0;
    double relativeStrainEnergyChange = 0.0;
    double relativeSectionStressChange = 0.0;
    double tolerance = 0.0;
    bool converged = false;

    friend bool operator==(const StructuralMeshConvergence&, const StructuralMeshConvergence&) = default;
};

struct StructuralAnalysisStudy {
    std::string id;
    std::string name;
    std::string cadObjectId;
    std::string materialDefinitionId;
    std::optional<std::string> materialStateId;
    std::string equationDefinitionId;
    std::vector<BoundaryCondition> boundaryConditions;
    std::vector<StructuralAnalysisRun> runs;
    std::optional<StructuralMeshConvergence> meshConvergence;

    void validate() const;

    friend bool operator==(const StructuralAnalysisStudy&, const StructuralAnalysisStudy&) = default;
};

}  // namespace datumloop
