PYTHON ?= python3

.PHONY: check freecad-check build clean

check:
	PYTHONPATH=src $(PYTHON) -m unittest discover -s tests -v

freecad-check:
	./scripts/check-freecad-core.sh

build:
	$(PYTHON) -m pip wheel --no-deps --no-build-isolation --wheel-dir dist .

clean:
	rm -rf build dist *.egg-info
