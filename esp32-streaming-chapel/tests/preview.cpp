#include "Chapel.hpp"
#include "TiledSpan.hpp"
#include <fstream>
#include <filesystem>
#include <chrono>
#include <thread>

static uint64_t hash(const std::vector<uint16_t>& frame) {
    uint64_t h=1469598103934665603ull;for(auto p:frame){h^=p;h*=1099511628211ull;}return h;
}
static void parallel(Renderer::Scene& scene) {
    static unsigned step=0;const int split=32+4*(step++%64);
    std::vector<uint8_t> top(scene.lastFrameDrawnTriangles),bottom(scene.lastFrameDrawnTriangles);
    std::thread worker([&]{scene.rasterizeBand(0,split,top.data());});scene.rasterizeBand(split,320,bottom.data());worker.join();
    scene.lastFrameRasterizedTriangles=0;for(unsigned i=0;i<top.size();++i)scene.lastFrameRasterizedTriangles+=(top[i]|bottom[i])!=0;
}
static void samplerChecks(const std::vector<uint8_t>& data) {
    uint32_t random=42;
    for(int exponent=-20;exponent<=20;++exponent)for(unsigned n=0;n<10000;++n){
        random=random*1664525+1013904223;
        const float value=std::ldexp(1.f+(random&0x7fffff)/8388608.f,exponent);
        const double error=std::abs(double(Renderer::tileReciprocal(value))*value-1.);
        assert(error<0.000001);
    }
    for(unsigned material=0;material<ChapelAssets::mapCount;++material){
        const auto& map=ChapelAssets::maps[material];
        for(unsigned mip=0;mip<map.levelCount;++mip){
            const auto& level=ChapelAssets::levels[map.firstLevel+mip];
            // Decode tiles into an ordinary row-major reference image, with an
            // independent nested tile/row/column traversal.
            std::vector<uint16_t> reference(level.width*level.height);
            for(unsigned y=0;y<level.height;++y)for(unsigned x=0;x<level.width;++x){
                const unsigned offset=level.firstPage<0?level.offset+y*level.width+x:ChapelAssets::pageOffsets[level.firstPage+(y/32)*level.pagesX+x/32]+(y%32)*32+x%32;
                const unsigned index=data[offset];reference[y*level.width+x]=uint16_t(data[map.paletteOffset+index*2])|uint16_t(data[map.paletteOffset+index*2+1])<<8;
            }
            for(unsigned pass=0;pass<2;++pass){
                Chapel::cache.finishRender();Chapel::cache.beginPlan();
                if(level.firstPage>=0)for(unsigned page=0;page<std::min(Chapel::cache.capacity,unsigned(level.pagesX*level.pagesY));++page)Chapel::cache.request(level.firstPage+page);
                Chapel::cache.load(pass?Chapel::cache.capacity:0);
                const auto sampled=Chapel::cache.view(material,mip,false);
                for(unsigned n=0;n<1200;++n){random=random*1664525+1013904223;const int u=int(random%3072)-1024;random=random*1664525+1013904223;const int v=int(random%3072)-1024;
                    const unsigned x=unsigned(std::clamp(u,0,1023))*level.width/1024,y=unsigned(std::clamp(v,0,1023))*level.height/1024;
                    assert(sampled.sample(u,v)==reference[y*level.width+x]);
                    // Independent row-major bilinear oracle: double precision
                    // weights, channel decoding before interpolation.
                    const double sx=std::clamp(u,0,1023)*(level.width-1)/1024.0;
                    const double sy=std::clamp(v,0,1023)*(level.height-1)/1024.0;
                    const unsigned bx=unsigned(sx),by=unsigned(sy);
                    const unsigned ex=std::min(bx+1,unsigned(level.width-1));
                    const unsigned ey=std::min(by+1,unsigned(level.height-1));
                    const double fx=sx-bx,fy=sy-by;
                    const unsigned colors[]={reference[by*level.width+bx],reference[by*level.width+ex],reference[ey*level.width+bx],reference[ey*level.width+ex]};
                    unsigned expected=0;
                    for(unsigned channel=0;channel<3;++channel){
                        const unsigned shift=channel==0?11:channel==1?5:0,mask=channel==1?63:31;
                        const double a=(colors[0]>>shift)&mask,b=(colors[1]>>shift)&mask;
                        const double c=(colors[2]>>shift)&mask,d=(colors[3]>>shift)&mask;
                        expected|=unsigned((a*(1-fx)+b*fx)*(1-fy)+(c*(1-fx)+d*fx)*fy)<<shift;
                    }
                    assert(sampled.sampleBilinear(u,v)==expected);
                    auto direct=sampled;direct.direct=true;
                    assert(direct.sampleBilinear(u,v)==expected);
                    // Barycentric reference on a unit triangle, using the
                    // row-major decoded image rather than tiled sampling.
                    const bool upper=fx+fy>1;
                    const unsigned corners[]={upper?colors[3]:colors[0],colors[1],colors[2]};
                    const double weights[]={upper?fx+fy-1:1-fx-fy,upper?1-fy:fx,upper?1-fx:fy};
                    unsigned triangle=0;
                    for(unsigned channel=0;channel<3;++channel){
                        const unsigned shift=channel==0?11:channel==1?5:0,mask=channel==1?63:31;
                        double sum=0;
                        for(unsigned tap=0;tap<3;++tap)sum+=((corners[tap]>>shift)&mask)*weights[tap];
                        triangle|=unsigned(sum)<<shift;
                    }
                    assert(sampled.sampleThreePoint(u,v)==triangle);
                    assert(direct.sampleThreePoint(u,v)==triangle);
                }
            }
        }
    }
    Chapel::cache.finishRender();
    Chapel::TileCache flashOnly;flashOnly.init(data.data(),false);
    assert(!flashOnly.pool&&!flashOnly.slotMap&&!flashOnly.feedback[0]&&flashOnly.capacity==0);
    for(unsigned m=0;m<ChapelAssets::mapCount;++m){auto a=flashOnly.view(m,0,false),b=Chapel::cache.view(m,0,true);assert(a.direct);for(int uv=-20;uv<1060;uv+=7)assert(a.sample(uv,1024-uv)==b.sample(uv,1024-uv));}
}
int main(int argc,char** argv) {
    unsigned mismatches=0;
    std::ifstream input(ASSET_FILE,std::ios::binary);assert(input);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)),{});assert(data.size()==ChapelAssets::assetBytes);
    constexpr int count=240*160;
    std::vector<uint16_t> frame(count+128,0xa55a),full(480*320),depth(240*320);
    Renderer::Scene scene(frame.data()+64,depth.data(),480,320);scene.getRenderer()->interlacedMode=true;
    const bool useCache=!(argc>2&&std::string(argv[2])=="no-cache");
    Chapel::init(scene,data.data(),useCache);
    if(useCache)samplerChecks(data);
    for(int arg=1;arg<argc;++arg){
        if(std::string(argv[arg])=="nearest")Chapel::setFilter(Renderer::TileFilter::Nearest);
        if(std::string(argv[arg])=="bilinear")Chapel::setFilter(Renderer::TileFilter::Bilinear);
        if(std::string(argv[arg])=="threepoint")Chapel::setFilter(Renderer::TileFilter::ThreePoint);
    }
    if(argc>1&&std::string(argv[1])=="prefilter-test"){
        std::vector<uint16_t> prepared(1024*1024);
        const auto view=Chapel::cache.view(Chapel::prefilterMap,0,true);
        for(int v=0;v<1024;++v)for(int u=0;u<1024;++u)prepared[(v<<10)+u]=view.sampleBilinear(u,v);
        Chapel::prefilteredUV=prepared.data();Chapel::setFilter(Renderer::TileFilter::Bilinear);
        unsigned eligible=0;
        for(float t:{0.f,20.f,47.f,51.f,55.f,69.f})for(unsigned field=0;field<2;++field){
            Chapel::usePrefilter=false;Chapel::seek(t);scene.frameCounter=field;scene.render(parallel);const auto reference=frame;
            Chapel::usePrefilter=true;Chapel::seek(t);scene.frameCounter=field;scene.render(parallel);
            assert(frame==reference);
            for(const auto& s:Chapel::surfaces)if(s.visible){
                const bool correct=s.map==Chapel::prefilterMap&&s.mip==0;
                assert(bool(s.tile.filteredUV)==correct);eligible+=correct;
            }
        }
        assert(eligible>0);
        auto cached=view;cached.filteredUV=prepared.data();
        for(int v:{-100,0,511,1023,1024,4000})for(int u:{-50,0,512,1023,1024,5000})assert(cached.sampleBilinear(u,v)==view.sampleBilinear(u,v));
        Chapel::usePrefilter=false;Chapel::prefilteredUV=nullptr;
        std::puts("PASS: exact prefiltered lookup equals bilinear, both fields, mip/material eligibility and clamped edges");return 0;
    }
    if(argc>1&&std::string(argv[1])=="video") {
        std::ofstream output("chapel-tour.rgb",std::ios::binary);
        std::vector<char> rgb(full.size()*3);
        for(unsigned field=0;field<unsigned(ChapelView::tourDuration*60);++field){
            Chapel::seek(field/60.f);scene.frameCounter=field%2;scene.render(parallel);
            for(unsigned y=1-field%2;y<320;y+=2)for(unsigned x=0;x<480;++x)full[y*480+x]=frame[64+(y/2)*240+x/2];
            if(field%2){for(unsigned i=0;i<full.size();++i){auto p=full[i];rgb[i*3]=char(((p>>11)&31)*255/31);rgb[i*3+1]=char(((p>>5)&63)*255/63);rgb[i*3+2]=char((p&31)*255/31);}output.write(rgb.data(),rgb.size());}
        }
        std::puts("VIDEO: 90 seconds, 60 alternating fields/s, 480x320 RGB565 with half-width rendering");return 0;
    }
    if(argc>1&&std::string(argv[1])=="tour") {
        // Close windows, oblique transitions and the loop, at hardware resolution.
        const float times[]={0,15,20,24,28,35,43,47,51,55,59,65,69,73,81,89.9f};
        for(unsigned view=0;view<std::size(times);++view) {
            for(unsigned field=0;field<2;++field) {
                Chapel::seek(times[view]);scene.frameCounter=field;scene.render(parallel);
                const auto threaded=frame;
                scene.frameCounter=field;scene.render();assert(frame==threaded);
                assert(std::all_of(frame.begin(),frame.begin()+64,[](auto p){return p==0xa55a;}));
                assert(std::all_of(frame.end()-64,frame.end(),[](auto p){return p==0xa55a;}));
                for(unsigned y=1-field;y<320;y+=2)for(unsigned x=0;x<480;++x)full[y*480+x]=frame[64+(y/2)*240+x/2];
            }
            char name[40];std::snprintf(name,sizeof(name),"chapel-close-%02u.ppm",view);
            std::ofstream image(name,std::ios::binary);image<<"P6\n480 320\n255\n";
            for(auto p:full){char rgb[]={char(((p>>11)&31)*255/31),char(((p>>5)&63)*255/63),char((p&31)*255/31)};image.write(rgb,3);}
            unsigned topWindows=0;
            for(const auto& surface:Chapel::surfaces)if(surface.visible&&surface.mip==0)++topWindows;
            std::printf("TOUR t=%.1f visible=%u top_mip_surfaces=%u tris=%d hash=%016llx\n",times[view],Chapel::visibleCount,topWindows,scene.lastFrameRasterizedTriangles,(unsigned long long)hash(full));
        }
        std::puts("PASS: new tour, both fields, serial/parallel equality and framebuffer guards");return 0;
    }
    const float times[]={0,8,15,23,32,42};
    for(unsigned view=0;view<6;++view) {
        Chapel::directBacking=false;
        for(unsigned warm=0;warm<64;++warm) {
            Chapel::benchmarkSeek(times[view%6]);scene.frameCounter=warm%2;scene.render(parallel);
        }
        assert(Chapel::cache.requests.size()<=Chapel::cache.capacity);
        for(int field=0;field<2;++field) {
            Chapel::directBacking=false;Chapel::benchmarkSeek(times[view%6]);scene.frameCounter=field;scene.render(parallel);
            const auto streamed=frame;
            Chapel::directBacking=true;Chapel::benchmarkSeek(times[view%6]);scene.frameCounter=field;scene.render();
            if(frame!=streamed){unsigned count=0;for(unsigned k=0;k<frame.size();++k)count+=frame[k]!=streamed[k];std::printf("CACHE REFERENCE MISMATCH view=%u field=%d pixels=%u\n",view,field,count);++mismatches;}
            for(auto& surface:Chapel::surfaces)surface.tile.exactPerspective=true;
            scene.frameCounter=field;scene.render();
            unsigned changed=0;uint64_t error=0;
            for(unsigned k=64;k<64+count;++k){auto a=streamed[k],b=frame[k];changed+=a!=b;error+=std::abs((a>>11)*255/31-(b>>11)*255/31)+std::abs(((a>>5)&63)*255/63-((b>>5)&63)*255/63)+std::abs((a&31)*255/31-(b&31)*255/31);}
            const double mae=double(error)/(count*3);
            std::printf("PRECISION view=%u field=%d changed=%.2f%% RGB_MAE=%.3f\n",view,field,100.*changed/count,mae);
            assert(mae<1.0);std::copy(streamed.begin(),streamed.end(),frame.begin());
            Chapel::directBacking=false;
            assert(std::all_of(frame.begin(),frame.begin()+64,[](auto p){return p==0xa55a;}));
            assert(std::all_of(frame.end()-64,frame.end(),[](auto p){return p==0xa55a;}));
            for(int y=1-field;y<320;y+=2)for(int x=0;x<480;++x)full[y*480+x]=frame[64+(y/2)*240+x/2];
        }
        char name[40];std::snprintf(name,sizeof(name),"chapel-%02u.ppm",view);
        std::ofstream image(name,std::ios::binary);image<<"P6\n480 320\n255\n";
        for(auto p:full){char rgb[]={char(((p>>11)&31)*255/31),char(((p>>5)&63)*255/63),char((p&31)*255/31)};image.write(rgb,3);}
        std::printf("VIEW %u t=%.1f visible=%u tris=%d tiles=%zu/%u bias=%u hot=%u/%u hash=%016llx\n",view,times[view%6],Chapel::visibleCount,scene.lastFrameRasterizedTriangles,Chapel::cache.requests.size(),Chapel::cache.capacity,Chapel::bias,Chapel::cache.hotSamples,Chapel::cache.sampled,(unsigned long long)hash(full));
    }
    // Coarse fallback is always valid during large camera moves and constrained loads.
    Chapel::directBacking=false;Chapel::loadBudget=1;
    for(unsigned step=0;step<100;++step){Chapel::seek(float(step%50));scene.render(parallel);assert(Chapel::cache.loaded<=1);assert(Chapel::cache.requests.size()<=Chapel::cache.capacity);}
    if(mismatches)return 2;
    std::puts("PASS: source scene, independent sampler oracle, cache/reference equality, both fields, moving parallel bands, guards and bounded loads");
}
