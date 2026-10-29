import bpy
import _cycles

assert _cycles.with_dlss, 'DLSS was not compiled in'
print('DLSS_COMPILED:', _cycles.with_dlss, flush=True)
prefs = bpy.context.preferences.addons['cycles'].preferences
prefs.compute_device_type = 'OPTIX'
devices = prefs.get_device_list('OPTIX')
rtx_devices = [d for d in devices if d.type == 'OPTIX']
for d in devices:
    print('DEVICE:', d.name, d.type, 'DLSS:', d.has_dlss,
          'DRIVER_OK:', d.meets_driver_requirement, flush=True)
assert rtx_devices, 'No OptiX GPU detected'
assert any(d.has_dlss for d in rtx_devices), 'NGX reports DLSS unavailable on this GPU'
for d in prefs.devices:
    d.use = d.type == 'OPTIX'
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.device = 'GPU'
scene.cycles.samples = 4
scene.render.resolution_x = 64
scene.render.resolution_y = 64
scene.render.resolution_percentage = 100
bpy.ops.render.render()
print('OPTIX_RENDER_OK', flush=True)
print('FULL_DLSS_VERIFICATION_OK', flush=True)
