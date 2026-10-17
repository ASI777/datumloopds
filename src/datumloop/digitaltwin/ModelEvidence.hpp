#pragma once

#include "datumloop/knowledge/EquationRegistry.hpp"

#include <optional>
#include <string>
#include <vector>

namespace datumloop {

enum class ParameterSourceKind {
    ManufacturerDatasheet,
    Cad,
    MaterialDatabase,
    Experiment,
    SystemIdentification,
    ManualAssumption,
};

struct ParameterProvenance {
    ParameterSourceKind kind = ParameterSourceKind::ManualAssumption;
    std::string sourceId;
    std::string method;
    std::string datasetId;
    std::string description;

    void validate() const;
};

struct ParameterEstimate {
    std::string id;
    UnitDimension dimension;
    double nominalValue = 0.0;
    std::optional<double> estimatedValue;
    std::optional<double> standardUncertainty;
    ParameterProvenance provenance;

    void validate() const;
    [[nodiscard]] double valueForSimulation(bool useEstimate = true) const;
};

struct ObservableValue {
    EquationQuantityType quantityType = EquationQuantityType::Scalar;
    UnitDimension dimension;
    std::string referenceFrame;
    std::string coordinateSystem;
    std::vector<double> components;
    std::string provenanceReference;

    void validate() const;
};

struct ObservableResidual {
    std::string observableId;
    double timeS = 0.0;
    ObservableValue predicted;
    ObservableValue measured;

    void validate() const;
    [[nodiscard]] std::vector<double> residual() const;
    [[nodiscard]] double rmsResidual() const;
};

// Stores measured-minus-predicted residuals. It never mutates the model.
struct ModelDiscrepancy {
    std::string modelId;
    std::string modelRevision;
    std::vector<ObservableResidual> observables;

    void add(ObservableResidual residual);
    void validate() const;
};

}  // namespace datumloop
