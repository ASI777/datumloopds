#pragma once

#include <optional>
#include <string>
#include <vector>

namespace datumloop {

struct MaterialConstituent {
    std::string chemicalSymbol;
    double massFraction = 0.0;

    friend bool operator==(const MaterialConstituent&, const MaterialConstituent&) = default;
};

struct MaterialPhaseDefinition {
    std::string name;
    std::string crystalStructure;

    friend bool operator==(const MaterialPhaseDefinition&, const MaterialPhaseDefinition&) = default;
};

struct MaterialPhaseFraction {
    std::string phaseName;
    double fraction = 0.0;

    friend bool operator==(const MaterialPhaseFraction&, const MaterialPhaseFraction&) = default;
};

struct MaterialMicrostructure {
    std::optional<double> grainSizeM;
    std::optional<double> porosityFraction;
    std::optional<double> defectDensityPerM2;
    std::optional<double> residualStressPa;
    std::string textureDescription;

    friend bool operator==(const MaterialMicrostructure&, const MaterialMicrostructure&) = default;
};

struct MaterialPropertyPoint {
    double independentValue = 0.0;
    double propertyValue = 0.0;

    friend bool operator==(const MaterialPropertyPoint&, const MaterialPropertyPoint&) = default;
};

enum class MaterialPropertyLaw {
    Constant,
    PiecewiseLinear,
};

enum class MaterialSourceKind {
    ManufacturerDatasheet,
    MaterialDatabase,
    PublishedSource,
    ExperimentalMeasurement,
    UserInput,
    EstimatedAssumption,
    CalibratedValue,
};

[[nodiscard]] std::string materialSourceKindName(MaterialSourceKind kind);
[[nodiscard]] std::optional<MaterialSourceKind> materialSourceKindFromName(const std::string& name);

struct MaterialSource {
    std::string id;
    MaterialSourceKind kind = MaterialSourceKind::UserInput;
    std::string title;
    std::string publisher;
    std::string version;
    std::string reference;
    std::string url;

    void validate() const;

    friend bool operator==(const MaterialSource&, const MaterialSource&) = default;
};

struct MaterialProperty {
    std::string propertyName;
    std::string propertyUnit;
    std::string independentVariable;
    std::string independentUnit;
    MaterialPropertyLaw law = MaterialPropertyLaw::Constant;
    std::optional<double> constantValue;
    std::vector<MaterialPropertyPoint> table;
    std::string source;
    std::string sourceVersion;
    std::optional<double> confidence;
    std::optional<std::string> sourceId;
    // Absolute uncertainty is expressed in propertyUnit. Missing uncertainty
    // and temperature bounds mean the source did not establish those values.
    std::optional<double> absoluteUncertainty;
    std::optional<double> minimumTemperatureK;
    std::optional<double> maximumTemperatureK;

    friend bool operator==(const MaterialProperty&, const MaterialProperty&) = default;
};

using MaterialPropertyModel = MaterialProperty;

struct MaterialProcessHistoryEntry {
    std::string process;
    std::string recordedAt;
    std::optional<double> temperatureK;
    std::optional<double> pressurePa;
    std::optional<double> durationS;
    std::string provenance;

    friend bool operator==(const MaterialProcessHistoryEntry&, const MaterialProcessHistoryEntry&) = default;
};

// A definition contains sourced material knowledge. Missing composition,
// microstructure, and property data stay unknown rather than being inferred.
struct MaterialDefinition {
    std::string id;
    std::string name;
    std::string crystalStructure;
    std::vector<MaterialConstituent> composition;
    std::vector<MaterialPhaseDefinition> phases;
    std::optional<MaterialMicrostructure> microstructure;
    std::vector<MaterialProcessHistoryEntry> processHistory;
    std::vector<MaterialSource> sources;
    std::vector<MaterialPropertyModel> propertyModels;
    std::vector<std::string> provenance;

    void validate() const;

    friend bool operator==(const MaterialDefinition&, const MaterialDefinition&) = default;
};

// State is separate from the nominal definition and can later be associated
// with solver fields without embedding large datasets in a CAD property.
struct MaterialState {
    std::string id;
    std::string materialDefinitionId;
    std::optional<double> temperatureK;
    std::optional<double> pressurePa;
    std::vector<MaterialPhaseFraction> phaseFractions;
    std::optional<MaterialMicrostructure> microstructure;
    std::vector<std::string> fieldDatasetReferences;
    std::vector<std::string> provenance;

    void validate() const;

    friend bool operator==(const MaterialState&, const MaterialState&) = default;
};

}  // namespace datumloop
