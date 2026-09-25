#pragma once
#include "TileCache.hpp"
#include <chrono>
#include <new>
#ifdef ESP_PLATFORM
#include "esp_timer.h"
#endif

namespace Chapel {
// One published slot contains exact bilinear samples for a 64x64 region of
// the existing 10-bit UV grid. Slots never move or change during raster work.
class HotFilterCache {
    static constexpr unsigned levels=sizeof(ChapelAssets::levels)/sizeof(ChapelAssets::levels[0]);
    static constexpr unsigned keysCount=levels*256,slotsCount=128;
    static_assert(keysCount<65535);
    struct Request {uint16_t key,count;};
    uint16_t* pool=nullptr;
    uint8_t* lookup=nullptr;
    Renderer::TileFeedback* lanes[2]={nullptr,nullptr};
    Request* requests=nullptr;
    unsigned requestCount=0;
    std::array<uint16_t,slotsCount> keys{},scores{};
    std::array<unsigned,slotsCount> lastSeen{};
    std::array<uint8_t,levels> material{},mip{};
    int pending=-1;
    unsigned pendingRow=0,epoch=0;
    Renderer::TileTexture pendingSource;
    static void* external(size_t bytes){
#ifdef ESP_PLATFORM
        return heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
        return std::malloc(bytes);
#endif
    }
    static int64_t now(){
#ifdef ESP_PLATFORM
        return esp_timer_get_time();
#else
        return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
    }
    void collect(){
        requestCount=0;sampled=hits=dropped=0;scores.fill(0);
        for(auto* lane:lanes){
            sampled+=lane->sampled;hits+=lane->hits;dropped+=lane->dropped;
            for(const auto& item:lane->entries)if(item.key!=65535)requests[requestCount++]={item.key,item.count};
            lane->clear();
        }
        std::sort(requests,requests+requestCount,[](auto a,auto b){return a.key<b.key;});
        unsigned count=0;
        for(unsigned i=0;i<requestCount;++i){
            if(count&&requests[count-1].key==requests[i].key)
                requests[count-1].count=uint16_t(std::min(65535u,unsigned(requests[count-1].count)+requests[i].count));
            else requests[count++]=requests[i];
        }
        requestCount=count;
        for(unsigned i=0;i<count;++i){
            const auto r=requests[i];const unsigned slot=lookup[r.key];
            if(slot!=255){lastSeen[slot]=epoch;scores[slot]=r.count;}
        }
        std::sort(requests,requests+count,[](auto a,auto b){return a.count!=b.count?a.count>b.count:a.key<b.key;});
    }
    bool begin(TileCache& source){
        for(unsigned i=0;i<requestCount;++i){
            const auto r=requests[i];if(lookup[r.key]!=255)continue;
            int slot=-1;
            for(unsigned j=0;j<slotsCount;++j)if(keys[j]==65535){slot=int(j);break;}
            if(slot<0){
                unsigned weakest=0;
                for(unsigned j=1;j<slotsCount;++j){
                    const bool stale=epoch-lastSeen[j]>12,oldStale=epoch-lastSeen[weakest]>12;
                    if((stale&&!oldStale)||(stale==oldStale&&(scores[j]<scores[weakest]||(scores[j]==scores[weakest]&&lastSeen[j]<lastSeen[weakest]))))weakest=j;
                }
                if(epoch-lastSeen[weakest]<=12&&r.count<=unsigned(scores[weakest])+std::max(2u,unsigned(scores[weakest])/4))return false;
                slot=int(weakest);lookup[keys[slot]]=255;++evicted;
            }
            // The old mapping is gone; partially filled slots remain hidden.
            pending=slot;pendingRow=0;keys[slot]=r.key;scores[slot]=r.count;
            const unsigned level=r.key>>8;
            pendingSource=source.view(material[level],mip[level],true);
            return true;
        }
        return false;
    }
public:
    bool enabled=false;
    unsigned sampled=0,hits=0,dropped=0,completed=0,evicted=0;
    unsigned lastBuildUs=0,maxBuildUs=0,rowsThisField=0;
    unsigned budgetUs=1000,rowLimit=64;
    bool init(){
        if(enabled)return true;
        pool=static_cast<uint16_t*>(external(slotsCount*4096*2));
        lookup=static_cast<uint8_t*>(external(keysCount));
        requests=static_cast<Request*>(external(2*Renderer::TileFeedback::capacity*sizeof(Request)));
        for(auto& lane:lanes){auto* memory=TileCache::internal(sizeof(Renderer::TileFeedback));if(memory)lane=new(memory) Renderer::TileFeedback;}
        if(!pool||!lookup||!requests||!lanes[0]||!lanes[1]){release();return false;}
        std::memset(lookup,255,keysCount);keys.fill(65535);scores.fill(0);lastSeen.fill(0);
        for(auto* lane:lanes)lane->clear();
        for(unsigned map=0;map<ChapelAssets::mapCount;++map)
            for(unsigned m=0;m<ChapelAssets::maps[map].levelCount;++m){
                const unsigned level=ChapelAssets::maps[map].firstLevel+m;
                material[level]=uint8_t(map);mip[level]=uint8_t(m);
            }
        enabled=true;return true;
    }
    void tick(TileCache& source){
        rowsThisField=lastBuildUs=0;if(!enabled)return;
        const int64_t start=now();++epoch;
        if(epoch%4==0)collect();
        if(pending<0)begin(source);
        while(pending>=0&&rowsThisField<rowLimit&&(!budgetUs||now()-start<int64_t(budgetUs))){
            const unsigned page=keys[pending]&255,u0=(page&15)*64,v0=(page>>4)*64;
            auto* out=pool+unsigned(pending)*4096+pendingRow*64;
            for(unsigned x=0;x<64;++x)out[x]=pendingSource.sampleBilinear(int(u0+x),int(v0+pendingRow));
            ++pendingRow;++rowsThisField;
            if(pendingRow==64){
                lookup[keys[pending]]=uint8_t(pending);lastSeen[pending]=epoch;
                pending=-1;++completed;break;
            }
        }
        lastBuildUs=unsigned(now()-start);maxBuildUs=std::max(maxBuildUs,lastBuildUs);
    }
    void bind(Renderer::TileTexture& view,unsigned map,unsigned m) const {
        if(!enabled)return;
        const unsigned level=ChapelAssets::maps[map].firstLevel+m;
        view.hotPool=pool;view.hotSlots=lookup+level*256;view.hotKeyBase=uint16_t(level*256);
        view.hotFeedback[0]=lanes[0];view.hotFeedback[1]=lanes[1];
    }
    unsigned used()const {return unsigned(std::count_if(keys.begin(),keys.end(),[](auto k){return k!=65535;}))-(pending>=0?1u:0u);}
    bool filling()const{return pending>=0;}
    size_t allocatedExternal()const{return enabled?slotsCount*4096*2+keysCount+2*Renderer::TileFeedback::capacity*sizeof(Request):0;}
    size_t allocatedInternal()const{return enabled?2*sizeof(Renderer::TileFeedback):0;}
    void release(){
        std::free(pool);std::free(lookup);std::free(requests);pool=nullptr;lookup=nullptr;requests=nullptr;
        for(auto& lane:lanes){std::free(lane);lane=nullptr;}enabled=false;pending=-1;requestCount=0;
    }
    ~HotFilterCache(){release();}
};
}
