#include "Chapel.hpp"
#include <fstream>
#include <thread>
#include <vector>
#include <cstdio>
#include <cassert>

static void validate(Chapel::HotFilterCache& hot,Chapel::TileCache& source,unsigned map,unsigned mip){
    auto view=source.view(map,mip,true);hot.bind(view,map,mip);
    for(int v=-20;v<1060;v+=31)for(int u=-20;u<1060;u+=29){
        const unsigned x=unsigned(std::clamp(u,0,1023)),y=unsigned(std::clamp(v,0,1023));
        const bool hit=view.hotSlots[((y>>6)<<4)+(x>>6)]!=255;
        assert(view.sampleHot(u,v)==(hit?view.sampleBilinear(u,v):view.sample(u,v)));
    }
}
int main(){
    std::ifstream input(ASSET_FILE,std::ios::binary);assert(input);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)),{});
    Chapel::TileCache source;source.init(data.data(),false);
    Chapel::HotFilterCache hot;assert(hot.init());hot.budgetUs=0;hot.rowLimit=1;
    auto view=source.view(14,0,true);hot.bind(view,14,0);
    assert(view.sampleHot(480,480,0)==view.sample(480,480));
    for(unsigned field=0;field<4;++field){view.sampleHot(480,480,0);hot.tick(source);}
    assert(hot.filling()&&hot.used()==0&&hot.completed==0);
    assert(view.sampleHot(480,480)==view.sample(480,480));
    while(!hot.completed){view.sampleHot(480,480,1);hot.tick(source);assert(hot.rowsThisField<=1);}
    assert(hot.used()==1);
    for(int v=448;v<512;++v)for(int u=448;u<512;++u)assert(view.sampleHot(u,v)==view.sampleBilinear(u,v));
    validate(hot,source,14,0);validate(hot,source,14,1);validate(hot,source,11,0);

    // Force more than the pool's 128 tiles, including both workers' feedback.
    // Every published entry must still match its material, mip and UV key.
    hot.rowLimit=64;
    for(unsigned page=0;page<180;++page){
        const int u=int((page&15)*64+23),v=int((page>>4)*64+29);
        for(unsigned f=0;f<8;++f){
            for(unsigned n=0;n<20;++n)view.sampleHot(u,v,int(n&1));
            hot.tick(source);assert(hot.used()<=128);
        }
        if(page%20==0){validate(hot,source,14,0);validate(hot,source,14,1);}
    }
    assert(hot.evicted>0);validate(hot,source,14,0);
    auto* feedback=view.hotFeedback[0];feedback->clear();
    for(unsigned key=0;key<4000;++key)feedback->record(uint16_t(key),false);
    assert(feedback->dropped>0&&feedback->sampled==4000);feedback->clear();

    // Real moving-camera rendering with disjoint raster bands, including
    // cache fills/evictions between frames and frozen-cache serial parity.
    constexpr unsigned pixels=240*160;
    std::vector<uint16_t> frame(pixels+128,0xa55a),depth(240*320);
    Renderer::Scene scene(frame.data()+64,depth.data(),480,320);scene.getRenderer()->interlacedMode=true;
    Chapel::init(scene,data.data(),false);assert(Chapel::hot.init());Chapel::hot.budgetUs=0;
    Chapel::setFilter(Renderer::TileFilter::CachedBilinear);
    auto parallel=[](Renderer::Scene& scene){
        static unsigned phase=0;const int split=32+4*(phase++%64);
        std::vector<uint8_t> a(scene.lastFrameDrawnTriangles),b(scene.lastFrameDrawnTriangles);
        std::thread worker([&]{scene.rasterizeBand(0,split,a.data());});
        scene.rasterizeBand(split,320,b.data());worker.join();
    };
    for(unsigned field=0;field<200;++field){
        const float times[]={20,47,69,0};Chapel::seek(times[field/50]+(field%50)/60.f);
        scene.frameCounter=field%2;scene.render(parallel);const auto threaded=frame;
        scene.frameCounter=field%2;scene.render();assert(frame==threaded);
        assert(std::all_of(frame.begin(),frame.begin()+64,[](auto p){return p==0xa55a;}));
        assert(std::all_of(frame.end()-64,frame.end(),[](auto p){return p==0xa55a;}));
        assert(Chapel::hot.used()<=128);
    }
    assert(Chapel::hot.completed>10);
    std::puts("PASS: nearest misses, exact bilinear hits, hidden partial tiles, material/mip keys, eviction, bounded feedback, moving parallel bands and guards");
}
