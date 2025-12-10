#!/bin/bash
BUILD_DATE=$(date "+%Y-%m-%d %H:%M:%S")
echo "Building vshbgm with date: $BUILD_DATE"
make clean
make USER_DEFS="-DBUILD_DATE='\"$BUILD_DATE\"'"
