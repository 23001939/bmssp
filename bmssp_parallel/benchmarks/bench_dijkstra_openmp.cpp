#include "baseline/dijkstra_openmp.hpp"
#include <omp.h>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

using namespace bmssp;
using Clock = std::chrono::steady_clock;
static_assert(Clock::is_steady, "Benchmark requires monotonic elapsed wall time");

namespace {
double milliseconds(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b-a).count();
}
double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size()/2];
}
void samples_json(const std::vector<double>& values) {
    std::cout << '[';
    for (size_t i=0; i<values.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << values[i];
    }
    std::cout << ']';
}
void quoted(const std::string& value) {
    std::cout << '"';
    for (unsigned char c : value) {
        if (c=='\\' || c=='"') std::cout << '\\';
        if (c < 32) throw std::runtime_error("invalid control character in module path");
        std::cout << c;
    }
    std::cout << '"';
}
void runtime_modules() {
    std::cout << '[';
#ifdef _WIN32
    bool comma = false;
    for (const char* name : {"libomp.dll", "libgomp-1.dll", "libiomp5md.dll"}) {
        if (auto handle = GetModuleHandleA(name)) {
            char path[32768];
            const auto length = GetModuleFileNameA(handle, path, sizeof(path));
            if (length && length < sizeof(path)) {
                if (comma) std::cout << ',';
                comma = true;
                quoted(std::string(path,length));
            }
        }
    }
#endif
    std::cout << ']';
}
struct Mode { const char* name; const char* scope; int threads; };
}

int main() {
    try {
        constexpr int warmup=2, samples=7;
        constexpr unsigned seed=20261119;
        omp_set_dynamic(0);
        std::cout << std::setprecision(12)
                  << "{\"schema_version\":2,\"seed\":" << seed << ",\"warmup\":" << warmup
                  << ",\"samples\":" << samples << ",\"openmp\":" << _OPENMP
                  << ",\"num_procs\":" << omp_get_num_procs()
                  << ",\"timer\":\"std::chrono::steady_clock; start/stop on main thread\",\"cases\":[";
        bool comma=false;
        const std::vector<Mode> modes{
            {"heap","api",0}, {"scan","api",0},
            {"omp1","api",1}, {"omp2","api",2}, {"omp4","api",4}, {"omp8","api",8},
            {"scan","sssp_prepared",0}, {"omp1","sssp_prepared",1},
            {"omp2","sssp_prepared",2}, {"omp4","sssp_prepared",4}, {"omp8","sssp_prepared",8}
        };
        for (const std::string family : {"sparse", "dense"}) {
            for (uint32_t n : {100U,1000U}) {
                std::mt19937 rng(seed+n);
                std::vector<Edge> edges;
                for (uint32_t u=0; u<n; ++u) edges.push_back({u,(u+1)%n,1});
                if (family=="sparse") {
                    for (uint32_t k=0; k<8*n; ++k)
                        edges.push_back({static_cast<uint32_t>(rng()%n),static_cast<uint32_t>(rng()%n),static_cast<double>(rng()%100)});
                } else {
                    for (uint32_t u=0; u<n; ++u) for (uint32_t v=0; v<n; ++v)
                        if (u!=v) edges.push_back({u,v,static_cast<double>(rng()%100)});
                }
                auto graph=CSRGraph::from_edge_list(n,edges);
                const auto reference=dijkstra_sequential(graph,0);
                PreparedDijkstraGraph prepared(graph);
                const auto& normalized=prepared.graph();
                uint64_t max_degree=0;
                for (uint32_t u=0; u<n; ++u)
                    max_degree=std::max(max_degree,normalized.edge_end(u)-normalized.edge_begin(u));
                std::vector<double> preparation;
                for (int round=-warmup; round<samples; ++round) {
                    auto begin=Clock::now();
                    PreparedDijkstraGraph work(graph);
                    auto end=Clock::now();
                    if (work.graph().m != normalized.m) throw std::runtime_error("preparation mismatch");
                    if (round>=0) preparation.push_back(milliseconds(begin,end));
                }
                std::vector<int> actual(modes.size(),1);
                for (size_t i=0; i<modes.size(); ++i) if (modes[i].threads) {
                    const int requested=modes[i].threads;
                    int team=0;
                    #pragma omp parallel num_threads(requested) shared(team)
                    {
                        #pragma omp single
                        team=omp_get_num_threads();
                    }
                    actual[i]=team;
                }
                std::vector<std::vector<double>> times(modes.size());
                for (int round=-warmup; round<samples; ++round) {
                    const size_t offset=static_cast<size_t>(round+warmup)%modes.size();
                    for (size_t position=0; position<modes.size(); ++position) {
                        const size_t i=(position+offset)%modes.size();
                        const auto mode=modes[i];
                        const bool api=std::string(mode.scope)=="api";
                        const bool heap=std::string(mode.name)=="heap";
                        // Start/stop stay on this caller thread. No clock() or CPU-time sum.
                        const auto begin=Clock::now();
                        auto result=mode.threads
                            ? (api ? dijkstra_openmp(graph,0,mode.threads)
                                   : dijkstra_openmp_prepared(prepared,0,mode.threads))
                            : (heap ? dijkstra_sequential(graph,0)
                               : (api ? dijkstra_scan_sequential(graph,0) : dijkstra_scan_prepared(prepared,0)));
                        const auto end=Clock::now();
                        if (result.dist!=reference.dist) throw std::runtime_error("benchmark distance mismatch");
                        if (round>=0) times[i].push_back(milliseconds(begin,end));
                    }
                }
                std::vector<double> medians;
                for (const auto& values : times) medians.push_back(median(values));
                if (comma) std::cout << ',';
                comma=true;
                std::cout << "{\"family\":\"" << family << "\",\"n\":" << n
                          << ",\"m\":" << graph.m << ",\"normalized_m\":" << normalized.m
                          << ",\"source\":0,\"exact_distance_match\":true,\"max_out_degree\":" << max_degree
                          << ",\"mean_out_degree\":" << static_cast<double>(normalized.m)/n
                          << ",\"preparation\":{\"median_ms\":" << median(preparation) << ",\"samples_ms\":";
                samples_json(preparation);
                std::cout << "},\"modes\":[";
                for (size_t i=0; i<modes.size(); ++i) {
                    if (i) std::cout << ',';
                    const bool api=std::string(modes[i].scope)=="api";
                    std::cout << "{\"mode\":\"" << modes[i].name << "\",\"scope\":\"" << modes[i].scope
                              << "\",\"requested_threads\":" << (modes[i].threads ? modes[i].threads : 1)
                              << ",\"actual_threads_probe\":" << actual[i]
                              << ",\"vertices_per_thread\":" << static_cast<double>(n)/actual[i]
                              << ",\"mean_relax_edges_per_thread\":" << static_cast<double>(normalized.m)/n/actual[i]
                              << ",\"median_ms\":" << medians[i]
                              << ",\"speedup_vs_scan\":" << medians[api ? 1 : 6]/medians[i]
                              << ",\"speedup_vs_heap\":";
                    if (api) std::cout << medians[0]/medians[i]; else std::cout << "null";
                    std::cout << ",\"samples_ms\":"; samples_json(times[i]); std::cout << '}';
                }
                std::cout << "]}";
            }
        }
        std::cout << "],\"runtime_modules\":"; runtime_modules(); std::cout << "}\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
