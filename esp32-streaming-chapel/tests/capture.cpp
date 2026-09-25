#include "Chapel.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <thread>
#include <string>
#include <vector>

// Actual ESP32 buffer layout, without the scanout FPS overlay. This tool's
// nominal clock and deterministic fill allowance are not hardware timings.
int main(int argc,char** argv) {
    const std::filesystem::path output=argc>1?argv[1]:"capture";
    std::filesystem::create_directories(output);
    std::ifstream input(ASSET_FILE,std::ios::binary);assert(input);
    std::vector<uint8_t> assets((std::istreambuf_iterator<char>(input)),{});
    assert(assets.size()==ChapelAssets::assetBytes);
    std::vector<uint16_t> buffer(240*160+128,0xa55a),full(480*320),depth(240*320);
    Renderer::Scene scene(buffer.data()+64,depth.data(),480,320);
    scene.getRenderer()->interlacedMode=true;
    Chapel::init(scene,assets.data(),false);assert(Chapel::hot.init());
    Chapel::hot.budgetUs=0;Chapel::hot.rowLimit=64;
    Chapel::setFilter(Renderer::TileFilter::CachedBilinear);
    const auto parallel=[](Renderer::Scene& s){
        std::thread worker([&]{s.rasterizeBand(0,160,nullptr);});
        s.rasterizeBand(160,320,nullptr);worker.join();
    };
    std::ofstream video(output/"chapel.rgb",std::ios::binary);assert(video);
    std::vector<uint8_t> rgb(480*320*3);
    unsigned frames=0;
    for(unsigned field=0;field<60*51;++field){
        const float t=field/60.f;
        Chapel::seek(t);scene.frameCounter=field%2;scene.render(parallel);
        assert(std::all_of(buffer.begin(),buffer.begin()+64,[](auto p){return p==0xa55a;}));
        assert(std::all_of(buffer.end()-64,buffer.end(),[](auto p){return p==0xa55a;}));
        for(unsigned y=1-field%2;y<320;y+=2)for(unsigned x=0;x<480;++x)
            full[y*480+x]=buffer[64+(y/2)*240+x/2];
        if(field%2){
            for(unsigned i=0;i<full.size();++i){const auto p=full[i];
                rgb[i*3]=uint8_t((p>>11)*255/31);
                rgb[i*3+1]=uint8_t(((p>>5)&63)*255/63);
                rgb[i*3+2]=uint8_t((p&31)*255/31);
            }
            if((t>=4&&t<8)||(t>=20&&t<24)||(t>=46&&t<50)){
                video.write(reinterpret_cast<const char*>(rgb.data()),rgb.size());++frames;
            }
            if(field==8*60-1||field==35*60-1||field==47*60-1){
                const auto name="chapel-"+std::to_string((field+1)/60)+".ppm";
                std::ofstream still(output/name,std::ios::binary);still<<"P6\n480 320\n255\n";
                still.write(reinterpret_cast<const char*>(rgb.data()),rgb.size());
            }
        }
    }
    assert(frames==360);
    std::printf("Captured %u RGB frames; 12 seconds at nominal 30 paired frames/s.\n",frames);
}
