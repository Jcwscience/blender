# Local Blender feature branches

Use `touch-support` for the combined Blender build. It includes the DLSS setup
and the touchscreen work. Use `dlss-support` to work on DLSS without touch changes.

| Branch | Contents | Where new work belongs |
| --- | --- | --- |
| `dlss-support` | Official Blender plus DLSS build configuration and sccache | DLSS fixes and build dependencies |
| `touch-support` | Everything in `dlss-support`, plus touchscreen support | Touch gestures and node editing |
| `main` | Existing GitHub mirror baseline | Upstream reference |

The old `dlss` branch contains the first combined build. The existing
`node_touch_support` branch contains an earlier, separate touch implementation.
They are preserved for reference; new feature work belongs on the two support
branches above. No published history was rewritten to establish this layout.

## Daily work

```sh
git switch touch-support
./build_files/build_dlss_linux.sh
```

The executable is `../build_linux_full/bin/blender`. Both support branches contain
this build script, the DLSS verification script, and the sccache configuration
arguments. Switching branches shares that build directory; rebuild before using
its executable. The local NVIDIA SDK and dependencies remain outside the feature
commits.

## Adding features

For a touch feature, start a short-lived branch from `touch-support`:

```sh
git switch touch-support
git switch -c touch/descriptive-feature-name
# Implement, test, and commit the feature.
git switch touch-support
git merge --no-ff touch/descriptive-feature-name
git push origin touch-support
```

Use a `dlss/descriptive-feature-name` branch starting from `dlss-support` for DLSS
work. After merging it into `dlss-support`, merge `dlss-support` into
`touch-support` so the combined build receives the update. Keep touch commits out
of `dlss-support`; merge in the other direction only.

Both branches currently use official Blender revision `42f90a2a3a0` as their
source baseline. Updating upstream is a separate, deliberate operation:

```sh
git fetch upstream
git switch dlss-support
git merge upstream/main
# Resolve changes, build, and verify DLSS before publishing.
git push origin dlss-support
git switch touch-support
git merge dlss-support
# Resolve changes, build, and verify touch and DLSS before publishing.
git push origin touch-support
```

The official repository is `upstream`; your GitHub fork is `origin`. Use merges
for these published branches. Avoid rebasing or force-pushing shared history.

## Versions and tested snapshots

Keep Blender's upstream version number. Individual Git commits identify feature
revisions. Annotated tags `dlss-support-YYYY-MM-DD` and
`touch-support-YYYY-MM-DD` identify tested snapshots; use a suffix such as `-2`
for another snapshot on the same day. Never move an existing published tag.
A tag records validation status; it is not a new upstream Blender release.

Before tagging a combined snapshot, run the touch recognizer tests, build, and
run `build_files/verify_dlss_linux.py` as described in the feature documentation.
Physical touchscreen acceptance is separate from synthetic contact testing.
