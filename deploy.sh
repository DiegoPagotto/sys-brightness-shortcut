#!/bin/bash

# Deployment script for Brightness Shortcut Sysmodule
# Title ID: 0x0004000001EB3400

TITLE_ID="0004000001EB3400"
NSP_FILE="sys-brightness-shortcut.nsp"

echo "=== Brightness Shortcut Sysmodule Deployment Script ==="
echo "Title ID: $TITLE_ID"
echo ""

# Check if NSP file exists
if [ ! -f "$NSP_FILE" ]; then
    echo "Error: $NSP_FILE not found!"
    echo "Please run 'make' first to compile the sysmodule."
    exit 1
fi

# Create deployment directory structure
DEPLOY_DIR="deploy/atmosphere/contents/$TITLE_ID"
mkdir -p "$DEPLOY_DIR/flags"

# Copy the NSP file
cp "$NSP_FILE" "$DEPLOY_DIR/exefs.nsp"

# Copy toolbox.json for Hekate toolbox integration
if [ -f "toolbox.json" ]; then
    cp "toolbox.json" "$DEPLOY_DIR/toolbox.json"
    echo "✓ Copied toolbox.json for Hekate integration"
else
    echo "⚠ Warning: toolbox.json not found"
fi

# Create boot flag for auto-loading at boot
touch "$DEPLOY_DIR/flags/boot2.flag"

echo "✓ Created deployment structure in ./deploy/"
echo "✓ Copied $NSP_FILE to exefs.nsp"
echo ""
echo "Deployment structure:"
find deploy -type f | sort

echo ""
echo "To install on your Switch:"
echo "1. Copy the contents of ./deploy/ to your SD card root"
echo "2. Reboot your Switch"
echo "3. Check Hekate toolbox > Background services for 'sys-brightness-shortcut'"
echo "4. Check sdmc:/atmosphere/logs/brightness_shortcut.log for output"
echo "5. Use L+R+Up/Down to control brightness system-wide!"
echo ""
echo "✅ Auto-loading at boot is ENABLED (boot2.flag present)"
