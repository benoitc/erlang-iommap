#!/bin/sh -x
# CMake configure step: generate build files for the iommap NIF.
# Mirrors erlang-rocksdb's do_cmake.sh pattern.

mkdir -p _build/cmake
cd _build/cmake

if type cmake3 > /dev/null 2>&1 ; then
    CMAKE=cmake3
else
    CMAKE=cmake
fi

${CMAKE} -DCMAKE_BUILD_TYPE=Release "$@" ../../c_src || exit 1

echo done.
