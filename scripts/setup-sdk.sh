#!/usr/bin/env bash

# Setup Discord SDK for build
# This script downloads the Discord Social SDK from GitHub releases

set -euo pipefail

SDK_DIR="lib/discord_social_sdk"
RELEASE_LIB_DIR="$SDK_DIR/lib/release"
DEBUG_LIB_DIR="$SDK_DIR/lib/debug"
TMP_DIR="$(mktemp -d)"

GITHUB_USER="RainzDev"
GITHUB_REPO="BeatSaberBridgeAPI.CPP"
ASSET_NAME="discord-sdk.tar.xz"
DOWNLOAD_URL="https://github.com/$GITHUB_USER/$GITHUB_REPO/releases/download/SDK/$ASSET_NAME"

download_file() {
    local dest="$1"
    local url="$2"

    if command -v curl >/dev/null 2>&1; then
        curl -L --fail -o "$dest" "$url"
    elif command -v wget >/dev/null 2>&1; then
        wget -O "$dest" "$url"
    elif command -v pwsh >/dev/null 2>&1; then
        pwsh -Command "Invoke-WebRequest -Uri '$url' -OutFile '$dest' -UseBasicParsing"
    elif command -v powershell >/dev/null 2>&1; then
        powershell -Command "Invoke-WebRequest -Uri '$url' -OutFile '$dest' -UseBasicParsing"
    else
        echo "❌ No download tool available (curl, wget, or powershell required)"
        exit 1
    fi
}

echo "Setting up Discord Social SDK..."

if [ -d "$SDK_DIR" ] && [ -f "$RELEASE_LIB_DIR/libdiscord_partner_sdk.so" ]; then
    echo "✓ Discord SDK already present"
    exit 0
fi

mkdir -p "$SDK_DIR"
mkdir -p "$RELEASE_LIB_DIR"
mkdir -p "$DEBUG_LIB_DIR"

echo "📥 Downloading Discord SDK from GitHub release..."
FILE_PATH="$TMP_DIR/$ASSET_NAME"
download_file "$FILE_PATH" "$DOWNLOAD_URL"

echo "📦 Extracting SDK..."
tar -xJf "$FILE_PATH" -C "$TMP_DIR"

if [ -d "$TMP_DIR/discord-sdk/discord_social_sdk" ]; then
    rm -rf "$SDK_DIR"
    mv "$TMP_DIR/discord-sdk/discord_social_sdk" "$SDK_DIR"
elif [ -d "$TMP_DIR/discord_social_sdk" ]; then
    rm -rf "$SDK_DIR"
    mv "$TMP_DIR/discord_social_sdk" "$SDK_DIR"
else
    echo "❌ Extracted archive does not contain discord_social_sdk in the expected location"
    echo "Contents of $TMP_DIR:"
    find "$TMP_DIR" -maxdepth 3 | sed 's#^$TMP_DIR##'
    exit 1
fi

rm -rf "$TMP_DIR"

echo "✓ Discord SDK setup complete"
exit 0

