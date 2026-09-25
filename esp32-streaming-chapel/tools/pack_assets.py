from pathlib import Path
import argparse,hashlib,json,math,os
os.environ.setdefault("OPENBLAS_NUM_THREADS","1")
import numpy as np
from PIL import Image, __version__ as pillow_version
from perceptual_palette import choose_palette, expand, metrics

root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description='Pack the pinned chapel artwork into tiled indexed RGB565 textures.')
parser.add_argument('--upstream',type=Path,default=root/'upstream',help='Local checkout of the pinned n64brew2023 source')
args=parser.parse_args()
project=root/'esp32-streaming-chapel'
(project/'generated').mkdir(exist_ok=True)
(project/'build-artifacts').mkdir(exist_ok=True)
scene=json.loads((project/'generated/scene.json').read_text(encoding='utf-8'))
names=sorted({s['material'] for s in scene['surfaces']})
pack=bytearray();textures=[];page_offsets=[]
def align():
    while len(pack)%16:pack.append(0)
def as565(rgb):
    c=np.asarray(rgb,dtype=np.uint16)
    return ((c[:,:,0]>>3)<<11)|((c[:,:,1]>>2)<<5)|(c[:,:,2]>>3)
for name in names:
    source=args.upstream/'assets/materials/megatextures'/f'{name}.png'
    rgba=Image.open(source);alpha=np.asarray(rgba.convert('RGBA'))[:,:,3]
    original=rgba.convert('RGB');factor=min(1,512/max(original.size))
    size=tuple(int(n*factor) for n in original.size)
    base=original.resize(size,Image.Resampling.LANCZOS)
    print('Optimizing',name,flush=True)
    palette=choose_palette(base)
    # Use the actual displayable palette when finding the nearest index at every mip.
    palette_image=Image.new('P',(1,1));palette_image.putpalette(expand(palette).reshape(-1).tolist())
    align();palette_offset=len(pack);pack.extend(palette.astype('<u2').tobytes())
    entry={'name':name,'source':f'upstream/assets/materials/megatextures/{name}.png','source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'original_size':original.size,'size':size,'palette_offset':palette_offset,'levels':[],'nonopaque_alpha_pixels':int(np.count_nonzero(alpha!=255))}
    w,h=size
    while True:
        image=base if (w,h)==size else base.resize((w,h),Image.Resampling.BOX)
        # Diffuse each complete mip before splitting tiles, avoiding tile seams.
        indices=np.asarray(image.quantize(palette=palette_image,dither=Image.Dither.FLOYDSTEINBERG),dtype=np.uint8)
        if (w,h)==size:
            source565=as565(base);decoded=palette[indices]
            entry["oklab_error"]=metrics(base,expand(decoded))
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
report={'palette_method':'RGB565-constrained weighted Oklab; lightness distance x1.5; bright/chromatic importance; edge-aware training cleanup',
        'dither':'Pillow Floyd-Steinberg over each whole mip before tiling; RGB source mips remain undithered',
        'palette_scope':'one 256-entry RGB565 palette per source texture, shared by all tiles and mips',
        'generator_versions':{'numpy':np.__version__,'pillow':pillow_version},'asset_bytes':len(pack),'flash_budget':16*1024*1024,'asset_ceiling':12*1024*1024,'tile_bytes':1024,'page_count':len(page_offsets),'palette_bytes':len(names)*512,'coarse_bytes':sum(t['levels'][-1]['width']*t['levels'][-1]['height'] for t in textures),'sha256':hashlib.sha256(pack).hexdigest(),'textures':textures}
(project/'generated/assets.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
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
(project/'main/ChapelAssets.hpp').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print('PACKED',len(pack),'bytes;',len(page_offsets),'pages;',report['coarse_bytes'],'coarse bytes')
