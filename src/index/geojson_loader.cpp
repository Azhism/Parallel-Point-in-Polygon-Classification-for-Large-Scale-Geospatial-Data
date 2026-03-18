#include "index/geojson_loader.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cmath>

// Include nlohmann/json (must be in include/ directory)
#include "nlohmann/json.hpp"

using json = nlohmann::json;

namespace pdc_geo {

std::string GeoJSONLoader::read_file(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open GeoJSON file: " + filepath);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

BBox GeoJSONLoader::compute_bbox(const std::vector<Point>& exterior) {
    if (exterior.empty()) {
        return BBox(0, 0, 0, 0);
    }
    
    double min_x = exterior[0].x, max_x = exterior[0].x;
    double min_y = exterior[0].y, max_y = exterior[0].y;
    
    for (const auto& p : exterior) {
        min_x = std::min(min_x, p.x);
        max_x = std::max(max_x, p.x);
        min_y = std::min(min_y, p.y);
        max_y = std::max(max_y, p.y);
    }
    
    return BBox(min_x, min_y, max_x, max_y);
}

void GeoJSONLoader::extract_polygon_rings(
    const std::vector<std::vector<std::vector<double>>>& coords,
    std::vector<Point>& exterior,
    std::vector<std::vector<Point>>& holes) {
    
    if (coords.empty()) return;
    
    // Extract exterior ring (first ring)
    exterior.clear();
    for (const auto& coord : coords[0]) {
        if (coord.size() >= 2) {
            double lon = coord[0];  // x
            double lat = coord[1];  // y
            exterior.emplace_back(lon, lat);
        }
    }
    
    // Extract holes (remaining rings)
    holes.clear();
    for (size_t i = 1; i < coords.size(); ++i) {
        std::vector<Point> hole;
        for (const auto& coord : coords[i]) {
            if (coord.size() >= 2) {
                double lon = coord[0];
                double lat = coord[1];
                hole.emplace_back(lon, lat);
            }
        }
        if (!hole.empty()) {
            holes.push_back(hole);
        }
    }
}

std::vector<Polygon> GeoJSONLoader::load_polygons_from_geojson(const std::string& filepath) {
    std::vector<Polygon> result;
    uint64_t polygon_id = 0;
    
    try {
        std::string content = read_file(filepath);
        json data = json::parse(content);
        
        if (!data.contains("features")) {
            std::cerr << "Warning: GeoJSON has no 'features' array" << std::endl;
            return result;
        }
        
        auto features = data["features"];
        if (!features.is_array()) {
            std::cerr << "Warning: 'features' is not an array" << std::endl;
            return result;
        }
        
        for (const auto& feature : features) {
            if (!feature.contains("geometry")) continue;
            
            const auto& geom = feature["geometry"];
            if (!geom.contains("type") || !geom.contains("coordinates")) continue;
            
            std::string geom_type = geom["type"].get<std::string>();
            auto coords = geom["coordinates"];
            
            if (geom_type == "Polygon") {
                // Single polygon
                std::vector<Point> exterior;
                std::vector<std::vector<Point>> holes;
                
                auto coords_array = coords.get<std::vector<std::vector<std::vector<double>>>>();
                extract_polygon_rings(coords_array, exterior, holes);
                
                if (!exterior.empty()) {
                    Polygon poly(polygon_id++, exterior);
                    for (const auto& hole : holes) {
                        poly.add_hole(hole);
                    }
                    poly.compute_bbox();
                    result.push_back(poly);
                }
                
            } else if (geom_type == "MultiPolygon") {
                // Multiple polygons
                auto multi_coords = coords.get<std::vector<std::vector<std::vector<std::vector<double>>>>>();
                
                for (const auto& poly_coords : multi_coords) {
                    std::vector<Point> exterior;
                    std::vector<std::vector<Point>> holes;
                    
                    extract_polygon_rings(poly_coords, exterior, holes);
                    
                    if (!exterior.empty()) {
                        Polygon poly(polygon_id++, exterior);
                        for (const auto& hole : holes) {
                            poly.add_hole(hole);
                        }
                        poly.compute_bbox();
                        result.push_back(poly);
                    }
                }
            }
        }
        
        std::cout << "✓ Loaded " << result.size() << " polygons from " << filepath << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error loading GeoJSON: " << e.what() << std::endl;
    }
    
    return result;
}

std::vector<Polygon> GeoJSONLoader::load_karachi_polygons(const std::string& filepath) {
    std::vector<Polygon> result;
    uint64_t polygon_id = 0;
    
    try {
        std::string content = read_file(filepath);
        json data = json::parse(content);
        
        if (!data.contains("features")) {
            std::cerr << "Warning: GeoJSON has no 'features' array" << std::endl;
            return result;
        }
        
        auto features = data["features"];
        if (!features.is_array()) {
            std::cerr << "Warning: 'features' is not an array" << std::endl;
            return result;
        }
        
        for (const auto& feature : features) {
            // Filter by adm1_name == "Sindh"
            if (feature.contains("properties")) {
                const auto& props = feature["properties"];
                if (props.contains("adm1_name")) {
                    std::string adm1 = props["adm1_name"].get<std::string>();
                    if (adm1 != "Sindh") continue;
                }
            }
            
            if (!feature.contains("geometry")) continue;
            
            const auto& geom = feature["geometry"];
            if (!geom.contains("type") || !geom.contains("coordinates")) continue;
            
            std::string geom_type = geom["type"].get<std::string>();
            auto coords = geom["coordinates"];
            
            if (geom_type == "Polygon") {
                std::vector<Point> exterior;
                std::vector<std::vector<Point>> holes;
                
                auto coords_array = coords.get<std::vector<std::vector<std::vector<double>>>>();
                extract_polygon_rings(coords_array, exterior, holes);
                
                if (!exterior.empty()) {
                    Polygon poly(polygon_id++, exterior);
                    for (const auto& hole : holes) {
                        poly.add_hole(hole);
                    }
                    poly.compute_bbox();
                    result.push_back(poly);
                }
                
            } else if (geom_type == "MultiPolygon") {
                auto multi_coords = coords.get<std::vector<std::vector<std::vector<std::vector<double>>>>>();
                
                for (const auto& poly_coords : multi_coords) {
                    std::vector<Point> exterior;
                    std::vector<std::vector<Point>> holes;
                    
                    extract_polygon_rings(poly_coords, exterior, holes);
                    
                    if (!exterior.empty()) {
                        Polygon poly(polygon_id++, exterior);
                        for (const auto& hole : holes) {
                            poly.add_hole(hole);
                        }
                        poly.compute_bbox();
                        result.push_back(poly);
                    }
                }
            }
        }
        
        std::cout << "✓ Loaded " << result.size() << " Sindh polygons from " << filepath << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error loading Karachi GeoJSON: " << e.what() << std::endl;
    }
    
    return result;
}

std::vector<Point> GeoJSONLoader::load_centroids_from_geojson(const std::string& filepath) {
    std::vector<Point> result;
    uint64_t point_id = 0;

    auto json_to_double = [](const json& value, double& out) -> bool {
        if (value.is_number_float() || value.is_number_integer() || value.is_number_unsigned()) {
            out = value.get<double>();
            return true;
        }
        if (value.is_string()) {
            try {
                out = std::stod(value.get<std::string>());
                return true;
            } catch (const std::exception&) {
                return false;
            }
        }
        return false;
    };
    
    try {
        std::string content = read_file(filepath);
        json data = json::parse(content);
        
        if (!data.contains("features")) {
            std::cerr << "Warning: GeoJSON has no 'features' array" << std::endl;
            return result;
        }
        
        auto features = data["features"];
        if (!features.is_array()) {
            std::cerr << "Warning: 'features' is not an array" << std::endl;
            return result;
        }
        
        for (const auto& feature : features) {
            double lon = 0.0;
            double lat = 0.0;
            bool has_lon = false;
            bool has_lat = false;

            if (feature.contains("properties") && feature["properties"].is_object()) {
                const auto& props = feature["properties"];

                // Preferred schema used by some centroid exports.
                if (props.contains("center_lon") && props.contains("center_lat")) {
                    has_lon = json_to_double(props["center_lon"], lon);
                    has_lat = json_to_double(props["center_lat"], lat);
                }

                // Alternate schema used in pak_admincentroids.geojson.
                if ((!has_lon || !has_lat) && props.contains("x_coord") && props.contains("y_coord")) {
                    has_lon = json_to_double(props["x_coord"], lon);
                    has_lat = json_to_double(props["y_coord"], lat);
                }
            }

            // Fallback to point geometry coordinates [lon, lat].
            if ((!has_lon || !has_lat) && feature.contains("geometry")) {
                const auto& geom = feature["geometry"];
                if (geom.contains("type") && geom.contains("coordinates") && geom["type"] == "Point") {
                    const auto& coords = geom["coordinates"];
                    if (coords.is_array() && coords.size() >= 2) {
                        has_lon = json_to_double(coords[0], lon);
                        has_lat = json_to_double(coords[1], lat);
                    }
                }
            }

            if (has_lon && has_lat) {
                result.emplace_back(lon, lat, point_id++);
            }
        }
        
        std::cout << "✓ Loaded " << result.size() << " centroids from " << filepath << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error loading centroids: " << e.what() << std::endl;
    }
    
    return result;
}

}  // namespace pdc_geo
