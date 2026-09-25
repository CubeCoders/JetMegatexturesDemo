from pathlib import Path
import hashlib,json,math,struct
import numpy as np
from PIL import Image

root=Path(__file__).resolve().parents[2]
project=root/'esp32-streaming-chapel'
(project/'generated').mkdir(exist_ok=True)
(project/'build-artifacts').mkdir(exist_ok=True)
scene=json.loads((project/'generated/scene.json').read_text())
names=sorted({s['material'] for s in scene['surfaces']})
pack=bytearray();textures=[];page_offsets=[]
def align():
    while len(pack)%16:pack.append(0)
def as565(rgb):
    c=np.asarray(rgb,dtype=np.uint16)
    return ((c[:,:,0]>>3)<<11)|((c[:,:,1]>>2)<<5)|(c[:,:,2]>>3)
def expand(p):
    return np.stack([((p>>11)&31)*255//31,((p>>5)&63)*255//63,(p&31)*255//31],axis=-1).astype(np.uint8)
for name in names:
    source=root/'upstream/assets/materials/megatextures'/f'{name}.png'
    rgba=Image.open(source);alpha=np.asarray(rgba.convert('RGBA'))[:,:,3]
    original=rgba.convert('RGB');factor=min(1,512/max(original.size))
    size=tuple(int(n*factor) for n in original.size)
    base=original.resize(size,Image.Resampling.LANCZOS)
    quantized=base.quantize(colors=256,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE)
    palette_rgb=np.array(quantized.getpalette(),dtype=np.uint16).reshape(-1,3)
    palette=((palette_rgb[:,0]>>3)<<11)|((palette_rgb[:,1]>>2)<<5)|(palette_rgb[:,2]>>3)
    # Use the actual displayable palette when finding the nearest index at every mip.
    palette_image=Image.new('P',(1,1));palette_image.putpalette(expand(palette).reshape(-1).tolist())
    align();palette_offset=len(pack);pack.extend(palette.astype('<u2').tobytes())
    entry={'name':name,'source':str(source.relative_to(root)),'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'original_size':original.size,'size':size,'palette_offset':palette_offset,'levels':[],'nonopaque_alpha_pixels':int(np.count_nonzero(alpha!=255))}
    w,h=size
    while True:
        image=base if (w,h)==size else base.resize((w,h),Image.Resampling.BOX)
        indices=np.asarray(image.quantize(palette=palette_image,dither=Image.Dither.NONE),dtype=np.uint8)
        if (w,h)==size:
            source565=as565(base);decoded=palette[indices]
            error=expand(source565).astype(np.float32)-expand(decoded).astype(np.float32)
            entry.update({'unique_rgb565_colours':int(len(np.unique(source565))),'palette_colours':int(len(np.unique(palette))),'mean_rgb_error':float(np.abs(error).mean()),'rgb_psnr_db':float(10*math.log10(255**2/max(float((error**2).mean()),1e-10)))})
            if name in ['window_main','floor_main','wall_left_0']:
                Image.fromarray(expand(decoded)).save(project/'build-artifacts'/f'{name}-indexed.png')
                Image.fromarray(expand(source565)).save(project/'build-artifacts'/f'{name}-rgb565.png')
        align();offset=len(pack)
        coarse=max(w,h)<=16
        first_page=-1 if coarse else len(page_offsets)
        if coarse:pack.extend(indices.tobytes())
        else:
            padded=np.pad(indices,((0,(-h)%32),(0,(-w)%32)),mode='edge')
            for y in range(0,h,32):
                for x in range(0,w,32):
                    page_offsets.append(len(pack));pack.extend(padded[y:y+32,x:x+32].tobytes())
        entry['levels'].append({'width':w,'height':h,'offset':offset,'first_page':first_page,'pages_x':(w+31)//32,'pages_y':(h+31)//32})
        if coarse:break
        w=max(1,w//2);h=max(1,h//2)
    textures.append(entry)
    print(name,size,'colours',entry['unique_rgb565_colours'],'PSNR',round(entry['rgb_psnr_db'],2),flush=True)
assert len(pack)<12*1024*1024
(project/'main/assets.bin').write_bytes(pack)
report={'asset_bytes':len(pack),'flash_budget':16*1024*1024,'asset_ceiling':12*1024*1024,'tile_bytes':1024,'page_count':len(page_offsets),'palette_bytes':len(names)*512,'coarse_bytes':sum(t['levels'][-1]['width']*t['levels'][-1]['height'] for t in textures),'sha256':hashlib.sha256(pack).hexdigest(),'textures':textures}
(project/'generated/assets.json').write_text(json.dumps(report,indent=2)+'\n')
lines=['#pragma once','#include <cstdint>','namespace ChapelAssets {','struct Level { uint16_t width,height; uint32_t offset; int32_t firstPage; uint16_t pagesX,pagesY; };','struct Map { uint32_t paletteOffset; uint16_t firstLevel,levelCount; };','struct Vertex { int16_t x,y,z,u,v; };','struct Face { uint16_t a,b,c; };','struct Surface { uint16_t firstVertex,vertices,firstFace,faces; uint8_t material; int8_t sortGroup; };']
def emit(kind,name,rows):lines.extend([f'inline const {kind} {name}[]={{',*['{'+','.join(map(str,row))+'},' for row in rows],'};'])
level_rows=[];map_rows=[]
for t in textures:
    map_rows.append([t['palette_offset'],len(level_rows),len(t['levels'])])
    level_rows += [[l['width'],l['height'],l['offset'],l['first_page'],l['pages_x'],l['pages_y']] for l in t['levels']]
emit('Level','levels',level_rows);emit('Map','maps',map_rows)
verts=[];faces=[];surfaces=[]
for s in scene['surfaces']:
    surfaces.append([len(verts),len(s['vertices']),len(faces),len(s['triangles']),names.index(s['material']),s['sort_group']])
    verts+=s['vertices'];faces+=s['triangles']
emit('Vertex','vertices',verts);emit('Face','faces',faces);emit('Surface','surfaces',surfaces)
lines+=['inline const uint32_t pageOffsets[]={'+','.join(map(str,page_offsets))+'};',f'inline constexpr unsigned pageCount={len(page_offsets)},mapCount={len(names)},surfaceCount={len(surfaces)},assetBytes={len(pack)};','}']
(project/'main/ChapelAssets.hpp').write_text('\n'.join(lines)+'\n')
print('PACKED',len(pack),'bytes;',len(page_offsets),'pages;',report['coarse_bytes'],'coarse bytes')
