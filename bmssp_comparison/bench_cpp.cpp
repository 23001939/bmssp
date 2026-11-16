#include <bits/stdc++.h>
#include "repos/cpp/include/bmssp.hpp"
#include "repos/cpp/helpers/dijkstra.hpp"
using Clock = std::chrono::steady_clock;
using Adj = std::vector<std::vector<std::pair<int, double>>>;
template<class Solver> void bench(const Adj& adj, const std::string& out, bool transform) {
    std::vector<double> times, kernels, dist;
    for(int r = 0; r < 6; ++r) {
        auto start = Clock::now();
        Solver solver(adj);
        if constexpr(requires { solver.prepare_graph(true); }) solver.prepare_graph(transform);
        auto prepared = Clock::now();
        auto result = solver.execute(0);
        auto end = Clock::now();
        dist = std::move(result.first);
        if(r) {
            times.push_back(std::chrono::duration<double, std::milli>(end-start).count());
            kernels.push_back(std::chrono::duration<double, std::milli>(end-prepared).count());
        }
    }
    std::ofstream f(out);
    f << std::setprecision(17);
    for(auto d : dist) f << d << '\n';
    auto emit = [](const auto& a) { for(size_t i=0;i<a.size();++i) std::cout << (i?",":"") << a[i]; };
    std::cout << "{\"samples_ms\":["; emit(times);
    std::cout << "],\"kernel_ms\":["; emit(kernels);
    std::cout << "]}" << std::endl;
}
int main(int argc, char** argv) {
    if(argc != 4) return 2;
    std::ifstream input(argv[2]); int n, m; input >> n >> m;
    Adj adj(n);
    for(int i=0;i<m;++i) { int u,v; double w; input>>u>>v>>w; adj[u].push_back({v,w}); }
    std::string mode=argv[1];
    if(mode=="wc") bench<spp::bmssp<double>>(adj,argv[3],true);
    else if(mode=="expected") bench<spp_expected::bmssp<double>>(adj,argv[3],true);
    else if(mode=="wc_bounded") bench<spp::bmssp<double>>(adj,argv[3],false);
    else if(mode=="expected_bounded") bench<spp_expected::bmssp<double>>(adj,argv[3],false);
    else if(mode=="dijkstra") bench<spp::dijkstra<double>>(adj,argv[3],false);
    else return 3;
}
