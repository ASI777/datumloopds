# Licensing Strategy — Product Owner Decision Required

> **REQUIRES PRODUCT OWNER DECISION.** This is an inventory and architecture
> note, not a license change or legal opinion. No existing file is relicensed
> here. Keep the repository's current declarations in force until the product
> owner chooses a strategy and the rights/dependency review is complete.

## Current declarations and ownership evidence

The root `LICENSE`, `pyproject.toml`, README, and FreeCAD package metadata
currently declare DatumLoop-authored repository code under **GPL-3.0-or-later**.
Native C++, QML, and Python files generally have no per-file SPDX or copyright
headers. The commit history inspected for this audit is attributed to the
`ASI777` account; package metadata names the authors as “DatumLoop DS
contributors”. The repository contains no contributor license agreement,
copyright assignment, or per-contributor ownership record. Treat precise
ownership of historical contributions as **not yet verified**.

The current license is evidence of the project's declared terms. It does not
establish that a single person owns every contribution or has authority to
offer every file under a different license.

## Component inventory

| Component / files | Current license | Copyright owner evidence | Third-party dependency or relationship | Independent authorship and relicensing assessment | Commercial implications |
|---|---|---|---|---|---|
| DatumLoop Python package, C++ core/UI, QML, scripts, tests, and original icon | Root `GPL-3.0-or-later` declaration; no per-file headers in most files | Git history currently attributes commits to `ASI777`; package metadata says “DatumLoop DS contributors”; individual rights are not documented | Uses Qt and OCCT in the native app; optional SMLib integration; Python build backend is setuptools | Appears independently authored from this repository's history; no copied FreeCAD or Blender source was identified in the audited paths. Re-licensing is only potentially possible after confirming every contributor's rights or obtaining permission. **Not authorized.** | GPL permits commercial distribution, but does not itself provide a closed-source license for covered DatumLoop code. Do not promise separately proprietary modules can be distributed with the current core without a reviewed module boundary and legal analysis. |
| FreeCAD workbench and Python adapter (`freecad/DatumLoopDS/`, `src/datumloopds/freecad/`, related smoke tests) | GPL-3.0-or-later in this repository | Same DatumLoop ownership uncertainty as above | Calls FreeCAD APIs and runs inside the FreeCAD host; the FreeCAD application source is LGPL 2.0-or-later | The DatumLoop integration appears independently authored against FreeCAD APIs, not copied from FreeCAD source. It is FreeCAD-dependent. Re-licensing requires the same DatumLoop rights review and a separate analysis of how the host is used. **Not authorized.** | Keep isolated in the compatibility subsystem. The external FreeCAD fork retains its own upstream license and notices; do not treat the FreeCAD fork as DatumLoop-owned code. |
| FreeCAD source fork (`ASI777/FreeCAD`, maintained separately) | Upstream FreeCAD source is LGPL 2.0-or-later; check per-file and bundled-library notices in that repository | FreeCAD contributors and third-party authors; upstream requires compatible contributions | FreeCAD's third-party libraries, including its OCCT integration and other packaged components | The fork contains upstream FreeCAD source plus a limited branding integration. It is not independently authored as a whole and cannot be relicensed by DatumLoop. | Redistributing the fork requires following FreeCAD's LGPL terms and every bundled dependency's notices. This remains a compatibility/migration product. |
| Native OCCT integration (`src/datumloop/geometry/`) | DatumLoop files currently inherit the root GPL declaration | DatumLoop authorship is subject to the ownership caveat above | Directly uses system Open CASCADE Technology 7.9.2; OCCT states its libraries are LGPL 2.1 with an additional exception | Integration code appears independently written against OCCT APIs; no OCCT implementation source is vendored in the main repository. The integration's own license can only change after DatumLoop rights confirmation. **Not authorized.** | OCCT's license and exception must be reviewed for the exact version, build, and distribution. Preserve its license and notices in any binary distribution. |
| Optional SMLib integration (`src/datumloopds/geometry/smlib/`) | DatumLoop integration currently inherits the root GPL declaration | DatumLoop authorship is subject to the ownership caveat above | Optional `smlibpy==0.3.4`; upstream smlibpy is MIT and bundles USD BRep under Apache-2.0, with additional notices for included fonts and patches | The adapter appears independently authored against the smlibpy API; no SMLib implementation is copied into the main repository. Relicensing the adapter is conditional on DatumLoop ownership confirmation. **Not authorized.** | SMLib remains optional. If a future installer bundles it, ship its MIT, Apache-2.0, font, patch, and other required notices as applicable. |
| Blender UX reference (`docs/blender-ux-reference.md`) | Repository documentation currently falls under the root GPL declaration; no separate notice | DatumLoop-authored analysis; linked Blender materials remain owned/licensed by their authors | References Blender 5.2 source/manual pages for study only | The document records interaction concepts and clean-room implementation boundaries; it contains no Blender source or bundled Blender artwork/assets. Any future copied excerpt or asset requires a new review. | Keep the reference-only boundary. DatumLoop code and visual assets must be independently created. |
| Qt 6.10.2 modules: Core, Gui, Qml, Quick, Quick Controls 2, Quick 3D, Concurrent | Qt offers module-specific open-source and commercial terms; the Qt licensing table lists Qt Quick 3D under GPL-3.0 for its open-source option | The Qt Company and third-party authors identified by Qt | Current CMake directly requests the modules above; Qt modules themselves include separately licensed third-party code | Not DatumLoop-owned and cannot be relicensed. Confirm the exact license selection for every shipped Qt module and its third-party components. | Qt Quick 3D's GPL-only open-source option is a material constraint for future proprietary distribution. A Qt commercial license or an alternative renderer may be a product decision; neither is selected here. |
| Setuptools 78.1.1 build backend | MIT | Python Packaging Authority and contributors | Build-time dependency in `requirements-build.lock`; not a runtime dependency | Third-party and not DatumLoop-owned. | Retain the MIT notice if redistributed. |

## Architecture boundaries for future modules

The native target keeps these source boundaries independently reviewable:

- `DatumLoop Core` owns document and engineering concepts and must not depend on
  QML, FreeCAD, or a specific viewport renderer.
- UI and renderer adapters consume core interfaces. The current Qt Quick 3D
  implementation is an adapter, not the owner of CAD geometry.
- Professional, enterprise, plugin, and solver modules have no license
  selection yet. Define their API and distribution model separately; source
  directories alone do not create a legal boundary.
- FreeCAD compatibility and SMLib integration remain separate adapters.
- Third-party sources remain under their respective licenses with notices
  preserved; they are not absorbed into the DatumLoop licensing decision.

## Open product decisions

1. Choose whether the existing GPL-3.0-or-later declaration remains the product
   license, is offered alongside another license, or changes in a future
   authorized release. No option is selected by this document.
2. Identify every copyright holder and contributor for code that may be
   relicensed. Obtain explicit authorization where needed; do not infer it from
   a repository admin role or commit email.
3. Decide how Qt Quick 3D's GPL/commercial availability fits the product's
   commercial plan, and verify the license of the exact Qt 6.10.2 modules and
   third-party packages that would ship.
4. Review the exact OCCT, FreeCAD, SMLib/USD BRep, and packaging distribution
   artifacts with qualified counsel before publishing a proprietary binary or
   module strategy.

## Sources checked

- [DatumLoop `pyproject.toml`](../pyproject.toml), root `LICENSE`, package
  metadata, dependency locks, source imports, and current git history.
- [FreeCAD licensing documentation](https://github.com/FreeCAD/FreeCAD-documentation/blob/main/wiki/License.md)
  and [FreeCAD contribution/license guidance](https://github.com/FreeCAD/FreeCAD/blob/main/CONTRIBUTING.md#4-licensing-ownership-and-credit).
- [Qt 6 licensing](https://doc.qt.io/qt-6.10/licensing.html) and [Qt 6.10
  third-party component inventory](https://doc.qt.io/qt-6.10/licenses-used-in-qt.html).
- [Open CASCADE Technology license](https://github.com/Open-Cascade-SAS/OCCT/blob/master/dox/license.md), plus the exact OCCT 7.9.2 distribution used by the native build.
- [smlibpy upstream repository and license summary](https://github.com/awv-informatik/smlibpy), plus its checked-out `LICENSE` and `NOTICE`.
- [setuptools upstream project](https://github.com/pypa/setuptools), plus the
  pinned package version in `requirements-build.lock`.
