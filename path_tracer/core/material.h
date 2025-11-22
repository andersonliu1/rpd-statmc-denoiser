#pragma once

#include "common.h"
#include <algorithm>
#include <cmath>
#include <tuple>
#include <variant>

struct Lambertian {
    Vec3f albedo;

    Vec3f eval() const { 
        return albedo / M_PI;
     }

    Vec3f eval(Vec3f, Vec3f, Vec3f) const {
        return eval();
    }

    /// @brief Sample BRDF for the Lambertian
    /// @param normal The surface normal
    /// @param u A random number (0,1)^2
    /// @return A tuple containing the sampled direction in world space and the PDF
    std::tuple<Vec3f, float> sample(Vec3f normal, Vec2f u) const {
        float phi = u.y() * 2 * M_PI;
        float r = sqrt(1 - u.x() * u.x());
        
        Vec3f out(r * cos(phi), r * sin(phi), u.x());
        
        return {local_to_world(out, normal), M_1_2PI};
    }

    std::tuple<Vec3f, float> sample(Vec3f, Vec3f normal, Vec2f u) const {
        return sample(normal, u);
    }

    /// @brief Computes the PDF for the Lambertian
    /// @param wo_world The outgoing direction
    /// @param wi_world The light incident direction
    /// @param normal The surface normal (unused for Lambertian)
    /// @return The PDF value
    float pdf(Vec3f wo_world, Vec3f wi_world, Vec3f normal) const {
        return M_1_2PI;
    }

};

struct Microfacet {
    enum class Distribution { Beckmann, GGX };

    Vec3f albedo;
    float roughness;

    Vec3f n1, n2;
    Distribution distribution = Distribution::GGX;

    /// @brief Compute the Fresnel term for the microfacet
    /// @param cos_theta Incident direction dot half vector
    /// @return Fresnel reflectance
    Vec3f F(float cos_theta) const {
        cos_theta = std::clamp(cos_theta, 0.0f, 1.0f);
        Vec3f r0 = (n1 - n2).cwiseProduct(n1 - n2).cwiseQuotient((n1 + n2).cwiseProduct(n1 + n2));
        float factor = std::pow(1.0f - cos_theta, 5.0f);
        return r0 + (Vec3f::Ones() - r0) * factor;
    }

    /// @brief Beckmann normal distribution
    /// @param h Local half-vector
    /// @return NDF value
    float D(const Vec3f& h) const {
        Vec3f m = h.normalized();
        float cos_theta = std::clamp(m.z(), EPS, 1.0f);
        float sin_theta2 = std::max(0.0f, 1.0f - cos_theta * cos_theta);
        float tan_theta2 = sin_theta2 / (cos_theta * cos_theta);
        float alpha = std::max(roughness, EPS);

        if (distribution == Distribution::Beckmann) {
            float roughness_square = alpha * alpha;
            return std::exp(-tan_theta2 / roughness_square) / (M_PI * roughness_square * std::pow(cos_theta, 4.0f));
        }

        float denom = cos_theta * cos_theta * (alpha * alpha - 1.0f) + 1.0f;
        return (alpha * alpha) / (M_PI * denom * denom);
    }

    /// @brief Smith shadowing/masking term
    /// @param wo Local outgoing direction
    /// @param wi Local incoming direction
    /// @return Shadowing-masking factor
    float G(const Vec3f& wo, const Vec3f& wi) const {
        auto smith = [&](const Vec3f& v) {
            float cos_theta = std::clamp(v.z(), EPS, 1.0f);
            float sin_theta2 = std::max(0.0f, 1.0f - cos_theta * cos_theta);
            if (sin_theta2 <= EPS_SMALL) return 1.0f;

            float tan_theta = std::sqrt(sin_theta2) / cos_theta;
            float alpha = std::max(roughness, EPS);

            if (distribution == Distribution::Beckmann) {
                float a = 1.0f / (alpha * tan_theta);
                if (a >= 1.6f) return 1.0f;
                return (3.535f * a + 2.181f * a * a) / (1.0f + 2.276f * a + 2.577f * a * a);
            }

            float lambda = (-1.0f + std::sqrt(1.0f + alpha * alpha * tan_theta * tan_theta)) * 0.5f;
            return 1.0f / (1.0f + lambda);
        };

        return smith(wo) * smith(wi);
    }
 
    Vec3f eval(Vec3f wo_world, Vec3f wi_world, Vec3f normal) const {
        Vec3f wo = world_to_local(wo_world, normal).normalized();
        Vec3f wi = world_to_local(wi_world, normal).normalized();

        if (wo.z() <= EPS_SMALL || wi.z() <= EPS_SMALL) return Vec3f::Zero();

        Vec3f h = (wo + wi).normalized();
        if (!std::isfinite(h.x()) || !std::isfinite(h.y()) || !std::isfinite(h.z())) return Vec3f::Zero();

        float cos_o = std::clamp(wo.z(), 0.0f, 1.0f);
        float cos_i = std::clamp(wi.z(), 0.0f, 1.0f);
        
        Vec3f fr = F(std::fabs(wi.dot(h))) * G(wo, wi) * D(h) / (4.0f * cos_i * cos_o);
        return fr.cwiseProduct(albedo);
    }

    float pdf(Vec3f wo_world, Vec3f wi_world, Vec3f normal) const {
        Vec3f wo = world_to_local(wo_world, normal);
        Vec3f wi = world_to_local(wi_world, normal);

        if (wo.z() <= EPS_SMALL || wi.z() <= EPS_SMALL) return 0.0f;

        Vec3f h = (wo + wi).normalized();
        if (!std::isfinite(h.x()) || !std::isfinite(h.y()) || !std::isfinite(h.z())) return 0.0f;

        float ndoth = std::max(0.0f, h.z());
        float wodoth = std::max(0.0f, wo.dot(h));

        if (ndoth <= EPS_SMALL || wodoth <= EPS_SMALL) return 0.0f;

        return D(h) * ndoth / (4.0f * wodoth);
    }

    std::tuple<Vec3f, float> sample(Vec3f wo_world, Vec3f normal, Vec2f u) const {
        Vec3f wo = world_to_local(wo_world, normal).normalized();
        if (wo.z() <= EPS_SMALL) return {Vec3f::Zero(), 0.0f};

        float u1 = std::clamp(u.x(), EPS_SMALL, 1.0f - EPS_SMALL);
        float u2 = std::clamp(u.y(), EPS_SMALL, 1.0f - EPS_SMALL);

        float alpha = std::max(roughness, EPS);
        Vec3f wh;

        if (distribution == Distribution::Beckmann) {
            float r = alpha * std::sqrt(-std::log(1.0f - u1));
            float phi = 2.0f * M_PI * u2;
            float slope_x = r * std::cos(phi);
            float slope_y = r * std::sin(phi);
            wh = Vec3f(-slope_x, -slope_y, 1.0f).normalized();
        } else {
            float phi = 2.0f * M_PI * u1;
            float cos_phi = std::cos(phi);
            float sin_phi = std::sin(phi);
            float cos_theta = std::sqrt((1.0f - u2) / (1.0f + (alpha * alpha - 1.0f) * u2));
            float sin_theta = std::sqrt(std::max(0.0f, 1.0f - cos_theta * cos_theta));
            wh = Vec3f(sin_theta * cos_phi, sin_theta * sin_phi, cos_theta).normalized();
        }

        if (wh.z() <= EPS_SMALL) wh.z() = EPS;

        Vec3f wi_local = (wo - 2.0f * wo.dot(wh) * wh).normalized();
        if (wi_local.z() <= EPS_SMALL) return {Vec3f::Zero(), 0.0f};

        Vec3f wi_world = local_to_world(wi_local, normal);
        float pdf_val = pdf(wo_world, wi_world, normal);
        return {wi_world, pdf_val};
    }
};

struct Mirror {
    Vec3f albedo = Vec3f::Ones();

    static Vec3f reflect(const Vec3f& dir, const Vec3f& normal) {
        return dir - 2.0f * dir.dot(normal) * normal;
    }

    Vec3f eval(Vec3f wo_world, Vec3f wi_world, Vec3f normal) const {
        Vec3f n = normal.normalized();
        if (wo_world.dot(n) <= EPS_SMALL || wi_world.dot(n) <= EPS_SMALL) return Vec3f::Zero();

        Vec3f expected = reflect(-wo_world.normalized(), n).normalized();
        Vec3f incoming = wi_world.normalized();
        if ((expected - incoming).squaredNorm() <= EPS_SMALL) {
            return albedo;
        }
        return Vec3f::Zero();
    }

    float pdf(Vec3f wo_world, Vec3f wi_world, Vec3f normal) const {
        Vec3f n = normal.normalized();
        if (wo_world.dot(n) <= EPS_SMALL || wi_world.dot(n) <= EPS_SMALL) return 0.0f;

        Vec3f expected = reflect(-wo_world.normalized(), n).normalized();
        Vec3f incoming = wi_world.normalized();
        return (expected - incoming).squaredNorm() <= EPS_SMALL ? 1.0f : 0.0f;
    }

    std::tuple<Vec3f, float> sample(Vec3f wo_world, Vec3f normal, Vec2f) const {
        Vec3f n = normal.normalized();
        Vec3f wo = wo_world.normalized();
        if (wo.dot(n) <= EPS_SMALL) return {Vec3f::Zero(), 0.0f};

        Vec3f wi = reflect(-wo, n).normalized();
        if (!std::isfinite(wi.x()) || !std::isfinite(wi.y()) || !std::isfinite(wi.z())) {
            return {Vec3f::Zero(), 0.0f};
        }
        return {wi, 1.0f};
    }
};

struct Material {
    using Variant = std::variant<Lambertian, Microfacet, Mirror>;

    Variant brdf;

    Material() : brdf(Lambertian{Vec3f(0.5f, 0.5f, 0.5f)}) {}
    template <typename BRDF>
    Material(const BRDF& v) : brdf(v) {}
};

inline Vec3f brdf_eval(const Material& material, Vec3f wo_world, Vec3f wi_world, Vec3f normal) {
    return std::visit([&](const auto& brdf) { return brdf.eval(wo_world, wi_world, normal); }, material.brdf);
}

inline float brdf_pdf(const Material& material, Vec3f wo_world, Vec3f wi_world, Vec3f normal) {
    return std::visit([&](const auto& brdf) { return brdf.pdf(wo_world, wi_world, normal); }, material.brdf);
}

inline std::tuple<Vec3f, float> brdf_sample(const Material& material, Vec3f wo_world, Vec3f normal, Vec2f u) {
    return std::visit([&](const auto& brdf) { return brdf.sample(wo_world, normal, u); }, material.brdf);
}
