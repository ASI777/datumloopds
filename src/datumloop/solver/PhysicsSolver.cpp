#include "datumloop/solver/PhysicsSolver.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace datumloop {
namespace {

void requireText(const std::string& value, const char* field)
{
    if (value.find_first_not_of(" \t\n\r") == std::string::npos) {
        throw std::invalid_argument(std::string(field) + " must not be empty");
    }
}

}  // namespace

void SolverRequest::validate(const EquationRegistry& registry) const
{
    requireText(requestId, "solver request ID");
    requireText(equationDefinitionId, "solver equation ID");
    requireText(designRevision, "solver design revision");
    if (!std::isfinite(absoluteTolerance) || absoluteTolerance <= 0.0 ||
        !std::isfinite(relativeTolerance) || relativeTolerance <= 0.0) {
        throw std::invalid_argument("solver tolerances must be finite and positive");
    }
    const EquationDefinition* equation = registry.findAtLeast(equationDefinitionId, minimumMaturity);
    if (equation == nullptr) {
        throw std::invalid_argument("requested equation is missing or below the required maturity level");
    }

    std::unordered_set<std::string> inputNames;
    for (const auto& [name, value] : inputs) {
        requireText(name, "solver input name");
        if (!inputNames.insert(name).second) throw std::invalid_argument("solver input names must be unique");
        if (!std::isfinite(value.value)) throw std::invalid_argument("solver input values must be finite");
        value.dimension.validate();
        const auto variable = std::find_if(equation->variables.begin(), equation->variables.end(),
                                           [&name](const EquationVariable& candidate) {
                                               return candidate.symbol == name;
                                           });
        if (variable == equation->variables.end()) throw std::invalid_argument("solver input is not a registered equation variable");
        if (!value.dimension.sameDimension(variable->dimension) ||
            value.dimension.unitSymbol != variable->dimension.unitSymbol) {
            throw std::invalid_argument("solver input unit does not exactly match its registered equation variable unit");
        }
    }
}

void TransientSolverRequest::validate(const EquationRegistry& registry) const
{
    model.validate(registry);
    if (!std::isfinite(startTimeS) || !std::isfinite(endTimeS) || !std::isfinite(timeStepS) ||
        startTimeS < 0.0 || endTimeS <= startTimeS || timeStepS <= 0.0 || timeStepS > endTimeS - startTimeS) {
        throw std::invalid_argument("transient solver time range and step must be finite and positive");
    }
}

void PhysicsSolverResult::validateFor(const SolverRequest& request) const
{
    if (requestId != request.requestId) throw std::invalid_argument("solver result request ID does not match its request");
    requireText(solverId, "solver result ID");
    requireText(solverVersion, "solver result version");
    if (status == SolverResultStatus::Failed || status == SolverResultStatus::NotConverged) {
        requireText(detail, "failed solver result detail");
    }
    if (status != SolverResultStatus::Succeeded && !fields.empty()) {
        throw std::invalid_argument("unsuccessful solver result cannot publish field values");
    }
    std::unordered_set<std::string> names;
    for (const SolverResultField& field : fields) {
        requireText(field.name, "solver result field name");
        if (!names.insert(field.name).second) throw std::invalid_argument("solver result field names must be unique");
        field.dimension.validate();
        const bool hasScalar = field.scalarValue.has_value();
        const bool hasDataset = !field.datasetReference.empty();
        if (hasScalar == hasDataset) {
            throw std::invalid_argument("solver result field must contain exactly one scalar value or dataset reference");
        }
        if (hasScalar && !std::isfinite(*field.scalarValue)) {
            throw std::invalid_argument("solver result scalar field value must be finite");
        }
    }
}

}  // namespace datumloop
