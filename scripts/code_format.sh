#!/bin/bash
# Format all C++ source files with clang-format
find . -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.mm' | while read f; do
    clang-format -i "$f"
done
echo "Code formatted"
