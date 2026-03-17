#include "generator/polygon_loader.hpp"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace pdc_gen {

pdc_geo::Polygon PolygonLoader::create_square(
    uint64_t id,
    double x_min, double y_min,
    double x_max, double y_max) {
    
    std::vector<pdc_geo::Point> vertices{
        {x_min, y_min},
        {x_max, y_min},
        {x_max, y_max},
        {x_min, y_max}
    };
    
    pdc_geo::Polygon poly(id, vertices);
    return poly;
}

std::vector<pdc_geo::Polygon> PolygonLoader::create_grid(
    double x_min, double y_min,
    double x_max, double y_max,
    size_t cols, size_t rows) {
    
    std::vector<pdc_geo::Polygon> polygons;
    
    double cell_width = (x_max - x_min) / cols;
    double cell_height = (y_max - y_min) / rows;
    
    uint64_t id = 0;
    for (size_t r = 0; r < rows; r++) {
        for (size_t c = 0; c < cols; c++) {
            double x0 = x_min + c * cell_width;
            double y0 = y_min + r * cell_height;
            double x1 = x0 + cell_width;
            double y1 = y0 + cell_height;
            
            polygons.push_back(create_square(id++, x0, y0, x1, y1));
        }
    }
    
    return polygons;
}

pdc_geo::Polygon PolygonLoader::create_circle(
    uint64_t id,
    double center_x, double center_y,
    double radius,
    size_t num_vertices) {
    
    std::vector<pdc_geo::Point> vertices;
    double angle_step = 2.0 * M_PI / num_vertices;
    
    for (size_t i = 0; i < num_vertices; i++) {
        double angle = i * angle_step;
        double x = center_x + radius * std::cos(angle);
        double y = center_y + radius * std::sin(angle);
        vertices.emplace_back(x, y);
    }
    
    pdc_geo::Polygon poly(id, vertices);
    return poly;
}

}  // namespace pdc_gen
