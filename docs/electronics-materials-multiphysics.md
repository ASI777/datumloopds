# Electronics, Materials, and Multiphysics Foundation

## Product boundary

DatumLoop Core owns the canonical engineering meaning and stable DatumLoop
identities. Geometry kernels, EDA formats, and numerical solvers are replaceable
adapters. KiCad remains an interchange and comparison target; DatumLoop does
not fork or embed KiCad application code. Circuit JSON is an interchange
format, not the native project model. The native application remains C++20,
Qt 6/QML, and OCCT; tscircuit's React/TypeScript UI is not part of the
application architecture.

The first implementation slice adds a native material/electronics model,
`.dld` schema 7 persistence, Circuit JSON import/export, a bounded KiCad PCB
importer, an OCCT-derived PCB preview, and a process-isolated DC circuit solver.
It does not implement a general physics graph or multiphysics coupling engine.

## Canonical engineering model

`DatumDocument` owns CAD objects and keeps engineering metadata in separate
models. `DatumElectronicsModel` currently owns common material definitions and
states, circuits, and PCB documents. Each model object receives a stable
DatumLoop UUID; adapter source IDs are retained as provenance/lookup data and
are not used as canonical identities.

`MaterialDefinition` carries a name, optional composition, phase definitions,
microstructure metadata, process history, property models, and provenance.
`MaterialState` is separate and can hold scalar temperature and pressure,
phase fractions, state-specific microstructure, and references to field
datasets. Property models currently support a constant or a piecewise-linear
table with explicit property and independent-variable units, a source/version,
and optional confidence. Unknown values stay absent. This is a data foundation,
not a validated thermodynamic or materials database: composition-to-property
inference, phase equilibrium, grain orientation fields, precipitate populations,
and calibrated material laws are not implemented.

`CircuitDocument` owns components, pins, and nets. The current component
catalog is intentionally small: voltage source, current source, resistor, and
unspecified. Optional values carry explicit units. It does not yet represent
schematics, arbitrary SPICE models, semiconductor junctions, supplier parts,
or rich package/material descriptions.

`PcbDocument` owns board bounds and thickness, a stack-up, placed components,
pads, traces, vias, and material regions. PCB layer and board material IDs
must refer to the same `MaterialDefinition` collection used by the rest of
DatumLoop. Stack-up layers have optional thickness, roughness, dielectric
permittivity, and loss tangent. The current model does not yet cover copper
planes, keepouts, solder joints, plated-hole barrels, stack-up dielectric
layers, manufacturing rules, or temperature/frequency-dependent layer laws.
Missing material and stack-up data is preserved as unknown.

The `.dld` schema 7 stores these definitions, source IDs, relationships,
placement, units, optional values, and provenance. Geometry remains exact OCCT
B-Rep data. Large fields are represented by dataset references rather than
being copied into ordinary CAD properties. Design geometry, simulation fields,
measured data, and calibrated parameters are separate concepts; only the design
document, material states, and the first bounded structural studies and fields
are persisted. See
[`native-project-format.md`](native-project-format.md).

## Import and viewport scope

The native executable accepts a PCB file path with `--pcb FILE`. A
`.kicad_pcb` suffix selects the KiCad importer; other paths are parsed as
Circuit JSON.

The Circuit JSON adapter currently requires one `pcb_board` and maps the
following profile into native UUID-backed records: board bounds and thickness,
`pcb_component`, `source_component`, `source_net`, `pcb_net`, `source_port`,
`pcb_port`, `pcb_smtpad`, `pcb_trace`, and `pcb_via`. It maps supported pin/net
relationships and exports one native PCB back to Circuit JSON. Plated holes
are currently skipped with a warning. Other element types, multiple-board
files, schematic-only content, and full Circuit JSON schema conformance are
outside this profile.

The KiCad parser reads a `.kicad_pcb` S-expression without importing KiCad
source code. The tested profile uses a rectangular `Edge.Cuts` outline built
from `gr_rect`/`gr_line`, board thickness, copper layer declarations,
footprints with rectangular/circular SMD pads, nets, tracks, and through-board
vias. Unsupported pad shapes, non-SMD pads, and out-of-profile elements are
skipped or reported. It does not import `.kicad_sch` or `.kicad_pro`, zones,
arbitrary board outlines, full design rules, footprint 3D model files, or
every KiCad property; there is no KiCad exporter yet. The regression fixture
and tests verify counts, connectivity, pad placement (including a rotated
footprint), trace geometry, vias, and layer assignment for this subset. They
do not establish general KiCad round-trip compatibility.

`PcbShapeBuilder` creates a display-only OCCT compound for the substrate,
supported pads, traces, and approximate via rings. If package height is
provided, it also draws a rectangular component envelope from the footprint
bounds; it does not load package CAD models. If copper thickness is missing,
the preview uses a clearly reported 0.035 mm visual thickness without writing
that value into the model. Inner-layer Z positions and via spans are likewise
identified as preview assumptions. The current preview does not draw solder
mask, solder paste, dielectric detail, board holes, planes, or exact component
solids. The generated OCCT compound is not the manufacturing or analysis
representation and is not written back as canonical PCB geometry.

## Solver and coupling boundary

`ICircuitSolver` is the DatumLoop-owned interface. The initial ngspice adapter
starts a separate worker process and sends a versioned-in-practice JSON
request/result exchange; the worker dynamically loads the system ngspice shared
library and never receives a live `DatumDocument`. The implemented analysis is
a DC operating point for two-pin resistors and ideal voltage sources. The
verified example is a 5 V source and 1 kOhm load, returning approximately 5 V,
5 mA, and 25 mW for the resistor. The result includes a thermal-source
placeholder containing component power. It is explicitly not connected to a
thermal solver, does not compute PCB trace loss, and does not update resistance
from temperature.

Future solver jobs should use immutable, versioned requests tied to a design
revision and explicit domain inputs. Results should carry solver identity and
version, units, assumptions, convergence/status, and provenance. DatumLoop
validates a result before offering it as a new simulation-state artifact; a
solver process must never mutate the open design document.

The planned coupling engine is an explicit graph of fields and mappings. A
coupling edge identifies a source result, a target field/parameter, unit and
spatial mapping, interpolation, and update rule. Iterations and time stepping
belong to a job, with convergence criteria and stop conditions recorded in its
provenance. A future electrical-to-thermal loop would map current and losses to
a heat-source field, solve temperature, update temperature-dependent material
properties, and then submit a new electrical request. No edge in that loop is
implemented yet.

Physics-domain requests should describe domains, fields, boundary and initial
conditions, and requested outputs without binding Core to one solver. Planned
adapters include ngspice/Xyce for circuits; Elmer, MOOSE, or other validated
FEM tools for thermal/structural work; OpenFOAM for CFD; openEMS for full-wave
EM; and Cantera/pycalphad for chemistry and metallurgy. Candidate selection,
license review, and capability validation are required before adding any
backend. GPL solver code will stay behind process/service boundaries when
that keeps it replaceable and avoids linking it into DatumLoop Core.

## Digital-twin data flow

```text
Design model (geometry + electronics + material definitions)
       | immutable, revisioned job request
       v
Solver adapters -> validated simulation fields/results
       |                         ^
       | field mapping/coupling  | calibrated model parameters
       v                         |
Measured state -> comparison -> calibration
```

The long-term unified engineering graph can connect machine bodies, PCB
assemblies, circuits, motors, controls, airflow, and sensors by stable IDs.
Field results remain independent datasets, with overlays for temperature,
current density, stress, displacement, pressure, velocity, and EM fields.
Deformed geometry is derived from nominal geometry plus displacement data;
it must not silently replace nominal manufacturing geometry. Telemetry,
calibration, uncertainty propagation, electrothermal feedback, CFD coupling,
electromagnetics, reliability, and time-domain twins remain future work.

## tscircuit repository and license review

The table records package versions and declared licenses checked on
2026-10-17. Package metadata is used where it declares an SPDX license; where
it does not, the repository's LICENSE/readme declaration is recorded. These
are candidate references only. DatumLoop has not copied or linked any of
these projects. A release must recheck the selected tag/commit, transitive
dependency notices, generated assets, and license obligations before adding a
dependency or source.

| Repository/package | Version checked | Declared license | Evaluation |
|---|---:|---|---|
| [`tscircuit`](https://github.com/tscircuit/tscircuit) | 0.0.2646 | MIT | Do not adopt the all-in-one React/TypeScript runtime; use as a format/workflow reference only. Its broad dependency tree needs a separate audit. |
| [`@tscircuit/core`](https://github.com/tscircuit/core) | 0.0.2121 | MIT | Candidate reference for circuit concepts and Circuit JSON generation; not a C++ core dependency. |
| [`circuit-json`](https://github.com/tscircuit/circuit-json) | 0.0.514 | ISC | Interchange schema/ecosystem reference; DatumLoop keeps its own native model and C++ adapter. |
| [`@tscircuit/capacity-autorouter`](https://github.com/tscircuit/tscircuit-autorouter) | 0.0.985 | MIT | Candidate for a future isolated `IPcbAutorouter` worker; not integrated or yet benchmarked against DatumLoop constraints. |
| [`kicad-converter`](https://github.com/tscircuit/kicad-converter) | 0.0.17 | MIT | A possible conversion reference. Its package manifest does not declare a license field, so the repository MIT license is the source for this row; no converter code is used by the native parser. |
| [`circuit-json-to-step`](https://github.com/tscircuit/circuit-json-to-step) | 0.0.43 | MIT | STEP-export reference only. Evaluate geometry fidelity and asset/dependency licenses before any use. |
| [`circuit-json-to-gltf`](https://github.com/tscircuit/circuit-json-to-gltf) | 0.0.152 | MIT | GLTF/export and viewer reference only; DatumLoop keeps its native Qt/OCCT viewport. |

The native profile is implemented directly in C++ and Qt JSON/S-expression
parsing. MIT/ISC upstream declarations do not by themselves clear a dependency
tree, imported footprints, symbols, supplier assets, test fixtures, or
generated models. KiCad format interoperability is not permission to copy
KiCad implementation code or third-party libraries.

## Implemented versus planned

Implemented in this slice: common material definitions and state; sourced
material property uncertainty and temperature-bound metadata; native circuits
and PCB stack-up/geometry/connectivity; schema 7 persistence including the
first CAD-linked 3D structural study, MFEM fields, and provenance;
Circuit JSON profile import/export; simple KiCad PCB profile import; a
display-only OCCT PCB compound; an isolated ngspice DC adapter; and a
power-to-thermal-source placeholder. Tests cover native round-trip/migrations,
adapter element counts/relationships/coordinates, PCB preview construction,
and the 5 V/resistor numerical result.

Not yet implemented: schematic editing, broad KiCad compatibility/export,
component/package libraries and exact component solids, design-rule checking,
autorouting, trace resistance/power distribution, thermal/structural/fluid/EM
solvers, physics-field datasets and viewers, coupling iterations, telemetry,
calibration, uncertainty, and reliability calculations. These are roadmap
items, not current product claims.
