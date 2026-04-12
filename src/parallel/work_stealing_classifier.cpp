#include "parallel/work_stealing_classifier.hpp"
#include "geometry/ray_casting.hpp"

#include <omp.h>
#include <deque>
#include <mutex>
#include <limits>
#include <algorithm>

namespace pdc_geo {

// ============================================================
// Work-stealing task definition
// ============================================================

struct Task {
    size_t start;
    size_t end;
};

// ============================================================
// Helper: classify one point
// ============================================================

uint64_t WorkStealingClassifier::classify_one(
    const Point&                p,
    const std::vector<Polygon>& polygons,
    const QuadTreeIndex&        index
) const {
    auto candidates = index.query_point(p);
    for (uint64_t pid : candidates) {
        if (pid < polygons.size()) {
            if (RayCaster::point_in_polygon(p, polygons[pid]) == RayCaster::Classification::INSIDE) {
                return pid;
            }
        }
    }
    return std::numeric_limits<uint64_t>::max();
}

// ============================================================
// Work-stealing classifier implementation
// ============================================================

std::vector<WorkStealingResult> WorkStealingClassifier::classify(
    const std::vector<Point>&   points,
    const std::vector<Polygon>& polygons,
    const QuadTreeIndex&        index,
    int                         num_threads
) const {
    if (num_threads > 0) {
        omp_set_num_threads(num_threads);
    }
    
    int nt = omp_get_max_threads();
    size_t n = points.size();
    
    // Determine chunk size: create M*nt chunks (M = 4 for good granularity)
    const int MULTIPLIER = 4;
    size_t num_chunks = std::max<size_t>(1, nt * MULTIPLIER);
    size_t chunk_size = (n + num_chunks - 1) / num_chunks;
    
    // Create task queues (one deque per thread)
    std::vector<std::deque<Task>> queues(nt);
    std::vector<std::mutex> locks(nt);
    
    // Distribute initial tasks round-robin across queues
    size_t pos = 0;
    int queue_id = 0;
    while (pos < n) {
        size_t end = std::min(pos + chunk_size, n);
        {
            std::lock_guard<std::mutex> lk(locks[queue_id]);
            queues[queue_id].push_back({pos, end});
        }
        pos = end;
        queue_id = (queue_id + 1) % nt;
    }
    
    // Result storage
    std::vector<WorkStealingResult> results(n);
    
    // Worker function for each thread
    #pragma omp parallel num_threads(nt)
    {
        int tid = omp_get_thread_num();
        
        while (true) {
            Task task;
            bool found = false;
            
            // Try to steal from own queue first
            {
                std::lock_guard<std::mutex> lk(locks[tid]);
                if (!queues[tid].empty()) {
                    task = queues[tid].front();
                    queues[tid].pop_front();
                    found = true;
                }
            }
            
            // If own queue is empty, try to steal from other threads
            if (!found) {
                for (int attempt = 0; attempt < nt; ++attempt) {
                    int victim = (tid + attempt + 1) % nt;
                    if (victim == tid) continue;
                    
                    std::lock_guard<std::mutex> lk(locks[victim]);
                    if (!queues[victim].empty()) {
                        // Steal from BACK (victim works from FRONT, we steal from BACK)
                        // This reduces contention
                        task = queues[victim].back();
                        queues[victim].pop_back();
                        found = true;
                        break;
                    }
                }
            }
            
            if (!found) {
                break;  // All queues empty, this thread is done
            }
            
            // Process the task: classify all points in this range
            for (size_t i = task.start; i < task.end; ++i) {
                uint64_t polygon_id = classify_one(points[i], polygons, index);
                results[i] = { i, polygon_id };
            }
        }
    }
    
    return results;
}

}  // namespace pdc_geo
