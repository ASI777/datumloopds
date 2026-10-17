PYTHON ?= python3

.PHONY: check freecad-check geometry-benchmark native-check native-smoke native-qml-smoke build clean

check:
	PYTHONPATH=src $(PYTHON) -m unittest discover -s tests -v

freecad-check:
	./scripts/check-freecad-core.sh

geometry-benchmark:
	./scripts/run-geometry-benchmarks.sh

native-check:
	cmake -S . -B build/native -DBUILD_TESTING=ON
	cmake --build build/native --parallel 1
	ctest --test-dir build/native --output-on-failure

native-smoke:
	./build/native/apps/datumloop-desktop/DatumLoopDS --smoke

native-qml-smoke:
	QT_QPA_PLATFORM=offscreen ./build/native/apps/datumloop-desktop/DatumLoopDS --smoke-load

build:
	$(PYTHON) -m pip wheel --no-deps --no-build-isolation --wheel-dir dist .

clean:
	rm -rf build dist *.egg-info
