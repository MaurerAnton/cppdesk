#!/bin/bash
# Notarize macOS build
set -e
APPLE_ID=${CPPDESK_APPLE_ID:-}
APPLE_PASSWORD=${CPPDESK_APPLE_PASSWORD:-}
TEAM_ID=${CPPDESK_TEAM_ID:-}

if [ -z "$APPLE_ID" ]; then
    echo "Set CPPDESK_APPLE_ID environment variable"
    exit 1
fi

DMG="build/cppdesk-1.3.0.dmg"

echo "Submitting for notarization..."
xcrun notarytool submit "$DMG" \
    --apple-id "$APPLE_ID" \
    --password "$APPLE_PASSWORD" \
    --team-id "$TEAM_ID" \
    --wait

echo "Stapling notarization ticket..."
xcrun stapler staple "$DMG"

echo "macOS notarization complete"
