#!/bin/bash
set -e

if [ "${EUID:-$(id -u)}" -eq 0 ]; then
    SUDO=""
elif command -v sudo >/dev/null 2>&1; then
    SUDO="sudo"
else
    echo "Error: root or sudo is required to install build dependencies." >&2
    exit 1
fi

echo "================================================"
echo "Setting up Nexus LLL-TAO Development Environment"
echo "================================================"

# Prevent interactive prompts during package installation
export DEBIAN_FRONTEND=noninteractive

# Update package lists
echo "Updating package lists..."
$SUDO apt-get update

# Install essential build tools
echo "Installing build essentials..."
$SUDO apt-get install -y \
    build-essential \
    gcc \
    g++ \
    make \
    cmake \
    autoconf \
    automake \
    libtool \
    pkg-config \
    git \
    wget \
    curl \
    vim \
    gdb

# Install Berkeley DB 5.3 (required for blockchain database)
echo "Installing Berkeley DB 5.3..."
$SUDO apt-get install -y libdb5.3-dev libdb5.3++-dev

# Install OpenSSL (required for cryptographic operations)
echo "Installing OpenSSL..."
$SUDO apt-get install -y libssl-dev

# Install Boost libraries (required for various utilities)
echo "Installing Boost libraries..."
$SUDO apt-get install -y \
    libboost-all-dev \
    libboost-system-dev \
    libboost-filesystem-dev \
    libboost-thread-dev \
    libboost-program-options-dev

# Install MiniUPnP (required for NAT traversal)
echo "Installing MiniUPnP..."
$SUDO apt-get install -y miniupnpc libminiupnpc-dev

# Install libevent (required for async I/O)
echo "Installing libevent..."
$SUDO apt-get install -y libevent-dev

# Install additional utilities
echo "Installing additional utilities..."
$SUDO apt-get install -y \
    libgmp-dev \
    libsodium-dev \
    zlib1g-dev

echo "Verifying Berkeley DB headers..."
TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT
cat >"$TMPDIR/dbcheck.cpp" <<'EOF'
#include <db_cxx.h>
int main() { return 0; }
EOF
g++ -std=c++17 -c "$TMPDIR/dbcheck.cpp" -o "$TMPDIR/dbcheck.o"

# Clean up apt cache to reduce image size
echo "Cleaning up..."
$SUDO apt-get clean
$SUDO rm -rf /var/lib/apt/lists/*

# Set proper permissions for workspace
if [ -d /workspace ] && id -u vscode >/dev/null 2>&1; then
    $SUDO chown -R vscode:vscode /workspace
fi

echo ""
echo "================================================"
echo "Environment Setup Complete!"
echo "================================================"
echo ""
echo "To build the project:"
echo "  cd /workspace"
echo "  make -f makefile.cli clean"
echo "  make -f makefile.cli -j\$(nproc)"
echo ""
echo "To run tests:"
echo "  make -f makefile.cli UNIT_TESTS=1 -j\$(nproc)"
echo "  ./nexus \"[llp]\""
echo ""
echo "Nexus ports forwarded:"
echo "  8323 - Tritium Protocol (Legacy Mining)"
echo "  9323 - Tritium Protocol (Stateless Mining)"
echo "  9325 - Testnet"
echo "  9336 - API Server"
echo ""
