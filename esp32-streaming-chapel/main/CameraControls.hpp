#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ChapelView {
constexpr float pi = 3.14159265358979323846f;
constexpr float tourDuration = 90.f;
enum Button : uint8_t { Left=1, Forward=2, Backward=4, Right=8, Fast=16, Look=32, Height=64 };
constexpr uint8_t buttonMask = 127;
struct Pose { float x, y, z, yaw, pitch, fov; };

inline float angleDelta(float a, float b) {
    float delta = std::fmod(b-a, 360.f);
    if (delta > 180.f) delta -= 360.f;
    if (delta < -180.f) delta += 360.f;
    return delta;
}
inline Pose mix(const Pose& a, const Pose& b, float f) {
    const auto lerp = [f](float x, float y) { return x+(y-x)*f; };
    return {lerp(a.x,b.x),lerp(a.y,b.y),lerp(a.z,b.z),
            a.yaw+angleDelta(a.yaw,b.yaw)*f,lerp(a.pitch,b.pitch),lerp(a.fov,b.fov)};
}
inline Pose tour(float seconds) {
    struct Key { float seconds; Pose pose; };
    // Slow lateral/vertical passes put the highest window mips on screen.
    // The camera stays inside the wall planes, including the recessed glass.
    static constexpr Key keys[] = {
        {0,  {0,2.2f,-12.5f,0,-7,82}},
        {8,  {0,2.4f,-6,0,-7,78}},
        {15, {-3.6f,2.9f,-7,-90,-3,72}},
        {20, {-4.65f,2.7f,-7.45f,-90,-6,70}},
        {28, {-4.65f,3.6f,-6.65f,-90,0,70}},
        {35, {-2,3,-2,-20,-10,78}},
        {43, {-1.2f,4.5f,.8f,14,-12,72}},
        {51, {0,5.5f,2,0,-4,68}},
        {59, {1.2f,4.3f,1.3f,-28,-4,72}},
        {65, {4.65f,3.2f,-3.65f,90,-4,70}},
        {73, {4.65f,2.7f,-4.45f,90,-6,70}},
        {81, {1.5f,2.5f,-8,170,0,80}},
        {90, {0,2.2f,-12.5f,360,-7,82}}
    };
    const float t = std::fmod(std::max(0.f,seconds),tourDuration);
    for (unsigned i=1;i<sizeof(keys)/sizeof(keys[0]);++i) {
        if (t <= keys[i].seconds) {
            float f=(t-keys[i-1].seconds)/(keys[i].seconds-keys[i-1].seconds);
            f=f*f*(3.f-2.f*f);
            return mix(keys[i-1].pose,keys[i].pose,f);
        }
    }
    return keys[0].pose;
}

// Require two consecutive reads so a switch bounce does not change modes.
class Debouncer {
    uint8_t candidate=0, stable=0;
public:
    uint8_t read(uint8_t raw) {
        raw &= buttonMask;
        if (raw == candidate) stable=raw;
        else candidate=raw;
        return stable;
    }
};

class Controller {
    bool releaseLatch=false;
    float returnElapsed=1.2f;
    Pose returnFrom=tour(0);
public:
    bool automatic=true;
    float tourTime=0;
    Pose pose=tour(0);

    void step(float seconds, uint8_t buttons) {
        const float dt=std::clamp(seconds,0.f,.05f);
        buttons &= buttonMask;
        if ((buttons & (Forward|Backward)) == (Forward|Backward)) {
            if (!automatic) { returnFrom=pose; returnElapsed=0; }
            automatic=true;
            releaseLatch=true;
        }
        // Consume the entire return chord, including staggered releases.
        if (releaseLatch) {
            if (!buttons) releaseLatch=false;
            buttons=0;
        }
        if (automatic && buttons) automatic=false;
        if (automatic) {
            tourTime=std::fmod(tourTime+dt,tourDuration);
            returnElapsed=std::min(1.2f,returnElapsed+dt);
            float f=returnElapsed/1.2f; f=f*f*(3.f-2.f*f);
            pose=mix(returnFrom,tour(tourTime),f);
            return;
        }

        const float horizontal=float(bool(buttons&Right))-float(bool(buttons&Left));
        const float vertical=float(bool(buttons&Forward))-float(bool(buttons&Backward));
        float forward=0,side=0,up=0;
        if (buttons&Height) { side=horizontal; up=vertical; }
        else if (buttons&Look) {
            side=horizontal;
            pose.pitch=std::clamp(pose.pitch-vertical*60.f*dt,-78.f,78.f);
        } else {
            pose.yaw=std::remainder(pose.yaw+horizontal*84.f*dt,360.f);
            forward=vertical;
        }
        const float speed=(buttons&Fast)?4.8f:1.6f;
        const float length=std::sqrt(forward*forward+side*side+up*up);
        if (length>0) {
            const float amount=speed*dt/std::max(1.f,length);
            const float yaw=pose.yaw*pi/180.f;
            pose.x+=(std::sin(yaw)*forward+std::cos(yaw)*side)*amount;
            pose.z+=(std::cos(yaw)*forward-std::sin(yaw)*side)*amount;
            pose.y+=up*amount;
            // Inspection-camera bounds, rather than a physics/collision system.
            // The entry narrows beyond z=-13; do not move through its side wall.
            pose.x=std::clamp(pose.x,-4.7f,4.7f);
            pose.z=std::clamp(pose.z,-14.6f,2.65f);
            if (pose.z < -12.75f && std::abs(pose.x)>.75f) pose.z=-12.75f;
            const float floor=std::clamp((pose.z+3.f)*.5f,0.f,1.f);
            pose.y=std::clamp(pose.y,floor+.3f,6.2f);
        }
    }
};
}
