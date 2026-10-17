# ECX FLAT 22 L bracket — source ledger

The first part is a motor carrier for one Z-spindle head in a precision
placement machine. The source record is `requirements.json`. It is deliberately
source-only: unresolved machine and bracket requirements remain unknown.
The user has delegated application selection and optimization; subsequent
study-specific selections must be recorded separately from manufacturer data.

## Manufacturer reference

- Manufacturer: maxon; ECX FLAT 22 L, catalog standard flange.
- Document: [official catalog page 259](https://www.maxongroup.com/medias/sys_master/root/9399287709726/Cataloge-Page-EN-259.pdf).
- Revision: February 2025, provisional data, subject to change.
- PDF page 1, printed page 259. SHA256:
  `9aa3139520c7f4c6cdd5c74c1dafe24df351556142f77a1e26500e9c2b405d17`.
- The original front/end and side views were rendered and visually inspected;
  values were transcribed from callouts, not measured from pixels.
- This is a configurable family reference; a configured motor must be checked
  against this interface before manufacturing. The integrated-electronics motor
  is a different configuration and is not silently substituted.

| Feature | Source view | Interpretation | Evidence | Initial CAD state |
| --- | --- | --- | --- | --- |
| Mounting pattern | Mounting end | Ø16 pitch circle, three locations at 120° | Printed | Fixed; not generated yet |
| Motor fixing threads | End note | M2, 2.9 mm thread depth | Printed | Motor thread; bracket clearance is a separate choice |
| Locating pilot | Side | Ø6 mm, offsets −0.05/0; projection 1.7 mm, offsets −0.2/0 | Printed | Fixed motor interface; mating bore fit unresolved |
| Flange envelope | End | Ø22 ±0.1 mm | Printed | Envelope check only |
| Body envelope | Side | 17 mm maximum | Printed | Envelope check only |
| Motor loading data | Table, 18 V | 32 g motor mass; nominal torque 28.3 mN·m | Printed | Not a complete machine load case |
| Gantry pattern | No source | Mating locations absent | Unknown | No guessed holes |
| Precision criteria and acceleration | No source | Machine error/load budget absent | Unknown | No assumed pass |

The bracket does not reproduce the motor's shaft, connectors or internal
features. Its future locating bore is a precision interface, with a selected
fit/tolerance to be recorded independently. A thread depth is not an approved
screw engagement or screw length. Manufacturer GD&T is not transferred to a
new bracket without a separate datum/tolerance decision.

## Executed slice

Native requirements ingestion, unit conversion, source/derivation validation,
fixed-interface protection, bounded edits, quantitative criterion evaluation,
and `.dld` persistence are implemented. The dedicated test exercises source
unknowns, rejected interface changes, atomic bound rejection, failed and passing
criterion values, and schema-eight migration. No bracket geometry, machine-part
mesh, structural result or optimized design is claimed by this source slice.
