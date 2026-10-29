#!/usr/bin/env bash
set -euo pipefail
source_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
workspace_dir="$(dirname -- "$source_dir")"
sdk_dir="${DLSS_SDK_ROOT:-$workspace_dir/dlss-sdk}"
optix_dir="${OPTIX_ROOT_DIR:-/opt/optix}"
sccache_bin="$(command -v sccache)"
make -C "$source_dir" full ninja NPROCS="${NPROCS:-24}" \
  BUILD_CMAKE_ARGS="-DWITH_COMPILER_CCACHE=OFF -DCMAKE_C_COMPILER_LAUNCHER=$sccache_bin -DCMAKE_CXX_COMPILER_LAUNCHER=$sccache_bin -DWITH_DLSS=ON -DDLSS_SDK_ROOT=$sdk_dir -DDLSS_INCLUDE_DIR=$sdk_dir/include -DOPTIX_ROOT_DIR=$optix_dir -DOPTIX_INCLUDE_DIR=$optix_dir/include -DCYCLES_RUNTIME_OPTIX_ROOT_DIR=$optix_dir"
runtime="$sdk_dir/lib/Linux_x86_64/rel/libnvidia-ngx-dlssd.so.310.9.16"
cp -p "$runtime" "$workspace_dir/build_linux_full/bin/"
ln -sfn "$(basename -- "$runtime")" "$workspace_dir/build_linux_full/bin/libnvidia-ngx-dlssd.so"
