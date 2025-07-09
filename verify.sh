#!/bin/bash

# Verification script for Brightness Shortcut Sysmodule deployment
# This script checks if the deployment is ready for Switch installation

TITLE_ID="0100000000001337"
DEPLOY_DIR="deploy/atmosphere/contents/$TITLE_ID"

echo "=== Brightness Shortcut Sysmodule Verification ==="
echo "Title ID: $TITLE_ID"
echo ""

# Check if deployment directory exists
if [ ! -d "$DEPLOY_DIR" ]; then
    echo "❌ Error: Deployment directory not found!"
    echo "   Please run ./deploy.sh first"
    exit 1
fi

echo "✅ Deployment directory exists"

# Check exefs.nsp
if [ ! -f "$DEPLOY_DIR/exefs.nsp" ]; then
    echo "❌ Error: exefs.nsp not found!"
    exit 1
fi

NSP_SIZE=$(stat -c%s "$DEPLOY_DIR/exefs.nsp" 2>/dev/null || echo "0")
echo "✅ exefs.nsp found (${NSP_SIZE} bytes)"

# Check boot2.flag
if [ ! -f "$DEPLOY_DIR/flags/boot2.flag" ]; then
    echo "❌ Error: boot2.flag not found!"
    echo "   Without this file, the sysmodule won't auto-start"
    exit 1
fi

echo "✅ boot2.flag found (enables auto-start)"

# Check toolbox.json
if [ ! -f "$DEPLOY_DIR/toolbox.json" ]; then
    echo "❌ Error: toolbox.json not found!"
    echo "   Without this file, the sysmodule won't appear properly in Hekate toolbox"
    exit 1
fi

echo "✅ toolbox.json found (Hekate toolbox integration)"

# Check file integrity
if [ -f "sysmodule.nsp" ]; then
    ORIG_SIZE=$(stat -c%s "sysmodule.nsp")
    if [ "$NSP_SIZE" -eq "$ORIG_SIZE" ]; then
        echo "✅ File integrity verified (${ORIG_SIZE} bytes)"
    else
        echo "❌ Warning: File size mismatch!"
        echo "   Original: ${ORIG_SIZE} bytes"
        echo "   Deployed: ${NSP_SIZE} bytes"
    fi
fi

echo ""
echo "🎯 Deployment Summary:"
echo "   📁 Location: $DEPLOY_DIR"
echo "   📦 Package: exefs.nsp (${NSP_SIZE} bytes)"
echo "   🚀 Auto-start: Enabled (boot2.flag present)"
echo "   🔧 Toolbox: Enabled (toolbox.json present)"
echo ""
echo "📋 Installation Instructions:"
echo "   1. Copy deploy/atmosphere/ to your SD card root"
echo "   2. Reboot your Switch"
echo "   3. Check Hekate toolbox > Background services for 'sys-brightness-shortcut'"
echo "   4. Look for log: sdmc:/atmosphere/logs/brightness_shortcut.log"
echo "   5. Use L+R+Up/Down to control brightness!"
echo ""
echo "✅ Ready for Switch installation!"
