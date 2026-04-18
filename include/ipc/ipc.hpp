#pragma once

#include "geometry/point.hpp"
#include "geometry/polygon.hpp"

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace pdc_ipc {

enum class PolygonMode : int32_t {
    Replicated = 0,
    Sharded = 1
};

struct WorkerInputHeader {
    int32_t worker_id = 0;
    int32_t total_points = 0;
    int32_t polygon_mode = 0;
    double stripe_x_min = 0.0;
    double stripe_x_max = 0.0;
};

struct WorkerResult {
    int64_t points_processed = 0;
    int64_t matched = 0;
    int64_t unmatched = 0;
    int64_t candidate_checks = 0;
    uint64_t checksum = 0;
};

inline void ensure_stream(bool ok, const std::string& message) {
    if (!ok) {
        throw std::runtime_error(message);
    }
}

template <typename T>
inline void write_pod(std::ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
    ensure_stream(static_cast<bool>(out), "Failed while writing binary file");
}

template <typename T>
inline void read_pod(std::ifstream& in, T& value) {
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    ensure_stream(static_cast<bool>(in), "Failed while reading binary file");
}

inline void write_point(std::ofstream& out, const pdc_geo::Point& point) {
    write_pod(out, point.x);
    write_pod(out, point.y);
    write_pod(out, point.id);
}

inline pdc_geo::Point read_point(std::ifstream& in) {
    pdc_geo::Point point;
    read_pod(in, point.x);
    read_pod(in, point.y);
    read_pod(in, point.id);
    return point;
}

inline void write_ring(std::ofstream& out, const std::vector<pdc_geo::Point>& ring) {
    uint64_t count = static_cast<uint64_t>(ring.size());
    write_pod(out, count);
    for (const auto& point : ring) {
        write_point(out, point);
    }
}

inline std::vector<pdc_geo::Point> read_ring(std::ifstream& in) {
    uint64_t count = 0;
    read_pod(in, count);
    std::vector<pdc_geo::Point> ring;
    ring.reserve(static_cast<size_t>(count));
    for (uint64_t i = 0; i < count; ++i) {
        ring.push_back(read_point(in));
    }
    return ring;
}

inline void write_polygons(const std::string& path,
                           const std::vector<pdc_geo::Polygon>& polygons) {
    std::ofstream out(path, std::ios::binary);
    ensure_stream(out.is_open(), "Cannot open polygon IPC file for write: " + path);

    uint64_t count = static_cast<uint64_t>(polygons.size());
    write_pod(out, count);
    for (const auto& polygon : polygons) {
        write_pod(out, polygon.id);
        write_ring(out, polygon.exterior);

        uint64_t hole_count = static_cast<uint64_t>(polygon.holes.size());
        write_pod(out, hole_count);
        for (const auto& hole : polygon.holes) {
            write_ring(out, hole);
        }
    }
}

inline std::vector<pdc_geo::Polygon> read_polygons(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    ensure_stream(in.is_open(), "Cannot open polygon IPC file for read: " + path);

    uint64_t count = 0;
    read_pod(in, count);
    std::vector<pdc_geo::Polygon> polygons;
    polygons.reserve(static_cast<size_t>(count));
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t id = 0;
        read_pod(in, id);
        auto exterior = read_ring(in);
        pdc_geo::Polygon polygon(id, exterior);

        uint64_t hole_count = 0;
        read_pod(in, hole_count);
        for (uint64_t h = 0; h < hole_count; ++h) {
            polygon.add_hole(read_ring(in));
        }
        polygon.compute_bbox();
        polygons.push_back(std::move(polygon));
    }
    return polygons;
}

inline void write_worker_input(const std::string& path,
                               const WorkerInputHeader& header,
                               const std::vector<pdc_geo::Point>& points) {
    std::ofstream out(path, std::ios::binary);
    ensure_stream(out.is_open(), "Cannot open worker input file for write: " + path);

    write_pod(out, header);
    for (const auto& point : points) {
        write_point(out, point);
    }
}

inline std::vector<pdc_geo::Point> read_worker_input(const std::string& path,
                                                     WorkerInputHeader* header) {
    std::ifstream in(path, std::ios::binary);
    ensure_stream(in.is_open(), "Cannot open worker input file for read: " + path);

    read_pod(in, *header);
    std::vector<pdc_geo::Point> points;
    points.reserve(static_cast<size_t>(header->total_points));
    for (int32_t i = 0; i < header->total_points; ++i) {
        points.push_back(read_point(in));
    }
    return points;
}

inline void write_worker_result(const std::string& path, const WorkerResult& result) {
    std::ofstream out(path, std::ios::binary);
    ensure_stream(out.is_open(), "Cannot open worker result file for write: " + path);
    write_pod(out, result);
}

inline WorkerResult read_worker_result(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    ensure_stream(in.is_open(), "Cannot open worker result file for read: " + path);
    WorkerResult result;
    read_pod(in, result);
    return result;
}

}  // namespace pdc_ipc
