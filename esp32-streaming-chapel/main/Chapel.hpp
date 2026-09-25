#pragma once
#include "Scene.hpp"
#include "TileCache.hpp"
#include "HotFilterCache.hpp"
#include "CameraControls.hpp"
#include <cmath>
#include <memory>
#include <numeric>
#include <cstdio>
#include <climits>

namespace Chapel {
using namespace Renderer;
inline constexpr TileFilter defaultFilter=TileFilter::ThreePoint;
inline const char* filterName(TileFilter filter){
    if(filter==TileFilter::CachedBilinear)return "hot-bilinear";
    return filter==TileFilter::Nearest?"nearest":filter==TileFilter::Bilinear?"bilinear":"threepoint";
}
inline Camera camera;
inline ChapelView::Controller controls;
inline Scene* scene=nullptr;
inline TileCache cache;
inline HotFilterCache hot;
struct SurfaceState {
    std::unique_ptr<Object> mesh;
    std::unique_ptr<Material> material;
    std::unique_ptr<Texture> texture;
    TileTexture tile;
    float nx=0,ny=0,nz=0,depth=0;
    int minU=1024,minV=1024,maxU=0,maxV=0;
    unsigned wantedMip=0,mip=0,map=0;
    int group=0;
    bool visible=false;
};
inline std::array<SurfaceState,ChapelAssets::surfaceCount> surfaces;
inline float time=0;
inline bool directBacking=false;
inline unsigned bias=0,visibleCount=0;
inline unsigned loadBudget=8;
inline unsigned fieldNumber=0;
// Map 14 is window_main in the pinned generated asset set, top mip only.
inline constexpr unsigned prefilterMap=14;
inline uint16_t* prefilteredUV=nullptr;
inline bool usePrefilter=false;
inline constexpr float pi=3.14159265358979323846f;

inline void benchmarkCameraAt(float seconds) {
    camera.setFOV(92.8f,480);
    // Linear camera travel and angular motion avoid interpolating a look-at
    // target through the camera itself during the turn at the altar.
    struct Key{float x,y,z,yaw,pitch;};
    static const Key keys[]={{0,3,-14,0,0},{0,2.6f,-7,0,-2},{0,2.8f,-1,0,-10},{0,3,-1,180,0},{0,3,-10,180,0},{0,3,-14,360,0}};
    const float t=std::fmod(std::max(seconds,0.f),50.f)/10.f;
    const int i=std::min(4,int(t));const float f=t-i;
    const auto& a=keys[i];const auto& b=keys[i+1];
    auto mix=[&](float u,float v){return int((u+(v-u)*f)*256.f);};
    camera.setPosition(mix(a.x,b.x),mix(a.y,b.y),mix(a.z,b.z));
    camera.rotation.assign((a.pitch+(b.pitch-a.pitch)*f)*pi/180.f,(a.yaw+(b.yaw-a.yaw)*f)*pi/180.f,0.f);
}
inline void applyCamera(const ChapelView::Pose& pose) {
    camera.setPosition(int(pose.x*256.f),int(pose.y*256.f),int(pose.z*256.f));
    camera.rotation.assign(pose.pitch*pi/180.f,pose.yaw*pi/180.f,0.f);
    camera.setFOV(pose.fov,480);
}
inline void cameraAt(float seconds) { applyCamera(ChapelView::tour(seconds)); }
inline void init(Scene& target,const uint8_t* backing,bool enableCache=true) {
    scene=&target;controls=ChapelView::Controller{};time=0;fieldNumber=0;
    directBacking=!enableCache;
#ifdef ESP_PLATFORM
    heap_caps_malloc_extmem_enable(128);
#endif
    cache.init(backing,enableCache);
    camera.setFOV(92.8f,480);camera.nearPlane=13;camera.farPlane=7000;
    scene->setCamera(&camera);scene->setClearBuffer(true);scene->setBackcolor(0);
    for(unsigned i=0;i<surfaces.size();++i) {
        auto& dst=surfaces[i];const auto& src=ChapelAssets::surfaces[i];
        dst.map=src.material;dst.group=src.sortGroup;
        dst.mesh=std::make_unique<Object>();dst.mesh->cullingMode=CullingMode::NO_CULLING;
        dst.texture=std::make_unique<Texture>(16,16,reinterpret_cast<uint16_t*>(cache.coarse[dst.map]),false,0,false,CLAMP,cache.palettes+dst.map*256);
        dst.texture->tiled=&dst.tile;dst.texture->tiledFilter=defaultFilter;
        dst.material=std::make_unique<Material>(0xffff);dst.material->shadingMode=ShadingMode::UNLIT;
        dst.material->diffuseMap=dst.texture.get();dst.material->perspectiveCorrect=true;
        for(unsigned j=0;j<src.vertices;++j) {
            const auto& v=ChapelAssets::vertices[src.firstVertex+j];
            dst.mesh->addVertex({{v.x,v.y,v.z},{v.u,v.v},{0,1024,0}});
            dst.minU=std::min(dst.minU,int(v.u));dst.minV=std::min(dst.minV,int(v.v));
            dst.maxU=std::max(dst.maxU,int(v.u));dst.maxV=std::max(dst.maxV,int(v.v));
        }
        for(unsigned j=0;j<src.faces;++j) {
            const auto& f=ChapelAssets::faces[src.firstFace+j];dst.mesh->addTriangle(f.a,f.b,f.c,dst.material.get());
        }
        const auto& f=ChapelAssets::faces[src.firstFace];
        const auto a=dst.mesh->vertices[f.a].position,b=dst.mesh->vertices[f.b].position,c=dst.mesh->vertices[f.c].position;
        const float ux=float(b.x-a.x),uy=float(b.y-a.y),uz=float(b.z-a.z),vx=float(c.x-a.x),vy=float(c.y-a.y),vz=float(c.z-a.z);
        dst.nx=uy*vz-uz*vy;dst.ny=uz*vx-ux*vz;dst.nz=ux*vy-uy*vx;
        dst.mesh->calculateBoundingBox();dst.mesh->cachePositions();
    }
    scene->getObjects().reserve(surfaces.size());
#ifdef ESP_PLATFORM
    heap_caps_malloc_extmem_enable(CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL);
#endif
    std::printf("CHAPEL: %u source triangles; %u KiB tile cache, %u palettes, %u backing pages\n",325,cache.capacity,ChapelAssets::mapCount,ChapelAssets::pageCount);
}
inline void setFilter(TileFilter filter){
    for(auto& s:surfaces){s.texture->bilinear=filter!=TileFilter::Nearest;s.texture->tiledFilter=filter;}
}
inline void visibility() {
    visibleCount=0;
    int32_t cx,sx,cy,sy,cz,sz;camera.getRotationMatrix(cx,sx,cy,sy,cz,sz);
    auto toCamera=[&](Vector3 p){
        p.assign((p.x*cy+p.z*sy)/1024,p.y,(-p.x*sy+p.z*cy)/1024);
        p.assign(p.x,(p.y*cx-p.z*sx)/1024,(p.y*sx+p.z*cx)/1024);
        p.assign((p.x*cz-p.y*sz)/1024,(p.x*sz+p.y*cz)/1024,p.z);
        return p;
    };
    for(auto& s:surfaces) {
        s.visible=false;const auto& p=s.mesh->vertices.front().position;
        if(s.nx*(camera.position.x-p.x)+s.ny*(camera.position.y-p.y)+s.nz*(camera.position.z-p.z)<=0)continue;
        float minX=1e9f,minY=1e9f,maxX=-1e9f,maxY=-1e9f;int32_t minZ=INT32_MAX,maxZ=INT32_MIN;float depth=0;
        for(const auto& vertex:s.mesh->vertices) {
            const auto v=toCamera(vertex.position-camera.position);
            minZ=std::min(minZ,v.z);maxZ=std::max(maxZ,v.z);depth+=v.z;
            const float z=float(std::max(v.z,camera.nearPlane));
            const float x=240+v.x*camera.fovFactor/z,y=160-v.y*camera.fovFactor/z;
            minX=std::min(minX,x);minY=std::min(minY,y);maxX=std::max(maxX,x);maxY=std::max(maxY,y);
        }
        if(maxZ<=camera.nearPlane||minZ>camera.farPlane)continue;
        if(minZ>camera.nearPlane&&(maxX<0||minX>480||maxY<0||minY>320))continue;
        s.depth=depth/s.mesh->vertices.size();s.visible=true;++visibleCount;
        const auto& map=ChapelAssets::maps[s.map];const auto& level=ChapelAssets::levels[map.firstLevel];
        const float texels=std::max(level.width*float(s.maxU-s.minU)/1024,level.height*float(s.maxV-s.minV)/1024);
        const float pixels=std::max(1.f,std::max(std::min(maxX-minX,960.f),std::min(maxY-minY,640.f))*.5f);
        s.wantedMip=unsigned(std::max(0.f,std::floor(std::log2(std::max(1.f,texels/pixels)))));

    }
}
inline void plan() {
    hot.tick(cache);
    if(directBacking){cache.beginPlan();cache.load(0);cache.sampled=cache.hotSamples=0;}
    else if(fieldNumber%8==0){cache.planFromFeedback();cache.load(loadBudget);}
    else {cache.loaded=cache.missing=0;cache.frozen=true;}
    auto& objects=scene->getObjects();objects.clear();
    std::array<unsigned,ChapelAssets::surfaceCount> order;std::iota(order.begin(),order.end(),0);
    std::sort(order.begin(),order.end(),[](unsigned a,unsigned b){const auto& x=surfaces[a];const auto& y=surfaces[b];if(x.group!=y.group)return x.group<y.group;if(x.group>=0&&x.depth!=y.depth)return x.depth>y.depth;return a<b;});
    for(unsigned i:order) {
        auto& s=surfaces[i];if(!s.visible)continue;
        const auto& map=ChapelAssets::maps[s.map];
        s.mip=std::min(unsigned(map.levelCount-1),s.wantedMip+bias);
        s.tile=cache.view(s.map,s.mip,directBacking);hot.bind(s.tile,s.map,s.mip);
        if(usePrefilter&&s.map==prefilterMap&&s.mip==0)s.tile.filteredUV=prefilteredUV;
        if(fieldNumber%8!=7)s.tile.feedback[0]=s.tile.feedback[1]=nullptr;
        s.texture->width=s.tile.width;s.texture->height=s.tile.height;objects.push_back(s.mesh.get());
    }
}
inline void seek(float seconds) {
    cache.finishRender();time=seconds;cameraAt(time);visibility();plan();
    ++fieldNumber;
}
// Keep the original six benchmark poses independent of the presentation tour.
inline void benchmarkSeek(float seconds) {
    cache.finishRender();time=seconds;benchmarkCameraAt(time);visibility();plan();
    ++fieldNumber;
}
inline void update(float seconds,uint8_t buttons=0) {
    cache.finishRender();controls.step(seconds,buttons);time=controls.tourTime;
    applyCamera(controls.pose);visibility();plan();++fieldNumber;
}
}
