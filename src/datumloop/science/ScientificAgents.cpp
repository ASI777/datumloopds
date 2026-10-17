#include "datumloop/science/ScientificAgents.hpp"

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

void validateTextList(const std::vector<std::string>& values, const char* field)
{
    for (const std::string& value : values) requireText(value, field);
}

bool hasRole(const ScientificAgentIdentity& identity, ScientificAgentRole role)
{
    return std::find(identity.roles.begin(), identity.roles.end(), role) != identity.roles.end();
}

bool maturityAtMostE1(ModelMaturity maturity)
{
    return static_cast<int>(maturity) <= static_cast<int>(ModelMaturity::E1);
}

}  // namespace

void ScientificAgentIdentity::validate() const
{
    requireText(agentId, "scientific agent ID");
    requireText(providerId, "scientific agent provider ID");
    requireText(modelId, "scientific agent model ID");
    requireText(modelVersion, "scientific agent model version");
    if (roles.empty()) throw std::invalid_argument("scientific agent must declare at least one role");
    if (reasoningTier != ScientificReasoningTier::Routine && reasoningTier != ScientificReasoningTier::Strong &&
        reasoningTier != ScientificReasoningTier::High) {
        throw std::invalid_argument("scientific agent reasoning tier is invalid");
    }
    std::unordered_set<int> uniqueRoles;
    for (const ScientificAgentRole role : roles) {
        if (!uniqueRoles.insert(static_cast<int>(role)).second) {
            throw std::invalid_argument("scientific agent roles must be unique");
        }
    }
}

void ScientificTask::validate() const
{
    requireText(id, "scientific task ID");
    requireText(problemDefinition, "scientific task problem definition");
    requireText(inputEngineeringRevision, "scientific task input revision");
    if (!std::isfinite(absoluteTolerance) || absoluteTolerance <= 0.0 ||
        !std::isfinite(relativeTolerance) || relativeTolerance <= 0.0) {
        throw std::invalid_argument("scientific task solver tolerances must be finite and positive");
    }
    std::unordered_set<std::string> equationIds;
    for (const std::string& equationId : candidateEquationIds) {
        requireText(equationId, "candidate equation ID");
        if (!equationIds.insert(equationId).second) {
            throw std::invalid_argument("candidate equation IDs must be unique");
        }
    }
    std::unordered_set<std::string> inputNames;
    for (const auto& [name, value] : solverInputs) {
        requireText(name, "scientific task solver input name");
        if (!inputNames.insert(name).second) throw std::invalid_argument("scientific task solver inputs must be unique");
        if (!std::isfinite(value.value)) throw std::invalid_argument("scientific task solver inputs must be finite");
        value.dimension.validate();
    }
}

void CaeProposal::validate(const EquationRegistry& registry) const
{
    requireText(problemDefinition, "CAE proposal problem definition");
    if (!equationRegistrySearchCompleted) {
        throw std::invalid_argument("CAE proposals must record an Equation Registry search");
    }
    validateTextList(governingEquationIds, "CAE proposal equation ID");
    validateTextList(assumptions, "CAE proposal assumption");
    validateTextList(domains, "CAE proposal domain");
    validateTextList(materials, "CAE proposal material");
    validateTextList(constitutiveModels, "CAE proposal constitutive model");
    validateTextList(initialConditions, "CAE proposal initial condition");
    validateTextList(boundaryConditions, "CAE proposal boundary condition");
    validateTextList(convergenceCriteria, "CAE proposal convergence criterion");
    validateTextList(verificationPlan, "CAE proposal verification step");
    validateTextList(expectedFailureModes, "CAE proposal expected failure mode");
    validateTextList(references, "CAE proposal reference");
    requireText(recommendedSolver, "CAE proposal recommended solver");
    requireText(discretization, "CAE proposal discretization");
    requireText(meshStrategy, "CAE proposal mesh strategy");
    if (!timeIntegration.empty()) requireText(timeIntegration, "CAE proposal time integration");
    for (const CouplingDefinition& coupling : couplingDefinitions) {
        requireText(coupling.sourceDomain, "coupling source domain");
        requireText(coupling.targetDomain, "coupling target domain");
        requireText(coupling.transferredQuantity, "coupling transferred quantity");
        requireText(coupling.strategy, "coupling strategy");
    }
    if (selectedExistingEquationId) {
        requireText(*selectedExistingEquationId, "selected existing equation ID");
        if (std::find(governingEquationIds.begin(), governingEquationIds.end(), *selectedExistingEquationId) ==
            governingEquationIds.end()) {
            throw std::invalid_argument("selected Equation Registry entry must appear among governing equations");
        }
        if (registry.find(*selectedExistingEquationId) == nullptr) {
            throw std::invalid_argument("selected governing equation is not present in the Equation Registry");
        }
    }
    if (newEquationHypothesis) {
        if (std::find(governingEquationIds.begin(), governingEquationIds.end(), newEquationHypothesis->id) ==
            governingEquationIds.end()) {
            throw std::invalid_argument("new equation hypothesis must be listed among governing equations");
        }
        if (!maturityAtMostE1(newEquationHypothesis->maturity)) {
            throw std::invalid_argument("new CAE equation hypotheses cannot begin above maturity E1");
        }
        if (registry.find(newEquationHypothesis->id) != nullptr) {
            throw std::invalid_argument("CAE proposal duplicates an Equation Registry ID instead of reusing it");
        }
        newEquationHypothesis->validate();
    }
}

void ScientificProposal::validate(const ScientificTask& task, const EquationRegistry& registry) const
{
    task.validate();
    requireText(id, "scientific proposal ID");
    requireText(taskId, "scientific proposal task ID");
    if (taskId != task.id) throw std::invalid_argument("scientific proposal belongs to a different task");
    agent.validate();
    requireText(createdAtUtc, "scientific proposal timestamp");
    if (inputEngineeringRevision != task.inputEngineeringRevision) {
        throw std::invalid_argument("scientific proposal input revision does not match its task");
    }
    if (!std::isfinite(confidence) || confidence < 0.0 || confidence > 1.0) {
        throw std::invalid_argument("scientific proposal confidence must be in [0, 1]");
    }
    if (!maturityAtMostE1(proposedMaturity)) {
        throw std::invalid_argument("AI scientific proposals cannot promote their own maturity above E1");
    }
    validateTextList(references, "scientific proposal reference");
    if (proposedMaturity == ModelMaturity::E1 && references.empty()) {
        throw std::invalid_argument("literature-sourced E1 proposals must record references");
    }
    if (cae) cae->validate(registry);
    if (task.kind == ScientificTaskKind::CaeFormulation && !cae) {
        throw std::invalid_argument("CAE formulation task requires a structured CAE proposal");
    }
    if (!engineeringConclusion.empty()) requireText(engineeringConclusion, "scientific engineering conclusion");
}

void ScientificReview::validateAgainst(const ScientificProposal& proposal) const
{
    requireText(id, "scientific review ID");
    requireText(proposalId, "reviewed scientific proposal ID");
    requireText(taskId, "scientific review task ID");
    reviewer.validate();
    requireText(reviewedAtUtc, "scientific review timestamp");
    if (proposalId != proposal.id || taskId != proposal.taskId) {
        throw std::invalid_argument("scientific review does not identify the supplied proposal and task");
    }
    if (!hasRole(reviewer, ScientificAgentRole::IndependentReviewer)) {
        throw std::invalid_argument("scientific review must be performed by an independent-review role");
    }
    if (reviewer.reasoningTier < ScientificReasoningTier::Strong) {
        throw std::invalid_argument("independent scientific review requires a strong reasoning tier");
    }
    if (recommendation != ScientificReviewRecommendation::ApproveForNextGate &&
        recommendation != ScientificReviewRecommendation::Revise &&
        recommendation != ScientificReviewRecommendation::Reject &&
        recommendation != ScientificReviewRecommendation::InsufficientEvidence) {
        throw std::invalid_argument("scientific review recommendation is invalid");
    }
    if (!independent || reviewer.agentId == proposal.agent.agentId || reviewer.modelId == proposal.agent.modelId) {
        throw std::invalid_argument("independent review must use a different agent and model");
    }
    validateTextList(findings, "scientific review finding");
}

void EngineeringDecision::validate() const
{
    requireText(id, "engineering decision ID");
    requireText(problem, "engineering decision problem");
    validateTextList(alternatives, "engineering decision alternative");
    if (alternatives.empty()) throw std::invalid_argument("engineering decision must preserve at least one alternative");
    requireText(selectedModel, "engineering decision selected model");
    validateTextList(assumptions, "engineering decision assumption");
    validateTextList(evidence, "engineering decision evidence");
    if (evidence.empty()) throw std::invalid_argument("engineering decision must record its supporting evidence");
    requireText(validationStatus, "engineering decision validation status");
    if (requiresIndependentReview && (!reviewerAgentId || !reviewIndependent)) {
        throw std::invalid_argument("high-risk engineering decision requires a recorded independent review");
    }
    if (reviewIndependent && !reviewerAgentId) throw std::invalid_argument("independent review status requires a reviewer ID");
    if (reviewerAgentId) requireText(*reviewerAgentId, "engineering decision reviewer ID");
    if (supersedes) requireText(*supersedes, "superseded engineering decision ID");
}

MockScientificAgent::MockScientificAgent(ScientificAgentIdentity identity, bool available,
                                         ProposalFactory proposalFactory, ReviewFactory reviewFactory)
    : identity_(std::move(identity)), available_(available), proposalFactory_(std::move(proposalFactory)),
      reviewFactory_(std::move(reviewFactory))
{
    identity_.validate();
}

ScientificProposal MockScientificAgent::propose(const ScientificTask& task,
                                                 const EquationRegistry& registry) const
{
    if (!available_) throw std::runtime_error("scientific reasoning agent is unavailable");
    if (!proposalFactory_) throw std::logic_error("mock scientific agent has no proposal factory");
    return proposalFactory_(task, registry);
}

ScientificReview MockScientificAgent::review(const ScientificTask& task,
                                               const ScientificProposal& proposal) const
{
    if (!available_) throw std::runtime_error("scientific review agent is unavailable");
    if (!reviewFactory_) throw std::logic_error("mock scientific agent has no review factory");
    return reviewFactory_(task, proposal);
}

ScientificAgentRouter::ScientificAgentRouter(
    ScientificRoutingPolicy policy,
    std::vector<std::shared_ptr<const IScientificReasoningAgent>> agents)
    : policy_(std::move(policy)), agents_(std::move(agents))
{
    std::unordered_set<std::string> agentIds;
    for (const auto& agent : agents_) {
        if (!agent) throw std::invalid_argument("scientific agent router cannot contain a null agent");
        agent->identity().validate();
        if (!agentIds.insert(agent->identity().agentId).second) {
            throw std::invalid_argument("scientific agent IDs in a router must be unique");
        }
    }
}

const IScientificReasoningAgent* ScientificAgentRouter::findAvailable(const std::string& agentId) const noexcept
{
    const auto found = std::find_if(agents_.begin(), agents_.end(), [&agentId](const auto& agent) {
        return agent->identity().agentId == agentId && agent->available();
    });
    return found == agents_.end() ? nullptr : found->get();
}

const IScientificReasoningAgent& ScientificAgentRouter::selectByRole(
    ScientificAgentRole role, const std::vector<std::string>& preferredIds,
    const ScientificProposal* proposer) const
{
    const auto isEligible = [role, proposer](const IScientificReasoningAgent& agent) {
        const auto& identity = agent.identity();
        const bool strongEnough = role != ScientificAgentRole::IndependentReviewer ||
                                  identity.reasoningTier >= ScientificReasoningTier::Strong;
        return agent.available() && hasRole(identity, role) && strongEnough &&
               (proposer == nullptr ||
                (identity.agentId != proposer->agent.agentId && identity.modelId != proposer->agent.modelId));
    };
    for (const std::string& id : preferredIds) {
        const IScientificReasoningAgent* agent = findAvailable(id);
        if (agent != nullptr && isEligible(*agent)) return *agent;
    }
    for (const auto& agent : agents_) {
        if (isEligible(*agent)) return *agent;
    }
    throw std::runtime_error("no available scientific agent matches the requested role and independence policy");
}

const IScientificReasoningAgent& ScientificAgentRouter::route(const ScientificTask& task) const
{
    task.validate();
    switch (task.kind) {
    case ScientificTaskKind::Implementation:
        return selectByRole(ScientificAgentRole::Implementation,
                            policy_.implementationAgentId.empty() ? std::vector<std::string>{}
                                                                  : std::vector<std::string>{policy_.implementationAgentId});
    case ScientificTaskKind::CaeFormulation:
    case ScientificTaskKind::NumericalAnalysis:
    case ScientificTaskKind::ResultInterpretation:
    case ScientificTaskKind::SimToReal:
    case ScientificTaskKind::Research: {
        std::vector<std::string> preferred;
        if (!policy_.preferredCaeAgentId.empty()) preferred.push_back(policy_.preferredCaeAgentId);
        preferred.insert(preferred.end(), policy_.fallbackCaeAgentIds.begin(), policy_.fallbackCaeAgentIds.end());
        return selectByRole(ScientificAgentRole::CaeDirector, preferred);
    }
    case ScientificTaskKind::Architecture:
        return selectByRole(ScientificAgentRole::IndependentReviewer, policy_.independentReviewerAgentIds);
    case ScientificTaskKind::Verification:
    case ScientificTaskKind::Validation:
        return selectByRole(ScientificAgentRole::Verifier, policy_.verifierAgentIds);
    }
    throw std::invalid_argument("unknown scientific task kind");
}

ScientificProposal ScientificAgentRouter::propose(const ScientificTask& task,
                                                    const EquationRegistry& registry) const
{
    const IScientificReasoningAgent& agent = route(task);
    ScientificProposal proposal = agent.propose(task, registry);
    proposal.validate(task, registry);
    if (proposal.agent.agentId != agent.identity().agentId ||
        proposal.agent.modelId != agent.identity().modelId ||
        proposal.agent.modelVersion != agent.identity().modelVersion) {
        throw std::invalid_argument("scientific proposal provenance does not match the routed agent");
    }
    return proposal;
}

ScientificReview ScientificAgentRouter::reviewIndependently(const ScientificTask& task,
                                                              const ScientificProposal& proposal) const
{
    if (proposal.taskId != task.id) throw std::invalid_argument("review task does not match the scientific proposal");
    const IScientificReasoningAgent& reviewer = selectByRole(
        ScientificAgentRole::IndependentReviewer, policy_.independentReviewerAgentIds, &proposal);
    ScientificReview review = reviewer.review(task, proposal);
    review.validateAgainst(proposal);
    if (review.reviewer.agentId != reviewer.identity().agentId ||
        review.reviewer.modelId != reviewer.identity().modelId) {
        throw std::invalid_argument("scientific review provenance does not match the routed reviewer");
    }
    return review;
}

SolverRequest makeSolverRequestFromProposal(const ScientificTask& task,
                                           const ScientificProposal& proposal,
                                           const EquationRegistry& registry,
                                           std::string requestId,
                                           const ScientificReview* independentReview)
{
    proposal.validate(task, registry);
    if (task.requiresIndependentReview && independentReview == nullptr) {
        throw std::invalid_argument("critical scientific task requires independent review before solver setup");
    }
    if (independentReview != nullptr) {
        independentReview->validateAgainst(proposal);
        if (independentReview->recommendation != ScientificReviewRecommendation::ApproveForNextGate) {
            throw std::invalid_argument("scientific review did not approve progression to solver setup");
        }
    }
    if (!proposal.cae || !proposal.cae->selectedExistingEquationId) {
        throw std::invalid_argument("solver request requires a CAE proposal selecting an existing registry equation");
    }
    const std::string& equationId = *proposal.cae->selectedExistingEquationId;
    if (registry.findAtLeast(equationId, task.minimumEquationMaturity) == nullptr) {
        throw std::invalid_argument("selected registry equation is below the task's required maturity");
    }
    SolverRequest request;
    request.requestId = std::move(requestId);
    request.equationDefinitionId = equationId;
    request.designRevision = task.inputEngineeringRevision;
    request.minimumMaturity = task.minimumEquationMaturity;
    request.absoluteTolerance = task.absoluteTolerance;
    request.relativeTolerance = task.relativeTolerance;
    request.inputs = task.solverInputs;
    request.validate(registry);
    return request;
}

}  // namespace datumloop
