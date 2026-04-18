cmake_minimum_required(VERSION 3.16)
project(PointInPolygonPDC
    VERSION 1.0.0
    LANGUAGES CXX
    DESCRIPTION "Parallel Point-in-Polygon Classification for Large-Scale Geospatial Data"
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(OpenMP REQUIRED)

# Platform-specific optimization flags
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /O2 /EHsc")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native")
endif()

# Boost for geometry and spatial indexing
find_package(Boost QUIET)
if(NOT Boost_FOUND)
    message(WARNING "Boost not found. Using header-only Boost geometry mode.")
endif()

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/include)
if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIR})
endif()

# Core library: geometry + indexing
add_library(pip_core
    src/geometry/polygon.cpp
    src/geometry/point.cpp
    src/geometry/ray_casting.cpp
    src/index/bbox_filter.cpp
    src/index/quadtree.cpp
    src/index/strip_index.cpp
    src/index/geojson_loader.cpp
)
target_include_directories(pip_core PUBLIC ${PROJECT_SOURCE_DIR}/include)
if(Boost_FOUND)
    target_link_libraries(pip_core PUBLIC Boost::system)
endif()

# Data generators
add_library(pip_generators
    src/generator/uniform_distribution.cpp
    src/generator/clustered_distribution.cpp
    src/generator/polygon_loader.cpp
)
target_link_libraries(pip_generators PUBLIC pip_core)

# Parallel library (classifiers)
add_library(pip_parallel
    src/parallel/parallel_classifier.cpp
    src/parallel/work_stealing_classifier.cpp
)
target_link_libraries(pip_parallel PUBLIC pip_core)
if(OpenMP_CXX_FOUND)
    target_link_libraries(pip_parallel PUBLIC OpenMP::OpenMP_CXX)
endif()

# Unit tests
enable_testing()
add_executable(test_ray_casting tests/test_ray_casting.cpp)
target_link_libraries(test_ray_casting PUBLIC pip_core)
add_test(NAME RayCastingTests COMMAND test_ray_casting)

# Milestone 1 benchmark
add_executable(benchmark_m1 src/benchmark_m1.cpp)
target_link_libraries(benchmark_m1 PUBLIC pip_core pip_generators)

# Milestone 2 benchmark
add_executable(benchmark_m2 src/benchmark_m2.cpp)
target_link_libraries(benchmark_m2 PUBLIC pip_core pip_generators pip_parallel)

# Milestone 3 benchmark
add_executable(benchmark_m3 src/benchmark_m3.cpp)
target_link_libraries(benchmark_m3 PUBLIC pip_core pip_generators pip_parallel OpenMP::OpenMP_CXX)

# Milestone 3 worker executable for process-based IPC
add_executable(worker src/worker_main.cpp)
target_link_libraries(worker PUBLIC pip_core)

# Install targets
install(TARGETS pip_core pip_generators pip_parallel LIBRARY DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
