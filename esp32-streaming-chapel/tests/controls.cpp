#include "CameraControls.hpp"
#include <cassert>
#include <cstdio>
using namespace ChapelView;
static bool close(float a,float b,float epsilon=.0001f){return std::abs(a-b)<epsilon;}
static void same(const Pose& a,const Pose& b){
    assert(close(a.x,b.x)&&close(a.y,b.y)&&close(a.z,b.z));
    assert(close(angleDelta(a.yaw,b.yaw),0)&&close(a.pitch,b.pitch)&&close(a.fov,b.fov));
}
int main(){
    Controller c;
    assert(c.automatic);same(c.pose,tour(0));
    for(int i=0;i<600;++i)c.step(1.f/60,0);
    assert(c.automatic);same(c.pose,tour(c.tourTime));
    const auto start=c.pose;const float paused=c.tourTime;
    c.step(0,Forward);assert(!c.automatic);same(c.pose,start);
    c.step(.05f,Forward);assert(c.pose.z>start.z);assert(close(c.tourTime,paused));
    const auto idle=c.pose;c.step(.05f,0);same(c.pose,idle);assert(!c.automatic);
    c.step(0,Forward|Backward);assert(c.automatic);same(c.pose,idle);
    for(int i=0;i<50;++i)c.step(.05f,Forward|Backward);
    assert(c.automatic);same(c.pose,tour(c.tourTime));
    c.step(.05f,Forward);assert(c.automatic);
    c.step(.05f,Forward|Fast);assert(c.automatic);
    c.step(.05f,0);assert(c.automatic);
    const auto resumed=c.pose;c.step(0,Look);assert(!c.automatic);same(c.pose,resumed);
    c.pose={0,3,-7,0,0,70};
    c.step(.05f,Look|Right);assert(close(c.pose.x,.08f)&&close(c.pose.z,-7));
    c.pose={0,3,-7,90,0,70};
    c.step(.05f,Forward);assert(close(c.pose.x,.08f)&&close(c.pose.z,-7));
    c.pose={0,3,-7,0,0,70};c.step(.05f,Fast|Forward);assert(close(c.pose.z,-6.76f));
    c.pose={0,3,-7,0,0,70};c.step(10,Forward);assert(close(c.pose.z,-6.92f));
    c.step(.05f,Height|Forward);assert(close(c.pose.y,3.08f));
    for(int i=0;i<100;++i)c.step(.05f,Look|Forward);
    assert(close(c.pose.pitch,-78));
    for(int i=0;i<200;++i)c.step(.05f,Look|Backward);
    assert(close(c.pose.pitch,78));
    c.pose={4.6f,3,-12.7f,0,0,70};
    for(int i=0;i<200;++i)c.step(.05f,Backward|Fast);
    assert(close(c.pose.z,-12.75f));
    // Taking over partway through the return blend must also preserve the pose.
    c.step(.05f,Forward|Backward);c.step(.05f,0);const auto blend=c.pose;
    c.step(0,Fast);assert(!c.automatic);same(c.pose,blend);
    // Either staggered chord release remains automatic.
    c.step(0,Forward|Backward);c.step(.05f,Backward);assert(c.automatic);
    c.step(.05f,0);c.step(.05f,Backward);assert(!c.automatic);
    Debouncer d;assert(d.read(0)==0);assert(d.read(Forward)==0);
    assert(d.read(0)==0);assert(d.read(Forward)==0);assert(d.read(Forward)==Forward);
    assert(d.read(Forward|Backward)==Forward);assert(d.read(Forward|Backward)==(Forward|Backward));
    assert(d.read(0)==(Forward|Backward));assert(d.read(0)==0);assert(d.read(128)==0);
    same(tour(0),tour(tourDuration));
    for(int i=0;i<9000;++i){
        const auto p=tour(i*.01f);assert(std::isfinite(p.yaw));
        assert(p.x>=-4.7f&&p.x<=4.7f&&p.z>=-12.5f&&p.z<=2.65f);
        const auto q=tour((i+1)*.01f);
        assert(std::abs(q.x-p.x)<.05f&&std::abs(q.z-p.z)<.05f);
        assert(std::abs(angleDelta(p.yaw,q.yaw))<1.f);
    }
    std::puts("PASS: input takeover, return chord/release latch, smooth return, movement, bounds, debounce, tour continuity");
}
