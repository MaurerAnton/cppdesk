# Convenience Makefile for common cppdesk tasks
.PHONY: all build debug release test clean format install

all: release

build:
	cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
	cmake --build build --parallel $$(nproc)

debug:
	cmake -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
	cmake --build build-debug --parallel $$(nproc)

release:
	cmake -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
	cmake --build build-release --parallel $$(nproc)

test: debug
	cd build-debug && ctest --output-on-failure -j$$(nproc)

clean:
	rm -rf build build-debug build-release build-sanitize

format:
	find src libs include -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.mm' | xargs clang-format -i

install:
	cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
	cmake --build build
	sudo cmake --install build

lint:
	find src libs include -name '*.cpp' -o -name '*.hpp' | xargs clang-tidy -p build

package: build
	cd build && cpack -G DEB && cpack -G RPM && cpack -G ZIP

docker-build:
	docker build -t maureranton/cppdesk:latest .

docker-run:
	docker run -p 21116:21116 -p 21117:21117 -p 21118:21118 maureranton/cppdesk:latest

count:
	@echo "Source lines:"
	@find src libs include -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.mm' | xargs cat 2>/dev/null | wc -l
	@echo "Test lines:"
	@find tests -name '*.cpp' | xargs cat 2>/dev/null | wc -l
	@echo "Flutter lines:"
	@find flutter -name '*.dart' -not -path '*/build/*' | xargs cat 2>/dev/null | wc -l
	@echo "Total:"
	@find . -type f -not -path '*/.git/*' | xargs cat 2>/dev/null | wc -l

help:
	@echo "cppdesk Makefile"
	@echo "  make build     - Build release"
	@echo "  make debug     - Build debug with tests"
	@echo "  make test      - Build and run tests"
	@echo "  make format    - Format source code"
	@echo "  make install   - Install system-wide"
	@echo "  make package   - Create packages"
	@echo "  make count     - Count lines"
	@echo "  make help      - This help"
