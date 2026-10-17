# Initial Z-spindle motor carrier — executed geometry slice

- Native editable project: `carrier.dld` (schema 9).
- Manufacturer interface: Maxon standard-flange ECX FLAT 22 L,
  [2025 catalog drawing, page 259](https://www.maxongroup.com/medias/sys_master/root/9399287709726/Cataloge-Page-EN-259.pdf).
- Requirements: `examples/parts/maxon-ecx-flat-22-l/design-study.json`;
  the source-only record preserves the original unknown machine inputs.
- Exact OCCT solid: valid, 34 × 44 × 1.5 mm, eight bores and nine editable
  sourced features. The actual BRep cylinder axes were checked for Ø16 motor
  pitch circle and 120° spacing, pilot position, gantry locations and diameters.
- Nominal volume: 2115.280623761895 mm³; nominal reference mass:
  5.732410490394736 g using the attached sourced 2710 kg/m³ density.
- Geometric manufacturing screen: passed for this restricted plate/normal-bore
  recipe. Worst retained thickness/wall with stated offsets is 1.45 mm versus
  a selected 1 mm limit; worst depth/diameter ratio is 0.704545 versus 5;
  maximum through depth is 1.55 mm versus 10 mm selected tool reach.
- Selected motor screw engagement: 2.5 mm, between the selected 2 mm minimum
  and the manufacturer's 2.9 mm thread-depth callout. Blind-hole bottom,
  washer and fastener stack-up still need assembly review.

## Tests actually executed

The MFEM/MuJoCo-enabled native build passes 13/13 CTest targets. The new tests
include requirements ingestion, missing-value rejection, dimensional conversion,
fixed-interface protection, bounded edits, `.dld` migration and source
persistence, actual BRep dimension checks, 1.5-to-2.5 mm thickness regeneration,
rejected stale source data and a deliberately failed minimum-wall criterion.
The native-independence check passes. The native Qt application loaded this
`.dld` and initialized its QML and OCCT-derived viewport data in offscreen mode;
that check does not establish presented 3D frames.

`cad-review.png` is a software review render of the actual exported OCCT display
triangles. Front, edge and isometric views were inspected; it is not GPU evidence.

## Engineering limits and next integration task

The study selects 40 g motor/accessory mass allocation, 60 m/s² normal
acceleration, a 2.792266 N motor/accessory equivalent normal load and 0.1935 N·m
reaction torque. These are design inputs, not computed structural results.
The 5 µm elastic interface-displacement and 0.2 mrad elastic angle limits are
selected design targets. Bracket self-inertia must be included in the FE load
cases. Nominal stress cap: 100 MPa, with no certified material safety factor.

**Machine-part FEA has not run.** The existing numerical pipeline supports Q1
hex meshes of a rectangular solid. The next integration task is an established
volume mesher with persistent-face boundary mapping and a supported tetrahedral
MFEM transfer, followed by convergence and quantitative design evaluation.
No automatic FEA-driven redesign, real-part ROM, Colab execution or GPU solver
verification is claimed. Manufacturing process qualification and full assembly
interference/tolerance verification also remain open.
