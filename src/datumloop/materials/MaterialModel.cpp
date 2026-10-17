#include "datumloop/materials/MaterialModel.hpp"

#include "datumloop/core/DatumUuid.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace datumloop {
namespace {

bool hasText(const std::string& value)
{
    return value.find_first_not_of(" \t\n\r") != std::string::npos;
}

void requireOptionalPositive(const std::optional<double>& value, const char* field)
{
    if (value.has_value() && (!std::isfinite(*value) || *value <= 0.0)) {
        throw std::invalid_argument(std::string(field) + " must be finite and positive when specified");
    }
}

void validateMicrostructure(const MaterialMicrostructure& value)
{
    requireOptionalPositive(value.grainSizeM, "grainSizeM");
    requireOptionalPositive(value.defectDensityPerM2, "defectDensityPerM2");
    if (value.porosityFraction.has_value() &&
        (!std::isfinite(*value.porosityFraction) || *value.porosityFraction < 0.0 ||
         *value.porosityFraction >= 1.0)) {
        throw std::invalid_argument("porosityFraction must be in [0, 1)");
    }
    if (value.residualStressPa.has_value() && !std::isfinite(*value.residualStressPa)) {
        throw std::invalid_argument("residualStressPa must be finite when specified");
    }
}

void validatePhaseFractions(const std::vector<MaterialPhaseFraction>& values)
{
    std::unordered_set<std::string> names;
    double total = 0.0;
    for (const MaterialPhaseFraction& item : values) {
        if (!hasText(item.phaseName) || !names.insert(item.phaseName).second ||
            !std::isfinite(item.fraction) || item.fraction < 0.0 || item.fraction > 1.0) {
            throw std::invalid_argument("phase fractions require unique names and values in [0, 1]");
        }
        total += item.fraction;
    }
    if (total > 1.0 + 1e-9) {
        throw std::invalid_argument("phase fractions cannot sum to more than one");
    }
}

}  // namespace

void MaterialDefinition::validate() const
{
    if (!isDatumUuid(id) || !hasText(name)) {
        throw std::invalid_argument("material definition requires a UUID and non-empty name");
    }

    std::unordered_set<std::string> constituentSymbols;
    double totalMassFraction = 0.0;
    for (const MaterialConstituent& constituent : composition) {
        if (!hasText(constituent.chemicalSymbol) || !constituentSymbols.insert(constituent.chemicalSymbol).second ||
            !std::isfinite(constituent.massFraction) || constituent.massFraction < 0.0 ||
            constituent.massFraction > 1.0) {
            throw std::invalid_argument("material composition requires unique species and mass fractions in [0, 1]");
        }
        totalMassFraction += constituent.massFraction;
    }
    if (!composition.empty() && std::abs(totalMassFraction - 1.0) > 1e-6) {
        throw std::invalid_argument("specified material mass fractions must sum to one");
    }

    std::unordered_set<std::string> phaseNames;
    for (const MaterialPhaseDefinition& phase : phases) {
        if (!hasText(phase.name) || !phaseNames.insert(phase.name).second) {
            throw std::invalid_argument("material phase definitions require unique non-empty names");
        }
    }
    if (microstructure.has_value()) {
        validateMicrostructure(*microstructure);
    }
    for (const MaterialProcessHistoryEntry& step : processHistory) {
        if (!hasText(step.process)) {
            throw std::invalid_argument("material process history requires a process name");
        }
        requireOptionalPositive(step.temperatureK, "process temperatureK");
        requireOptionalPositive(step.pressurePa, "process pressurePa");
        requireOptionalPositive(step.durationS, "process durationS");
    }
    for (const MaterialPropertyModel& property : propertyModels) {
        if (!hasText(property.propertyName) || !hasText(property.propertyUnit) || !hasText(property.source)) {
            throw std::invalid_argument("material property models require a name, unit, and provenance source");
        }
        if (property.confidence.has_value() &&
            (!std::isfinite(*property.confidence) || *property.confidence < 0.0 || *property.confidence > 1.0)) {
            throw std::invalid_argument("material property confidence must be in [0, 1]");
        }
        if (property.law == MaterialPropertyLaw::Constant) {
            if (!property.constantValue.has_value() || !std::isfinite(*property.constantValue) ||
                !property.table.empty()) {
                throw std::invalid_argument("constant material property requires one finite value and no table");
            }
        } else if (property.law == MaterialPropertyLaw::PiecewiseLinear) {
            if (property.independentVariable.empty() || property.independentUnit.empty() ||
                property.table.size() < 2 || property.constantValue.has_value()) {
                throw std::invalid_argument("piecewise material property requires an independent variable and at least two points");
            }
            double previous = -std::numeric_limits<double>::infinity();
            for (const MaterialPropertyPoint& point : property.table) {
                if (!std::isfinite(point.independentValue) || !std::isfinite(point.propertyValue) ||
                    point.independentValue <= previous) {
                    throw std::invalid_argument("material property table coordinates must increase strictly and remain finite");
                }
                previous = point.independentValue;
            }
        } else {
            throw std::invalid_argument("unknown material property law");
        }
    }
}

void MaterialState::validate() const
{
    if (!isDatumUuid(id) || !isDatumUuid(materialDefinitionId)) {
        throw std::invalid_argument("material state requires UUID identities");
    }
    requireOptionalPositive(temperatureK, "temperatureK");
    requireOptionalPositive(pressurePa, "pressurePa");
    if (microstructure.has_value()) {
        validateMicrostructure(*microstructure);
    }
    validatePhaseFractions(phaseFractions);
    for (const std::string& reference : fieldDatasetReferences) {
        if (!hasText(reference)) {
            throw std::invalid_argument("material field dataset references must not be empty");
        }
    }
}

}  // namespace datumloop
