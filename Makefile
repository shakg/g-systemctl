.PHONY: build test clean

NPROC := $(shell command -v nproc >/dev/null 2>&1 && nproc || sysctl -n hw.ncpu 2>/dev/null || echo 4)

build:
	rm -rf build && mkdir build && cd build && cmake -DBUILD_TESTING=ON .. && $(MAKE) -j$(NPROC)

test:
	cd build/tests && ./g-systemctl-tests 2>&1 && ctest --output-on-failure

clean:
	rm -rf build
