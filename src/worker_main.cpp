#include "geometry/ray_casting.hpp"
#include "index/quadtree.hpp"
#include "ipc/ipc.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace pdc_geo;

namespace {

static constexpr uint64_t NO_POLYGON = std::numeric_limits<uint64_t>::max();

static uint64_t mix_u64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

static bool intersects_stripe(const Polygon& polygon, double stripe_min, double stripe_max) {
    return polygon.bbox.max_x >= stripe_min && polygon.bbox.min_x <= stripe_max;
}

static void prepare_worker_polygons(const std::vector<Polygon>& source,
                                    const pdc_ipc::WorkerInputHeader& header,
                                    std::vector<Polygon>* polygons,
                                    std::vector<uint64_t>* original_ids) {
    polygons->clear();
    original_ids->clear();

    bool replicated = header.polygon_mode == static_cast<int32_t>(pdc_ipc::PolygonMode::Replicated);
    for (const auto& polygon : source) {
        if (replicated || intersects_stripe(polygon, header.stripe_x_min, header.stripe_x_max)) {
            Polygon copy = polygon;
            copy.id = static_cast<uint64_t>(polygons->size());
            original_ids->push_back(polygon.id);
            polygons->push_back(std::move(copy));
        }
    }
}

static pdc_ipc::WorkerResult classify_points(const std::vector<Point>& points,
                                             const std::vector<Polygon>& polygons,
                                             const std::vector<uint64_t>& original_ids,
                                             const QuadTreeIndex& index) {
    pdc_ipc::WorkerResult result;
    result.points_processed = static_cast<int64_t>(points.size());

    for (const auto& point : points) {
        auto candidates = index.query_point(point);
        result.candidate_checks += static_cast<int64_t>(candidates.size());

        uint64_t polygon_id = NO_POLYGON;
        for (uint64_t local_id : candidates) {
            if (local_id >= polygons.size()) {
                continue;
            }
            auto classification = RayCaster::point_in_polygon(point, polygons[local_id]);
            if (classification == RayCaster::Classification::INSIDE ||
                classification == RayCaster::Classification::ON_BOUNDARY) {
                polygon_id = original_ids[local_id];
                break;
            }
        }

        if (polygon_id == NO_POLYGON) {
            result.unmatched++;
        } else {
            result.matched++;
        }
        result.checksum ^= mix_u64((point.id << 32) ^ polygon_id);
    }

    return result;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: worker.exe <input_file> <polygon_file> <result_file>\n";
        return 2;
    }

    try {
        std::string input_path = argv[1];
        std::string polygon_path = argv[2];
        std::string result_path = argv[3];

        pdc_ipc::WorkerInputHeader header;
        auto points = pdc_ipc::read_worker_input(input_path, &header);
        auto source_polygons = pdc_ipc::read_polygons(polygon_path);

        std::vector<Polygon> worker_polygons;
        std::vector<uint64_t> original_ids;
        prepare_worker_polygons(source_polygons, header, &worker_polygons, &original_ids);

        QuadTreeIndex index;
        index.build(worker_polygons);

        auto result = classify_points(points, worker_polygons, original_ids, index);
        pdc_ipc::write_worker_result(result_path, result);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "worker failed: " << e.what() << "\n";
        return 1;
    }
}
