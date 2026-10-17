PYTHON ?= python3
NATIVE_CMAKE_ARGS ?=

.PHONY: check freecad-check geometry-benchmark native-check native-smoke native-qml-smoke native-pointer-smoke native-independence-check build clean

check:
	PYTHONPATH=src $(PYTHON) -m unittest discover -s tests -v

freecad-check:
	./scripts/check-freecad-core.sh

geometry-benchmark:
	./scripts/run-geometry-benchmarks.sh

native-check:
	cmake -S . -B build/native -DBUILD_TESTING=ON $(NATIVE_CMAKE_ARGS)
	cmake --build build/native --parallel 1
	ctest --test-dir build/native --output-on-failure

native-smoke:
	./build/native/apps/datumloop-desktop/DatumLoopDS --smoke

native-pointer-smoke:
	xvfb-run -a -s '-screen 0 1600x1000x24 +extension GLX' python3 scripts/native_pointer_smoke.py

native-qml-smoke:
	QT_QPA_PLATFORM=offscreen ./build/native/apps/datumloop-desktop/DatumLoopDS --smoke-load

native-independence-check:
	cmake --build build/native --target DatumLoopDS --parallel 1
	./scripts/check-native-independence.sh

build:
	$(PYTHON) -m pip wheel --no-deps --no-build-isolation --wheel-dir dist .

clean:
	rm -rf build dist *.egg-info
