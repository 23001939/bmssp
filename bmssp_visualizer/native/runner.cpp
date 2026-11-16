#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "bmssp-traced.hpp"

using Clock = std::chrono::steady_clock;
using Ms = std::chrono::duration<double, std::milli>;
struct Cancelled {};
struct Edge { int v; double w; };
int main(int argc, char** argv) {
 try {
    if(argc < 4 || argc > 5) throw std::runtime_error("usage: runner INPUT SOURCE trace|plain [SAMPLE_EVERY]");
    const std::string mode = argv[3];
    if(mode != "trace" && mode != "plain") throw std::runtime_error("invalid mode");
    size_t used=0; int source=std::stoi(argv[2], &used);
    if(used != std::string(argv[2]).size()) throw std::runtime_error("invalid source");
    int sample=1;
    if(argc==5) { sample=std::stoi(argv[4], &used); if(used!=std::string(argv[4]).size() || sample<1) throw std::runtime_error("invalid sample interval"); }
    std::ifstream in(argv[1]); long long n64,m64;
    if(!(in>>n64>>m64) || n64<1 || n64>10000000 || m64<0 || m64>100000000 || source<0 || source>=n64) throw std::runtime_error("invalid graph header or source");
    int n=(int)n64; spp::bmssp<double> solver(n); std::vector<std::vector<Edge>> edges(n);
    const double limit=1e290 / n;
    for(long long i=0;i<m64;++i) {
        long long u,v; double w;
        if(!(in>>u>>v>>w) || u<0 || u>=n || v<0 || v>=n || !std::isfinite(w) || w<0 || w>limit) throw std::runtime_error("invalid edge or weight out of supported numeric range");
        solver.addEdge((int)u,(int)v,w); edges[(int)u].push_back({(int)v,w});
    }
    std::string extra; if(in>>extra) throw std::runtime_error("extra input after edges");
    std::cout << std::setprecision(17);
    long long seen=0, emitted=0; double wait_ms=0;
    auto send=[&](const std::string& json, bool force=false) {
        if(mode!="trace") return;
        ++seen; if(!force && (seen-1)%sample != 0) return;
        ++emitted; std::cout << json << std::endl;
        auto before=Clock::now(); std::string ack;
        if(!std::getline(std::cin,ack) || ack!="next") throw Cancelled{};
        wait_ms += Ms(Clock::now()-before).count();
    };
    auto begin=Clock::now();
    { std::ostringstream s; s << "{\"type\":\"start\",\"n\":" << n << ",\"source\":" << source << ",\"algorithm\":\"BMSSP WC - trace + real comparison fix\",\"sample_every\":" << sample << ",\"sampled\":" << (sample>1?"true":"false") << ",\"degree_transformation\":false,\"comparison_rounding\":false,\"patches\":[\"real_distance_comparison\",\"single_vertex_parameters\",\"deduplicate_pivot_frontier\",\"safe_bound_lookup\"]}"; send(s.str(),true); }
    solver.prepare_graph(false);
    if(mode=="trace") solver.trace=[&](const char* type,int level,const std::vector<int>& vertices,int u,int v,double value) {
        std::ostringstream s; s << std::setprecision(17) << "{\"type\":\"" << type << "\",\"level\":" << level;
        if(std::string(type)=="relax") s << ",\"u\":" << u << ",\"v\":" << v << ",\"predecessor\":" << u << ",\"distance\":" << value;
        else {
            s << (std::string(type)=="enter"?",\"sources\":[":",\"vertices\":[");
            for(size_t i=0;i<vertices.size();++i) { if(i) s << ','; s << vertices[i]; }
            s << ']';
            if(std::string(type)=="enter" || std::string(type)=="exit" || std::string(type)=="complete") { s << ",\"bound\":"; if(value>=solver.oo || !std::isfinite(value)) s << "null"; else s << value; }
        }
        s << '}'; send(s.str());
    };
    auto [dist, rawpred]=solver.execute(source);
    // Validate the solver's own predecessors against exact floating-point edge
    // equalities and a source-rooted, acyclic tree before exporting them.
    std::vector<int> pred(n,-1);
    std::vector<std::vector<int>> children(n);
    std::vector<bool> valid_parent(n,false);
    for (int u=0; u<n; ++u) if(dist[u]<solver.oo && std::isfinite(dist[u])) {
        for (auto e:edges[u]) {
            int v=e.v;
            if(v!=source && v!=u && rawpred[v]==u && dist[v]<solver.oo && dist[u]+e.w==dist[v]) valid_parent[v]=true;
        }
    }
    for(int v=0; v<n; ++v) if(valid_parent[v]) children[rawpred[v]].push_back(v);
    std::vector<bool> visited(n,false); std::queue<int> q;
    visited[source]=true; q.push(source);
    while(!q.empty()) {
        int u=q.front(); q.pop();
        for(int v:children[u]) if(!visited[v]) { visited[v]=true; pred[v]=u; q.push(v); }
    }
    int missing=0;
    for(int i=0;i<n;++i) if(dist[i]<solver.oo && std::isfinite(dist[i]) && !visited[i]) ++missing;
    std::string predecessor_origin="solver";
    if(missing) {
        // If raw parents are invalid, build a tree only using exact equalities.
        // No approximate fallback is allowed to turn a wrong path into success.
        predecessor_origin="exact_edge_reconstruction";
        std::fill(pred.begin(),pred.end(),-1);
        std::fill(visited.begin(),visited.end(),false);
        visited[source]=true; q.push(source);
        while(!q.empty()) { int u=q.front(); q.pop(); for(auto e:edges[u]) {
            if(!visited[e.v] && dist[e.v]<solver.oo && dist[u]+e.w==dist[e.v]) {
                visited[e.v]=true; pred[e.v]=u; q.push(e.v);
            }
        }}
        missing=0;
        for(int i=0;i<n;++i) if(dist[i]<solver.oo && std::isfinite(dist[i]) && !visited[i]) ++missing;
    }
    if(missing) throw std::runtime_error("could not construct exact predecessor tree for computed distances");
    double elapsed=Ms(Clock::now()-begin).count();
    std::cout << "{\"type\":\"native_result\",\"distances\":[";
    for(int i=0;i<n;++i) { if(i) std::cout << ','; if(dist[i]>=solver.oo || !std::isfinite(dist[i])) std::cout << "null"; else std::cout << dist[i]; }
    std::cout << "],\"predecessors\":[";
    for(int i=0;i<n;++i) { if(i) std::cout << ','; if(pred[i]<0) std::cout << "null"; else std::cout << pred[i]; }
    std::cout << "],\"compute_ms\":" << std::max(0.0,elapsed-wait_ms) << ",\"control_wait_ms\":" << wait_ms << ",\"sample_every\":" << sample << ",\"sampled\":" << (mode=="trace" && sample>1?"true":"false") << ",\"events_seen\":" << seen << ",\"events_emitted\":" << emitted << ",\"degree_transformation\":false,\"comparison_rounding\":false,\"patches\":[\"real_distance_comparison\",\"single_vertex_parameters\",\"deduplicate_pivot_frontier\",\"safe_bound_lookup\"],\"predecessor_origin\":\"" << predecessor_origin << "\",\"predecessor_unresolved\":" << missing << "}" << std::endl;
    return 0;
 } catch(const Cancelled&) { return 0; }
 catch(const std::exception& e) { std::cerr << e.what() << std::endl; return 1; }
}
