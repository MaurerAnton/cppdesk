# Contributing to cppdesk

## Code Style
- C++20 standard
- snake_case for functions/variables
- PascalCase for classes/structs
- Use `#pragma once` for headers
- Include project headers with relative paths: `"common/config.hpp"`

## Commit Messages
- Format: `module: brief description`
- Examples: `scrap: add DXGI desktop duplication capturer`

## Pull Request Process
1. Fork the repository
2. Create a feature branch
3. Add tests for new functionality
4. Ensure all tests pass (`scripts/run_tests.sh`)
5. Run code formatter (`scripts/code_format.sh`)
6. Submit PR with description

## Build Verification
```bash
cmake -B build -G Ninja -DBUILD_TESTS=ON
cmake --build build
cd build && ctest --output-on-failure
```
