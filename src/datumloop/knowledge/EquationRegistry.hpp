#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace datumloop {

enum class ModelMaturity {
    E0,  // Unreviewed hypothesis.
    E1,  // Literature sourced.
    E2,  // Dimensional and symbolic checks passed.
    E3,  // Analytical benchmark passed.
    E4,  // Independently implemented numerical cross-check passed.
    E5,  // Published experimental validation.
    E6,  // DatumLoop hardware validation.
};

[[nodiscard]] std::string modelMaturityName(ModelMaturity maturity);
[[nodiscard]] bool meetsMaturity(ModelMaturity actual, ModelMaturity required) noexcept;

struct UnitDimension {
    // SI base dimensions: mass, length, time, current, temperature, amount,
    // luminous intensity. Exponents are integers for this initial registry.
    std::array<int, 7> exponents{};
    std::string unitSymbol;

    void validate() const;
    [[nodiscard]] bool sameDimension(const UnitDimension& other) const noexcept;
    [[nodiscard]] UnitDimension addCompatible(const UnitDimension& other) const;
    [[nodiscard]] UnitDimension multiply(const UnitDimension& other) const;
    [[nodiscard]] UnitDimension divide(const UnitDimension& other) const;
    [[nodiscard]] UnitDimension pow(int exponent) const;

    [[nodiscard]] static UnitDimension dimensionless();
    [[nodiscard]] static UnitDimension kilogram();
    [[nodiscard]] static UnitDimension meter();
    [[nodiscard]] static UnitDimension second();
    [[nodiscard]] static UnitDimension ampere();
    [[nodiscard]] static UnitDimension mole();
    [[nodiscard]] static UnitDimension candela();
    [[nodiscard]] static UnitDimension newton();
    [[nodiscard]] static UnitDimension pascal();
    [[nodiscard]] static UnitDimension joule();
    [[nodiscard]] static UnitDimension watt();
    [[nodiscard]] static UnitDimension kelvin();
    [[nodiscard]] static UnitDimension wattPerCubicMeter();
};

enum class EquationQuantityType {
    Scalar,
    Vector,
    Tensor2,
    Tensor = Tensor2,
};

struct EquationVariable {
    std::string symbol;
    std::string name;
    UnitDimension dimension;
    EquationQuantityType quantityType = EquationQuantityType::Scalar;
    std::string referenceFrame;
    std::string coordinateSystem;

    void validate() const;
};

enum class EquationSourceKind {
    Textbook,
    Paper,
    Standard,
    Manufacturer,
    Measurement,
    Database,
    AgentDerivation,
};

struct EquationSource {
    EquationSourceKind kind = EquationSourceKind::Textbook;
    std::string title;
    std::string author;
    std::string editionOrVersion;
    std::string reference;
    std::string url;

    void validate() const;
};

enum class VerificationStatus { NotRun, Passed, Failed, Inconclusive };

struct EquationVerification {
    std::string id;
    std::string method;
    std::string description;
    VerificationStatus status = VerificationStatus::NotRun;
    bool analyticalBenchmark = false;
    std::string evidenceReference;

    void validate() const;
};

struct EquationTerm {
    std::string expression;
    UnitDimension dimension;
};

struct EquationDefinition {
    std::string id;
    std::string name;
    std::string domain;
    std::string symbolicForm;
    std::string strongForm;
    std::optional<std::string> weakForm;
    std::vector<EquationVariable> variables;
    std::vector<EquationTerm> leftTerms;
    std::vector<EquationTerm> rightTerms;
    std::vector<std::string> assumptions;
    std::vector<std::string> constitutiveRelationships;
    std::string validityRange;
    std::vector<std::string> boundaryConditions;
    std::vector<std::string> initialConditions;
    std::vector<EquationSource> sources;
    std::vector<EquationVerification> verifications;
    ModelMaturity maturity = ModelMaturity::E0;

    void validate() const;
};

class EquationRegistry final {
public:
    void add(EquationDefinition equation);
    [[nodiscard]] const EquationDefinition* find(const std::string& id) const noexcept;
    [[nodiscard]] const EquationDefinition* findAtLeast(const std::string& id,
                                                        ModelMaturity required) const noexcept;
    [[nodiscard]] const std::vector<EquationDefinition>& equations() const noexcept { return equations_; }

private:
    std::vector<EquationDefinition> equations_;
};

[[nodiscard]] EquationRegistry makeInitialEquationRegistry();

}  // namespace datumloop
