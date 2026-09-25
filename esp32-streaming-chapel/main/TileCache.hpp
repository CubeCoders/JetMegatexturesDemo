#pragma once
#include "ChapelAssets.hpp"
#include "TileTexture.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <vector>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

namespace Chapel {
struct TileCache {
    static constexpr unsigned requestedSlots = 48;
    const uint8_t* backing = nullptr;
    uint8_t* pool = nullptr;
    uint8_t* slotMap = nullptr;
    uint16_t* palettes = nullptr;
    uint8_t* feedback[2] = {nullptr,nullptr};
    unsigned capacity = 0;
    std::array<uint8_t*, ChapelAssets::mapCount> coarse{};
    std::array<int, requestedSlots> slotPage{};
    std::array<unsigned, requestedSlots> age{};
    std::vector<uint8_t> wanted;
    std::vector<uint16_t> requests;
    unsigned epoch = 0, loaded = 0, missing = 0, evicted = 0;
    unsigned sampled=0,hotSamples=0;
    bool frozen = false;

    static void* internal(size_t size) {
#ifdef ESP_PLATFORM
        return heap_caps_malloc(size,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
#else
        return std::malloc(size);
#endif
    }
    void init(const uint8_t* data,bool enableCache=true) {
        backing=data;
        if(enableCache)slotMap=static_cast<uint8_t*>(internal(ChapelAssets::pageCount));
        palettes=static_cast<uint16_t*>(internal(ChapelAssets::mapCount*512));
        if(enableCache)for(auto& p:feedback){p=static_cast<uint8_t*>(internal(ChapelAssets::pageCount));if(!p)std::abort();std::memset(p,0,ChapelAssets::pageCount);}
        if(!palettes||(enableCache&&!slotMap))std::abort();
        if(enableCache)std::memset(slotMap,255,ChapelAssets::pageCount);
        for(unsigned i=0;i<ChapelAssets::mapCount;++i) {
            const auto& map=ChapelAssets::maps[i];
            std::memcpy(palettes+i*256,backing+map.paletteOffset,512);
            const auto& level=ChapelAssets::levels[map.firstLevel+map.levelCount-1];
            coarse[i]=static_cast<uint8_t*>(internal(level.width*level.height));
            if(!coarse[i])std::abort();
            std::memcpy(coarse[i],backing+level.offset,level.width*level.height);
        }
        if(!enableCache)return;
        for(capacity=requestedSlots;capacity>=8;capacity-=8) {
#ifdef ESP_PLATFORM
            if(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<capacity*1024+48000)continue;
#endif
            pool=static_cast<uint8_t*>(internal(capacity*1024));if(pool)break;
        }
        if(!pool||capacity<8)std::abort();
        wanted.resize(ChapelAssets::pageCount);
        requests.reserve(ChapelAssets::pageCount);
        slotPage.fill(-1);age.fill(0);
    }
    void beginPlan() {
        assert(!frozen);requests.clear();std::fill(wanted.begin(),wanted.end(),0);
    }
    void request(unsigned page) {
        assert(!frozen&&page<ChapelAssets::pageCount);
        if(!wanted[page]){wanted[page]=1;requests.push_back(uint16_t(page));}
    }
    void planFromFeedback() {
        beginPlan();sampled=hotSamples=0;
        // Keep the pages most frequently sampled in the previous field. A
        // small residency bonus prevents churn when two pages have similar use.
        struct Candidate {unsigned page,score;};
        std::array<Candidate,requestedSlots> top{};unsigned count=0;
        auto less=[](const Candidate& a,const Candidate& b){return a.score>b.score;};
        for(unsigned page=0;page<ChapelAssets::pageCount;++page) {
            const unsigned hits=unsigned(feedback[0][page])+feedback[1][page];
            sampled+=hits;if(slotMap[page]!=255)hotSamples+=hits;
            feedback[0][page]=feedback[1][page]=0;if(!hits)continue;
            const Candidate item{page,hits+(slotMap[page]!=255?2u:0u)};
            if(count<capacity){top[count++]=item;std::push_heap(top.begin(),top.begin()+count,less);}
            else if(item.score>top[0].score){std::pop_heap(top.begin(),top.begin()+count,less);top[count-1]=item;std::push_heap(top.begin(),top.begin()+count,less);}
        }
        std::sort(top.begin(),top.begin()+count,less);
        for(unsigned i=0;i<count;++i)request(top[i].page);
    }
    void load(unsigned budget) {
        assert(!frozen&&requests.size()<=capacity);
        ++epoch;loaded=missing=evicted=0;
        for(unsigned page:requests)if(slotMap[page]!=255)age[slotMap[page]]=epoch;
        for(unsigned page:requests) {
            if(slotMap[page]!=255)continue;
            if(loaded>=budget){++missing;continue;}
            int selected=-1;
            for(unsigned slot=0;slot<capacity;++slot) {
                if(slotPage[slot]<0){selected=int(slot);break;}
                if(!wanted[slotPage[slot]]&&(selected<0||age[slot]<age[selected]))selected=int(slot);
            }
            assert(selected>=0);
            if(slotPage[selected]>=0){slotMap[slotPage[selected]]=255;++evicted;}
            std::memcpy(pool+selected*1024,backing+ChapelAssets::pageOffsets[page],1024);
            slotPage[selected]=int(page);age[selected]=epoch;slotMap[page]=uint8_t(selected);++loaded;
        }
        frozen=true;
    }
    void finishRender(){frozen=false;}
    Renderer::TileTexture view(unsigned material,unsigned mip,bool direct) const {
        const auto& map=ChapelAssets::maps[material];
        const auto& level=ChapelAssets::levels[map.firstLevel+mip];
        const auto& fallback=ChapelAssets::levels[map.firstLevel+map.levelCount-1];
        Renderer::TileTexture result;
        result.backing=backing+level.offset;result.cache=pool;
        result.slots=level.firstPage>=0&&slotMap?slotMap+level.firstPage:nullptr;
        result.coarse=coarse[material];result.palette=palettes+material*256;
        if(level.firstPage>=0&&!direct&&pool)for(unsigned i=0;i<2;++i)result.feedback[i]=feedback[i]+level.firstPage;
        result.width=level.width;result.height=level.height;result.pagesX=level.pagesX;
        result.coarseWidth=fallback.width;result.coarseHeight=fallback.height;
        result.direct=direct||!pool;result.coarseOnly=level.firstPage<0;
        return result;
    }
    ~TileCache(){std::free(pool);std::free(slotMap);std::free(palettes);for(auto p:coarse)std::free(p);for(auto p:feedback)std::free(p);}
};
}
