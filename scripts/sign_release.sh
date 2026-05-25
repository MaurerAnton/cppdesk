#!/bin/bash
# Sign release packages
set -e
VERSION=${1:-1.3.0}
GPG_KEY=${CPPDESK_GPG_KEY:-}

if [ -z "$GPG_KEY" ]; then
    echo "Set CPPDESK_GPG_KEY environment variable"
    exit 1
fi

echo "Signing packages with GPG key $GPG_KEY..."
cd build
for pkg in cppdesk-${VERSION}*; do
    if [ -f "$pkg" ]; then
        gpg --detach-sign --armor --local-user "$GPG_KEY" "$pkg"
        echo "Signed: $pkg"
    fi
done

# Generate checksums
sha256sum cppdesk-${VERSION}* > SHA256SUMS
gpg --detach-sign --armor --local-user "$GPG_KEY" SHA256SUMS
echo "Checksums signed"
