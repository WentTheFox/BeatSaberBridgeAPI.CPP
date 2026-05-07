#!/bin/bash

# Setup Discord SDK for build
# This script downloads the Discord Social SDK from GitHub releases

set -e

SDK_DIR="lib/discord_social_sdk"
RELEASE_LIB_DIR="$SDK_DIR/lib/release"
DEBUG_LIB_DIR="$SDK_DIR/lib/debug"

echo "Setting up Discord Social SDK..."

# Create directory structure
mkdir -p "$RELEASE_LIB_DIR"
mkdir -p "$DEBUG_LIB_DIR"
mkdir -p "$SDK_DIR/include"
mkdir -p "$SDK_DIR/bin/release"

# Check if SDK files exist locally
if [ -d "$SDK_DIR" ] && [ -f "$RELEASE_LIB_DIR/libdiscord_partner_sdk.so" ]; then
    echo "✓ Discord SDK already present"
    exit 0
fi

# Download SDK from GitHub release
echo "📥 Downloading Discord SDK from GitHub release..."

GITHUB_USER="RainzDev"
GITHUB_REPO="BeatSaberBridgeAPI.CPP"
ASSET_NAME="discord-sdk.tar.gz"

# Get the latest release download URL
DOWNLOAD_URL=$(curl -s "https://api.github.com/repos/$GITHUB_USER/$GITHUB_REPO/releases/tag/SDK" | grep -o "\"browser_download_url\": \"[^\"]*$ASSET_NAME" | cut -d'"' -f4)

if [ -z "$DOWNLOAD_URL" ]; then
    echo "❌ Failed to find Discord SDK in GitHub releases"
    echo "Please ensure the SDK is uploaded as '$ASSET_NAME' in the latest release"
    exit 1
fi

echo "Downloading from: $DOWNLOAD_URL"
wget -O /tmp/discord-sdk.tar.gz "$DOWNLOAD_URL"

# Extract SDK
echo "📦 Extracting SDK..."
tar -xzf /tmp/discord-sdk.tar.gz -C lib/
rm /tmp/discord-sdk.tar.gz

echo "✓ Discord SDK setup complete"
exit 0

