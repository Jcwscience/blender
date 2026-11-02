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

# Bevel and Ambient Occlusion load a second OptiX module into the shading
# pipeline. A plain startup-cube render does not exercise this linker path.
material = bpy.data.materials.new('OptiX shader pipeline verification')
material.use_nodes = True
cube = bpy.data.objects['Cube']
cube.data.materials.clear()
cube.data.materials.append(material)
nodes = material.node_tree.nodes
links = material.node_tree.links
bsdf = nodes.get('Principled BSDF')
bevel = nodes.new('ShaderNodeBevel')
bevel.inputs['Radius'].default_value = 0.05
ao = nodes.new('ShaderNodeAmbientOcclusion')


def render_shader_case(name, use_bevel=False, use_ao=False):
    for socket in (bsdf.inputs['Normal'], bsdf.inputs['Base Color']):
        for link in list(socket.links):
            links.remove(link)
    if use_bevel:
        links.new(bevel.outputs['Normal'], bsdf.inputs['Normal'])
    if use_ao:
        links.new(ao.outputs['Color'], bsdf.inputs['Base Color'])
    print('OPTIX_SHADER_TEST:', name, flush=True)
    bpy.ops.render.render()
    print('OPTIX_SHADER_RENDER_OK:', name, flush=True)


render_shader_case('BEVEL', use_bevel=True)
render_shader_case('AMBIENT_OCCLUSION', use_ao=True)
render_shader_case('BEVEL_AND_AMBIENT_OCCLUSION', use_bevel=True, use_ao=True)

# Shadow caustics (MNEE) use another optional module in the same pipeline.
cube.cycles.is_caustics_caster = True
bsdf.inputs['Transmission Weight'].default_value = 1.0
bpy.ops.mesh.primitive_plane_add(size=10, location=(0, 0, -1.1))
bpy.context.object.cycles.is_caustics_receiver = True
bpy.data.objects['Light'].data.cycles.is_caustics_light = True
render_shader_case('SHADOW_CAUSTICS')
render_shader_case('SHADOW_CAUSTICS_AND_SHADER_NODES', use_bevel=True, use_ao=True)
print('FULL_DLSS_VERIFICATION_OK', flush=True)
