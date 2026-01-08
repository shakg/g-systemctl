.PHONY: build test clean

build:
	rm -rf build && mkdir build && cd build && cmake -DBUILD_TESTING=ON .. && make -j4

test:
	cd build/tests && ./g-systemctl-tests 2>&1 && ctest --output-on-failure

clean:
	rm -rf build
