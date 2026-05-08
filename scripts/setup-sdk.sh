#!/usr/bin/env bash

# Setup Discord SDK for build
# Downloads the Discord Social SDK from the private release using a token.

set -euo pipefail

SDK_DIR="lib/discord_social_sdk"
RELEASE_LIB_DIR="$SDK_DIR/lib/release"
DEBUG_LIB_DIR="$SDK_DIR/lib/debug"
TMP_DIR="$(mktemp -d)"

# SHA-256 of the official Discord Social SDK zip as downloaded from the
# Discord Developer Portal. Used as both the cache key in CI and to verify
# the downloaded archive has not been tampered with.
SDK_SHA256=$(tr -d '[:space:]' < "$(dirname "$0")/sdk-sha256.txt")

ASSET_NAME="DiscordSocialSdk-1.9.15332.zip"
DOWNLOAD_URL="${SDK_DOWNLOAD_URL:?SDK_DOWNLOAD_URL is not set}"
DOWNLOAD_TOKEN="${SDK_DOWNLOAD_TOKEN:?SDK_DOWNLOAD_TOKEN is not set}"

download_file() {
    local dest="$1"
    local url="$2"
    local token="$3"

    echo "🌐 Requesting: $url"

    if command -v curl >/dev/null 2>&1; then
        curl -L --fail \
            -H "Authorization: Bearer $token" \
            -H "Accept: application/octet-stream" \
            -o "$dest" "$url"
    elif command -v wget >/dev/null 2>&1; then
        wget \
            --header="Authorization: Bearer $token" \
            --header="Accept: application/octet-stream" \
            -O "$dest" "$url"
    elif command -v pwsh >/dev/null 2>&1; then
        pwsh -Command "Invoke-WebRequest -Uri '$url' -OutFile '$dest' -UseBasicParsing -Headers @{'Authorization'='Bearer $token';'Accept'='application/octet-stream'}"
    elif command -v powershell >/dev/null 2>&1; then
        powershell -Command "Invoke-WebRequest -Uri '$url' -OutFile '$dest' -UseBasicParsing -Headers @{'Authorization'='Bearer $token';'Accept'='application/octet-stream'}"
    else
        echo "❌ No download tool available (curl, wget, or powershell required)"
        exit 1
    fi
}

verify_sha256() {
    local file="$1"
    local expected="$2"
    local actual

    if command -v sha256sum >/dev/null 2>&1; then
        actual=$(sha256sum "$file" | awk '{print $1}')
    elif command -v shasum >/dev/null 2>&1; then
        actual=$(shasum -a 256 "$file" | awk '{print $1}')
    else
        echo "❌ No SHA-256 tool available (sha256sum or shasum required)"
        exit 1
    fi

    if [ "$actual" != "$expected" ]; then
        echo "❌ SHA-256 mismatch — file may have been tampered with"
        echo "   Expected: $expected"
        echo "   Got:      $actual"
        exit 1
    fi

    echo "✓ SHA-256 verified"
}

echo "Setting up Discord Social SDK..."

if [ -d "$SDK_DIR" ] && [ -f "$RELEASE_LIB_DIR/libdiscord_partner_sdk.so" ]; then
    echo "✓ Discord SDK already present"
    exit 0
fi

mkdir -p "$SDK_DIR"
mkdir -p "$RELEASE_LIB_DIR"
mkdir -p "$DEBUG_LIB_DIR"

echo "📥 Downloading Discord SDK..."
FILE_PATH="$TMP_DIR/$ASSET_NAME"
download_file "$FILE_PATH" "$DOWNLOAD_URL" "$DOWNLOAD_TOKEN"

echo "🔍 Verifying integrity..."
verify_sha256 "$FILE_PATH" "$SDK_SHA256"

echo "📦 Extracting SDK..."
unzip -q "$FILE_PATH" -d "$TMP_DIR"

if [ -d "$TMP_DIR/discord-sdk/discord_social_sdk" ]; then
    rm -rf "$SDK_DIR"
    mv "$TMP_DIR/discord-sdk/discord_social_sdk" "$SDK_DIR"
elif [ -d "$TMP_DIR/discord_social_sdk" ]; then
    rm -rf "$SDK_DIR"
    mv "$TMP_DIR/discord_social_sdk" "$SDK_DIR"
else
    echo "❌ Extracted archive does not contain discord_social_sdk in the expected location"
    echo "Contents of $TMP_DIR:"
    find "$TMP_DIR" -maxdepth 3 | sed "s#^$TMP_DIR##"
    exit 1
fi

rm -rf "$TMP_DIR"

echo "✓ Discord SDK setup complete"
exit 0
