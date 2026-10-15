"""Blender asset authoring: geometry and linear vertex colors, no image textures.

Run Blender --background --factory-startup --python scripts/make_planets.py
-- /absolute/path/to/assets/models. Profiles and editable .blend live in source/;
runtime .fmesh exports are written directly to the models directory.
"""

import bpy
import json
import math
from pathlib import Path
import struct
import sys
from mathutils import Vector, noise


def mix(a, b, weight):
    weight = max(0.0, min(1.0, weight))
    return tuple(x + (y - x) * weight for x, y in zip(a, b))


def terrain(direction, profile, octaves):
    offset = Vector((profile['seed'] * .73, profile['seed'] * 1.17, profile['seed'] * .31))
    frequency = profile['continent_frequency']
    total, amplitude, normalization = 0.0, 1.0, 0.0
    for _ in range(octaves):
        total += noise.noise(direction * frequency + offset, noise_basis='PERLIN_NEW') * amplitude
        normalization += amplitude
        frequency *= 2.05
        amplitude *= .48
    return total / normalization


def surface(direction, elevation, profile):
    name = profile['name']
    roughness, ior, emission = profile['roughness'], profile['ior'], 0.0
    latitude = abs(direction.y)
    color = mix(profile['land'], profile['rock'], max(0.0, elevation - .25) * 2.0)
    if name == 'temperate':
        if elevation < profile['sea_level']:
            depth = profile['sea_level'] - elevation
            color = mix((.025, .18, .23), profile['water'], depth * 18.0)
            roughness, ior = profile['water_roughness'], 1.333
        elif elevation < profile['sea_level'] + .018:
            color = (.38, .28, .13)
        snow = max((latitude - profile['polar_latitude']) * 24.0,
                   (elevation - .55) * 8.0)
        color = mix(color, profile['snow'], snow)
    elif name == 'desert':
        dunes = .5 + .5 * math.sin(direction.x * 95.0 + direction.z * 21.0 + elevation * 45.0)
        color = mix(color, profile['land'], dunes * .35)
    elif name == 'ice':
        cracks = abs(noise.noise(direction * 22.0, noise_basis='PERLIN_NEW'))
        color = mix(profile['rock'], profile['snow'], min(1.0, cracks * 6.0 + .45))
    elif name == 'lava':
        fissure = abs(noise.noise(direction * 12.0 + Vector((5, 9, 2)), noise_basis='PERLIN_NEW'))
        heat = max(0.0, 1.0 - fissure / .055)
        color = mix(color, profile['lava'], heat)
        emission = profile['emission'] * heat
    elif name == 'gas':
        warp = noise.noise(direction * 8.0, noise_basis='PERLIN_NEW')
        band = .5 + .5 * math.sin(direction.y * 47.0 + warp * 4.5)
        color = mix(profile['rock'], profile['land'], band)
        color = mix(color, profile['snow'], max(0.0, band - .65) * 2.0)
    return color, roughness, ior, emission


def export_mesh(obj, path, responses):
    mesh = obj.data
    mesh.calc_loop_triangles()
    with path.open('wb') as file:
        file.write(struct.pack('<8sII', b'FLMESH01', len(mesh.vertices), len(mesh.loop_triangles)))
        colors = mesh.color_attributes['Color'].data
        for vertex, response in zip(mesh.vertices, responses):
            roughness, ior, emission = response
            file.write(struct.pack('<12f', *vertex.co, ior, *vertex.normal, emission,
                                   *colors[vertex.index].color[:3], roughness))
        for triangle in mesh.loop_triangles:
            file.write(struct.pack('<4I', *triangle.vertices, 0))


def main():
    destination = Path(sys.argv[sys.argv.index('--') + 1]).resolve()
    source = destination / 'source'
    profiles = json.loads((source / 'planet_profiles.json').read_text())
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    for index, profile in enumerate(profiles['planets']):
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=profiles['subdivisions'], radius=1.0)
        obj = bpy.context.object
        obj.name = profile['name']
        mesh = obj.data
        colors = mesh.color_attributes.new(name='Color', type='FLOAT_COLOR', domain='POINT')
        rough = mesh.attributes.new(name='roughness', type='FLOAT', domain='POINT')
        emit = mesh.attributes.new(name='emission', type='FLOAT', domain='POINT')
        responses = []
        directions = [v.co.normalized() for v in mesh.vertices]
        heights = [terrain(direction, profile, profiles['terrain_octaves']) for direction in directions]
        ordered = sorted(heights)
        low, high = ordered[len(ordered)//20], ordered[len(ordered)*19//20]
        midpoint, span = (low+high)*.5, max(high-low, .000001)
        for vertex, direction, height_value in zip(mesh.vertices, directions, heights):
            elevation = max(-1.0, min(1.0, (height_value - midpoint) / span))
            height = max(elevation, profile['sea_level']) if profile['name'] == 'temperate' else elevation
            vertex.co = direction * (1.0 + height * profile['relief'])
            color, roughness, ior, emission = surface(direction, elevation, profile)
            colors.data[vertex.index].color = (*color, 1.0)
            rough.data[vertex.index].value = roughness
            emit.data[vertex.index].value = emission
            responses.append((roughness, ior, emission))
        for polygon in mesh.polygons:
            polygon.use_smooth = True
        mesh.update()
        export_mesh(obj, destination / (profile['name'] + '.fmesh'), responses)
        # A simple Blender preview material reads the same attributes as the game.
        material = bpy.data.materials.new(profile['name'])
        material.use_nodes = True
        nodes = material.node_tree.nodes
        bsdf = nodes.get('Principled BSDF')
        color_node = nodes.new('ShaderNodeAttribute')
        color_node.attribute_name = 'Color'
        rough_node = nodes.new('ShaderNodeAttribute')
        rough_node.attribute_name = 'roughness'
        emission_node = nodes.new('ShaderNodeAttribute')
        emission_node.attribute_name = 'emission'
        links = material.node_tree.links
        links.new(color_node.outputs['Color'], bsdf.inputs['Base Color'])
        links.new(color_node.outputs['Color'], bsdf.inputs['Emission Color'])
        links.new(rough_node.outputs['Fac'], bsdf.inputs['Roughness'])
        links.new(emission_node.outputs['Fac'], bsdf.inputs['Emission Strength'])
        bsdf.inputs['IOR'].default_value = profile['ior']
        obj.data.materials.append(material)
        obj.location.x = index * 3.0
        print('Exported', profile['name'], len(mesh.vertices), 'vertices;', len(mesh.polygons), 'triangles')
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(source / 'planets.blend'))


main()
