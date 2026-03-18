#pragma once

#include "geometry/polygon.hpp"
#include "geometry/point.hpp"
#include <vector>
#include <string>

namespace pdc_geo {

/**
 * GeoJSON loader for polygon and point data.
 * Supports standard GeoJSON FeatureCollections with Polygon and MultiPolygon geometries.
 * 
 * File Format:
 * - Top level: FeatureCollection with "features" array
 * - Each feature: { "properties": {...}, "geometry": {...} }
 * - Geometry: Polygon or MultiPolygon
 * - Coordinates: [longitude, latitude] (note: x=lon, y=lat)
 */
class GeoJSONLoader {
 public:
    /**
     * Load all polygons from a GeoJSON file.
     * 
     * @param filepath Path to the GeoJSON file
     * @return Vector of Polygon objects with sequential IDs (0, 1, 2, ...)
     * 
     * Handles:
     * - Polygon geometries (with optional holes)
     * - MultiPolygon geometries (creates separate Polygon for each component)
     * - Missing/null properties gracefully
     */
    static std::vector<Polygon> load_polygons_from_geojson(const std::string& filepath);
    
    /**
     * Load polygons from Pakistan administrative data, filtered by state/province.
     * Filters features where properties["adm1_name"] == "Sindh"
     * 
     * @param filepath Path to pak_admin2.geojson
     * @return Vector of Polygon objects (only Sindh province)
     */
    static std::vector<Polygon> load_karachi_polygons(const std::string& filepath);
    
    /**
     * Load point centroids from GeoJSON properties.
     * Extracts center_lat and center_lon from each feature's properties.
     * 
     * @param filepath Path to GeoJSON file (e.g., pak_admincentroids.geojson)
     * @return Vector of Point objects with sequential IDs
     * 
     * Properties expected:
     * - "center_lat": double (latitude, becomes y)
     * - "center_lon": double (longitude, becomes x)
     */
    static std::vector<Point> load_centroids_from_geojson(const std::string& filepath);

 private:
    /**
     * Helper: Extract exterior ring and holes from coordinate array.
     * Used for Polygon geometry type.
     * 
     * Coordinates format: [exterior, hole1, hole2, ...]
     * Each is an array of [lon, lat] pairs
     */
    static void extract_polygon_rings(
        const std::vector<std::vector<std::vector<double>>>& coords,
        std::vector<Point>& exterior,
        std::vector<std::vector<Point>>& holes
    );
    
    /**
     * Helper: Compute bounding box from exterior ring.
     */
    static BBox compute_bbox(const std::vector<Point>& exterior);
    
    /**
     * Helper: Read file contents as string.
     */
    static std::string read_file(const std::string& filepath);
};

}  // namespace pdc_geo
