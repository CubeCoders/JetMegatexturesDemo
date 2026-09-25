#include "Runtime.hpp"
#include "Chapel.hpp"
#include "BoardInput.hpp"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#ifndef CHAPEL_BENCHMARK
#define CHAPEL_BENCHMARK 0
#endif
#ifndef CHAPEL_USE_CACHE
#define CHAPEL_USE_CACHE 0
#endif
#ifndef CHAPEL_PSRAM_BENCH
#define CHAPEL_PSRAM_BENCH 0
#endif
#ifndef CHAPEL_PSRAM_BACKING
#define CHAPEL_PSRAM_BACKING 0
#endif
#ifndef CHAPEL_FILTER_BENCH
#define CHAPEL_FILTER_BENCH 0
#endif
#ifndef CHAPEL_PREFILTER_BENCH
#define CHAPEL_PREFILTER_BENCH 0
#endif
#if CHAPEL_PREFILTER_BENCH && (CHAPEL_FILTER_BENCH || CHAPEL_BENCHMARK)
#error Select only one chapel benchmark
#endif
#ifndef CHAPEL_HOT_FILTER
#define CHAPEL_HOT_FILTER 0
#endif
#if CHAPEL_HOT_FILTER && (CHAPEL_FILTER_BENCH || CHAPEL_PREFILTER_BENCH || CHAPEL_BENCHMARK)
#error Hot-filter playback must be benchmarked separately from static filter tests
#endif
extern const uint8_t assetStart[] asm("_binary_assets_bin_start");
static uint8_t* psramBacking=nullptr;
static const char* currentMode="flash";
static void init(Renderer::Scene& scene){
#if !CHAPEL_BENCHMARK
    ChapelInput::init();
#if CHAPEL_BUTTONS
    std::puts("CAMERA: AUTO; any input takes control; UP+DOWN resumes the tour");
    std::puts("CONTROLS: D-pad move/turn; CAMERA + D-pad look/strafe; REAR VIEW + D-pad rise/strafe; SPECIAL boost");
#endif
#endif
    std::printf("FILTER: mode=%s support=%d filter_benchmark=%d\n",CHAPEL_HOT_FILTER?"hot-bilinear":Chapel::filterName(Chapel::defaultFilter),BILINEAR_FILTER,CHAPEL_FILTER_BENCH);
#if !CHAPEL_BUTTONS
    std::puts("CAMERA: AUTO; buttons disabled (CHAPEL_BUTTONS=OFF)");
#endif
    const bool residentMode=CHAPEL_PSRAM_BACKING&&!CHAPEL_BENCHMARK;
    if(CHAPEL_PSRAM_BENCH||residentMode){
        psramBacking=static_cast<uint8_t*>(heap_caps_malloc(ChapelAssets::assetBytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if(psramBacking){std::memcpy(psramBacking,assetStart,ChapelAssets::assetBytes);std::printf("BACKING: %u texture bytes copied to PSRAM\n",ChapelAssets::assetBytes);}
        else std::puts("BACKING: PSRAM allocation unavailable; retaining flash backing");
    }
    Chapel::init(scene,residentMode&&psramBacking?psramBacking:assetStart,(CHAPEL_BENCHMARK||CHAPEL_USE_CACHE)&&!CHAPEL_PSRAM_BENCH);
#if CHAPEL_HOT_FILTER
    if(Chapel::hot.init()){
        Chapel::setFilter(Renderer::TileFilter::CachedBilinear);
        std::printf("HOT_READY external=%u internal=%u budget_us=%u slots=128\n",unsigned(Chapel::hot.allocatedExternal()),unsigned(Chapel::hot.allocatedInternal()),Chapel::hot.budgetUs);
    }else {Chapel::setFilter(Renderer::TileFilter::Nearest);std::puts("HOT_UNAVAILABLE: nearest fallback");}
#endif
#if CHAPEL_PREFILTER_BENCH
    constexpr size_t bytes=1024*1024*sizeof(uint16_t);
    std::printf("PREFILTER_ALLOC free_psram=%u largest_psram=%u requested=%u\n",unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)),unsigned(bytes));
    const auto started=esp_timer_get_time();
    Chapel::prefilteredUV=static_cast<uint16_t*>(heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(Chapel::prefilteredUV){
        const auto view=Chapel::cache.view(Chapel::prefilterMap,0,true);
        for(int v=0;v<1024;++v){
            for(int u=0;u<1024;++u)Chapel::prefilteredUV[(v<<10)+u]=view.sampleBilinear(u,v);
            if((v&31)==31)vTaskDelay(1);
        }
        std::printf("PREFILTER_READY build_us=%lld bytes=%u free_psram=%u largest_psram=%u internal=%u\n",esp_timer_get_time()-started,unsigned(bytes),unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)),unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
    }else std::puts("PREFILTER_UNAVAILABLE");
#endif
}
static unsigned frame=0;
static int64_t startUs=0,updateUs=0,totalWork=0,totalUpdate=0,totalHits=0,totalSamples=0,totalLoads=0;
static void update(float seconds){
    startUs=esp_timer_get_time();
#if CHAPEL_PREFILTER_BENCH
    if(frame<1800){
        static const float times[]={47,51,55};
        const unsigned phase=frame/100,mode=phase%6;
        const bool three=mode==0||mode==5;
        Chapel::usePrefilter=mode==2||mode==3;
        Chapel::setFilter(three?Renderer::TileFilter::ThreePoint:Renderer::TileFilter::Bilinear);
        currentMode=three?"threepoint":Chapel::usePrefilter?"cached":"bilinear";
        Chapel::seek(times[phase/6]);updateUs=esp_timer_get_time()-startUs;return;
    }
    if(frame==1800){Chapel::usePrefilter=false;Chapel::setFilter(Chapel::defaultFilter);Chapel::controls=ChapelView::Controller{};}
#endif
#if CHAPEL_FILTER_BENCH
    // Six views, N/B/T/T/B/N order; 20 warm + 80 measured fields per phase.
    if(frame<3600){
        static const float times[]={0,20,35,47,69,81};
        const unsigned phase=frame/100;
        static constexpr Renderer::TileFilter filters[]={Renderer::TileFilter::Nearest,Renderer::TileFilter::Bilinear,Renderer::TileFilter::ThreePoint,Renderer::TileFilter::ThreePoint,Renderer::TileFilter::Bilinear,Renderer::TileFilter::Nearest};
        const auto filter=filters[phase%6];Chapel::setFilter(filter);
        currentMode=Chapel::filterName(filter);
        Chapel::seek(times[phase/6]);
        updateUs=esp_timer_get_time()-startUs;
        return;
    }
    if(frame==3600){
        Chapel::setFilter(Chapel::defaultFilter);
        Chapel::controls=ChapelView::Controller{};
    }
#endif
#if CHAPEL_BENCHMARK
    const unsigned phase=frame/150;
    if(phase<24){
        static const float times[]={0,8,15,23,32,42};
        if(CHAPEL_PSRAM_BENCH){
            const bool usePsram=phase%4==1||phase%4==2;
            Chapel::directBacking=true;Chapel::cache.backing=usePsram&&psramBacking?psramBacking:assetStart;
            currentMode=usePsram?(psramBacking?"psram":"unavailable"):"flash";
        }else {Chapel::directBacking=phase%4==1||phase%4==2;currentMode=Chapel::directBacking?"flash":"cached";}
        Chapel::benchmarkSeek(times[phase/4]);
    }else {
        Chapel::directBacking=CHAPEL_PSRAM_BENCH!=0;
        if(CHAPEL_PSRAM_BENCH)Chapel::cache.backing=psramBacking?psramBacking:assetStart;
        Chapel::update(seconds);
    }
#else
    const bool automatic=Chapel::controls.automatic;
    Chapel::update(seconds,ChapelInput::read());
    if(automatic!=Chapel::controls.automatic)
        std::printf("CAMERA: %s at %.2f %.2f %.2f\n",Chapel::controls.automatic?"AUTO":"FPS",Chapel::controls.pose.x,Chapel::controls.pose.y,Chapel::controls.pose.z);
#endif
    updateUs=esp_timer_get_time()-startUs;
}
static void after(float){
    const int64_t work=esp_timer_get_time()-startUs;
#if CHAPEL_PREFILTER_BENCH
    if(frame<1800){
        if(frame%100>=20){totalWork+=work;totalUpdate+=updateUs;}
        if(frame%100==99){
            uint32_t hash=2166136261u;const auto* pixels=Chapel::scene->getRenderer()->getFramebuffer();
            for(unsigned i=0;i<240*160;++i){hash^=pixels[i];hash*=16777619u;}
            unsigned eligible=0;for(const auto& s:Chapel::surfaces)eligible+=s.visible&&s.map==Chapel::prefilterMap&&s.mip==0;
            std::printf("PREFILTER_BENCH phase=%u pose=%u mode=%s n=80 work_us=%lld update_us=%lld hash=%08lx eligible=%u internal=%u psram=%u\n",frame/100,frame/600,currentMode,totalWork,totalUpdate,(unsigned long)hash,eligible,unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)));
            totalWork=totalUpdate=0;
        }
        if(frame==1799)std::puts("PREFILTER_BENCH_DONE");
        ++frame;return;
    }
#endif
#if CHAPEL_FILTER_BENCH
    if(frame<3600){
        if(frame%100>=20){totalWork+=work;totalUpdate+=updateUs;}
        if(frame%100==99){
            uint32_t hash=2166136261u;const auto* pixels=Chapel::scene->getRenderer()->getFramebuffer();
            for(unsigned i=0;i<240*160;++i){hash^=pixels[i];hash*=16777619u;}
            std::printf("FILTER_BENCH phase=%u pose=%u mode=%s n=80 work_us=%lld update_us=%lld hash=%08lx internal=%u psram=%u\n",frame/100,frame/600,currentMode,totalWork,totalUpdate,(unsigned long)hash,unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)));
            totalWork=totalUpdate=0;
        }
        if(frame==3599)std::puts("FILTER_BENCH_DONE");
        ++frame;return;
    }
#endif
#if CHAPEL_BENCHMARK
    if(frame/150<24&&frame%150>=30){totalWork+=work;totalUpdate+=updateUs;totalHits+=Chapel::cache.hotSamples;totalSamples+=Chapel::cache.sampled;totalLoads+=Chapel::cache.loaded;}
    if(frame/150<24&&frame%150==149){
        uint32_t hash=2166136261u;const auto* pixels=Chapel::scene->getRenderer()->getFramebuffer();
        for(unsigned i=0;i<240*160;++i){hash^=pixels[i];hash*=16777619u;}
        std::printf("CHAPEL_BENCH phase=%u pose=%u mode=%s n=120 work_us=%lld update_us=%lld samples=%lld hot=%lld loads=%lld internal=%u\n",frame/150,frame/600,currentMode,totalWork,totalUpdate,totalSamples,totalHits,totalLoads,unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
        std::printf("CHAPEL_HASH phase=%u hash=%08lx\n",frame/150,(unsigned long)hash);
        totalWork=totalUpdate=totalSamples=totalHits=totalLoads=0;
    }
    if(frame==3599)std::puts("CHAPEL_BENCH_DONE");
#else
    totalWork+=work;totalUpdate+=updateUs;totalHits+=Chapel::cache.hotSamples;totalSamples+=Chapel::cache.sampled;totalLoads+=Chapel::cache.loaded;
    if(frame%120==119){
        std::printf("STREAM t=%.1f camera=%s work=%.2f ms plan=%.2f ms sampled_cache=%.1f%% uploads=%.2f KiB/field internal=%u psram=%u\n",Chapel::time,Chapel::controls.automatic?"AUTO":"FPS",totalWork/120000.0,totalUpdate/120000.0,totalSamples?100.*totalHits/totalSamples:0.,totalLoads/120.0,unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)));
#if CHAPEL_HOT_FILTER
        std::printf("HOT t=%.1f hit=%.1f%% sampled=%u used=%u pending=%u built=%u evicted=%u dropped=%u build_us=%u max_us=%u\n",Chapel::time,Chapel::hot.sampled?100.*Chapel::hot.hits/Chapel::hot.sampled:0.,Chapel::hot.sampled,Chapel::hot.used(),unsigned(Chapel::hot.filling()),Chapel::hot.completed,Chapel::hot.evicted,Chapel::hot.dropped,Chapel::hot.lastBuildUs,Chapel::hot.maxBuildUs);
#endif
        totalWork=totalUpdate=totalHits=totalSamples=totalLoads=0;
    }
#endif
    ++frame;
}
extern "C" void app_main(){Esp32Jet::start(init,update,after);}
