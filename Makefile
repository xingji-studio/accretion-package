.PHONY: all build package check test linux windows xj380 demo
PYTHON ?= python3
all: build
build:
	$(PYTHON) tools/project.py build
package:
	$(PYTHON) tools/project.py package
check:
	$(PYTHON) tools/project.py check
test:
	$(PYTHON) tools/project.py test
linux:
	$(PYTHON) tools/build.py --target linux
windows:
	$(PYTHON) tools/build.py --target windows
xj380:
	$(PYTHON) tools/build.py --target xj380
demo:
	$(PYTHON) tools/build_demo.py --target all
