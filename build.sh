#!/bin/bash
BUILD_DATE=$(date "+%Y-%m-%d %H:%M:%S")
echo "Building vshbgmX with date: $BUILD_DATE"
make USER_DEFS="-DBUILD_DATE='\"$BUILD_DATE\"'"
