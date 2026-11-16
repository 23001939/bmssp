#include <cassert>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include "bmssp-traced.hpp"
using Key=std::tuple<double,int,int,int>;
int main() {
    spp::batchPQ<Key> queue(100);
    const Key bound{1000,0,0,0};
    queue.initialize(3,bound);
    std::map<int,Key> expected;
    for(int id=0;id<30;++id) {
        Key key{double(id+10),0,id,0};
        queue.insert(key); expected[id]=key;
    }
    // A duplicate key with a lower value must replace its prior value.
    Key replacement{1,0,20,0}; queue.insert(replacement); expected[20]=replacement;
    // All values are lower than the existing minimum, including a duplicate key.
    std::vector<Key> prepend{{0.25,0,40,0},{0.5,0,41,0},{0.75,0,42,0},{0.2,0,20,0}};
    queue.batchPrepend(prepend);
    for(auto x:prepend) { int id=std::get<2>(x); auto it=expected.find(id); if(it==expected.end() || x<it->second) expected[id]=x; }
    while(!expected.empty()) {
        auto [next_bound, ids]=queue.pull();
        assert(!ids.empty() && ids.size()<=3);
        std::set<Key> sorted; for(auto [id,key]:expected) sorted.insert(key);
        std::set<Key> pulled; for(int id:ids) { assert(expected.count(id)); pulled.insert(expected.at(id)); expected.erase(id); }
        for(auto key:pulled) { assert(key==*sorted.begin()); sorted.erase(sorted.begin()); assert(key<next_bound); }
        if(sorted.empty()) assert(next_bound==bound);
        else assert(next_bound<=*sorted.begin());
    }
    assert(queue.size()==0);
    // Reach a lookup exactly equal to a stored upper bound. This exercised the
    // former default-iterator tie path in CompareUB under debug iterators.
    spp::batchPQ<Key> equal_bound(4);
    equal_bound.initialize(1,bound);
    equal_bound.insert(Key{1,0,0,0});
    equal_bound.insert(Key{2,0,1,2});
    equal_bound.insert(Key{2,0,1,1});
    auto [bound1, first]=equal_bound.pull();
    assert(first.size()==1 && first[0]==0);
    auto [bound2, second]=equal_bound.pull();
    assert(second.size()==1 && second[0]==1 && bound2==bound);
}
