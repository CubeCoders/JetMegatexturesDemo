"""Trusted Blender exporter for the unchanged James Lambert chapel."""
from pathlib import Path
import bpy,hashlib,json,re
root=Path(__file__).resolve().parents[2]
source=root/'upstream/assets/world/test.blend'
bpy.ops.wm.open_mainfile(filepath=str(source),load_ui=False,use_scripts=False)
assert not bpy.context.preferences.filepaths.use_scripts_auto_execute
surfaces=[]
for obj in sorted(bpy.context.scene.objects,key=lambda o:o.name):
    if obj.type!='MESH' or not obj.name.startswith('@megatexture'):continue
    assert not obj.modifiers
    mesh=obj.data;mesh.calc_loop_triangles();uvs=mesh.uv_layers.active.data
    vertices=[];lookup={};triangles=[];materials=set()
    for tri in mesh.loop_triangles:
        material=mesh.materials[tri.material_index].name
        materials.add(material);face=[]
        for loop in tri.loops:
            v=obj.matrix_world@mesh.vertices[mesh.loops[loop].vertex_index].co
            uv=uvs[loop].uv
            # Jet looks along +Z: Blender(x,y,z)->Jet(x,z,y), with image V flipped once.
            key=(round(v.x*256),round(v.z*256),round(v.y*256),round(uv.x*1024),round((1-uv.y)*1024))
            if key not in lookup:lookup[key]=len(vertices);vertices.append(list(key))
            face.append(lookup[key])
        triangles.append([face[0],face[2],face[1]])
    assert len(materials)==1
    surfaces.append({'name':obj.name,'material':materials.pop(),'sort_group':int(re.search(r'sort_group (-?\d+)',obj.name)[1]),'vertices':vertices,'triangles':triangles})
result={'upstream':'8841ddf3e7d591af17287391f4b3b8728064c7bf','source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'units_per_metre':256,'basis':'Blender x,z,y; reversed winding; UV u,1-v','surfaces':surfaces}
out=root/'esp32-streaming-chapel/generated/scene.json'
out.parent.mkdir(exist_ok=True)
out.write_text(json.dumps(result,separators=(',',':'))+'\n')
print('EXPORTED',len(surfaces),'surfaces',sum(len(s['triangles']) for s in surfaces),'triangles',sum(len(s['vertices']) for s in surfaces),'vertices')
