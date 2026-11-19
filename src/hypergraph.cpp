#include "hypergraph.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>

void Hypergraph::buildIncidence() {
    incOff.assign(n + 1, 0);
    for (int e = 0; e < m; ++e) {
        incOff[netSrc[e] + 1]++;
        for (int i = sinkOff[e]; i < sinkOff[e + 1]; ++i) incOff[sinks[i] + 1]++;
    }
    for (int v = 0; v < n; ++v) incOff[v + 1] += incOff[v];
    inc.assign(incOff[n], 0);
    std::vector<int> pos(incOff.begin(), incOff.end() - 1);
    for (int e = 0; e < m; ++e) {
        inc[pos[netSrc[e]]++] = e;
        for (int i = sinkOff[e]; i < sinkOff[e + 1]; ++i) inc[pos[sinks[i]]++] = e;
    }
}

// ---------------------------------------------------------------------------
// Input parsing
// ---------------------------------------------------------------------------
namespace {

[[noreturn]] void die(const char* msg) {
    std::fprintf(stderr, "error: %s\n", msg);
    std::exit(1);
}

class LineReader {
public:
    explicit LineReader(std::vector<char>& buf) : p_(buf.data()), end_(buf.data() + buf.size()) {}

    // Reads the integers of the next non-empty line into `out`.
    bool nextLine(std::vector<long long>& out) {
        out.clear();
        while (p_ < end_) {
            // parse one line
            while (p_ < end_ && *p_ != '\n') {
                char c = *p_;
                if (c >= '0' && c <= '9') {
                    long long x = 0;
                    while (p_ < end_ && *p_ >= '0' && *p_ <= '9') x = x * 10 + (*p_++ - '0');
                    out.push_back(x);
                } else if (c == '-') {
                    ++p_;
                    long long x = 0;
                    while (p_ < end_ && *p_ >= '0' && *p_ <= '9') x = x * 10 + (*p_++ - '0');
                    out.push_back(-x);
                } else {
                    ++p_;
                }
            }
            if (p_ < end_) ++p_;  // skip '\n'
            if (!out.empty()) return true;
        }
        return false;
    }

private:
    char* p_;
    char* end_;
};

}  // namespace

void readInput(const std::string& path, Hypergraph& H, Topology& T, std::vector<int>& nodeType) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) die("cannot open input file");
    std::vector<char> buf;
    {
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        buf.resize(sz > 0 ? (size_t)sz : 0);
        if (sz > 0 && std::fread(buf.data(), 1, (size_t)sz, f) != (size_t)sz) die("read failed");
        std::fclose(f);
    }
    LineReader rd(buf);
    std::vector<long long> v;

    // The header may in principle be spread over lines; collect 4 numbers.
    std::vector<long long> hdr;
    while (hdr.size() < 4) {
        if (!rd.nextLine(v)) die("missing header");
        hdr.insert(hdr.end(), v.begin(), v.end());
    }
    const int N = (int)hdr[0], K = (int)hdr[1], M = (int)hdr[2], L = (int)hdr[3];
    if (N <= 0 || K <= 0 || M < 0 || L < 0) die("bad header");

    H.n = N;
    H.w1.assign(N, 0);
    H.w2.assign(N, 0);
    nodeType.assign(N, 1);
    for (int i = 0; i < N; ++i) {
        if (!rd.nextLine(v)) die("missing node line");
        nodeType[i] = (int)v[0];
        if (v[0] == 2) H.w2[i] = 1; else H.w1[i] = 1;
    }

    T.K = K;
    T.ioLim.assign(K, 0);
    T.cap1.assign(K, 0);
    T.cap2.assign(K, 0);
    for (int i = 0; i < K; ++i) {
        if (!rd.nextLine(v) || v.size() < 3) die("bad FPGA line");
        T.ioLim[i] = v[0];
        T.cap1[i] = (int)v[1];
        T.cap2[i] = (int)v[2];
    }

    H.netW.clear();
    H.netSrc.clear();
    H.sinkOff.assign(1, 0);
    H.sinks.clear();
    H.netW.reserve(M);
    H.netSrc.reserve(M);
    H.sinkOff.reserve(M + 1);
    std::vector<int> tmp;
    for (int i = 0; i < M; ++i) {
        if (!rd.nextLine(v) || v.size() < 3) die("bad hyperedge line");
        int w = (int)v[0];
        int s = (int)v[1];
        if (s < 0 || s >= N) die("hyperedge source out of range");
        tmp.clear();
        for (size_t j = 2; j < v.size(); ++j) {
            int t = (int)v[j];
            if (t < 0 || t >= N) die("hyperedge sink out of range");
            if (t != s) tmp.push_back(t);
        }
        std::sort(tmp.begin(), tmp.end());
        tmp.erase(std::unique(tmp.begin(), tmp.end()), tmp.end());
        if (tmp.empty() || w <= 0) continue;  // never costs anything
        H.netW.push_back(w);
        H.netSrc.push_back(s);
        H.sinks.insert(H.sinks.end(), tmp.begin(), tmp.end());
        H.sinkOff.push_back((int)H.sinks.size());
    }
    H.m = (int)H.netW.size();

    T.big = 0;
    T.maxCost = 1;
    std::vector<std::pair<std::pair<int, int>, int>> links;
    for (int i = 0; i < L; ++i) {
        if (!rd.nextLine(v) || v.size() < 3) die("bad link line");
        int a = (int)v[0], b = (int)v[1], c = (int)v[2];
        if (a < 0 || a >= K || b < 0 || b >= K) die("link FPGA out of range");
        links.push_back({{a, b}, c});
        T.maxCost = std::max(T.maxCost, c);
    }
    T.big = std::max(10000, 1000 * T.maxCost);
    T.D.assign((size_t)K * K, T.big);
    T.adj.assign((size_t)K * K, 0);
    for (int a = 0; a < K; ++a) {
        T.D[a * K + a] = 0;
        T.adj[a * K + a] = 1;
    }
    for (auto& l : links) {
        int a = l.first.first, b = l.first.second, c = l.second;
        if (a == b) continue;
        T.D[a * K + b] = T.D[b * K + a] = c;
        T.adj[a * K + b] = T.adj[b * K + a] = 1;
    }
    H.buildIncidence();
}

bool writeOutput(const std::string& path, const std::vector<int>& part) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::string out;
    out.reserve(part.size() * 4);
    char tmp[16];
    for (int p : part) {
        int len = std::snprintf(tmp, sizeof(tmp), "%d\n", p);
        out.append(tmp, len);
    }
    bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    ok = (std::fclose(f) == 0) && ok;
    return ok;
}

// ---------------------------------------------------------------------------
// Contraction
// ---------------------------------------------------------------------------
Hypergraph contract(const Hypergraph& H, const std::vector<int>& cmap, int nc) {
    Hypergraph C;
    C.n = nc;
    C.w1.assign(nc, 0);
    C.w2.assign(nc, 0);
    for (int v = 0; v < H.n; ++v) {
        C.w1[cmap[v]] += H.w1[v];
        C.w2[cmap[v]] += H.w2[v];
    }

    // Build the contracted nets (unsorted pool first).
    std::vector<int> tw, tsrc, toff(1, 0), tsinks;
    std::vector<uint64_t> thash;
    tw.reserve(H.m);
    tsrc.reserve(H.m);
    tsinks.reserve(H.sinks.size());
    std::vector<int> mark(nc, -1);
    std::vector<int> buf;
    for (int e = 0; e < H.m; ++e) {
        int s = cmap[H.netSrc[e]];
        buf.clear();
        mark[s] = e;
        for (int i = H.sinkOff[e]; i < H.sinkOff[e + 1]; ++i) {
            int c = cmap[H.sinks[i]];
            if (mark[c] != e) {
                mark[c] = e;
                buf.push_back(c);
            }
        }
        if (buf.empty()) continue;
        std::sort(buf.begin(), buf.end());
        uint64_t h = 1469598103934665603ULL ^ (uint64_t)s * 0x9E3779B97F4A7C15ULL;
        for (int c : buf) {
            h ^= (uint64_t)c + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
            h *= 1099511628211ULL;
        }
        h ^= buf.size() * 0xC2B2AE3D27D4EB4FULL;
        tw.push_back(H.netW[e]);
        tsrc.push_back(s);
        tsinks.insert(tsinks.end(), buf.begin(), buf.end());
        toff.push_back((int)tsinks.size());
        thash.push_back(h);
    }
    int tm = (int)tw.size();

    // Merge identical nets: sort by hash and compare equal-hash candidates.
    std::vector<int> order(tm);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        if (thash[a] != thash[b]) return thash[a] < thash[b];
        return a < b;
    });
    auto same = [&](int a, int b) {
        if (tsrc[a] != tsrc[b]) return false;
        int la = toff[a + 1] - toff[a], lb = toff[b + 1] - toff[b];
        if (la != lb) return false;
        return std::equal(tsinks.begin() + toff[a], tsinks.begin() + toff[a + 1], tsinks.begin() + toff[b]);
    };
    std::vector<int> rep(tm, -1);  // representative for every temp net
    for (int i = 0; i < tm;) {
        int j = i;
        while (j < tm && thash[order[j]] == thash[order[i]]) ++j;
        for (int x = i; x < j; ++x) {
            int a = order[x];
            if (rep[a] != -1) continue;
            rep[a] = a;
            for (int y = x + 1; y < j; ++y) {
                int b = order[y];
                if (rep[b] == -1 && same(a, b)) {
                    rep[b] = a;
                    tw[a] += tw[b];
                }
            }
        }
        i = j;
    }

    C.sinkOff.assign(1, 0);
    for (int e = 0; e < tm; ++e) {
        if (rep[e] != e) continue;
        C.netW.push_back(tw[e]);
        C.netSrc.push_back(tsrc[e]);
        C.sinks.insert(C.sinks.end(), tsinks.begin() + toff[e], tsinks.begin() + toff[e + 1]);
        C.sinkOff.push_back((int)C.sinks.size());
    }
    C.m = (int)C.netW.size();
    C.buildIncidence();
    return C;
}
