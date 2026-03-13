#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

# Check Redis
if ! redis-cli ping > /dev/null 2>&1; then
    echo "Starting Redis..."
    redis-server --daemonize yes --loglevel warning
    sleep 1
fi

echo "Redis: $(redis-cli ping)"

# Build
if [ ! -f "$BUILD_DIR/evmdb" ]; then
    echo "Building..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
    cd "$PROJECT_DIR"
fi

# Run
echo "Starting evm-db..."
exec "$BUILD_DIR/evmdb" "$PROJECT_DIR/config/evmdb.toml"
