# Local Linux DLSS build

This branch starts at Blender revision `42f90a2a3a0`, matching the prior working build.
It uses the full Release preset, Ninja, 24 jobs, and runtime CUDA/OptiX kernel compilation.
No changes to Blender rendering code are required.

Dependencies: Blender Linux x64 libraries at `23a9f7e6e449d3c4bf8806baec8b12e13045a9ad`,
OptiX in `/opt/optix`, CUDA in `/opt/cuda`, and the NVIDIA DLSS SDK in `../dlss-sdk`
(relative to the source checkout). The SDK revision is
`9981c6350dc58c0ce21f024fd65e31e851c31689` from https://github.com/NVIDIA/DLSS.
The runtime is `lib/Linux_x86_64/rel/libnvidia-ngx-dlssd.so.310.9.16`.
Use `DLSS_SDK_ROOT`, `OPTIX_ROOT_DIR`, and `NPROCS` to override defaults.

From the source directory:

```sh
./build_files/build_dlss_linux.sh
../build_linux_full/bin/blender --background --factory-startup --python-exit-code 1 --python build_files/verify_dlss_linux.py
```

The script installs the NGX runtime and its unversioned symlink beside Blender.
Verification checks compiled DLSS support, GPU capability, and a small OptiX render;
it does not exercise interactive viewport denoising.

For viewport DLSS, enable the OptiX GPU in Preferences > System, then select
Cycles GPU Compute and DLSS under Render Properties > Sampling > Viewport denoising.

The GitHub mirror may lack LFS objects. Use the official asset endpoint locally:

```sh
git config lfs.url https://projects.blender.org/blender/blender.git/info/lfs
git lfs pull
```
