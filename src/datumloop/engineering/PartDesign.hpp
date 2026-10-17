#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace datumloop {

enum class RequirementStatus { Required, Preferred, Derived, Assumed, Unknown, Count };
enum class RequirementSourceType { PdfManual, DimensionedDrawing, ManufacturerDatasheet, StepReference, EngineeringSpecification, Count };
enum class RequirementEvidenceKind { Printed, Derived, StandardDefined, Measured, DesignSelection, Assumption, Unknown, Count };
enum class ParameterRole { FixedInterface, StandardDefined, BoundedVariable, FreeVariable, Count };
enum class DesignConstraintRelation { Maximum, Minimum, Equal, Count };
enum class DesignCheckStatus { Passed, Failed, Unknown, Count };

std::string partDesignName(RequirementStatus value);
std::string partDesignName(RequirementSourceType value);
std::string partDesignName(RequirementEvidenceKind value);
std::string partDesignName(ParameterRole value);
std::string partDesignName(DesignConstraintRelation value);
std::string partDesignName(DesignCheckStatus value);

struct RequirementSource {
    std::string id;
    RequirementSourceType type = RequirementSourceType::EngineeringSpecification;
    std::string title;
    std::string manufacturer;
    std::string partConfiguration;
    std::string revision;
    std::string reference;
    std::optional<std::string> sha256;
    friend bool operator==(const RequirementSource&, const RequirementSource&) = default;
};

struct RequirementEvidence {
    std::string sourceId;
    std::optional<int> pdfPage; // One-based, distinct from the printed page/section.
    std::string section;
    RequirementEvidenceKind kind = RequirementEvidenceKind::Unknown;
    std::string interpretation;
    bool reviewed = false;
    std::vector<std::string> derivedFrom;
    std::optional<std::string> derivation;
    friend bool operator==(const RequirementEvidence&, const RequirementEvidence&) = default;
};

struct Requirement {
    std::string id; // Stable key scoped to PartRequirements.
    std::string description;
    RequirementStatus status = RequirementStatus::Unknown;
    std::optional<double> value;
    std::string unit;
    std::optional<double> lowerDeviation;
    std::optional<double> upperDeviation;
    bool critical = false;
    RequirementEvidence evidence;
    std::optional<std::string> textValue; // Mutually exclusive with numeric value.
    friend bool operator==(const Requirement&, const Requirement&) = default;
};

struct DesignVariable {
    std::string id;
    std::string requirementId;
    ParameterRole role = ParameterRole::FixedInterface;
    double value = 0.0; // Same unit as the referenced requirement.
    std::optional<double> minimum;
    std::optional<double> maximum;
    RequirementEvidence evidence;
    friend bool operator==(const DesignVariable&, const DesignVariable&) = default;
};

struct MechanicalInterface {
    std::string id;
    std::string name;
    std::string matingPart;
    std::vector<std::string> parameterIds;
    std::vector<std::string> requirementIds; // Includes unresolved mating information.
    // CAD face references are attached after geometry generation, never guessed.
    std::vector<std::string> topologyReferenceIds;
    friend bool operator==(const MechanicalInterface&, const MechanicalInterface&) = default;
};

struct DesignConstraint {
    std::string id;
    std::string requirementId;
    std::string metric;
    DesignConstraintRelation relation = DesignConstraintRelation::Maximum;
    friend bool operator==(const DesignConstraint&, const DesignConstraint&) = default;
};

struct DesignMetric {
    std::string name;
    double value = 0.0;
    std::string unit;
    std::string evidenceReference; // Solver run or explicit inspection record.
    friend bool operator==(const DesignMetric&, const DesignMetric&) = default;
};

struct DesignCheck {
    std::string constraintId;
    DesignCheckStatus status = DesignCheckStatus::Unknown;
    std::optional<double> measuredValue;
    std::string reason;
    bool required = true;
};

struct DesignEvaluation {
    std::string candidateId;
    std::string geometryRevision;
    std::vector<DesignCheck> checks;
    bool requirementsResolved = false;
    [[nodiscard]] bool passed() const noexcept;
};

struct DesignParameterChange {
    std::string parameterId;
    double previousValue = 0.0;
    double nextValue = 0.0;
    std::string engineeringReason;
    friend bool operator==(const DesignParameterChange&, const DesignParameterChange&) = default;
};

struct PartRequirements {
    std::string id; // Document-wide UUID; nested keys are scoped to this object.
    std::string name;
    std::string revision;
    std::vector<RequirementSource> sources;
    std::vector<Requirement> requirements;
    std::vector<DesignVariable> variables;
    std::vector<MechanicalInterface> interfaces;
    std::vector<DesignConstraint> constraints;
    std::vector<DesignParameterChange> changes;

    void validate() const;
    [[nodiscard]] const Requirement& requirement(const std::string& key) const;
    [[nodiscard]] const DesignVariable& variable(const std::string& key) const;
    [[nodiscard]] std::vector<std::string> unresolvedCriticalRequirements() const;
    [[nodiscard]] double parameterIn(const std::string& key, const std::string& targetUnit) const;
    // Records the change reason. Fixed and standard interfaces cannot move.
    void changeParameter(const std::string& key, double value, const std::string& engineeringReason);
    [[nodiscard]] DesignEvaluation evaluate(const std::string& candidateId,
                                             const std::string& geometryRevision,
                                             const std::vector<DesignMetric>& metrics) const;
    friend bool operator==(const PartRequirements&, const PartRequirements&) = default;
};

// Deliberately bounded unit vocabulary for source ingestion; unknown units fail.
double convertRequirementValue(double value, const std::string& sourceUnit, const std::string& targetUnit);

} // namespace datumloop
