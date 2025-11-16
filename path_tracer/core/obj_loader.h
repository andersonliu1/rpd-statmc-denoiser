#pragma once

#define TINYOBJLOADER_IMPLEMENTATION
#define TINYOBJLOADER_USE_MAPBOX_EARCUT

#include <cstdlib>
#include <string>
#include <vector>

#include "../../extern/tinyobjloader/tiny_obj_loader.h"
#include "common.h"
#include "triangle.h"
#include <spdlog/spdlog.h>

inline Vec3f calc_normal(const Vec3f& v0, const Vec3f& v1, const Vec3f& v2){
    return ((v1 - v0).cross(v2 - v0)).normalized();
}

inline std::vector<Triangle> load_obj(std::string inputfile, int material_id = 0){
    tinyobj::ObjReaderConfig reader_config;
    tinyobj::ObjReader reader;

    std::vector<Triangle> res;

    if (!reader.ParseFromFile(inputfile, reader_config)){
        if (!reader.Error().empty()) {
            spdlog::error("TinyObjReader error while reading '{}': {}", inputfile, reader.Error());
        }
        exit(1);
    }

    if (!reader.Warning().empty()) {
        spdlog::warn("TinyObjReader warning for '{}': {}", inputfile, reader.Warning());
    }

    auto& attrib = reader.GetAttrib();
    auto& shapes = reader.GetShapes();

    for (size_t s = 0; s < shapes.size(); ++s){
        size_t offset = 0;
        
        for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); ++f){
            size_t verts = size_t(shapes[s].mesh.num_face_vertices[f]);
            Triangle tri;

            for (size_t i = 0; i < 3; ++i){
                auto idx = shapes[s].mesh.indices[offset + i];
                tri.v[i] = Vec3f(
                    attrib.vertices[3 * size_t(idx.vertex_index)],
                    attrib.vertices[3 * size_t(idx.vertex_index) + 1],
                    attrib.vertices[3 * size_t(idx.vertex_index) + 2]
                );
            }

            tri.normal = calc_normal(tri.v0(), tri.v1(), tri.v2());
            tri.emission = Vec3f::Zero();
            tri.material_id = material_id;

            res.push_back(tri);
            offset += verts;
        }
    }

    return res;
}
