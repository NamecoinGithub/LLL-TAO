#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "================================================"
echo "Setting up Nexus LLL-TAO Development Environment"
echo "================================================"

echo "Installing shared build dependencies..."
bash "${REPO_ROOT}/contrib/devtools/install-build-deps.sh"

# Clean up apt cache to reduce image size
echo "Cleaning up..."
apt-get clean
rm -rf /var/lib/apt/lists/*

# Set proper permissions for workspace
chown -R vscode:vscode /workspace || true

echo ""
echo "================================================"
echo "Environment Setup Complete!"
echo "================================================"
echo ""
echo "To build the project:"
echo "  cd /workspace"
echo "  make clean"
echo "  make -f makefile.cli -j\$(nproc)"
echo ""
echo "To run tests:"
echo "  make -f makefile.cli UNIT_TESTS=1 -j\$(nproc)"
echo ""
echo "Nexus ports forwarded:"
echo "  8323 - Tritium Protocol (Legacy Mining)"
echo "  9323 - Tritium Protocol (Stateless Mining)"
echo "  9325 - Testnet"
echo "  9336 - API Server"
echo ""
