# SPDX-FileCopyrightText: 2026 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""Run with blender --background --factory-startup --python this_file.

An optional TOUCH_HOLD_LEGACY_CONFIG directory can contain preferences saved by a
build without touch_hold_time, with mouse_double_click_time set to 427.
All preference writes use temporary directories, never the user's configuration.
"""

import os
import subprocess
import tempfile
from pathlib import Path

import bpy


def run_child(config_dir, code, *, factory=False):
    env = os.environ.copy()
    env['BLENDER_USER_CONFIG'] = str(config_dir)
    command = [bpy.app.binary_path, '--background', '--python-exit-code', '1']
    if factory:
        command.append('--factory-startup')
    command.extend(['--python-expr', code])
    result = subprocess.run(command, env=env, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def main():
    inputs = bpy.context.preferences.inputs
    prop = inputs.bl_rna.properties['touch_hold_time']
    assert prop.default == 500
    assert prop.hard_min == 200 and prop.hard_max == 5000
    assert inputs.touch_hold_time == 500
    bpy.context.preferences.is_dirty = False
    inputs.touch_hold_time = 1250
    assert bpy.context.preferences.is_dirty
    inputs.touch_hold_time = 1
    assert inputs.touch_hold_time == 200
    inputs.touch_hold_time = 9999
    assert inputs.touch_hold_time == 5000
    inputs.touch_hold_time = 500

    from bl_ui import space_userpref
    assert space_userpref.USERPREF_PT_input_touchscreen in space_userpref.classes

    with tempfile.TemporaryDirectory(prefix='blender-touch-hold-prefs-') as config_dir:
        run_child(config_dir, """
import bpy
bpy.context.preferences.inputs.touch_hold_time = 1250
bpy.context.preferences.inputs.mouse_double_click_time = 432
bpy.ops.wm.save_userpref()
""", factory=True)
        assert (Path(config_dir) / 'userpref.blend').is_file()
        run_child(config_dir, """
import bpy
assert bpy.context.preferences.inputs.touch_hold_time == 1250
assert bpy.context.preferences.inputs.mouse_double_click_time == 432
""")

    legacy = os.environ.get('TOUCH_HOLD_LEGACY_CONFIG')
    if legacy:
        assert (Path(legacy) / 'userpref.blend').is_file()
        run_child(legacy, """
import bpy
assert bpy.context.preferences.inputs.touch_hold_time == 500
assert bpy.context.preferences.inputs.mouse_double_click_time == 427
""")
        print('TOUCH_HOLD_LEGACY_PREFERENCES_OK', flush=True)
    print('TOUCH_HOLD_PREFERENCES_OK', flush=True)


if __name__ == '__main__':
    main()
