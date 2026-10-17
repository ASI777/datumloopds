#pragma once

#include "datumloop/knowledge/EquationRegistry.hpp"
#include "datumloop/solver/PhysicsSolver.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace datumloop {

enum class ScientificTaskKind {
    Implementation,
    Architecture,
    CaeFormulation,
    NumericalAnalysis,
    Verification,
    Validation,
    ResultInterpretation,
    SimToReal,
    Research,
};

enum class ScientificAgentRole { Implementation, CaeDirector, IndependentReviewer, Verifier };
enum class ScientificReasoningTier { Routine, Strong, High };
enum class ScientificReviewRecommendation { ApproveForNextGate, Revise, Reject, InsufficientEvidence };

struct ScientificAgentIdentity {
    std::string agentId;
    std::string providerId;
    std::string modelId;
    std::string modelVersion;
    std::vector<ScientificAgentRole> roles;
    ScientificReasoningTier reasoningTier = ScientificReasoningTier::Routine;

    void validate() const;
};

struct ScientificTask {
    std::string id;
    ScientificTaskKind kind = ScientificTaskKind::CaeFormulation;
    std::string problemDefinition;
    std::string inputEngineeringRevision;
    std::vector<std::string> candidateEquationIds;
    std::vector<std::pair<std::string, DimensionalScalar>> solverInputs;
    ModelMaturity minimumEquationMaturity = ModelMaturity::E3;
    double absoluteTolerance = 1e-9;
    double relativeTolerance = 1e-6;
    bool requiresIndependentReview = false;

    void validate() const;
};

struct CouplingDefinition {
    std::string sourceDomain;
    std::string targetDomain;
    std::string transferredQuantity;
    std::string strategy;
};

struct CaeProposal {
    std::string problemDefinition;
    std::vector<std::string> governingEquationIds;
    std::optional<std::string> selectedExistingEquationId;
    bool equationRegistrySearchCompleted = false;
    std::optional<EquationDefinition> newEquationHypothesis;
    std::vector<std::string> assumptions;
    std::vector<std::string> domains;
    std::vector<std::string> materials;
    std::vector<std::string> constitutiveModels;
    std::vector<std::string> initialConditions;
    std::vector<std::string> boundaryConditions;
    std::vector<CouplingDefinition> couplingDefinitions;
    std::string recommendedSolver;
    std::string discretization;
    std::string meshStrategy;
    std::string timeIntegration;
    std::vector<std::string> convergenceCriteria;
    std::vector<std::string> verificationPlan;
    std::vector<std::string> expectedFailureModes;
    std::vector<std::string> references;

    void validate(const EquationRegistry& registry) const;
};

struct ScientificProposal {
    std::string id;
    std::string taskId;
    ScientificAgentIdentity agent;
    std::string createdAtUtc;
    std::string inputEngineeringRevision;
    std::vector<std::string> references;
    double confidence = 0.0;
    ModelMaturity proposedMaturity = ModelMaturity::E0;
    std::optional<CaeProposal> cae;
    std::string engineeringConclusion;

    void validate(const ScientificTask& task, const EquationRegistry& registry) const;
};

struct ScientificReview {
    std::string id;
    std::string proposalId;
    std::string taskId;
    ScientificAgentIdentity reviewer;
    std::string reviewedAtUtc;
    bool independent = false;
    ScientificReviewRecommendation recommendation = ScientificReviewRecommendation::InsufficientEvidence;
    std::vector<std::string> findings;

    void validateAgainst(const ScientificProposal& proposal) const;
};

struct EngineeringDecision {
    std::string id;
    std::string problem;
    std::vector<std::string> alternatives;
    std::string selectedModel;
    std::vector<std::string> assumptions;
    std::vector<std::string> evidence;
    std::optional<std::string> reviewerAgentId;
    bool reviewIndependent = false;
    std::string validationStatus;
    std::optional<std::string> supersedes;
    bool requiresIndependentReview = false;

    void validate() const;
};

class IScientificReasoningAgent {
public:
    virtual ~IScientificReasoningAgent() = default;
    [[nodiscard]] virtual const ScientificAgentIdentity& identity() const noexcept = 0;
    [[nodiscard]] virtual bool available() const noexcept = 0;
    [[nodiscard]] virtual ScientificProposal propose(const ScientificTask& task,
                                                      const EquationRegistry& registry) const = 0;
    [[nodiscard]] virtual ScientificReview review(const ScientificTask& task,
                                                  const ScientificProposal& proposal) const = 0;
};

// Deterministic local test double; no provider SDK or credentials are required.
class MockScientificAgent final : public IScientificReasoningAgent {
public:
    using ProposalFactory = std::function<ScientificProposal(const ScientificTask&, const EquationRegistry&)>;
    using ReviewFactory = std::function<ScientificReview(const ScientificTask&, const ScientificProposal&)>;

    MockScientificAgent(ScientificAgentIdentity identity, bool available,
                        ProposalFactory proposalFactory = {}, ReviewFactory reviewFactory = {});
    [[nodiscard]] const ScientificAgentIdentity& identity() const noexcept override { return identity_; }
    [[nodiscard]] bool available() const noexcept override { return available_; }
    [[nodiscard]] ScientificProposal propose(const ScientificTask& task,
                                              const EquationRegistry& registry) const override;
    [[nodiscard]] ScientificReview review(const ScientificTask& task,
                                          const ScientificProposal& proposal) const override;

private:
    ScientificAgentIdentity identity_;
    bool available_ = false;
    ProposalFactory proposalFactory_;
    ReviewFactory reviewFactory_;
};

struct ScientificRoutingPolicy {
    // IDs and model names are runtime configuration strings, not Core dependencies.
    std::string implementationAgentId;
    std::string preferredCaeAgentId;
    std::vector<std::string> fallbackCaeAgentIds;
    std::vector<std::string> independentReviewerAgentIds;
    std::vector<std::string> verifierAgentIds;
};

class ScientificAgentRouter final {
public:
    ScientificAgentRouter(ScientificRoutingPolicy policy,
                          std::vector<std::shared_ptr<const IScientificReasoningAgent>> agents);

    [[nodiscard]] const IScientificReasoningAgent& route(const ScientificTask& task) const;
    [[nodiscard]] ScientificProposal propose(const ScientificTask& task,
                                             const EquationRegistry& registry) const;
    [[nodiscard]] ScientificReview reviewIndependently(const ScientificTask& task,
                                                       const ScientificProposal& proposal) const;

private:
    [[nodiscard]] const IScientificReasoningAgent* findAvailable(const std::string& agentId) const noexcept;
    [[nodiscard]] const IScientificReasoningAgent& selectByRole(ScientificAgentRole role,
                                                                const std::vector<std::string>& preferredIds,
                                                                const ScientificProposal* proposer = nullptr) const;

    ScientificRoutingPolicy policy_;
    std::vector<std::shared_ptr<const IScientificReasoningAgent>> agents_;
};

// Creates a solver request only from an existing registry entry. This function
// never executes a solver and never manufactures a SolverResult.
[[nodiscard]] SolverRequest makeSolverRequestFromProposal(const ScientificTask& task,
                                                          const ScientificProposal& proposal,
                                                          const EquationRegistry& registry,
                                                          std::string requestId,
                                                          const ScientificReview* independentReview = nullptr);

}  // namespace datumloop
