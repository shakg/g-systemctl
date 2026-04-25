BUILD_DIR ?= build
BUILD_TYPE ?= Release
JOBS ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 1)

.PHONY: all configure build run test install clean distclean

all: build

configure:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

build: configure
	cmake --build $(BUILD_DIR) --parallel $(JOBS)

run: build
	cd $(BUILD_DIR) && ./g-systemctl

test:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DBUILD_TESTING=ON
	cmake --build $(BUILD_DIR) --parallel $(JOBS)
	ctest --test-dir $(BUILD_DIR) --output-on-failure

install: build
	cmake --install $(BUILD_DIR)

clean:
	cmake --build $(BUILD_DIR) --target clean

distclean:
	rm -rf $(BUILD_DIR)
