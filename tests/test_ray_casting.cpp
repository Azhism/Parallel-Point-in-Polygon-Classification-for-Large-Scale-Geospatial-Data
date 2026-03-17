#include <iostream>
#include <cassert>
#include <cmath>
#include "geometry/ray_casting.hpp"
#include "generator/polygon_loader.hpp"

using namespace pdc_geo;
using namespace pdc_gen;

void test_simple_square() {
    std::cout << "Test: Simple square..." << std::endl;
    
    Polygon square = PolygonLoader::create_square(1, 0, 0, 10, 10);
    
    // Inside
    Point inside(5, 5);
    assert(RayCaster::point_in_polygon(inside, square) == RayCaster::Classification::INSIDE);
    
    // Outside
    Point outside(15, 5);
    assert(RayCaster::point_in_polygon(outside, square) == RayCaster::Classification::OUTSIDE);
    
    // On corner
    Point on_corner(0, 0);
    auto result = RayCaster::point_in_polygon(on_corner, square);
    assert(result == RayCaster::Classification::ON_BOUNDARY || result == RayCaster::Classification::INSIDE);
    
    // On edge
    Point on_edge(5, 0);
    result = RayCaster::point_in_polygon(on_edge, square);
    assert(result == RayCaster::Classification::ON_BOUNDARY || result == RayCaster::Classification::INSIDE);
    
    std::cout << "  PASSED" << std::endl;
}

void test_polygon_with_hole() {
    std::cout << "Test: Polygon with hole..." << std::endl;
    
    // Outer square: (0,0) to (10,10)
    std::vector<Point> outer{
        {0, 0}, {10, 0}, {10, 10}, {0, 10}
    };
    
    // Hole: (3,3) to (7,7)
    std::vector<Point> hole{
        {3, 3}, {7, 3}, {7, 7}, {3, 7}
    };
    
    Polygon poly(1, outer);
    poly.add_hole(hole);
    
    // Inside outer, outside hole
    Point inside_but_not_in_hole(1, 1);
    assert(RayCaster::point_in_polygon(inside_but_not_in_hole, poly) == RayCaster::Classification::INSIDE);
    
    // Inside hole
    Point inside_hole(5, 5);
    assert(RayCaster::point_in_polygon(inside_hole, poly) == RayCaster::Classification::OUTSIDE);
    
    // Outside both
    Point outside(15, 15);
    assert(RayCaster::point_in_polygon(outside, poly) == RayCaster::Classification::OUTSIDE);
    
    std::cout << "  PASSED" << std::endl;
}

void test_circle() {
    std::cout << "Test: Circle polygon..." << std::endl;
    
    Polygon circle = PolygonLoader::create_circle(1, 5, 5, 2, 64);
    
    // Center (should be inside)
    Point center(5, 5);
    auto result = RayCaster::point_in_polygon(center, circle);
    // Note: center might be ON_BOUNDARY for a polygon with even vertex count
    assert(result == RayCaster::Classification::INSIDE || result == RayCaster::Classification::ON_BOUNDARY);
    
    // Near edge (should be inside)
    Point near_edge(6.5, 5);
    result = RayCaster::point_in_polygon(near_edge, circle);
    assert(result == RayCaster::Classification::INSIDE || result == RayCaster::Classification::ON_BOUNDARY);
    
    // Far outside
    Point far_outside(10, 10);
    assert(RayCaster::point_in_polygon(far_outside, circle) == RayCaster::Classification::OUTSIDE);
    
    std::cout << "  PASSED" << std::endl;
}

void test_edge_cases() {
    std::cout << "Test: Edge cases..." << std::endl;
    
    Polygon square = PolygonLoader::create_square(1, 0, 0, 10, 10);
    
    // Point exactly at each corner
    for (const auto& corner : square.exterior) {
        auto result = RayCaster::point_in_polygon(corner, square);
        assert(result != RayCaster::Classification::OUTSIDE);  // Should be inside or on boundary
    }
    
    // Point on each edge
    for (size_t i = 0; i < square.exterior.size(); i++) {
        const Point& a = square.exterior[i];
        const Point& b = square.exterior[(i + 1) % square.exterior.size()];
        Point mid{(a.x + b.x) / 2, (a.y + b.y) / 2};
        auto result = RayCaster::point_in_polygon(mid, square);
        assert(result == RayCaster::Classification::ON_BOUNDARY || result == RayCaster::Classification::INSIDE);
    }
    
    std::cout << "  PASSED" << std::endl;
}

int main() {
    std::cout << "=== Ray-Casting Unit Tests ===" << std::endl;
    
    test_simple_square();
    test_polygon_with_hole();
    test_circle();
    test_edge_cases();
    
    std::cout << "\nAll tests PASSED!" << std::endl;
    return 0;
}
