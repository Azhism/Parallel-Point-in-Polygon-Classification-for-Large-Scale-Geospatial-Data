#include "index/bbox_filter.hpp"

namespace pdc_geo {

std::vector<uint64_t> BBoxFilter::get_candidates(
    const Point& p,
    const std::vector<Polygon>& polygons) {
    
    std::vector<uint64_t> candidates;
    
    for (const auto& poly : polygons) {
        if (poly.bbox.contains(p)) {
            candidates.push_back(poly.id);
        }
    }
    
    return candidates;
}

}  // namespace pdc_geo
