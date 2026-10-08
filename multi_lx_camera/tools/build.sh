#!/usr/bin/env bash
# Build the two multi-camera helpers.
#
# These are deliberately NOT in CMakeLists.txt. The driver is built with
# -DLX_DYNAMIC and dlopen()s libLxCameraApi.so at runtime to keep the SDK away
# from ROS 2's FastDDS; these tools link the SDK directly, which is fine only
# because they never load ROS. Keeping them out of the ament build keeps that
# distinction impossible to get wrong.
set -euo pipefail
cd "$(dirname "$0")"
for t in lx_discover lx_setip; do
  g++ -O2 -o "$t" "$t.cpp" \
      -I/opt/MRDVS/include -L/opt/MRDVS/lib -lLxCameraApi \
      -Wl,-rpath,/opt/MRDVS/lib
  echo "built $(pwd)/$t"
done
