"""Run inside the open Blender Python Console to export a collection as GLB.

Linked CAD parts can have parents outside the collection. Exporting only the
selected meshes loses those parent transforms. Temporary mesh copies bake
their world matrices into vertices and export with identity object transforms.
The source objects and .blend stay intact.
"""

import bpy


def export_collection(filepath, collection_name="Crabit"):
    collection = bpy.data.collections[collection_name]
    context = bpy.context
    selected = tuple(context.selected_objects)
    active = context.view_layer.objects.active
    copies = []
    meshes = []

    try:
        for source in collection.all_objects:
            if source.type != "MESH":
                continue
            mesh = source.data.copy()
            mesh.transform(source.matrix_world)
            copy = bpy.data.objects.new(f"{source.name}_export", mesh)
            context.scene.collection.objects.link(copy)
            copies.append(copy)
            meshes.append(mesh)

        for obj in selected:
            obj.select_set(False)
        for copy in copies:
            copy.select_set(True)

        context.view_layer.update()

        result = bpy.ops.export_scene.gltf(
            filepath=filepath, export_format="GLB", use_selection=True
        )
        print("CRABIT_EXPORT", len(copies), filepath, result)
        return result
    finally:
        for copy in copies:
            bpy.data.objects.remove(copy, do_unlink=True)
        for mesh in meshes:
            bpy.data.meshes.remove(mesh)
        for obj in selected:
            obj.select_set(True)
        context.view_layer.objects.active = active
