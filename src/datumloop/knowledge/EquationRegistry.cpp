#include "datumloop/knowledge/EquationRegistry.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace datumloop {
namespace {

void requireText(const std::string& text, const char* field)
{
    if (text.find_first_not_of(" \t\n\r") == std::string::npos) {
        throw std::invalid_argument(std::string(field) + " must not be empty");
    }
}

bool hasPassed(const std::vector<EquationVerification>& verifications,
               const std::string& method,
               bool analytical = false)
{
    return std::any_of(verifications.begin(), verifications.end(), [&](const EquationVerification& check) {
        return check.status == VerificationStatus::Passed && check.method == method &&
               (!analytical || check.analyticalBenchmark);
    });
}

UnitDimension makeDimension(std::array<int, 7> exponents, std::string symbol)
{
    return {exponents, std::move(symbol)};
}

EquationVariable variable(std::string symbol,
                          std::string name,
                          UnitDimension dimension,
                          EquationQuantityType type = EquationQuantityType::Scalar,
                          std::string frame = {})
{
    return {std::move(symbol), std::move(name), std::move(dimension), type, std::move(frame)};
}

EquationTerm term(std::string expression, UnitDimension dimension)
{
    return {std::move(expression), std::move(dimension)};
}

EquationVerification verification(std::string id,
                                 std::string method,
                                 std::string description,
                                 bool analytical = false,
                                 std::string evidence = {})
{
    return {std::move(id), std::move(method), std::move(description),
            VerificationStatus::Passed, analytical, std::move(evidence)};
}

EquationSource mitSource(std::string title, std::string reference, std::string url)
{
    return {EquationSourceKind::Textbook, std::move(title), "MIT OpenCourseWare", "course material",
            std::move(reference), std::move(url)};
}

}  // namespace

std::string modelMaturityName(ModelMaturity maturity)
{
    switch (maturity) {
    case ModelMaturity::E0: return "E0";
    case ModelMaturity::E1: return "E1";
    case ModelMaturity::E2: return "E2";
    case ModelMaturity::E3: return "E3";
    case ModelMaturity::E4: return "E4";
    case ModelMaturity::E5: return "E5";
    case ModelMaturity::E6: return "E6";
    }
    throw std::invalid_argument("unknown model maturity");
}

bool meetsMaturity(ModelMaturity actual, ModelMaturity required) noexcept
{
    return static_cast<int>(actual) >= static_cast<int>(required);
}

void UnitDimension::validate() const
{
    requireText(unitSymbol, "unit symbol");
}

bool UnitDimension::sameDimension(const UnitDimension& other) const noexcept
{
    return exponents == other.exponents;
}

UnitDimension UnitDimension::dimensionless() { return makeDimension({}, "1"); }
UnitDimension UnitDimension::kilogram() { return makeDimension({1, 0, 0, 0, 0, 0, 0}, "kg"); }
UnitDimension UnitDimension::meter() { return makeDimension({0, 1, 0, 0, 0, 0, 0}, "m"); }
UnitDimension UnitDimension::second() { return makeDimension({0, 0, 1, 0, 0, 0, 0}, "s"); }
UnitDimension UnitDimension::newton() { return makeDimension({1, 1, -2, 0, 0, 0, 0}, "N"); }
UnitDimension UnitDimension::pascal() { return makeDimension({1, -1, -2, 0, 0, 0, 0}, "Pa"); }
UnitDimension UnitDimension::kelvin() { return makeDimension({0, 0, 0, 0, 1, 0, 0}, "K"); }
UnitDimension UnitDimension::wattPerCubicMeter()
{
    return makeDimension({1, -1, -3, 0, 0, 0, 0}, "W/m^3");
}

void EquationVariable::validate() const
{
    requireText(symbol, "equation variable symbol");
    requireText(name, "equation variable name");
    dimension.validate();
    if (quantityType != EquationQuantityType::Scalar) {
        requireText(referenceFrame, "vector/tensor reference frame");
    }
}

void EquationSource::validate() const
{
    requireText(title, "equation source title");
    requireText(reference, "equation source reference");
    if (kind != EquationSourceKind::AgentDerivation && url.empty()) {
        throw std::invalid_argument("literature equation sources require a URL or persistent reference");
    }
}

void EquationVerification::validate() const
{
    requireText(id, "equation verification ID");
    requireText(method, "equation verification method");
    requireText(description, "equation verification description");
    if (status == VerificationStatus::Passed) requireText(evidenceReference, "passed verification evidence");
}

void EquationDefinition::validate() const
{
    requireText(id, "equation ID");
    requireText(name, "equation name");
    requireText(domain, "equation domain");
    requireText(symbolicForm, "symbolic equation form");
    requireText(validityRange, "equation validity range");
    if (variables.empty() || leftTerms.empty() || rightTerms.empty() || sources.empty()) {
        throw std::invalid_argument("equations require variables, dimensional terms, and sources");
    }

    std::unordered_set<std::string> symbols;
    for (const EquationVariable& item : variables) {
        item.validate();
        if (!symbols.insert(item.symbol).second) throw std::invalid_argument("equation variable symbols must be unique");
    }
    const UnitDimension& referenceDimension = leftTerms.front().dimension;
    for (const EquationTerm* item : [&]() {
             std::vector<const EquationTerm*> all;
             all.reserve(leftTerms.size() + rightTerms.size());
             for (const EquationTerm& value : leftTerms) all.push_back(&value);
             for (const EquationTerm& value : rightTerms) all.push_back(&value);
             return all;
         }()) {
        requireText(item->expression, "equation term");
        item->dimension.validate();
        if (!referenceDimension.sameDimension(item->dimension)) {
            throw std::invalid_argument("equation terms have incompatible SI dimensions");
        }
    }

    for (const EquationSource& source : sources) source.validate();
    std::unordered_set<std::string> verificationIds;
    for (const EquationVerification& check : verifications) {
        check.validate();
        if (!verificationIds.insert(check.id).second) {
            throw std::invalid_argument("equation verification IDs must be unique");
        }
    }
    if (maturity >= ModelMaturity::E2 &&
        (!hasPassed(verifications, "dimensional-analysis") || !hasPassed(verifications, "symbolic-check"))) {
        throw std::invalid_argument("E2 and later equations require passed dimensional and symbolic checks");
    }
    if (maturity >= ModelMaturity::E3 && !hasPassed(verifications, "analytical-benchmark", true)) {
        throw std::invalid_argument("E3 and later equations require a passed analytical benchmark");
    }
    if (maturity >= ModelMaturity::E4 && !hasPassed(verifications, "independent-numerical")) {
        throw std::invalid_argument("E4 and later equations require independent numerical verification");
    }
    if (maturity >= ModelMaturity::E5 && !hasPassed(verifications, "published-experiment")) {
        throw std::invalid_argument("E5 and later equations require published experimental validation");
    }
    if (maturity >= ModelMaturity::E6 && !hasPassed(verifications, "hardware-measurement")) {
        throw std::invalid_argument("E6 and later equations require DatumLoop hardware measurements");
    }
}

void EquationRegistry::add(EquationDefinition equation)
{
    equation.validate();
    if (find(equation.id) != nullptr) throw std::invalid_argument("equation registry IDs must be unique");
    equations_.push_back(std::move(equation));
}

const EquationDefinition* EquationRegistry::find(const std::string& id) const noexcept
{
    const auto found = std::find_if(equations_.begin(), equations_.end(), [&id](const EquationDefinition& item) {
        return item.id == id;
    });
    return found == equations_.end() ? nullptr : &*found;
}

const EquationDefinition* EquationRegistry::findAtLeast(const std::string& id,
                                                         ModelMaturity required) const noexcept
{
    const EquationDefinition* result = find(id);
    return result != nullptr && meetsMaturity(result->maturity, required) ? result : nullptr;
}

EquationRegistry makeInitialEquationRegistry()
{
    const UnitDimension force = UnitDimension::newton();
    const UnitDimension acceleration = makeDimension({0, 1, -2, 0, 0, 0, 0}, "m/s^2");
    const UnitDimension density = makeDimension({1, -3, 0, 0, 0, 0, 0}, "kg/m^3");
    const UnitDimension heatCapacity = makeDimension({0, 2, -2, 0, -1, 0, 0}, "J/(kg K)");
    const UnitDimension temperatureRate = makeDimension({0, 0, -1, 0, 1, 0, 0}, "K/s");
    const UnitDimension conductivity = makeDimension({1, 1, -3, 0, -1, 0, 0}, "W/(m K)");
    const UnitDimension temperatureGradient = makeDimension({0, -1, 0, 0, 1, 0, 0}, "K/m");
    const UnitDimension heatGeneration = UnitDimension::wattPerCubicMeter();
    const UnitDimension modulus = UnitDimension::pascal();

    EquationDefinition newton;
    newton.id = "mechanics.newton.second-law.translation.v1";
    newton.name = "Newton's second law for translation";
    newton.domain = "mechanics";
    newton.symbolicForm = "sum(F) = m a";
    newton.variables = {
        variable("F", "net force", force, EquationQuantityType::Vector, "inertial-frame"),
        variable("m", "mass", UnitDimension::kilogram()),
        variable("a", "acceleration", acceleration, EquationQuantityType::Vector, "inertial-frame"),
    };
    newton.leftTerms = {term("sum(F)", force)};
    newton.rightTerms = {term("m a", force)};
    newton.assumptions = {"Classical mechanics", "Constant mass", "Inertial reference frame"};
    newton.validityRange = "Macroscopic motion at speeds small compared with the speed of light; frame is inertial.";
    newton.sources = {mitSource("11.1 Newton's 2nd Law and Circular Motion", "MIT 8.01SC, Fall 2016",
                                 "https://ocw.mit.edu/courses/8-01sc-classical-mechanics-fall-2016/resources/11/")};
    newton.verifications = {
        verification("newton-dimensions-v1", "dimensional-analysis", "N and kg m/s^2 have dimensions M L T^-2.", false, "unit-vector calculation"),
        verification("newton-symbols-v1", "symbolic-check", "The registered relation keeps net force and mass-times-acceleration as equal vector quantities.", false, "canonical relation review"),
        verification("newton-constant-force-v1", "analytical-benchmark", "Constant-force trajectory agrees with x=x0+v0 t+0.5(F/m)t^2.", true, "tests/native/test_knowledge.cpp"),
    };
    newton.maturity = ModelMaturity::E3;

    EquationDefinition heat;
    heat.id = "thermal.heat-equation.isotropic.v1";
    heat.name = "Transient heat conduction with volumetric source";
    heat.domain = "thermal";
    heat.symbolicForm = "rho cp dT/dt = div(k grad(T)) + Q";
    heat.variables = {
        variable("rho", "mass density", density),
        variable("cp", "specific heat capacity", heatCapacity),
        variable("dT/dt", "temperature rate", temperatureRate),
        variable("k", "isotropic thermal conductivity", conductivity),
        variable("grad(T)", "temperature gradient", temperatureGradient, EquationQuantityType::Vector, "spatial-coordinate"),
        variable("Q", "volumetric heat source", heatGeneration),
    };
    heat.leftTerms = {term("rho cp dT/dt", heatGeneration)};
    heat.rightTerms = {term("div(k grad(T))", heatGeneration), term("Q", heatGeneration)};
    heat.assumptions = {"Continuum medium", "Local thermal equilibrium", "Scalar isotropic conductivity", "No bulk advection"};
    heat.validityRange = "Classical Fourier conduction within the continuum and material-property validity ranges; radiation and fluid advection are excluded.";
    heat.sources = {mitSource("Lecture 9: Thermodynamic and heat transfer equations", "MIT 12.800, Fall 2004, equations 9.37-9.38",
                               "https://ocw.mit.edu/courses/12-800-fluid-dynamics-of-the-atmosphere-and-ocean-fall-2004/d9d0ff4b4084467d756b69f612327c3d_lecture9.pdf")};
    heat.verifications = {
        verification("heat-dimensions-v1", "dimensional-analysis", "Both sides and Q have SI dimensions W/m^3.", false, "unit-vector calculation"),
        verification("heat-symbols-v1", "symbolic-check", "Fourier flux q=-k grad(T) gives the registered divergence term with positive source sign.", false, "MIT OCW equations 9.37-9.38"),
        verification("heat-linear-steady-v1", "analytical-benchmark", "The one-dimensional, source-free steady limit produces a linear temperature field and constant heat flux.", true, "tests/native/test_knowledge.cpp"),
    };
    heat.maturity = ModelMaturity::E3;

    EquationDefinition elasticity;
    elasticity.id = "materials.elasticity.uniaxial-linear.v1";
    elasticity.name = "Uniaxial linear-elastic constitutive relation";
    elasticity.domain = "structural";
    elasticity.symbolicForm = "sigma = E epsilon";
    elasticity.variables = {
        variable("sigma", "uniaxial normal stress", modulus),
        variable("E", "Young's modulus", modulus),
        variable("epsilon", "uniaxial engineering strain", UnitDimension::dimensionless()),
    };
    elasticity.leftTerms = {term("sigma", modulus)};
    elasticity.rightTerms = {term("E epsilon", modulus)};
    elasticity.assumptions = {"Small strain", "Uniaxial stress state", "Linear elastic response", "Material remains within its elastic range"};
    elasticity.validityRange = "Small-strain uniaxial loading within the material's linear elastic range.";
    elasticity.sources = {mitSource("Lecture 4: Development of Constitutive Equations", "MIT 2.080J, Fall 2013, equation 4.1",
                                    "https://ocw.mit.edu/courses/2-080j-structural-mechanics-fall-2013/32670f14cec210d98c5c7fe9dbf73eb6_MIT2_080JF13_Lecture4.pdf")};
    elasticity.verifications = {
        verification("elasticity-dimensions-v1", "dimensional-analysis", "Stress and Young's modulus both have SI dimension Pa; strain is dimensionless.", false, "unit-vector calculation"),
        verification("elasticity-symbols-v1", "symbolic-check", "Uniaxial stress is proportional to strain with Young's modulus as the proportionality parameter.", false, "MIT OCW equation 4.1"),
        verification("elasticity-rod-v1", "analytical-benchmark", "The rod response agrees with delta=P L/(A E) for the linear uniaxial model.", true, "tests/native/test_knowledge.cpp"),
    };
    elasticity.maturity = ModelMaturity::E3;

    EquationRegistry registry;
    registry.add(std::move(newton));
    registry.add(std::move(heat));
    registry.add(std::move(elasticity));
    return registry;
}

}  // namespace datumloop
