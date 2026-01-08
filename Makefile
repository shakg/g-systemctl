.PHONY: build test clean

build:
	rm -rf build && mkdir build && cd build && cmake -DBUILD_TESTING=ON .. && make -j$(nproc)

test:
	cd build && ./g-systemctl-tests 2>&1 && ctest --output-on-failure

clean:
	rm -rf build
