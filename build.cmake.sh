#!/bin/bash

set -e
mkdir -p build/hdf5-1.8.20
mkdir -p build/hdf_explorer
mkdir -p ext

remote=$(git config --get remote.origin.url)
echo "remote repository: $remote"

if [ ! -d "ext/hdf5-1.8.20" ]; then
    echo "Cloning HDF5 hdf5-1.8.20..."
    git clone -b hdf5-1_8_20 https://github.com/HDFGroup/hdf5.git ext/hdf5-1.8.20
else
    echo "ext/hdf5-1.8.20 already exists, skipping clone"
fi

echo "Building HDF5..."
pushd build/hdf5-1.8.20

cmake ../../ext/hdf5-1.8.20 \
    -DBUILD_SHARED_LIBS=OFF \
    -DHDF5_BUILD_STATIC_TOOLS=ON \
    -DBUILD_STATIC_LIBS=ON \
    -DBUILD_STATIC_EXECS=ON \
    -DHDF5_ENABLE_ZLIB_SUPPORT=OFF \
    -DH5_USE_18_API_DEFAULT=ON \
    -DCMAKE_BUILD_TYPE=Debug \
    -DHDF5_BUILD_EXAMPLES=OFF \
    -DHDF5_BUILD_TESTING=OFF \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5

cmake --build . --config Debug

popd

echo "Building HDFStudio..."
pushd build/hdf_explorer

if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" ]]; then
    echo "Configuring for Windows..."
    cmake ../../ -DCMAKE_BUILD_TYPE=Debug -DCMAKE_POLICY_VERSION_MINIMUM=3.5
elif [[ "$OSTYPE" == "darwin"* ]]; then
    echo "Configuring for macOS..."
    cmake ../../ -DCMAKE_BUILD_TYPE=Debug -DCMAKE_POLICY_VERSION_MINIMUM=3.5
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
    echo "Configuring for Linux..."
    cmake ../../ -DCMAKE_BUILD_TYPE=Debug -DCMAKE_POLICY_VERSION_MINIMUM=3.5
fi

cmake --build . --config Debug

popd
