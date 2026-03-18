#include "index/strip_index.hpp"
#include <algorithm>
#include <cmath>

namespace pdc_geo {

StripIndex::StripIndex(std::size_t num_strips)
    : requested_num_strips_(num_strips), y_min_(0.0), y_max_(0.0), strip_height_(1.0) {}

void StripIndex::build(const std::vector<Polygon>& polygons) {
    clear();

    if (polygons.empty()) {
        return;
    }

    y_min_ = polygons[0].bbox.min_y;
    y_max_ = polygons[0].bbox.max_y;

    for (const auto& poly : polygons) {
        y_min_ = std::min(y_min_, poly.bbox.min_y);
        y_max_ = std::max(y_max_, poly.bbox.max_y);
    }

    std::size_t n = requested_num_strips_;
    if (n == 0) {
        n = static_cast<std::size_t>(std::sqrt(static_cast<double>(polygons.size())));
    }
    if (n == 0) {
        n = 1;
    }

    double y_range = y_max_ - y_min_;
    if (y_range <= 0.0) {
        y_range = 1.0;
    }

    strip_height_ = y_range / static_cast<double>(n);
    if (strip_height_ <= 0.0) {
        strip_height_ = 1.0;
    }

    strips_.assign(n, {});

    for (const auto& poly : polygons) {
        int64_t min_strip = static_cast<int64_t>(std::floor((poly.bbox.min_y - y_min_) / strip_height_));
        int64_t max_strip = static_cast<int64_t>(std::floor((poly.bbox.max_y - y_min_) / strip_height_));

        if (min_strip < 0) {
            min_strip = 0;
        }
        if (max_strip < 0) {
            max_strip = 0;
        }

        int64_t last = static_cast<int64_t>(n) - 1;
        if (min_strip > last) {
            min_strip = last;
        }
        if (max_strip > last) {
            max_strip = last;
        }

        for (int64_t s = min_strip; s <= max_strip; ++s) {
            strips_[static_cast<std::size_t>(s)].push_back(poly.id);
        }
    }
}

std::vector<uint64_t> StripIndex::query_point(const Point& p) const {
    if (strips_.empty()) {
        return {};
    }

    if (p.y < y_min_ || p.y > y_max_) {
        return {};
    }

    int64_t strip = static_cast<int64_t>(std::floor((p.y - y_min_) / strip_height_));
    if (strip < 0) {
        strip = 0;
    }

    int64_t last = static_cast<int64_t>(strips_.size()) - 1;
    if (strip > last) {
        strip = last;
    }

    return strips_[static_cast<std::size_t>(strip)];
}

void StripIndex::clear() {
    strips_.clear();
    y_min_ = 0.0;
    y_max_ = 0.0;
    strip_height_ = 1.0;
}

}  // namespace pdc_geo
