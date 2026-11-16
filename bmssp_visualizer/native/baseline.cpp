#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "upstream/include/bmssp-worst-case.hpp"

struct Edge { int u, v; double w; };
int main(int argc, char** argv) {
 try {
    if(argc != 3) throw std::runtime_error("usage: baseline INPUT SOURCE");
    size_t used=0; int source=std::stoi(argv[2], &used);
    if(used!=std::string(argv[2]).size()) throw std::runtime_error("invalid source");
    std::ifstream in(argv[1]); long long n64,m64;
    if(!(in>>n64>>m64) || n64<1 || n64>10000000 || m64<0 || m64>100000000 || source<0 || source>=n64) throw std::runtime_error("invalid graph header or source");
    int n=(int)n64; const double limit=1e290/n; std::vector<Edge> edges;
    edges.reserve((size_t)m64);
    for(long long i=0;i<m64;++i) {
        long long u,v; double w;
        if(!(in>>u>>v>>w) || u<0 || u>=n || v<0 || v>=n || !std::isfinite(w) || w<0 || w>limit) throw std::runtime_error("invalid edge or weight out of supported numeric range");
        edges.push_back({(int)u,(int)v,w});
    }
    std::string extra; if(in>>extra) throw std::runtime_error("extra input after edges");
    auto begin=std::chrono::steady_clock::now();
    std::vector<double> dist;
    // Upstream computes t=0 for n=1; that graph's source distance is trivially zero.
    if(n==1) dist={0.0};
    else {
        spp::bmssp<double> solver(n);
        for(auto e:edges) solver.addEdge(e.u,e.v,e.w);
        solver.prepare_graph(false);
        dist=solver.execute(source).first;
    }
    double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    const double inf=std::numeric_limits<double>::max()/10;
    std::cout << std::setprecision(17) << "{\"type\":\"baseline_result\",\"distances\":[";
    for(int i=0;i<n;++i) { if(i) std::cout<<','; if(dist[i]>=inf || !std::isfinite(dist[i])) std::cout<<"null"; else std::cout<<dist[i]; }
    std::cout << "],\"compute_ms\":" << ms << "}" << std::endl;
    return 0;
 } catch(const std::exception& e) { std::cerr<<e.what()<<std::endl; return 1; }
}
