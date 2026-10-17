#include "datumloop/engineering/PartDesign.hpp"
#include "datumloop/core/DatumUuid.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace datumloop {
namespace {
template<class T, std::size_t N> std::string enumName(T value, const std::array<const char*, N>& names)
{
    const auto index = static_cast<std::size_t>(value);
    if (index >= N) throw std::invalid_argument("invalid part-design enumeration");
    return names[index];
}
void need(bool condition, const char* message) { if (!condition) throw std::invalid_argument(message); }
void text(const std::string& value) { need(value.find_first_not_of(" \t\n\r") != std::string::npos, "part-design text must not be empty"); }
struct Unit { const char* dimension; double scale; };
Unit unit(const std::string& symbol)
{
    static const std::map<std::string, Unit> units{
        {"1", {"dimensionless", 1}}, {"mm", {"length", 1e-3}}, {"m", {"length", 1}},
        {"um", {"length", 1e-6}}, {"g", {"mass", 1e-3}}, {"kg", {"mass", 1}},
        {"N", {"force", 1}}, {"N*m", {"torque", 1}}, {"mN*m", {"torque", 1e-3}},
        {"Pa", {"stress", 1}}, {"MPa", {"stress", 1e6}}, {"GPa", {"stress", 1e9}},
        {"deg", {"angle", std::acos(-1.0) / 180}}, {"rad", {"angle", 1}},
        {"m/s^2", {"acceleration", 1}}, {"kg/m^3", {"density", 1}},
        {"N/m", {"stiffness", 1}}, {"J", {"energy", 1}},
    };
    auto found = units.find(symbol);
    need(found != units.end(), "unsupported requirement unit");
    return found->second;
}
template<class T> void keys(const std::vector<T>& values)
{
    std::unordered_set<std::string> seen;
    for (const auto& value : values) { text(value.id); need(seen.insert(value.id).second, "duplicate scoped part-design key"); }
}
} // namespace

std::string partDesignName(RequirementStatus v) { return enumName(v, std::array{"REQUIRED","PREFERRED","DERIVED","ASSUMED","UNKNOWN"}); }
std::string partDesignName(RequirementSourceType v) { return enumName(v, std::array{"PDF_MANUAL","DIMENSIONED_DRAWING","MANUFACTURER_DATASHEET","STEP_REFERENCE","ENGINEERING_SPECIFICATION"}); }
std::string partDesignName(RequirementEvidenceKind v) { return enumName(v, std::array{"PRINTED","DERIVED","STANDARD_DEFINED","MEASURED","DESIGN_SELECTION","ASSUMPTION","UNKNOWN"}); }
std::string partDesignName(ParameterRole v) { return enumName(v, std::array{"FIXED_INTERFACE","STANDARD_DEFINED","BOUNDED_VARIABLE","FREE_VARIABLE"}); }
std::string partDesignName(DesignConstraintRelation v) { return enumName(v, std::array{"MAXIMUM","MINIMUM","EQUAL"}); }
std::string partDesignName(DesignCheckStatus v) { return enumName(v, std::array{"PASSED","FAILED","UNKNOWN"}); }

double convertRequirementValue(double value, const std::string& from, const std::string& to)
{
    need(std::isfinite(value), "requirement value must be finite");
    const Unit source = unit(from), target = unit(to);
    need(std::string(source.dimension) == target.dimension, "requirement units are dimensionally incompatible");
    const double result = value * source.scale / target.scale;
    need(std::isfinite(result), "converted requirement value is not finite");
    return result;
}
const Requirement& PartRequirements::requirement(const std::string& key) const
{
    const auto found = std::find_if(requirements.begin(), requirements.end(), [&](const auto& r) { return r.id == key; });
    if (found == requirements.end()) throw std::invalid_argument("unknown requirement key: " + key);
    return *found;
}
const DesignVariable& PartRequirements::variable(const std::string& key) const
{
    const auto found = std::find_if(variables.begin(), variables.end(), [&](const auto& v) { return v.id == key; });
    if (found == variables.end()) throw std::invalid_argument("unknown design parameter: " + key);
    return *found;
}
void PartRequirements::validate() const
{
    need(isDatumUuid(id), "part requirements must have a document UUID"); text(name); text(revision);
    keys(sources); keys(requirements); keys(variables); keys(interfaces); keys(constraints);
    for (const auto& s : sources) {
        (void)partDesignName(s.type); text(s.title); text(s.partConfiguration); text(s.revision); text(s.reference);
        if (s.sha256) need(s.sha256->size() == 64 && s.sha256->find_first_not_of("0123456789abcdef") == std::string::npos,
                           "source SHA256 must contain 64 lowercase hexadecimal digits");
    }
    const auto evidence = [&](const RequirementEvidence& e) {
        (void)partDesignName(e.kind); text(e.section); text(e.interpretation);
        need(std::any_of(sources.begin(), sources.end(), [&](const auto& s) { return s.id == e.sourceId; }), "evidence must reference a known source");
        if (e.pdfPage) need(*e.pdfPage > 0, "PDF page must be one-based");
        for (const auto& key : e.derivedFrom) { const auto& r = requirement(key); need(r.value.has_value(), "derivation depends on unknown requirement"); }
        if (e.kind == RequirementEvidenceKind::Derived) need(e.derivation && !e.derivedFrom.empty(), "derived evidence requires a formula and source requirements");
        if (e.derivation) text(*e.derivation);
    };
    for (const auto& r : requirements) {
        text(r.description); (void)partDesignName(r.status); (void)unit(r.unit); evidence(r.evidence);
        need(!(r.value && r.textValue), "requirement cannot have numeric and text values together");
        need((r.status == RequirementStatus::Unknown) == !(r.value || r.textValue), "UNKNOWN requirements must have no value; known requirements require a value");
        if (r.textValue) { text(*r.textValue); need(r.unit == "1", "text requirements must be dimensionless"); }
        if (r.value) need(std::isfinite(*r.value), "requirement must be finite");
        if (r.value || r.textValue) need(r.evidence.kind != RequirementEvidenceKind::Unknown, "known requirement needs explicit evidence");
        if (r.status == RequirementStatus::Derived) need(r.evidence.kind == RequirementEvidenceKind::Derived, "DERIVED requirement needs derived evidence");
        need(r.lowerDeviation.has_value() == r.upperDeviation.has_value(), "both tolerance deviations must be given together");
        if (r.lowerDeviation) need(r.value && std::isfinite(*r.lowerDeviation) && std::isfinite(*r.upperDeviation) &&
                                    *r.lowerDeviation <= 0 && *r.upperDeviation >= 0, "invalid requirement tolerance deviations");
    }
    // A derivation is evidence, not an equation evaluator. Reject circular evidence graphs.
    std::unordered_set<std::string> visiting, visited;
    const auto visit = [&](const auto& self, const Requirement& r) -> void {
        if (visited.contains(r.id)) return;
        need(visiting.insert(r.id).second, "cyclic requirement derivation");
        for (const auto& key : r.evidence.derivedFrom) self(self, requirement(key));
        visiting.erase(r.id); visited.insert(r.id);
    };
    for (const auto& r : requirements) visit(visit, r);
    for (const auto& v : variables) {
        (void)partDesignName(v.role); evidence(v.evidence);
        const auto& r = requirement(v.requirementId);
        need(r.value && std::isfinite(v.value), "design parameters require a known source value");
        need(v.evidence.reviewed, "design parameters require reviewed evidence or explicit selection");
        if (v.minimum) need(std::isfinite(*v.minimum) && v.value >= *v.minimum, "parameter below its lower bound");
        if (v.maximum) need(std::isfinite(*v.maximum) && v.value <= *v.maximum, "parameter above its upper bound");
        if (v.minimum && v.maximum) need(*v.minimum <= *v.maximum, "parameter bounds are inverted");
        if (v.role == ParameterRole::BoundedVariable) need(v.minimum && v.maximum, "bounded variable requires both bounds");
        if (v.role == ParameterRole::FixedInterface || v.role == ParameterRole::StandardDefined)
            need(v.value == *r.value, "fixed or standard dimension differs from its requirement");
    }
    for (const auto& i : interfaces) {
        text(i.name); text(i.matingPart); need(!i.parameterIds.empty() || !i.requirementIds.empty(), "mechanical interface requires parameters or requirements");
        for (const auto& key : i.requirementIds) (void)requirement(key);
        std::unordered_set<std::string> seen;
        for (const auto& key : i.parameterIds) { (void)variable(key); need(seen.insert(key).second, "interface repeats a parameter"); }
        for (const auto& key : i.topologyReferenceIds) need(isDatumUuid(key), "interface topology reference must be a UUID");
    }
    for (const auto& c : constraints) { (void)requirement(c.requirementId); text(c.metric); (void)partDesignName(c.relation); }
    for (const auto& c : changes) {
        const auto& v = variable(c.parameterId); text(c.engineeringReason);
        need(v.role == ParameterRole::BoundedVariable || v.role == ParameterRole::FreeVariable, "change history modifies a protected interface");
        need(std::isfinite(c.previousValue) && std::isfinite(c.nextValue), "non-finite design change");
    }
}
std::vector<std::string> PartRequirements::unresolvedCriticalRequirements() const
{
    std::vector<std::string> result;
    for (const auto& r : requirements) if (r.critical && (r.status == RequirementStatus::Unknown ||
        r.status == RequirementStatus::Assumed || !r.evidence.reviewed)) result.push_back(r.id);
    return result;
}
double PartRequirements::parameterIn(const std::string& key, const std::string& targetUnit) const
{
    const auto& v = variable(key);
    return convertRequirementValue(v.value, requirement(v.requirementId).unit, targetUnit);
}
void PartRequirements::changeParameter(const std::string& key, double next, const std::string& reason)
{
    validate(); text(reason);
    const auto& previous = variable(key);
    need(previous.role == ParameterRole::BoundedVariable || previous.role == ParameterRole::FreeVariable, "automatic redesign cannot modify a fixed or standard interface");
    PartRequirements changed = *this;
    auto& target = *std::find_if(changed.variables.begin(), changed.variables.end(), [&](const auto& v) { return v.id == key; });
    target.value = next;
    changed.changes.push_back({key, previous.value, next, reason});
    changed.validate();
    *this = std::move(changed);
}
bool DesignEvaluation::passed() const noexcept
{
    return requirementsResolved && !checks.empty() && std::all_of(checks.begin(), checks.end(), [](const auto& c) { return !c.required || c.status == DesignCheckStatus::Passed; });
}
DesignEvaluation PartRequirements::evaluate(const std::string& candidate, const std::string& geometry,
                                           const std::vector<DesignMetric>& metrics) const
{
    validate(); text(candidate); text(geometry);
    std::unordered_set<std::string> metricNames;
    for (const auto& m : metrics) {
        text(m.name); text(m.evidenceReference); (void)unit(m.unit);
        need(std::isfinite(m.value) && metricNames.insert(m.name).second, "metrics must be finite and unique");
    }
    DesignEvaluation result{candidate, geometry, {}, unresolvedCriticalRequirements().empty()};
    for (const auto& c : constraints) {
        const auto& r = requirement(c.requirementId);
        const auto found = std::find_if(metrics.begin(), metrics.end(), [&](const auto& m) { return m.name == c.metric; });
        DesignCheck check{c.id, DesignCheckStatus::Unknown, std::nullopt, "Missing requirement value or measurement"};
        if (r.value && found != metrics.end()) {
            const double value = convertRequirementValue(found->value, found->unit, r.unit);
            const double lower = *r.value + r.lowerDeviation.value_or(0);
            const double upper = *r.value + r.upperDeviation.value_or(0);
            bool passed = c.relation == DesignConstraintRelation::Maximum ? value <= upper :
                c.relation == DesignConstraintRelation::Minimum ? value >= lower : value >= lower && value <= upper;
            check = {c.id, passed ? DesignCheckStatus::Passed : DesignCheckStatus::Failed, value, found->evidenceReference};
        }
        check.required = r.status != RequirementStatus::Preferred;
        result.checks.push_back(std::move(check));
    }
    return result;
}
} // namespace datumloop
