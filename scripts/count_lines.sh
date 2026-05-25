#!/bin/bash
echo "=== cppdesk line count ==="
echo "C++ source:"
find src libs include -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.mm' | xargs cat 2>/dev/null | wc -l
echo "Dart (Flutter UI):"
find flutter -name '*.dart' -not -path '*/build/*' | xargs cat 2>/dev/null | wc -l
echo "Config/build:"
find . -name '*.cmake' -o -name 'CMakeLists.txt' -o -name '*.yml' -o -name '*.yaml' -o -name '*.json' -o -name '*.proto' | xargs cat 2>/dev/null | wc -l
echo "Total:"
find . -type f -not -path '*/.git/*' | xargs cat 2>/dev/null | wc -l
