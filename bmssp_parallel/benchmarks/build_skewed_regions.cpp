#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct RawEdge {
    uint32_t u;
    uint32_t v;
    uint32_t w;
};

struct RegionRaw {
    std::vector<int32_t> lon_u; // microdegrees
    std::vector<int32_t> lat_u; // microdegrees
    std::vector<RawEdge> edges;
};

struct LineReader {
    std::FILE* fp{nullptr};
    std::vector<char> buf;
    size_t pos{0}, len{0};

    explicit LineReader(const std::string& path) : buf(1 << 22) {
        fp = std::fopen(path.c_str(), "rb");
        if (!fp) {
            std::cerr << "[ERROR] Cannot open " << path << "\n";
            std::exit(1);
        }
    }
    ~LineReader() {
        if (fp) std::fclose(fp);
    }

    bool next_line(std::string& out) {
        out.clear();
        while (true) {
            if (pos >= len) {
                len = std::fread(buf.data(), 1, buf.size(), fp);
                pos = 0;
                if (len == 0) return !out.empty();
            }
            const char c = buf[pos++];
            if (c == '\n') {
                if (!out.empty() && out.back() == '\r') out.pop_back();
                return true;
            }
            out.push_back(c);
        }
    }
};

uint64_t pack_coord(int32_t lon_u, int32_t lat_u) noexcept {
    return (static_cast<uint64_t>(static_cast<uint32_t>(lon_u)) << 32) |
           static_cast<uint64_t>(static_cast<uint32_t>(lat_u));
}

RegionRaw load_states(const std::string& root, const std::vector<std::string>& states) {
    RegionRaw reg;
    reg.lon_u.reserve(2600000);
    reg.lat_u.reserve(2600000);
    reg.edges.reserve(3500000);

    std::unordered_map<uint64_t, uint32_t> coord_to_id;
    coord_to_id.reserve(3000000);

    std::string line;
    for (const std::string& st : states) {
        std::vector<uint32_t> local_to_global;
        local_to_global.reserve(1700000);

        {
            LineReader nr(root + "/" + st + "/nodes.csv");
            while (nr.next_line(line)) {
                if (line.empty() || line[0] == '#') continue;
                const char* p = line.c_str();
                char* endp = nullptr;
                const unsigned long idx = std::strtoul(p, &endp, 10);
                if (*endp == ',') ++endp;
                const double lon = std::strtod(endp, &endp);
                if (*endp == ',') ++endp;
                const double lat = std::strtod(endp, &endp);

                const int32_t lon_i = static_cast<int32_t>(std::llround(lon * 1000000.0));
                const int32_t lat_i = static_cast<int32_t>(std::llround(lat * 1000000.0));
                const uint64_t key = pack_coord(lon_i, lat_i);

                uint32_t gid = 0;
                auto it = coord_to_id.find(key);
                if (it == coord_to_id.end()) {
                    gid = static_cast<uint32_t>(reg.lon_u.size());
                    coord_to_id.emplace(key, gid);
                    reg.lon_u.push_back(lon_i);
                    reg.lat_u.push_back(lat_i);
                } else {
                    gid = it->second;
                }
                if (idx >= local_to_global.size()) {
                    local_to_global.resize(idx + 1, 0);
                }
                local_to_global[idx] = gid;
            }
        }

        {
            LineReader er(root + "/" + st + "/edges.csv");
            while (er.next_line(line)) {
                if (line.empty() || line[0] == '#') continue;
                const char* p = line.c_str();
                char* endp = nullptr;
                const unsigned long u_loc = std::strtoul(p, &endp, 10);
                if (*endp == ',') ++endp;
                const unsigned long v_loc = std::strtoul(endp, &endp, 10);
                if (*endp == ',') ++endp;
                const double tt = std::strtod(endp, &endp);

                const uint32_t u = local_to_global[u_loc];
                const uint32_t v = local_to_global[v_loc];
                if (u == v) continue;
                const uint32_t w = static_cast<uint32_t>(std::max<long long>(1LL, std::llround(tt)));
                reg.edges.push_back(RawEdge{u, v, w});
            }
        }
    }
    return reg;
}

struct DSU {
    std::vector<uint32_t> parent;
    std::vector<uint32_t> sz;

    explicit DSU(size_t n) : parent(n), sz(n, 1) {
        for (size_t i = 0; i < n; ++i) parent[i] = static_cast<uint32_t>(i);
    }
    uint32_t find(uint32_t x) noexcept {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    }
    void unite(uint32_t a, uint32_t b) noexcept {
        a = find(a);
        b = find(b);
        if (a == b) return;
        if (sz[a] < sz[b]) std::swap(a, b);
        parent[b] = a;
        sz[a] += sz[b];
    }
};

template <typename KeepFn>
std::pair<uint32_t, uint32_t> eval_lcc(const RegionRaw& reg, KeepFn&& keep, std::vector<uint8_t>& alive) {
    const size_t n = reg.lon_u.size();
    alive.assign(n, 0);
    for (size_t i = 0; i < n; ++i) {
        if (keep(reg.lon_u[i], reg.lat_u[i])) alive[i] = 1;
    }
    DSU dsu(n);
    for (const auto& e : reg.edges) {
        if (alive[e.u] && alive[e.v]) {
            dsu.unite(e.u, e.v);
        }
    }
    uint32_t best_root = 0;
    uint32_t best_sz = 0;
    for (size_t i = 0; i < n; ++i) {
        if (alive[i] && dsu.parent[i] == i && dsu.sz[i] > best_sz) {
            best_sz = dsu.sz[i];
            best_root = static_cast<uint32_t>(i);
        }
    }
    return {best_root, best_sz};
}

template <typename PredMaker>
int32_t tune_cutoff(const RegionRaw& reg, int32_t lo, int32_t hi, bool monotone_increasing,
                    uint32_t target_n, PredMaker&& make_pred) {
    std::vector<uint8_t> alive;
    int32_t best_c = lo;
    uint32_t best_diff = UINT32_MAX;

    for (int iter = 0; iter < 26 && lo <= hi; ++iter) {
        const int32_t mid = lo + (hi - lo) / 2;
        auto [root, lcc_sz] = eval_lcc(reg, make_pred(mid), alive);
        (void)root;
        const uint32_t diff = (lcc_sz > target_n) ? (lcc_sz - target_n) : (target_n - lcc_sz);
        if (diff < best_diff) {
            best_diff = diff;
            best_c = mid;
        }
        if (lcc_sz == target_n) break;
        if (monotone_increasing) {
            if (lcc_sz < target_n) lo = mid + 1;
            else hi = mid - 1;
        } else {
            if (lcc_sz < target_n) hi = mid - 1;
            else lo = mid + 1;
        }
    }
    return best_c;
}

template <typename KeepFn>
void write_dimacs_gr(const std::string& code, const std::string& desc, const std::string& out_path,
                     const RegionRaw& reg, KeepFn&& keep) {
    const size_t n = reg.lon_u.size();
    std::vector<uint8_t> alive(n, 0);
    for (size_t i = 0; i < n; ++i) {
        if (keep(reg.lon_u[i], reg.lat_u[i])) alive[i] = 1;
    }
    DSU dsu(n);
    for (const auto& e : reg.edges) {
        if (alive[e.u] && alive[e.v]) dsu.unite(e.u, e.v);
    }
    uint32_t best_root = 0, best_sz = 0;
    for (size_t i = 0; i < n; ++i) {
        if (alive[i] && dsu.parent[i] == i && dsu.sz[i] > best_sz) {
            best_sz = dsu.sz[i];
            best_root = static_cast<uint32_t>(i);
        }
    }

    std::vector<uint32_t> remap(n, 0);
    uint32_t next_id = 1; // 1-based DIMACS IDs
    for (size_t i = 0; i < n; ++i) {
        if (alive[i] && dsu.find(static_cast<uint32_t>(i)) == best_root) {
            remap[i] = next_id++;
        }
    }
    const uint32_t final_n = next_id - 1;

    // Collect bidirectional directed arcs
    std::vector<RawEdge> arcs;
    arcs.reserve(reg.edges.size() * 2);
    for (const auto& e : reg.edges) {
        const uint32_t u1 = remap[e.u];
        const uint32_t v1 = remap[e.v];
        if (u1 == 0 || v1 == 0 || u1 == v1) continue;
        arcs.push_back(RawEdge{u1, v1, e.w});
        arcs.push_back(RawEdge{v1, u1, e.w});
    }

    std::FILE* fp = std::fopen(out_path.c_str(), "wb");
    if (!fp) {
        std::cerr << "[ERROR] Cannot write " << out_path << "\n";
        std::exit(1);
    }

    char hdr[512];
    int hlen = std::snprintf(
        hdr, sizeof(hdr),
        "c 9th DIMACS Implementation Challenge TIGER/Line 2000 (%s)\n"
        "c Reconstructed from Dominik Schultes DIMACS-9 state travel_time archives (networks.skewed.de)\n"
        "c %s\n"
        "p sp %u %llu\n",
        code.c_str(), desc.c_str(), final_n, static_cast<unsigned long long>(arcs.size())
    );
    std::fwrite(hdr, 1, static_cast<size_t>(hlen), fp);

    std::vector<char> out_buf;
    out_buf.reserve(1 << 22);
    char line_buf[64];
    for (const auto& a : arcs) {
        int l = std::snprintf(line_buf, sizeof(line_buf), "a %u %u %u\n", a.u, a.v, a.w);
        if (out_buf.size() + static_cast<size_t>(l) > (1U << 22)) {
            std::fwrite(out_buf.data(), 1, out_buf.size(), fp);
            out_buf.clear();
        }
        out_buf.insert(out_buf.end(), line_buf, line_buf + l);
    }
    if (!out_buf.empty()) {
        std::fwrite(out_buf.data(), 1, out_buf.size(), fp);
    }
    std::fclose(fp);

    std::cout << "[OK] Built " << code << " -> " << out_path
              << " | n=" << final_n << ", raw_arcs=" << arcs.size()
              << " (" << desc << ")\n" << std::flush;
}

} // namespace

int main() {
    const std::string root = "bmssp_parallel/data/dimacs_road/skewed/extracted";
    const std::string out_dir = "bmssp_parallel/data/dimacs_road";

    // 1. NW: WA + OR + ID cropped by eastern longitude cutoff (target: 1,207,945 nodes)
    {
        std::cout << "[*] Assembling NW (WA + OR + ID)...\n" << std::flush;
        RegionRaw nw = load_states(root, {"WA", "OR", "ID"});
        const int32_t lon_max = tune_cutoff(
            nw, -117500000, -111000000, true, 1207945U,
            [](int32_t c) { return [c](int32_t lon, int32_t /*lat*/) noexcept { return lon <= c; }; }
        );
        char desc[128];
        std::snprintf(desc, sizeof(desc), "States=WA+OR+ID, lon <= %.6f, LCC", lon_max / 1e6);
        write_dimacs_gr("NW", desc, out_dir + "/USA-road-t.NW.gr", nw,
                        [lon_max](int32_t lon, int32_t) noexcept { return lon <= lon_max; });
    }

    // 2. NE: NY + NJ + PA + CT + MA cropped by western longitude cutoff (target: 1,524,453 nodes)
    {
        std::cout << "[*] Assembling NE (NY + NJ + PA + CT + MA)...\n" << std::flush;
        RegionRaw ne = load_states(root, {"NY", "NJ", "PA", "CT", "MA"});
        const int32_t lon_min = tune_cutoff(
            ne, -80600000, -71000000, false, 1524453U,
            [](int32_t c) { return [c](int32_t lon, int32_t /*lat*/) noexcept { return lon >= c; }; }
        );
        char desc[128];
        std::snprintf(desc, sizeof(desc), "States=NY+NJ+PA+CT+MA, lon >= %.6f, LCC", lon_min / 1e6);
        write_dimacs_gr("NE", desc, out_dir + "/USA-road-t.NE.gr", ne,
                        [lon_min](int32_t lon, int32_t) noexcept { return lon >= lon_min; });
    }

    // 3. CAL: CA + NV + southern OR strip inside CAL rectangle (target: 1,890,815 nodes)
    {
        std::cout << "[*] Assembling CAL (CA + NV + S.OR border)...\n" << std::flush;
        RegionRaw cal = load_states(root, {"CA", "NV", "OR"});
        const int32_t lat_max = tune_cutoff(
            cal, 42000000, 43000000, true, 1890815U,
            [](int32_t c) { return [c](int32_t /*lon*/, int32_t lat) noexcept { return lat <= c; }; }
        );
        char desc[128];
        std::snprintf(desc, sizeof(desc), "States=CA+NV+OR, lat <= %.6f, LCC", lat_max / 1e6);
        write_dimacs_gr("CAL", desc, out_dir + "/USA-road-t.CAL.gr", cal,
                        [lat_max](int32_t, int32_t lat) noexcept { return lat <= lat_max; });
    }

    return 0;
}
