#include "datumloop/science/ScientificAgents.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template <typename Function>
void requireThrows(Function&& function, const char* message)
{
    bool threw = false;
    try {
        function();
    } catch (const std::exception&) {
        threw = true;
    }
    require(threw, message);
}

datumloop::ScientificAgentIdentity identity(std::string agentId, std::string provider,
                                            std::string model, std::vector<datumloop::ScientificAgentRole> roles,
                                            datumloop::ScientificReasoningTier tier = datumloop::ScientificReasoningTier::Routine)
{
    return {std::move(agentId), std::move(provider), std::move(model), "test-config-1", std::move(roles), tier};
}

datumloop::CaeProposal thermalCantileverProposal(const datumloop::EquationRegistry& registry)
{
    const std::string heatId = "thermal.heat-equation.isotropic.v1";
    if (registry.find(heatId) == nullptr) throw std::runtime_error("thermal reference equation is missing");
    datumloop::CaeProposal cae;
    cae.problemDefinition = "Estimate the temperature field in a cantilever under a declared thermal load.";
    cae.governingEquationIds = {heatId};
    cae.selectedExistingEquationId = heatId;
    cae.equationRegistrySearchCompleted = true;
    cae.assumptions = {"Continuum solid", "Isotropic conduction", "Material properties are supplied by the engineering model"};
    cae.domains = {"thermal", "structural geometry used only as the heat-conduction domain"};
    cae.materials = {"Select a registry material; do not infer missing constants"};
    cae.constitutiveModels = {"Fourier heat conduction"};
    cae.boundaryConditions = {"Apply explicitly sourced temperature or heat-flux boundary conditions"};
    cae.recommendedSolver = "MFEM through the DatumLoop thermal solver adapter";
    cae.discretization = "H1 finite elements for temperature";
    cae.meshStrategy = "Refine the cantilever mesh and report a field norm and energy balance";
    cae.timeIntegration = "Steady-state request for this demonstration";
    cae.convergenceCriteria = {"Residual tolerance", "Mesh-refinement change in the reported thermal metric"};
    cae.verificationPlan = {"Check dimensions", "Compare a one-dimensional slab limit with its analytical solution"};
    cae.expectedFailureModes = {"Missing boundary data", "Unspecified thermal properties", "Insufficient mesh resolution"};
    cae.references = {"Existing DatumLoop Equation Registry entry: " + heatId};
    return cae;
}

datumloop::ScientificProposal proposalFor(const datumloop::ScientificTask& task,
                                           const datumloop::ScientificAgentIdentity& agent,
                                           const datumloop::EquationRegistry& registry)
{
    datumloop::ScientificProposal proposal;
    proposal.id = "proposal." + agent.agentId + ".thermal-cantilever";
    proposal.taskId = task.id;
    proposal.agent = agent;
    proposal.createdAtUtc = "2026-10-17T08:00:00Z";
    proposal.inputEngineeringRevision = task.inputEngineeringRevision;
    proposal.confidence = 0.73;
    proposal.proposedMaturity = datumloop::ModelMaturity::E0;
    proposal.cae = thermalCantileverProposal(registry);
    proposal.engineeringConclusion = "Reuse the registered heat equation; request a real solver run separately.";
    return proposal;
}

datumloop::ScientificTask thermalTask()
{
    datumloop::ScientificTask task;
    task.id = "task.thermal-cantilever.setup";
    task.kind = datumloop::ScientificTaskKind::CaeFormulation;
    task.problemDefinition = "Prepare a thermal analysis request for a cantilever using supplied loads and properties.";
    task.inputEngineeringRevision = "engineering-revision-7";
    task.candidateEquationIds = {"thermal.heat-equation.isotropic.v1"};
    task.minimumEquationMaturity = datumloop::ModelMaturity::E3;
    task.requiresIndependentReview = true;
    return task;
}

}  // namespace

int main()
{
    try {
        const datumloop::EquationRegistry registry = datumloop::makeInitialEquationRegistry();
        const auto implementationIdentity = identity("luna-workhorse", "configured-provider-a", "configured-model-luna",
            {datumloop::ScientificAgentRole::Implementation});
        const auto argonIdentity = identity("argon-cae", "configured-provider-b", "configured-model-argon",
            {datumloop::ScientificAgentRole::CaeDirector}, datumloop::ScientificReasoningTier::High);
        const auto solIdentity = identity("sol-cae-fallback", "configured-provider-c", "configured-model-sol",
            {datumloop::ScientificAgentRole::CaeDirector}, datumloop::ScientificReasoningTier::Strong);
        const auto reviewerIdentity = identity("astra-independent-review", "configured-provider-d", "configured-model-astra",
            {datumloop::ScientificAgentRole::IndependentReviewer}, datumloop::ScientificReasoningTier::Strong);

        const auto implementation = std::make_shared<datumloop::MockScientificAgent>(
            implementationIdentity, true,
            [implementationIdentity](const auto& task, const auto& equations) {
                return proposalFor(task, implementationIdentity, equations);
            });
        const auto argon = std::make_shared<datumloop::MockScientificAgent>(
            argonIdentity, true,
            [argonIdentity](const auto& task, const auto& equations) {
                return proposalFor(task, argonIdentity, equations);
            });
        const auto sol = std::make_shared<datumloop::MockScientificAgent>(
            solIdentity, true,
            [solIdentity](const auto& task, const auto& equations) {
                return proposalFor(task, solIdentity, equations);
            });
        const auto reviewer = std::make_shared<datumloop::MockScientificAgent>(
            reviewerIdentity, true, datumloop::MockScientificAgent::ProposalFactory{},
            [reviewerIdentity](const auto& task, const auto& proposal) {
                return datumloop::ScientificReview{
                    "review." + reviewerIdentity.agentId + ".1", proposal.id, task.id,
                    reviewerIdentity, "2026-10-17T08:01:00Z", true,
                    datumloop::ScientificReviewRecommendation::ApproveForNextGate,
                    {"Confirm the supplied thermal boundary data before solver execution."}};
            });

        datumloop::ScientificRoutingPolicy policy;
        policy.implementationAgentId = implementationIdentity.agentId;
        policy.preferredCaeAgentId = argonIdentity.agentId;
        policy.fallbackCaeAgentIds = {solIdentity.agentId};
        policy.independentReviewerAgentIds = {reviewerIdentity.agentId};
        datumloop::ScientificAgentRouter router(policy, {implementation, argon, sol, reviewer});

        const auto task = thermalTask();
        const auto proposal = router.propose(task, registry);
        require(proposal.agent.agentId == argonIdentity.agentId && proposal.cae &&
                    proposal.cae->selectedExistingEquationId == task.candidateEquationIds.front(),
                "CAE routing must prefer the configured scientific agent and select an existing equation");

        const auto review = router.reviewIndependently(task, proposal);
        require(review.independent && review.reviewer.modelId != proposal.agent.modelId &&
                    review.recommendation == datumloop::ScientificReviewRecommendation::ApproveForNextGate,
                "critical CAE work must receive a review from a distinct configured model");
        requireThrows([&]() {
            (void)datumloop::makeSolverRequestFromProposal(task, proposal, registry,
                                                          "solver-request.without-review");
        }, "critical CAE work created a solver request before independent review");
        auto reviseReview = review;
        reviseReview.recommendation = datumloop::ScientificReviewRecommendation::Revise;
        requireThrows([&]() {
            (void)datumloop::makeSolverRequestFromProposal(task, proposal, registry,
                                                          "solver-request.after-revision-request", &reviseReview);
        }, "solver request was created after the reviewer requested revisions");
        const auto solverRequest = datumloop::makeSolverRequestFromProposal(
            task, proposal, registry, "solver-request.thermal-cantilever.1", &review);
        require(solverRequest.equationDefinitionId == "thermal.heat-equation.isotropic.v1" &&
                    solverRequest.minimumMaturity == datumloop::ModelMaturity::E3,
                "CAE proposal must create a solver request after independent approval and equation maturity checks");

        auto overpromoted = proposal;
        overpromoted.proposedMaturity = datumloop::ModelMaturity::E4;
        requireThrows([&]() { overpromoted.validate(task, registry); },
                      "scientific agent proposal promoted its own maturity above E1");
        auto duplicateEquation = proposal;
        duplicateEquation.cae->newEquationHypothesis = *registry.find("thermal.heat-equation.isotropic.v1");
        requireThrows([&]() { duplicateEquation.validate(task, registry); },
                      "CAE proposal duplicated an existing Equation Registry entry");
        auto unsearched = proposal;
        unsearched.cae->equationRegistrySearchCompleted = false;
        requireThrows([&]() { unsearched.validate(task, registry); },
                      "CAE proposal bypassed its required Equation Registry search");
        auto weakReview = review;
        weakReview.reviewer = identity("weak-reviewer", "provider-e", "routine-model",
            {datumloop::ScientificAgentRole::IndependentReviewer});
        requireThrows([&]() { weakReview.validateAgainst(proposal); },
                      "critical scientific review accepted a routine reasoning tier");

        datumloop::ScientificRoutingPolicy fallbackPolicy = policy;
        fallbackPolicy.preferredCaeAgentId = argonIdentity.agentId;
        const auto unavailableArgon = std::make_shared<datumloop::MockScientificAgent>(
            argonIdentity, false,
            [argonIdentity](const auto& fallbackTask, const auto& equations) {
                return proposalFor(fallbackTask, argonIdentity, equations);
            });
        datumloop::ScientificAgentRouter fallbackRouter(
            fallbackPolicy, {implementation, unavailableArgon, sol, reviewer});
        const auto fallbackProposal = fallbackRouter.propose(task, registry);
        require(fallbackProposal.agent.agentId == solIdentity.agentId,
                "unavailable preferred CAE agent must fall back to a configured scientific agent");

        datumloop::ScientificTask implementationTask = task;
        implementationTask.id = "task.routine-implementation";
        implementationTask.kind = datumloop::ScientificTaskKind::Implementation;
        const auto& implementationRoute = router.route(implementationTask);
        require(implementationRoute.identity().agentId == implementationIdentity.agentId,
                "routine implementation must route independently of CAE formulation");

        datumloop::EngineeringDecision decision;
        decision.id = "decision.thermal-cantilever.model";
        decision.problem = task.problemDefinition;
        decision.alternatives = {"Transient heat equation", "Steady heat equation"};
        decision.selectedModel = "thermal.heat-equation.isotropic.v1";
        decision.assumptions = {"Thermal material constants and boundary conditions are supplied by the engineering model"};
        decision.evidence = {"Equation Registry entry and independent review record"};
        decision.reviewerAgentId = reviewerIdentity.agentId;
        decision.reviewIndependent = true;
        decision.validationStatus = "proposal-reviewed; no simulation executed";
        decision.requiresIndependentReview = true;
        auto unreviewedDecision = decision;
        unreviewedDecision.reviewIndependent = false;
        requireThrows([&]() { unreviewedDecision.validate(); },
                      "high-risk engineering decision accepted without an independent review record");
        decision.validate();

        std::cout << "Scientific task routing, provider-neutral provenance, Equation Registry reuse, maturity gates, fallback, independent review, and solver-request creation passed.\n";
    } catch (const std::exception& error) {
        std::cerr << "Scientific-agent test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
