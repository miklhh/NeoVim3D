#include "camera.hpp"

#include <algorithm>

namespace {
constexpr float kMaxPitch = 1.55f;

float approach(float cur, float goal, float t) { return cur + (goal - cur) * t; }
}  // namespace

bool Camera::moving() const {
    return std::abs(current.yaw - goal.yaw) > 1e-4f || std::abs(current.pitch - goal.pitch) > 1e-4f ||
           std::abs(current.roll - goal.roll) > 1e-4f ||
           std::abs(current.distance - goal.distance) > 1e-3f ||
           length(current.target - goal.target) > 1e-3f;
}

bool Camera::update(float dt) {
    goal.pitch = std::clamp(goal.pitch, -kMaxPitch, kMaxPitch);
    goal.distance = std::clamp(goal.distance, 0.5f, 5000.0f);
    float t = smoothing <= 0 ? 1.0f : 1.0f - std::exp(-smoothing * dt);
    current.target.x = approach(current.target.x, goal.target.x, t);
    current.target.y = approach(current.target.y, goal.target.y, t);
    current.target.z = approach(current.target.z, goal.target.z, t);
    current.yaw = approach(current.yaw, goal.yaw, t);
    current.pitch = approach(current.pitch, goal.pitch, t);
    current.roll = approach(current.roll, goal.roll, t);
    current.distance = approach(current.distance, goal.distance, t);

    if (moving()) return true;
    current = goal;
    return false;
}

Vec3 Camera::forward() const {
    // Looking down -Z when yaw = pitch = 0.
    float cp = std::cos(current.pitch), sp = std::sin(current.pitch);
    float cy = std::cos(current.yaw), sy = std::sin(current.yaw);
    return normalize(Vec3(-sy * cp, -sp, -cy * cp));
}

Vec3 Camera::right() const {
    Vec3 r = normalize(cross(forward(), Vec3(0, 1, 0)));
    // Apply roll by rotating right/up around forward.
    Vec3 u = cross(r, forward());
    float cr = std::cos(current.roll), sr = std::sin(current.roll);
    return r * cr + u * sr;
}

Vec3 Camera::up() const { return cross(right(), forward()); }

Vec3 Camera::eye() const { return current.target - forward() * current.distance; }

Mat4 Camera::view() const { return Mat4::look_at(eye(), current.target, up()); }

Mat4 Camera::projection(float aspect) const {
    float znear = std::max(0.05f, current.distance * 0.01f);
    return Mat4::perspective(fov, aspect, znear, current.distance * 20 + 500);
}

void Camera::orbit(float dyaw, float dpitch) {
    goal.yaw += dyaw;
    goal.pitch = std::clamp(goal.pitch + dpitch, -kMaxPitch, kMaxPitch);
}

void Camera::pan(float dx, float dy) {
    // Use the goal orientation so panning stays consistent while animating.
    CameraState saved = current;
    current = goal;
    Vec3 r = right(), u = up();
    current = saved;
    goal.target += r * dx + u * dy;
}

void Camera::zoom(float factor) { goal.distance = std::clamp(goal.distance * factor, 0.5f, 5000.0f); }

void Camera::dolly(float amount) {
    CameraState saved = current;
    current = goal;
    Vec3 f = forward();
    current = saved;
    goal.target += f * amount;
}

void Camera::ray(float ndc_x, float ndc_y, float aspect, Vec3& origin, Vec3& dir) const {
    Mat4 inv = (projection(aspect) * view()).inverse();
    Vec4 n = inv * Vec4{ndc_x, ndc_y, -1, 1};
    Vec4 f = inv * Vec4{ndc_x, ndc_y, 1, 1};
    Vec3 pn(n.x / n.w, n.y / n.w, n.z / n.w);
    Vec3 pf(f.x / f.w, f.y / f.w, f.z / f.w);
    origin = pn;
    dir = normalize(pf - pn);
}
