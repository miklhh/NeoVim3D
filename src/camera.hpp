// Orbit camera with smooth interpolation towards a goal state.
#pragma once

#include "math3d.hpp"

struct CameraState {
    Vec3 target;          // point the camera looks at
    float yaw = 0;        // radians, rotation around the world Y axis
    float pitch = 0;      // radians, rotation around the camera X axis
    float roll = 0;       // radians
    float distance = 10;  // distance from target
};

class Camera {
public:
    CameraState goal, current;
    float fov = 0.6f;          // vertical field of view, radians
    float smoothing = 12.0f;   // larger = snappier; 0 = no smoothing

    // Advance the interpolation; returns true while still moving.
    bool update(float dt);
    void snap() { current = goal; }
    bool moving() const;

    void orbit(float dyaw, float dpitch);
    void pan(float dx, float dy);  // in world units, camera-relative
    void zoom(float factor);       // multiplies distance
    void dolly(float amount);      // moves the target along the view direction

    Vec3 eye() const;
    Mat4 view() const;
    Mat4 projection(float aspect) const;
    // World space ray through normalized device coordinates (x,y in [-1,1]).
    void ray(float ndc_x, float ndc_y, float aspect, Vec3& origin, Vec3& dir) const;

private:
    Vec3 forward() const;
    Vec3 right() const;
    Vec3 up() const;
};
