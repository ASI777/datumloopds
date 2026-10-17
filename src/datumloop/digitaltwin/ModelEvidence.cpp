#include "datumloop/digitaltwin/ModelEvidence.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace datumloop {
namespace {

void requireText(const std::string& value, const char* field)
{
    if (value.find_first_not_of(" \t\n\r") == std::string::npos) {
        throw std::invalid_argument(std::string(field) + " must not be empty");
    }
}

std::size_t componentCount(EquationQuantityType type)
{
    switch (type) {
    case EquationQuantityType::Scalar: return 1;
    case EquationQuantityType::Vector: return 3;
    case EquationQuantityType::Tensor2: return 9;
    }
    throw std::invalid_argument("unknown observable quantity type");
}

}  // namespace

void ParameterProvenance::validate() const
{
    requireText(sourceId, "parameter source ID");
    requireText(method, "parameter provenance method");
    requireText(description, "parameter provenance description");
    switch (kind) {
    case ParameterSourceKind::ManufacturerDatasheet:
    case ParameterSourceKind::Cad:
    case ParameterSourceKind::MaterialDatabase:
    case ParameterSourceKind::Experiment:
    case ParameterSourceKind::SystemIdentification:
    case ParameterSourceKind::ManualAssumption: break;
    default: throw std::invalid_argument("unknown parameter source kind");
    }
    if ((kind == ParameterSourceKind::Experiment || kind == ParameterSourceKind::SystemIdentification) &&
        datasetId.empty()) {
        throw std::invalid_argument("measured and identified parameters require a dataset ID");
    }
}

void ParameterEstimate::validate() const
{
    requireText(id, "parameter estimate ID");
    dimension.validate();
    if (!std::isfinite(nominalValue)) throw std::invalid_argument("nominal parameter value must be finite");
    if (estimatedValue && !std::isfinite(*estimatedValue)) {
        throw std::invalid_argument("estimated parameter value must be finite");
    }
    if (standardUncertainty && (!estimatedValue || !std::isfinite(*standardUncertainty) || *standardUncertainty < 0.0)) {
        throw std::invalid_argument("parameter uncertainty requires an estimate and a finite non-negative value");
    }
    provenance.validate();
}

double ParameterEstimate::valueForSimulation(bool useEstimate) const
{
    validate();
    return useEstimate && estimatedValue ? *estimatedValue : nominalValue;
}

void ObservableValue::validate() const
{
    dimension.validate();
    requireText(provenanceReference, "observable value provenance reference");
    if (components.size() != componentCount(quantityType)) {
        throw std::invalid_argument("observable component count does not match scalar/vector/tensor semantics");
    }
    if (!std::all_of(components.begin(), components.end(), [](double value) { return std::isfinite(value); })) {
        throw std::invalid_argument("observable components must be finite");
    }
    if (quantityType == EquationQuantityType::Scalar) {
        if (!referenceFrame.empty() || !coordinateSystem.empty()) {
            throw std::invalid_argument("scalar observables cannot declare vector frame metadata");
        }
    } else {
        requireText(referenceFrame, "vector/tensor observable reference frame");
        requireText(coordinateSystem, "vector/tensor observable coordinate system");
    }
}

void ObservableResidual::validate() const
{
    requireText(observableId, "observable residual ID");
    if (!std::isfinite(timeS) || timeS < 0.0) throw std::invalid_argument("observable timestamp must be finite and non-negative");
    predicted.validate();
    measured.validate();
    if (predicted.quantityType != measured.quantityType ||
        !predicted.dimension.sameDimension(measured.dimension) ||
        predicted.dimension.unitSymbol != measured.dimension.unitSymbol ||
        predicted.referenceFrame != measured.referenceFrame ||
        predicted.coordinateSystem != measured.coordinateSystem) {
        throw std::invalid_argument("predicted and measured observables must share dimensions and reference-frame semantics");
    }
}

std::vector<double> ObservableResidual::residual() const
{
    validate();
    std::vector<double> result(measured.components.size());
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = measured.components[index] - predicted.components[index];
    }
    return result;
}

double ObservableResidual::rmsResidual() const
{
    const std::vector<double> values = residual();
    double sumSquares = 0.0;
    for (const double value : values) sumSquares += value * value;
    return std::sqrt(sumSquares / static_cast<double>(values.size()));
}

void ModelDiscrepancy::add(ObservableResidual residualValue)
{
    residualValue.validate();
    const auto duplicate = std::find_if(observables.begin(), observables.end(), [&](const auto& existing) {
        return existing.observableId == residualValue.observableId && existing.timeS == residualValue.timeS;
    });
    if (duplicate != observables.end()) throw std::invalid_argument("model discrepancy contains a duplicate observable timestamp");
    observables.push_back(std::move(residualValue));
}

void ModelDiscrepancy::validate() const
{
    requireText(modelId, "discrepancy model ID");
    requireText(modelRevision, "discrepancy model revision");
    if (observables.empty()) throw std::invalid_argument("model discrepancy requires at least one observable residual");
    for (std::size_t index = 0; index < observables.size(); ++index) {
        observables[index].validate();
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (observables[index].observableId == observables[previous].observableId &&
                observables[index].timeS == observables[previous].timeS) {
                throw std::invalid_argument("model discrepancy contains a duplicate observable timestamp");
            }
        }
    }
}

}  // namespace datumloop
