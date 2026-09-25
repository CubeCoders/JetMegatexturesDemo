"""Deterministic, RGB565-constrained, perceptually weighted palette selection.

Oklab conversion formulas adapted from Bjorn Ottosson's public-domain example:
https://bottosson.github.io/posts/oklab/
The artistic importance weights and lightness distance weight are independent.
Error diffusion is performed by Pillow in pack_assets.py, after palette fitting.
"""
import numpy as np

SCALE=np.array([1.5,1.,1.],np.float32)

def expand(p):
    return np.stack([((p>>11)&31)*255//31,((p>>5)&63)*255//63,(p&31)*255//31],axis=-1).astype(np.uint8)

def rgb565(rgb):
    # Closest displayed channel value, rather than truncating 8-bit palette means.
    a=np.clip(np.asarray(rgb,dtype=np.float32),0,255)
    q=np.rint(a*np.array([31,63,31],np.float32)/255).astype(np.uint16)
    return (q[...,0]<<11)|(q[...,1]<<5)|q[...,2]

def lab(rgb):
    c=np.asarray(rgb,dtype=np.float32)/255
    c=np.where(c<=.04045,c/12.92,((c+.055)/1.055)**2.4)
    m=np.array([[.4122214708,.5363325363,.0514459929],[.2119034982,.6806995451,.1073969566],[.0883024619,.2817188376,.6299787005]],np.float32)
    n=np.array([[.2104542553,.7936177850,-.0040720468],[1.9779984951,-2.4285922050,.4505937099],[.0259040371,.7827717662,-.8086757660]],np.float32)
    return np.cbrt(c@m.T)@n.T

def unlab(c):
    m=np.array([[1,.3963377774,.2158037573],[1,-.1055613458,-.0638541728],[1,-.0894841775,-1.2914855480]],np.float32)
    n=np.array([[4.0767416621,-3.3077115913,.2309699292],[-1.2684380046,2.6097574011,-.3413193965],[-.0041960863,-.7034186147,1.7076147010]],np.float32)
    c=(c@m.T)**3@n.T
    c=np.clip(c,0,1)
    return np.where(c<=.0031308,c*12.92,1.055*c**(1/2.4)-.055)*255

def clean_training(image):
    # Suppress low-contrast chroma speckle when learning the palette. Edge-aware
    # weights preserve lead lines and genuine colour boundaries. Source pixels
    # and mip images themselves remain unchanged before quantization.
    c=lab(image);h,w=c.shape[:2];pad=np.pad(c,((1,1),(1,1),(0,0)),mode='edge')
    total=np.zeros_like(c);weight=np.zeros((h,w),np.float32)
    for dy in range(-1,2):
        for dx in range(-1,2):
            other=pad[1+dy:1+dy+h,1+dx:1+dx+w]
            d=(other-c)/np.array([.022,.018,.018],np.float32)
            q=np.exp(-.5*np.sum(d*d,axis=2)-.5*(dx*dx+dy*dy))
            total+=other*q[...,None];weight+=q
    smooth=total/weight[...,None]
    return unlab(c+(smooth-c)*np.array([.15,.8,.8],np.float32))

def importance(c):
    chroma=np.linalg.norm(c[:,1:],axis=1)
    # Give bright, saturated colours extra palette influence. This is an artistic
    # priority; all colours retain nonzero weight, separate from lightness error.
    return (.4+.9*c[:,0]**2)*(1+4*np.clip(chroma/.18,0,1.5))

def nearest(points,centres):
    distances=np.empty(len(points),np.float32);labels=np.empty(len(points),np.int32)
    for start in range(0,len(points),2048):
        a=points[start:start+2048]
        d=np.sum((a[:,None,:]-centres[None,:,:])**2,axis=2)
        choice=np.argmin(d,axis=1)
        labels[start:start+len(a)]=choice;distances[start:start+len(a)]=d[np.arange(len(a)),choice]
    return labels,distances

def choose_palette(base):
    trained=rgb565(clean_training(base))
    colours,counts=np.unique(trained,return_counts=True)
    pts=lab(expand(colours));points=pts*SCALE
    weights=counts.astype(np.float32)**.85*importance(pts)
    # Deterministic weighted farthest-point initialization, including dark ink.
    chosen=[int(np.argmin(pts[:,0]))]
    distance=np.sum((points-points[chosen[0]])**2,axis=1)
    for i in range(1,min(256,len(colours))):
        chosen.append(int(np.argmax(distance*weights)))
        distance=np.minimum(distance,np.sum((points-points[chosen[-1]])**2,axis=1))
    palette=colours[chosen]
    for iteration in range(10):
        centres=lab(expand(palette))*SCALE
        labels,distances=nearest(points,centres)
        totals=np.bincount(labels,weights=weights,minlength=len(palette))
        means=np.stack([np.bincount(labels,weights=points[:,axis]*weights,minlength=len(palette)) for axis in range(3)],axis=1)
        valid=totals>0;means[valid]/=totals[valid,None];means[~valid]=centres[~valid]
        refined=rgb565(unlab(means/SCALE))
        # RGB565 snapping is part of every iteration: never waste palette slots
        # on duplicates introduced only after an RGB888 optimizer has finished.
        palette=np.unique(refined)
        while len(palette)<min(256,len(colours)):
            _,distance=nearest(points,lab(expand(palette))*SCALE)
            available=~np.isin(colours,palette)
            score=np.where(available,distance*weights,-1)
            palette=np.append(palette,colours[np.argmax(score)])
        if np.array_equal(np.sort(refined),np.sort(palette)) and iteration>=7:break
    if len(palette)<256:palette=np.pad(palette,(0,256-len(palette)),mode='edge')
    return palette.astype('<u2')

def metrics(reference,result):
    a=lab(reference);b=lab(result)
    l=a[...,0];chroma=np.linalg.norm(a[...,1:],axis=2)
    mask=(l>.65)&(chroma>.10)
    delta=(a-b)*SCALE
    def mean(values,mask):return float(values[mask].mean()) if mask.any() else None
    return {'lightness_mae':float(np.abs(a[...,0]-b[...,0]).mean()),
            'perceptual_rmse':float(np.sqrt(np.mean(np.sum(delta*delta,axis=2)))),
            'bright_saturated_rmse':float(np.sqrt(mean(np.sum(delta*delta,axis=2),mask))) if mask.any() else None,
            'dark_lightness_mae':mean(np.abs(a[...,0]-b[...,0]),l<.45)}
