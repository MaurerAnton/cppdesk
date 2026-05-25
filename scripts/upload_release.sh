#!/bin/bash
# Upload release to GitHub
set -e
VERSION=${1:-1.3.0}
TAG="v${VERSION}"

echo "Creating release $TAG..."
gh release create "$TAG" \
    build/cppdesk-${VERSION}* \
    --title "cppdesk v${VERSION}" \
    --notes-file CHANGELOG.md \
    --draft=false \
    --prerelease=false

echo "Release $TAG created"
