#pragma once

#include <mpi.h>
#include <vector>
#include <cstdint>
#include <stdexcept>

#include "geometry/point.hpp"
#include "geometry/polygon.hpp"
#include "parallel/parallel_classifier.hpp"

namespace pdc_mpi {

// Global MPI datatype handles — valid after register_mpi_types()
extern MPI_Datatype MPI_POINT;
extern MPI_Datatype MPI_CLASSIFICATION;

/**
 * Register custom MPI datatypes for Point and ClassificationResult.
 * Call once after MPI_Init().
 */
inline void register_mpi_types() {
    // --- Point: { double x, double y, uint64_t id } ---
    {
        pdc_geo::Point sample;
        int blocklens[3] = {1, 1, 1};
        MPI_Datatype types[3] = {MPI_DOUBLE, MPI_DOUBLE, MPI_UINT64_T};
        MPI_Aint disps[3];
        MPI_Aint base;
        MPI_Get_address(&sample, &base);
        MPI_Get_address(&sample.x, &disps[0]);
        MPI_Get_address(&sample.y, &disps[1]);
        MPI_Get_address(&sample.id, &disps[2]);
        disps[0] -= base;
        disps[1] -= base;
        disps[2] -= base;

        MPI_Type_create_struct(3, blocklens, disps, types, &MPI_POINT);
        MPI_Type_commit(&MPI_POINT);
    }

    // --- ClassificationResult: { uint64_t point_index, uint64_t polygon_id } ---
    {
        pdc_geo::ClassificationResult sample;
        int blocklens[2] = {1, 1};
        MPI_Datatype types[2] = {MPI_UINT64_T, MPI_UINT64_T};
        MPI_Aint disps[2];
        MPI_Aint base;
        MPI_Get_address(&sample, &base);
        MPI_Get_address(&sample.point_index, &disps[0]);
        MPI_Get_address(&sample.polygon_id, &disps[1]);
        disps[0] -= base;
        disps[1] -= base;

        MPI_Type_create_struct(2, blocklens, disps, types, &MPI_CLASSIFICATION);
        MPI_Type_commit(&MPI_CLASSIFICATION);
    }
}

/**
 * Free registered MPI datatypes.
 * Call before MPI_Finalize().
 */
inline void free_mpi_types() {
    MPI_Type_free(&MPI_POINT);
    MPI_Type_free(&MPI_CLASSIFICATION);
}

// ============================================================
// Polygon serialization for MPI broadcast/send
//
// Format per polygon:
//   [id_as_double, num_exterior_verts, x0, y0, x1, y1, ...,
//    num_holes, hole0_size, hx0, hy0, hx1, hy1, ..., hole1_size, ...]
// ============================================================

/**
 * Serialize a vector of polygons into a flat double buffer.
 * Polygon IDs and vertex counts are cast to double for uniform buffer type.
 */
inline std::vector<double> serialize_polygons(const std::vector<pdc_geo::Polygon>& polygons) {
    std::vector<double> buf;
    // Reserve approximate space: id + ext_count + 2*avg_verts + holes_count per polygon
    buf.reserve(polygons.size() * 30);

    for (const auto& poly : polygons) {
        buf.push_back(static_cast<double>(poly.id));
        buf.push_back(static_cast<double>(poly.exterior.size()));
        for (const auto& p : poly.exterior) {
            buf.push_back(p.x);
            buf.push_back(p.y);
        }
        buf.push_back(static_cast<double>(poly.holes.size()));
        for (const auto& hole : poly.holes) {
            buf.push_back(static_cast<double>(hole.size()));
            for (const auto& p : hole) {
                buf.push_back(p.x);
                buf.push_back(p.y);
            }
        }
    }
    return buf;
}

/**
 * Deserialize a flat double buffer back into a vector of polygons.
 */
inline std::vector<pdc_geo::Polygon> deserialize_polygons(const std::vector<double>& buf) {
    std::vector<pdc_geo::Polygon> polygons;
    size_t pos = 0;

    while (pos < buf.size()) {
        pdc_geo::Polygon poly;
        poly.id = static_cast<uint64_t>(buf[pos++]);

        size_t ext_count = static_cast<size_t>(buf[pos++]);
        poly.exterior.resize(ext_count);
        for (size_t i = 0; i < ext_count; ++i) {
            poly.exterior[i].x = buf[pos++];
            poly.exterior[i].y = buf[pos++];
        }

        size_t num_holes = static_cast<size_t>(buf[pos++]);
        poly.holes.resize(num_holes);
        for (size_t h = 0; h < num_holes; ++h) {
            size_t hole_size = static_cast<size_t>(buf[pos++]);
            poly.holes[h].resize(hole_size);
            for (size_t i = 0; i < hole_size; ++i) {
                poly.holes[h][i].x = buf[pos++];
                poly.holes[h][i].y = buf[pos++];
            }
        }

        poly.compute_bbox();
        polygons.push_back(std::move(poly));
    }

    return polygons;
}

}  // namespace pdc_mpi
