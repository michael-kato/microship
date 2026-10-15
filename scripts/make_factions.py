"""Author twelve physical, vertex-colored ships in Blender, without image maps.

Blender --background --factory-startup --python scripts/make_factions.py
-- /absolute/path/to/assets/models
Profiles and editable .blend live in source/; runtime .fmesh exports are written
directly to the models directory.
"""

import bpy
import json
import math
from pathlib import Path
import random
import struct
import sys
from mathutils import Matrix, Vector


PARTS = []
PROFILE = None
MATERIAL = None


def finish(obj, color, smooth=True, metal=False, emission=0.0, roughness=None):
    """Bake a piece's transform and response into ordinary mesh attributes."""
    # Compose explicitly: Blender may not yet have evaluated matrix_world after
    # an operator's scale/rotation was edited in this background script.
    transform = Matrix.LocRotScale(obj.location, obj.rotation_euler.to_quaternion(), obj.scale)
    obj.data.transform(transform)
    obj.matrix_world = Matrix.Identity(4)
    mesh = obj.data
    colors = mesh.color_attributes.new(name='Color', type='FLOAT_COLOR', domain='POINT')
    rough = mesh.attributes.new(name='roughness', type='FLOAT', domain='POINT')
    emit = mesh.attributes.new(name='emission', type='FLOAT', domain='POINT')
    ior = mesh.attributes.new(name='ior', type='FLOAT', domain='POINT')
    metallic = mesh.attributes.new(name='metallic', type='FLOAT', domain='FACE')
    for vertex in mesh.vertices:
        colors.data[vertex.index].color = (*color, 1.0)
        rough.data[vertex.index].value = PROFILE['roughness'] if roughness is None else roughness
        emit.data[vertex.index].value = emission
        ior.data[vertex.index].value = 1.5
    for polygon in mesh.polygons:
        polygon.use_smooth = smooth
        metallic.data[polygon.index].value = 1.0 if metal else 0.0
    mesh.materials.append(MATERIAL)
    mesh.update()
    PARTS.append(obj)
    return obj


def ellipsoid(center, scale, color, metal=False, emission=0.0, roughness=None):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=16, radius=1, location=center)
    obj = bpy.context.object
    obj.scale = scale
    return finish(obj, color, metal=metal, emission=emission, roughness=roughness)


def block(center, scale, color, yaw=0, metal=False):
    bpy.ops.mesh.primitive_cube_add(size=2, location=center)
    obj = bpy.context.object
    obj.scale = scale
    obj.rotation_euler.y = yaw
    return finish(obj, color, smooth=False, metal=metal)


def rod(a, b, radius, color, metal=False, emission=0):
    a, b = Vector(a), Vector(b)
    delta = b - a
    bpy.ops.mesh.primitive_cone_add(vertices=12, radius1=radius, radius2=radius * .55,
                                    depth=delta.length, location=(a + b) / 2)
    obj = bpy.context.object
    obj.rotation_euler = delta.to_track_quat('Z', 'Y').to_euler()
    return finish(obj, color, metal=metal, emission=emission)


def crystal(center, scale, color, lean=0):
    vertices = [(0, 0, -1), (lean, 0, 1)]
    for z, radius in ((-.45, .65), (.28, 1.0)):
        vertices.extend((radius * math.cos(i * math.tau / 6),
                         radius * math.sin(i * math.tau / 6), z) for i in range(6))
    faces = []
    for i in range(6):
        j = (i + 1) % 6
        faces.extend(((0, 2 + j, 2 + i), (2 + i, 2 + j, 8 + j, 8 + i), (1, 8 + i, 8 + j)))
    mesh = bpy.data.meshes.new('CrystalShard')
    mesh.from_pydata(vertices, [], faces)
    obj = bpy.data.objects.new('CrystalShard', mesh)
    bpy.context.collection.objects.link(obj)
    obj.location, obj.scale = center, scale
    return finish(obj, color, smooth=False)


def petal(angle, length, width, color, y=.04):
    """A closed, cupped petal with physical thickness and a swept tip."""
    vertices, faces = [], []
    rings, sides = 16, 8
    for ring in range(rings + 1):
        t = ring / rings
        radial = .12 + length * t
        half_width = width * (.04 + math.sin(math.pi * t) ** .8)
        height = y + .18 * math.sin(math.pi * t) - .12 * t
        for side in range(sides):
            phi = side * math.tau / sides
            across = half_width * math.cos(phi)
            vertices.append((radial * math.sin(angle) + across * math.cos(angle),
                             height + .025 * math.sin(phi),
                             radial * math.cos(angle) - across * math.sin(angle)))
    for ring in range(rings):
        for side in range(sides):
            j = (side + 1) % sides
            faces.append((ring*sides+side, ring*sides+j, (ring+1)*sides+j, (ring+1)*sides+side))
    faces.extend((tuple(reversed(range(sides))), tuple(rings*sides+i for i in range(sides))))
    mesh = bpy.data.meshes.new('Petal')
    mesh.from_pydata(vertices, [], faces)
    obj = bpy.data.objects.new('Petal', mesh)
    bpy.context.collection.objects.link(obj)
    obj = finish(obj, color)
    # Linear vertex pigment changes along the petal; no baked image map.
    for vertex in mesh.vertices:
        t = (vertex.index // sides) / rings
        mesh.color_attributes['Color'].data[vertex.index].color = (
            *(c * (.55 + .45*t) + a * .25*(1-t) for c, a in zip(color, PROFILE['accent'])), 1)
    return obj


def stone(center, scale, color, rng):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2, radius=1, location=center)
    obj = bpy.context.object
    for vertex in obj.data.vertices:
        vertex.co *= rng.uniform(.78, 1.18)
    obj.scale = scale
    return finish(obj, color, smooth=False)


def flower(large):
    hull, accent, green = (PROFILE[k] for k in ('hull', 'accent', 'secondary'))
    ellipsoid((0, .10, 0), (.22, .16, .42 if not large else .28), accent)
    if large:
        for i in range(7):
            petal(i * math.tau / 7, .85, .26, hull)
            petal((i + .5) * math.tau / 7, .48, .19, accent, .15)
        for i in range(9):
            angle = i * math.tau / 9
            ellipsoid((.13*math.sin(angle), .25, .13*math.cos(angle)), (.035, .075, .035), accent)
        for i in range(3):
            petal(i * math.tau / 3, .70, .11, green, -.11)
    else:
        for side in (-1, 1):
            petal(side * 1.05, .57, .18, hull)
            petal(side * 2.15, .38, .10, green, -.05)
        rod((0, -.02, -.28), (0, -.04, -.82), .035, green)
        ellipsoid((0, .20, .13), (.06, .04, .13), hull)


def crystalline(large):
    hull, accent, secondary = (PROFILE[k] for k in ('hull', 'accent', 'secondary'))
    crystal((0, 0, .06), (.20, .16, .85), hull)
    ellipsoid((0, .03, .02), (.10, .10, .18), accent, emission=3)
    if large:
        for side in (-1, 1):
            for i in range(3):
                x, z = side*(.30 + i*.17), .20 - i*.30
                rod((0, -.03, -.16), (x, -.03, z), .045, secondary)
                crystal((x, .02, z), (.10 + i*.025, .14, .44), accent if i == 1 else secondary, side*.25)
        crystal((0, .20, -.10), (.12, .22, .38), accent)
    else:
        for side in (-1, 1):
            crystal((side*.24, -.02, -.26), (.075, .09, .36), secondary, side*.2)


def liquid_metal(large):
    hull, accent, secondary = (PROFILE[k] for k in ('hull', 'accent', 'secondary'))
    ellipsoid((0, 0, .10), (.30 if large else .22, .24 if large else .19, .68), hull, metal=True)
    if large:
        for side in (-1, 1):
            wing = ellipsoid((side*.38, -.02, -.05), (.42, .12, .27), hull, metal=True)
            # Smooth lobes sweep backward into a manta-like liquid hull.
            for vertex in wing.data.vertices:
                vertex.co.z -= abs(vertex.co.x) * .35
            wing.data.update()
            ellipsoid((side*.68, .025, -.38), (.13, .08, .22), secondary, metal=True)
        ellipsoid((0, 0, -.65), (.09, .07, .24), hull, metal=True)
    else:
        for side in (-1, 1):
            ellipsoid((side*.32, 0, -.35), (.075, .065, .17), hull, metal=True)
    ellipsoid((0, .12, .14), (.065, .025, .18), accent, emission=.65)


def insectoid(large):
    hull, amber, dark = (PROFILE[k] for k in ('hull', 'accent', 'secondary'))
    ellipsoid((0, 0, .42), (.20, .15, .22), dark)
    ellipsoid((0, .04, .05), (.28 if large else .20, .17, .32), hull)
    for i in range(3):
        ellipsoid((0, -.015, -.23 - i*.16), (.22-i*.035, .13-i*.018, .16), hull)
    for side in (-1, 1):
        ellipsoid((side*.12, .095, .56), (.055, .035, .035), amber, emission=3)
        rod((side*.12, .06, .56), (side*.28, .15, .80), .015, amber)
        rod((side*.15, -.05, .51), (side*.10, -.08, .76), .025, dark)
        for i in range(3):
            z = .20 - i*.24
            knee = (side*(.48 if large else .33), -.08, z-.12)
            rod((side*.15, -.02, z), knee, .025 if large else .018, dark)
            rod(knee, (side*(.65 if large else .48), -.17, z+.03), .020 if large else .014, hull)
        if large:
            ellipsoid((side*.14, .15, -.14), (.15, .05, .39), hull, roughness=.22)
        else:
            petal(side*1.35, .48, .08, amber, .10)


def rocky(large):
    hull, ember, secondary = (PROFILE[k] for k in ('hull', 'accent', 'secondary'))
    rng = random.Random(PROFILE['seed'] + int(large))
    ellipsoid((0, 0, 0), (.21, .16, .44), ember, emission=1.7)
    for i in range(6 if large else 3):
        z = .53 - i*(.20 if large else .39)
        stone((rng.uniform(-.12, .12), .015, z), (.28, .22, .23), hull, rng)
    for side in (-1, 1):
        stone((side*(.38 if large else .26), -.02, -.13), (.21, .15, .45 if large else .24), secondary, rng)
        if large:
            stone((side*.62, -.04, -.43), (.20, .12, .30), hull, rng)
            rod((side*.08, .18, .1), (side*.35, .15, -.38), .015, ember, emission=2)


def robotic(large):
    hull, blue, dark = (PROFILE[k] for k in ('hull', 'accent', 'secondary'))
    block((0, 0, 0), (.22, .12, .47 if large else .30), hull, metal=True)
    block((0, .13, .05), (.13, .045, .18), dark)
    ellipsoid((0, .13, .28), (.09, .025, .035), blue, emission=4)
    for side in (-1, 1):
        if large:
            for z in (-.30, .20):
                block((side*.35, -.02, z), (.17, .055, .08), dark, metal=True)
                block((side*.56, 0, z), (.08, .13, .23), hull, yaw=side*.12, metal=True)
                block((side*.56, .14, z), (.05, .014, .17), dark)
                ellipsoid((side*.56, 0, z-.24), (.052, .075, .018), blue, emission=5)
        else:
            block((side*.33, 0, -.03), (.15, .045, .055), dark, metal=True)
            block((side*.47, 0, -.08), (.055, .075, .21), hull, metal=True)
            ellipsoid((side*.47, 0, -.295), (.035, .045, .018), blue, emission=5)
        block((side*.09, .04, .56 if large else .37), (.025, .035, .13), dark, metal=True)


def export_mesh(obj, path):
    mesh = obj.data
    mesh.calc_loop_triangles()
    colors = mesh.color_attributes['Color'].data
    attributes = [mesh.attributes[name].data for name in ('ior', 'emission', 'roughness')]
    metal = mesh.attributes['metallic'].data
    # Split corners only where geometry normals differ. Crystals and armor retain
    # sharp facets; petals and droplets retain their smooth geometric normals.
    records, indices, lookup = [], [], {}
    for triangle in mesh.loop_triangles:
        corners = []
        for vertex_index, loop_index in zip(triangle.vertices, triangle.loops):
            normal = mesh.corner_normals[loop_index].vector
            key = (vertex_index, *normal)
            if key not in lookup:
                vertex = mesh.vertices[vertex_index]
                lookup[key] = len(records)
                records.append((*vertex.co, attributes[0][vertex_index].value,
                                *normal, attributes[1][vertex_index].value,
                                *colors[vertex_index].color[:3], attributes[2][vertex_index].value))
            corners.append(lookup[key])
        indices.append((*corners, 2 if metal[triangle.polygon_index].value > .5 else 0))
    with path.open('wb') as file:
        file.write(struct.pack('<8sII', b'FLMESH01', len(records), len(indices)))
        for record in records:
            file.write(struct.pack('<12f', *record))
        for triangle in indices:
            file.write(struct.pack('<4I', *triangle))
    print(f'Exported {path.name}: {len(records)} vertices, {len(indices)} triangles', flush=True)


def make_preview_material():
    material = bpy.data.materials.new('Vertex response — no image maps')
    material.use_nodes = True
    bsdf = material.node_tree.nodes.get('Principled BSDF')
    for attribute, socket, output in (('Color', 'Base Color', 'Color'),
                                      ('Color', 'Emission Color', 'Color'),
                                      ('roughness', 'Roughness', 'Fac'),
                                      ('emission', 'Emission Strength', 'Fac'),
                                      ('ior', 'IOR', 'Fac'), ('metallic', 'Metallic', 'Fac')):
        node = material.node_tree.nodes.new('ShaderNodeAttribute')
        node.attribute_name = attribute
        material.node_tree.links.new(node.outputs[output], bsdf.inputs[socket])
    return material


def main():
    global PROFILE, PARTS, MATERIAL
    destination = Path(sys.argv[sys.argv.index('--') + 1]).resolve()
    source = destination / 'source'
    profiles = json.loads((source / 'faction_profiles.json').read_text())
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    MATERIAL = make_preview_material()
    builders = {'flowery': flower, 'crystalline': crystalline, 'liquid_metal': liquid_metal,
                'insectoid': insectoid, 'rocky': rocky, 'robotic': robotic}
    for column, profile in enumerate(profiles['factions']):
        PROFILE = profile
        for large in (True, False):
            PARTS = []
            builders[profile['name']](large)
            bpy.ops.object.select_all(action='DESELECT')
            for obj in PARTS:
                obj.select_set(True)
            bpy.context.view_layer.objects.active = PARTS[0]
            bpy.ops.object.join()
            obj = bpy.context.object
            obj.name = profile['name'] + ('_large' if large else '_small')
            bounding_radius = max(vertex.co.length for vertex in obj.data.vertices)
            for vertex in obj.data.vertices:
                vertex.co /= bounding_radius
            obj.data.update()
            obj['faction'] = profile['name']
            obj['class'] = 'large' if large else 'small'
            obj['axes'] = profiles['axis_convention']
            export_mesh(obj, destination / (obj.name + '.fmesh'))
            # Authoring layout only; in-game placement belongs in .world data.
            obj.location = (column*3.0, 0, 0 if large else 3)
    bpy.context.scene.world.color = (.01, .01, .015)
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(source / 'factions.blend'))
    print('Saved editable source/factions.blend', flush=True)


main()
