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
    const bool poseMode=argc==9&&std::string(argv[2])=="--pose";
    const bool inspect=argc==3&&std::string(argv[2])=="--inspect";
    if(argc>2&&!poseMode&&!inspect){
        std::fprintf(stderr,"Usage: chapel_capture output [--inspect | --pose x y z yaw pitch fov]\n");return 1;
    }
    ChapelView::Pose pose{};
    if(poseMode)pose={std::stof(argv[3]),std::stof(argv[4]),std::stof(argv[5]),
                     std::stof(argv[6]),std::stof(argv[7]),std::stof(argv[8])};
    std::ofstream video;
    if(!poseMode&&!inspect){video.open(output/"chapel.rgb",std::ios::binary);assert(video);}

    std::vector<uint8_t> rgb(480*320*3);
    unsigned frames=0;
    for(unsigned field=0;field<(poseMode?240:60*90);++field){
        const float t=field/60.f;
        if(poseMode){
            Chapel::cache.finishRender();Chapel::applyCamera(pose);
            Chapel::visibility();Chapel::plan();++Chapel::fieldNumber;
        }else Chapel::seek(t);
        scene.frameCounter=field%2;scene.render(parallel);
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
            if(video.is_open()&&((t>=0&&t<4)||(t>=36&&t<40)||(t>=74&&t<78))){
                video.write(reinterpret_cast<const char*>(rgb.data()),rgb.size());++frames;
            }
            if((poseMode&&field==239)||(inspect&&(field+1)%120==0)||(!poseMode&&(field==2*60-1||field==26*60-1||field==47*60-1))){
                const auto name=poseMode?std::string("pose.ppm"):"chapel-"+std::to_string((field+1)/60)+".ppm";
                std::ofstream still(output/name,std::ios::binary);still<<"P6\n480 320\n255\n";
                still.write(reinterpret_cast<const char*>(rgb.data()),rgb.size());
            }
        }
    }
    assert(poseMode||inspect||frames==360);
    if(poseMode)std::puts("Captured static pose after 240 warmup fields.");
    else if(inspect)std::puts("Captured tour inspection stills every two seconds.");
    else std::printf("Captured %u RGB frames; 12 seconds at nominal 30 paired frames/s.\n",frames);
}
