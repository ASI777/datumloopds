# Reproducible native build environment

The native Linux build uses the pinned image and direct package versions in
[`containers/native-build.Dockerfile`](../containers/native-build.Dockerfile)
and [`containers/native-build.lock`](../containers/native-build.lock). The
image is Ubuntu 26.04 for Linux amd64, identified by its immutable OCI digest.
APT uses the Ubuntu archive snapshot `20261017T000000Z`; the lock pins CMake,
GCC/build-essential, Qt 6, OCCT, and ngspice direct packages. Transitive APT
dependencies resolve from that dated archive snapshot rather than the live
archive. The minimal Ubuntu base omits CA roots, so the image first installs
the version-pinned CA and SSL bootstrap packages listed in the lock from
Ubuntu's signed archive metadata, then verifies the HTTPS snapshot. Both APT
index updates fail on partial downloads to avoid falling back to stale or live
package indexes. Ubuntu documents timestamped archive snapshots for
reproducible package sets in its [snapshot service guide](https://ubuntu.com/server/docs/how-to/software/snapshot-service/).
Docker documents digest-pinned base images in its
[build guidance](https://docs.docker.com/build/building/best-practices/#pin-base-image-versions).

Build and run the locked check from a clean checkout:

```sh
docker build --file containers/native-build.Dockerfile --tag datumloopds-native:locked .
docker run --rm --volume "$PWD:/workspace:ro" datumloopds-native:locked
```

The container performs a Release CMake configure/build, runs the native CTest
suite with MFEM disabled, then launches `DatumLoopDS --smoke-load` on Qt's
offscreen platform. This verifies the project QML shell initializes; it does
not verify rendered frames or hardware-GPU behavior. MFEM and MuJoCo remain
optional integrations and are intentionally excluded from this baseline lock.
When either backend is unavailable, CTest reports its integration checks as
skipped so the baseline does not imply those integrations were exercised.
The same commands run in
[`reproducible-native-build.yml`](../.github/workflows/reproducible-native-build.yml)
on pushes and pull requests.

The standalone C++/Qt/OCCT application and the FreeCAD 1.1.3 fork have separate
locked build images. The FreeCAD image and clean-checkout build/smoke command
are documented in the [FreeCAD build profile](freecad-1.1.3-build-profile.md).
Both use dated Ubuntu snapshot metadata for transitive package resolution.
These containers are build and CI environments, not application bundles. Any
future release that redistributes their packages must separately assemble the
required upstream license and notice files.
