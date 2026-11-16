// Matched parsed-input -> distances+predecessors scope; no IO/trace in timers.
#include <chrono>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <queue>
#include "../bmssp_visualizer/native/bmssp-traced.hpp"
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point a, Clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
int main(int argc,char**argv){try{
 if(argc!=4) throw std::runtime_error("benchmark_v2 MODE INPUT profile|timing");
 std::string mode=argv[1]; bool profile=std::string(argv[3])=="profile";
 if(mode!="dijkstra"&&mode!="bmssp"&&mode!="bmssp_transform") throw std::runtime_error("bad mode");
 int n,m; std::ifstream f(argv[2]); if(!(f>>n>>m)||n<1||m<0)throw std::runtime_error("bad header");
 using Adj=std::vector<std::vector<std::pair<int,double>>>; Adj input(n);
 for(int i=0;i<m;i++){int u,v;double w;if(!(f>>u>>v>>w)||u<0||u>=n||v<0||v>=n||!std::isfinite(w)||w<0)throw std::runtime_error("bad edge"); input[u].push_back({v,w});}
 std::vector<double>d;std::vector<int> pred,node_map; size_t nn=n,mm=m;int mi=0,mo=0;
 long long relax=0,pops=0,peak=0,enters=0,pivots=0; unsigned long long inserts=0,pulls=0,prepends=0,prepend_items=0;double setup,solve,total;
 auto begin=Clock::now();
 if(mode=="dijkstra"){
   Adj adj=input;auto ready=Clock::now();
   d.assign(n,std::numeric_limits<double>::infinity());pred.assign(n,-1); d[0]=0;
   using Item=std::pair<double,int>;std::priority_queue<Item,std::vector<Item>,std::greater<Item>>q;q.push({0,0});
   while(!q.empty()){auto [du,u]=q.top();q.pop();if(profile)pops++;if(du!=d[u])continue;
    for(auto[v,w]:adj[u])if(du+w<d[v]){d[v]=du+w;pred[v]=u;q.push({d[v],v});if(profile){relax++;peak=std::max(peak,(long long)q.size());}}
   }auto end=Clock::now();setup=ms(begin,ready);solve=ms(ready,end);total=ms(begin,end);
 }else{
   spp::bmssp<double> solver(input); solver.prepare_graph(mode=="bmssp_transform");
   solver.count_operations=profile;
   if(profile)solver.trace=[&](const char*t,int,const std::vector<int>&,int,int,double){std::string s=t;if(s=="relax")relax++;if(s=="enter")enters++;if(s=="pivots")pivots++;};
   auto ready=Clock::now();auto result=solver.execute(0);auto end=Clock::now();d=std::move(result.first);pred=std::move(result.second);
   setup=ms(begin,ready);solve=ms(ready,end);total=ms(begin,end);
   std::tie(nn,mm,mi,mo)=solver.graph_stats();
   if(profile)node_map=solver.original_to_prepared_nodes();
   inserts=solver.operation_counts.inserts;pulls=solver.operation_counts.pulls;prepends=solver.operation_counts.prepends;prepend_items=solver.operation_counts.prepend_items;
 }
 if(mode=="dijkstra"){std::vector<int> indegree(n);for(const auto&out:input){mo=std::max(mo,(int)out.size());for(auto[v,w]:out)mi=std::max(mi,++indegree[v]);}}
 std::cout<<std::setprecision(17)<<"{\"setup_ms\":"<<setup<<",\"solve_ms\":"<<solve<<",\"parsed_to_result_ms\":"<<total<<",\"n_actual\":"<<nn<<",\"m_actual\":"<<mm<<",\"max_in\":"<<mi<<",\"max_out\":"<<mo<<",\"profile\":"<<(profile?"true":"false")<<",\"counters\":{\"relax_success\":"<<relax<<",\"heap_pops\":"<<pops<<",\"heap_peak\":"<<peak<<",\"recursive_calls\":"<<enters<<",\"pivot_calls\":"<<pivots<<"},\"distances\":[";
 for(int i=0;i<n;i++){if(i)std::cout<<',';if(!std::isfinite(d[i])||d[i]>=std::numeric_limits<double>::max()/10)std::cout<<"null";else std::cout<<d[i];}std::cout<<"],\"ds_operations\":{\"insert\":"<<inserts<<",\"pull\":"<<pulls<<",\"batch_prepend\":"<<prepends<<",\"prepend_items\":"<<prepend_items<<"},\"predecessors\":[";
 for(int i=0;i<n;i++){if(i)std::cout<<',';if(i==0||!std::isfinite(d[i])||d[i]>=std::numeric_limits<double>::max()/10)std::cout<<"null";else std::cout<<pred[i];}std::cout<<"],\"original_to_prepared\":[";for(size_t i=0;i<node_map.size();++i)std::cout<<(i?",":"")<<node_map[i];std::cout<<"]}";
}catch(const std::exception&e){std::cerr<<e.what();return 1;}}
