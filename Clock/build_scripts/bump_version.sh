#!/bin/bash
# Bump the patch number in version.h. Sourced by upload scripts after a
# successful flash so the next build carries a new version.
#
# Usage: bump_version "$PROJECT_PATH/version.h"

bump_version() {
    local version_file="$1"
    [ -f "$version_file" ] || { echo "   ⚠️  version.h not found at $version_file"; return; }

    local current new major minor patch
    current=$(grep -o 'FIRMWARE_VERSION "[0-9]*\.[0-9]*\.[0-9]*"' "$version_file" \
              | grep -o '[0-9]*\.[0-9]*\.[0-9]*')
    if [ -z "$current" ]; then
        echo "   ⚠️  Could not parse version from $version_file"
        return
    fi

    major=$(echo "$current" | cut -d. -f1)
    minor=$(echo "$current" | cut -d. -f2)
    patch=$(echo "$current" | cut -d. -f3)
    new="$major.$minor.$((patch + 1))"

    sed -i '' "s/FIRMWARE_VERSION \"$current\"/FIRMWARE_VERSION \"$new\"/" "$version_file"
    echo "   Version: $current → $new"
}
