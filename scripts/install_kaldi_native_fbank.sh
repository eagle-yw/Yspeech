#!/bin/bash

# Install kaldi-native-fbank library

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
INSTALL_DIR="${PROJECT_ROOT}/3rd-lib"
BUILD_DIR="${PROJECT_ROOT}/build_knf"

echo "=========================================="
echo "Installing kaldi-native-fbank"
echo "=========================================="
echo "Install dir: ${INSTALL_DIR}"
echo ""

# Create directories
mkdir -p "${INSTALL_DIR}/include"
mkdir -p "${INSTALL_DIR}/lib"
mkdir -p "${BUILD_DIR}"

# Clone repository
cd "${BUILD_DIR}"
if [ ! -d "kaldi-native-fbank" ]; then
    echo "Cloning kaldi-native-fbank..."
    git clone --depth 1 --branch v1.20.0 https://github.com/csukuangfj/kaldi-native-fbank.git
fi

cd kaldi-native-fbank

# Create build directory
mkdir -p build
cd build

# Configure and build
echo "Configuring..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
    -DKALDI_NATIVE_FBANK_BUILD_TESTS=OFF \
    -DKALDI_NATIVE_FBANK_BUILD_PYTHON=OFF \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5

echo "Building..."
make -j4 kaldi-native-fbank-core

# Install headers
echo "Installing headers..."
cp -r ../kaldi-native-fbank/csrc "${INSTALL_DIR}/include/kaldi-native-fbank"

# Install library
echo "Installing library..."
cp lib/libkaldi-native-fbank-core.a "${INSTALL_DIR}/lib/"

# Cleanup
cd "${PROJECT_ROOT}"
rm -rf "${BUILD_DIR}"

echo ""
echo "=========================================="
echo "kaldi-native-fbank installed successfully!"
echo "=========================================="
echo "Headers: ${INSTALL_DIR}/include/kaldi-native-fbank"
echo "Library: ${INSTALL_DIR}/lib/libkaldi-native-fbank-core.a"
