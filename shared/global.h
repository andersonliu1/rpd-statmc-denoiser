#pragma once
#include <Eigen/Dense>

#define EPS_SMALL 1e-8f

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

template <int N, typename T>
inline Vec<N, T> clamp(const Vec<N, T>& v, const Vec<N, T>& min_val, const Vec<N, T>& max_val) {
    return v.cwiseMin(max_val).cwiseMax(min_val);
}

template <int N, typename T>
inline Vec<N, T> clamp(const Vec<N, T>& v, const T min_val, const T max_val) {
    return v.cwiseMin(max_val).cwiseMax(min_val);
}