#pragma once
#include "common.h"
#include "ray.h"
#include <tuple>

struct Triangle {
    Vec3f v[3];
    Vec3f normal;
    Vec3f emission;
    int material_id;

    inline const Vec3f& v0() const { return v[0]; }
    inline const Vec3f& v1() const { return v[1]; }
    inline const Vec3f& v2() const { return v[2]; }

    inline Vec3f& v0() { return v[0]; }
    inline Vec3f& v1() { return v[1]; }
    inline Vec3f& v2() { return v[2]; }

    inline float area() const {
        return 0.5f * (v1() - v0()).cross(v2() - v0()).norm();
    }

    inline bool is_emitter() const {
        return !emission.isZero(EPS_SMALL);
    }

    /// @brief Performs a watertight ray-triangle intersection.
    /// @param tri Triangle to test.
    /// @param ray Ray in world space.
    /// @param t_min Minimum accepted ray distance.
    /// @param t_max Maximum accepted ray distance.
    /// @return Hit flag and distance along the ray.
    /// @see http://jcgt.org/published/0002/01/05/paper.pdf
    static std::tuple<bool, float> ray_triangle_intersect(const Triangle& tri, const Ray& ray, float t_min, float t_max){
        const Vec3f abs_direction = ray.direction.cwiseAbs();
        unsigned int axis = 0;

        if (abs_direction[1] > abs_direction[0] &&
            abs_direction[1] > abs_direction[2])
            axis = 1;
        if (abs_direction[2] > abs_direction[0] &&
            abs_direction[2] > abs_direction[1])
            axis = 2;

        unsigned int kz = axis;
        unsigned int kx = (kz + 1) % 3;
        unsigned int ky = (kx + 1) % 3;
        if (ray.direction[kz] < 0.0f) {
            unsigned int swap = kx;
            kx = ky;
            ky = swap;
        }

        float Sx = ray.direction[kx] / ray.direction[kz];
        float Sy = ray.direction[ky] / ray.direction[kz];
        float Sz = 1.f / ray.direction[kz];

        const Vec3f A = tri.v[0] - ray.origin;
        const Vec3f B = tri.v[1] - ray.origin;
        const Vec3f C = tri.v[2] - ray.origin;

        const float Ax = A[kx] - Sx * A[kz];
        const float Ay = A[ky] - Sy * A[kz];
        const float Bx = B[kx] - Sx * B[kz];
        const float By = B[ky] - Sy * B[kz];
        const float Cx = C[kx] - Sx * C[kz];
        const float Cy = C[ky] - Sy * C[kz];

        float U = Cx * By - Cy * Bx;
        float V = Ax * Cy - Ay * Cx;
        float W = Bx * Ay - By * Ax;

        if (U == 0.f || V == 0.f || W == 0.f) {
            double CxBy = static_cast<double>(Cx) * static_cast<double>(By);
            double CyBx = static_cast<double>(Cy) * static_cast<double>(Bx);
            U = (float)(CxBy - CyBx);
            double AxCy = static_cast<double>(Ax) * static_cast<double>(Cy);
            double AyCx = static_cast<double>(Ay) * static_cast<double>(Cx);
            V = (float)(AxCy - AyCx);
            double BxAy = static_cast<double>(Bx) * static_cast<double>(Ay);
            double ByAx = static_cast<double>(By) * static_cast<double>(Ax);
            W = (float)(BxAy - ByAx);
        }

        if ((U < 0.f || V < 0.f || W < 0.f) && (U > 0.f || V > 0.f || W > 0.f))
            return {false, 0.0f};

        float det = U + V + W;
        if (det == 0.f) return {false, 0.0f};

        const float Az = Sz * A[kz];
        const float Bz = Sz * B[kz];
        const float Cz = Sz * C[kz];
        const float T = U * Az + V * Bz + W * Cz;
        const float rcp_det = 1.f / det;

        const float t = T * rcp_det;
        const Vec3f barycentrics = Vec3f(U, V, W) * rcp_det;

        if (t < t_min || t > t_max) return {false, 0.0f};

        return {true, t};
    }
};
