# Scientific Agent Routing

DatumLoop routes engineering reasoning through provider-neutral interfaces.
An agent may propose equations, assumptions, coupling, solver setup, or a
verification plan. It never supplies a numerical simulation result.

```text
ScientificTask
      |
task classifier and routing policy
      +-- routine implementation -> configured implementation agent
      +-- CAE formulation         -> preferred scientific reasoner
      |                              -> configured fallback if unavailable
      +-- critical review         -> independent strong reviewer
      |
structured proposal + provenance
      |
Equation Registry and maturity gate
      |
SolverRequest -> numerical backend -> SolverResult
      |
verification evidence -> experimental validation
```

The interfaces contain provider IDs, model IDs, and versions as runtime
strings. Engineering code has no Gemini, OpenAI, or other provider SDK types.
An operator can configure a Gemini 4 Argon model as the preferred CAE
reasoner, a Sol or Astra model as a fallback or independent reviewer, and Luna
for routine implementation without changing DatumLoop Core.

## Task and agent contracts

`ScientificTask` records its stable ID, task class, problem definition, input
engineering revision, candidate equation IDs, solver inputs, minimum equation
maturity, tolerances, and whether independent review is required. The router
classifies implementation, architecture, CAE formulation, numerical analysis,
verification, validation, result interpretation, sim-to-real analysis, and
research separately.

`ScientificAgentIdentity` records the configured agent, provider, model,
version, logical roles, and reasoning tier. Logical specializations such as
thermal, structural, materials, ROM, or sim-to-real reasoning do not require
separate deployments.

## Structured CAE proposals

`CaeProposal` is more than free-form prose. It records:

- Problem definition and governing Equation Registry IDs.
- Whether a registry search was completed and which existing equation was
  selected.
- Assumptions, physical domains, materials, constitutive models, initial and
  boundary conditions, and coupling definitions.
- Recommended solver, discretization, mesh strategy, and time integration.
- Convergence criteria, a verification plan, expected failure modes, and
  references.
- An optional new equation hypothesis, which must be a distinct registry ID
  and remain at E0 or E1.

Before suggesting a new equation, the proposal must record that the registry
was searched. If a candidate already exists, the proposal reuses that ID.
Proposals cannot promote themselves above E1. A SolverRequest is created only
when the selected equation already exists at the task's required maturity. If
the task requires independent review, request creation also requires an
independent strong-model review that approves progression; a revise or reject
recommendation blocks solver setup.

## Routing, fallback, and review

The routing policy accepts agent IDs as configuration:

```cpp
ScientificRoutingPolicy policy;
policy.implementationAgentId = "luna-agent-id";
policy.preferredCaeAgentId = "argon-cae-agent-id";
policy.fallbackCaeAgentIds = {"sol-cae-agent-id"};
policy.independentReviewerAgentIds = {"astra-review-agent-id"};
```

If the preferred CAE agent is unavailable, the router selects the first
available configured fallback and preserves its identity in proposal
provenance. Critical review requires a different agent and model with a
configured strong or high reasoning tier. An `ApproveForNextGate` review is
not a numerical verification result and cannot change an equation's maturity.

The test suite uses `MockScientificAgent`; it needs no credentials or network
access. Tests cover separate implementation and CAE routes, preferred-agent
selection, fallback, Equation Registry reuse, independent review, and
self-promotion rejection.

## Evidence chain

Every proposal records its task and input revision, provider/model/version,
creation timestamp, references, confidence, and proposed maturity. A review
records its reviewer, timestamp, recommendation, findings, and independence.
An `EngineeringDecision` records alternatives, the selected model,
assumptions, evidence, reviewer, validation status, and any superseded
decision. Store engineering conclusions and evidence; do not store private
chain-of-thought.

```text
AI reasoning != numerical execution != mathematical verification != experimental validation
```

The demonstration creates a thermal-cantilever `ScientificTask`, selects the
existing `thermal.heat-equation.isotropic.v1` equation, requires a distinct
reviewer approval, and then builds a maturity-gated `SolverRequest`. It does
not run a backend or create a `SolverResult`. Solver outputs must come from
the selected numerical adapter, then pass the separate benchmark and
validation gates.

For native tests, configure mock agents and run `ctest --test-dir
build/native --output-on-failure`. No live model credentials are required.
