#!/bin/bash

cat ./BootShim/AARCH64/BootShim.bin "./Build/zero3Pkg/${_TARGET_BUILD_MODE}_CLANGDWARF/FV/ZERO3_UEFI.fd" > "Mu-zero3.bin"||exit 1
