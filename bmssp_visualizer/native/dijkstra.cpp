#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;
using Ms = std::chrono::duration<double, std::milli>;
struct Cancelled {};
struct Edge { int u, v, id; double w; };
int main(int argc, char** argv) {
 try {
    if(argc < 4 || argc > 5) throw std::runtime_error("usage: dijkstra INPUT SOURCE trace|plain [SAMPLE_EVERY]");
    const std::string mode = argv[3];
    if(mode != "trace" && mode != "plain") throw std::runtime_error("invalid mode");
    size_t used=0; int source=std::stoi(argv[2], &used);
    if(used != std::string(argv[2]).size()) throw std::runtime_error("invalid source");
    int sample=1;
    if(argc==5) { sample=std::stoi(argv[4], &used); if(used!=std::string(argv[4]).size() || sample<1) throw std::runtime_error("invalid sample interval"); }
    std::ifstream in(argv[1]); long long n64,m64;
    if(!(in>>n64>>m64) || n64<1 || n64>10000000 || m64<0 || m64>100000000 || source<0 || source>=n64) throw std::runtime_error("invalid graph header or source");
    const int n=(int)n64; const double limit=1e290/n;
    std::vector<Edge> edges; edges.reserve((size_t)m64);
    for(long long i=0;i<m64;++i) {
        long long u,v; double w;
        if(!(in>>u>>v>>w) || u<0 || u>=n || v<0 || v>=n || !std::isfinite(w) || w<0 || w>limit) throw std::runtime_error("invalid edge or weight out of supported numeric range");
        edges.push_back({(int)u,(int)v,(int)i,w});
    }
    std::string extra; if(in>>extra) throw std::runtime_error("extra input after edges");
    long long seen=0, emitted=0; double wait_ms=0;
    // Renderer executes only for an emitted trace event, never in plain mode.
    auto send=[&](const auto& render, bool force=false) {
        if(mode!="trace") return;
        ++seen; if(!force && (seen-1)%sample != 0) return;
        ++emitted; std::ostringstream s; s << std::setprecision(17); render(s);
        std::cout << s.str() << std::endl;
        auto before=Clock::now(); std::string ack;
        if(!std::getline(std::cin,ack) || ack!="next") throw Cancelled{};
        wait_ms += Ms(Clock::now()-before).count();
    };
    auto begin=Clock::now();
    send([&](auto& s){s << "{\"type\":\"start\",\"n\":" << n << ",\"source\":" << source << ",\"algorithm_id\":\"dijkstra\",\"algorithm\":\"Dijkstra · binary heap\",\"sample_every\":" << sample << ",\"sampled\":" << (sample>1?"true":"false") << '}';},true);
    std::vector<std::vector<Edge>> adj(n);
    for(const auto& e:edges) adj[e.u].push_back(e);
    const double inf=std::numeric_limits<double>::infinity();
    std::vector<double> dist(n,inf); std::vector<int> pred(n,-1), pred_edge(n,-1);
    using Item=std::pair<double,int>;
    std::priority_queue<Item,std::vector<Item>,std::greater<Item>> heap;
    dist[source]=0; heap.emplace(0,source);
    while(!heap.empty()) {
        auto [du,u]=heap.top(); heap.pop();
        if(du!=dist[u]) continue;
        send([&](auto& s){s << "{\"type\":\"complete\",\"level\":0,\"vertices\":[" << u << "],\"distances\":[" << du << "]}";});
        for(const auto& e:adj[u]) {
            double candidate=du+e.w;
            if(candidate < dist[e.v]) {
                dist[e.v]=candidate; pred[e.v]=u; pred_edge[e.v]=e.id;
                heap.emplace(candidate,e.v);
                send([&](auto& s){s << "{\"type\":\"relax\",\"level\":0,\"u\":" << u << ",\"v\":" << e.v << ",\"edge_id\":" << e.id << ",\"distance\":" << candidate << ",\"predecessor\":" << u << '}';});
            }
        }
    }
    double elapsed=Ms(Clock::now()-begin).count();
    std::cout << std::setprecision(17) << "{\"type\":\"native_result\",\"algorithm_id\":\"dijkstra\",\"algorithm\":\"Dijkstra · binary heap\",\"distances\":[";
    for(int i=0;i<n;++i) { if(i) std::cout << ','; if(!std::isfinite(dist[i])) std::cout << "null"; else std::cout << dist[i]; }
    std::cout << "],\"predecessors\":[";
    for(int i=0;i<n;++i) { if(i) std::cout << ','; if(pred[i]<0) std::cout << "null"; else std::cout << pred[i]; }
    std::cout << "],\"predecessor_edges\":[";
    for(int i=0;i<n;++i) { if(i) std::cout << ','; if(pred_edge[i]<0) std::cout << "null"; else std::cout << pred_edge[i]; }
    std::cout << "],\"compute_ms\":" << std::max(0.0,elapsed-wait_ms) << ",\"control_wait_ms\":" << wait_ms << ",\"sample_every\":" << sample << ",\"sampled\":" << (mode=="trace" && sample>1?"true":"false") << ",\"events_seen\":" << seen << ",\"events_emitted\":" << emitted << ",\"comparison_rounding\":false}" << std::endl;
    return 0;
 } catch(const Cancelled&) { return 0; }
 catch(const std::exception& e) { std::cerr << e.what() << std::endl; return 1; }
}
