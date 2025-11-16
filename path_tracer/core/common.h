#pragma once
#include <Eigen/Dense>

#define EPS 1e-4f
#define EPS_ANYHIT 5e-4f
#define EPS_SMALL 1e-8f

#define M_1_2PI 0.159154943091895335769 // 1/2 pi

template<int N, typename T> using Vec = Eigen::Matrix<T, N, 1>;

template<class T> using Vec2 = Vec<2, T>;
template<class T> using Vec3 = Vec<3, T>;
template<class T> using Vec4 = Vec<4, T>;

using Vec2f = Vec2<float>;
using Vec3f = Vec3<float>;
using Vec4f = Vec4<float>;

using Vec2i = Vec2<int>;
using Vec3i = Vec3<int>;

template<typename T, int M, int N> using Mat = Eigen::Matrix<T, M, N>;

template<class T> using Mat2 = Mat<T, 2, 2>;
template<class T> using Mat3 = Mat<T, 3, 3>;
template<class T> using Mat4 = Mat<T, 4, 4>;

using Mat2f = Mat2<float>;
using Mat3f = Mat3<float>;
using Mat4f = Mat4<float>;

/// @brief Orthonormal Basis: https://graphics.pixar.com/library/OrthonormalB/paper.pdf
/// @param v 
/// @return 
inline std::tuple<Vec3f, Vec3f> coordinate_system(const Vec3f& v){
    float sign = copysignf(1.0f, v.z());
    const float a = -1.0f / (sign + v.z());
    const float b = v.x() * v.y() * a;

    return {
        Vec3f(1.0f + sign * v.x() * v.x() * a, sign * b, -sign * v.x()),
        Vec3f(b, sign + v.y() * v.y() * a, -v.y())
    };
}

inline Vec3f local_to_world(const Vec3f& v, const Vec3f& n){
    auto [x, y] = coordinate_system(n);
    return v.x() * x + v.y() * y + v.z() * n;
}

inline Vec3f world_to_local(const Vec3f& v, const Vec3f& n){
    auto [x, y] = coordinate_system(n);
    return Vec3f(v.dot(x), v.dot(y), v.dot(n));
}