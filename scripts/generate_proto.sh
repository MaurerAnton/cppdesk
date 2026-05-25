#!/bin/bash
# Generate protobuf C++ files
set -e
PROTOC=$(which protoc)
if [ -z "$PROTOC" ]; then
    echo "protoc not found. Install protobuf-compiler."
    exit 1
fi
echo "Generating C++ from proto files..."
$PROTOC --cpp_out=src/proto proto/messages.proto
echo "Proto generation complete"
