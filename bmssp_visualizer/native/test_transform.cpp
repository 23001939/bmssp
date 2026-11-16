#include <cassert>
#include <tuple>
#include "bmssp-traced.hpp"
int main() {
    spp::bmssp<double> g(5);
    g.addEdge(0,1,2); g.addEdge(0,1,1); g.addEdge(1,1,0); g.addEdge(1,2,0);
    g.prepare_graph(true);
    auto [nodes, edges, max_in, max_out]=g.graph_stats();
    assert(nodes==6 && edges==6 && max_in==2 && max_out==2);
    const auto &map=g.original_to_prepared_nodes();
    const auto &reverse=g.prepared_to_original_nodes();
    assert(map.size()==5 && map[3]!=map[4]);
    for(int i=0;i<5;++i) assert(reverse[map[i]]==i);
    auto [d,p]=g.execute(0);
    assert(d[0]==0 && d[1]==1 && d[2]==1 && d[3]==g.oo && d[4]==g.oo);
    assert(p[1]==0 && p[2]==1);
    auto [d2,p2]=g.execute(3);
    assert(d2[0]==g.oo && d2[1]==g.oo && d2[2]==g.oo && d2[3]==0 && d2[4]==g.oo);
    spp::bmssp<double> singleton(1); singleton.addEdge(0,0,0); singleton.prepare_graph(true);
    assert(std::get<0>(singleton.graph_stats())==1);
    assert(singleton.execute(0).first[0]==0);
}
