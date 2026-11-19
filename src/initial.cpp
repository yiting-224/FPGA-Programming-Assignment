// Initial partitioning and the multilevel V-cycle.
#include <algorithm>
#include <cstdio>
#include <limits>
#include <memory>
#include <numeric>

#include "partitioner.hpp"
#include "refiner.hpp"

namespace {

// Chooses the FPGA for a node given scores; prefers FPGAs with free
// capacity. Ties are broken by the larger relative free capacity.
int pickFpga(const Topology& T, const std::vector<double>& score, const std::vector<long long>& l1,
             const std::vector<long long>& l2, int w1, int w2) {
    int best = -1;
    double bestS = 0, bestFree = 0;
    for (int f = 0; f < T.K; ++f) {
        if (l1[f] + w1 > T.cap1[f] || l2[f] + w2 > T.cap2[f]) continue;
        const double fr1 = T.cap1[f] ? 1.0 - (double)l1[f] / T.cap1[f] : 0.0;
        const double fr2 = T.cap2[f] ? 1.0 - (double)l2[f] / T.cap2[f] : 0.0;
        const double fr = std::min(w1 ? fr1 : 1.0, w2 ? fr2 : 1.0) + 1e-3 * (fr1 + fr2);
        if (best < 0 || score[f] < bestS - 1e-9 || (score[f] <= bestS + 1e-9 && fr > bestFree)) {
            best = f;
            bestS = score[f];
            bestFree = fr;
        }
    }
    if (best >= 0) return best;
    // No FPGA can take the node: pick the one with the least relative load.
    double bestLoad = std::numeric_limits<double>::max();
    for (int f = 0; f < T.K; ++f) {
        const double ld = (double)(l1[f] + w1) / std::max(1, T.cap1[f]) + (double)(l2[f] + w2) / std::max(1, T.cap2[f]);
        if (ld < bestLoad) {
            bestLoad = ld;
            best = f;
        }
    }
    return best;
}

}  // namespace

std::vector<int> greedyGrowInit(const Hypergraph& H, const Topology& T, int ratingNetLimit, Rng& rng) {
    const int n = H.n, K = T.K;
    std::vector<int> part(n, -1);
    std::vector<long long> l1(K, 0), l2(K, 0);
    std::vector<double> score(K, 0.0);
    std::vector<char> seen(n, 0);
    std::vector<int> queue;
    queue.reserve(n);
    std::vector<int> seeds(n);
    std::iota(seeds.begin(), seeds.end(), 0);
    rng.shuffle(seeds);
    size_t head = 0;
    size_t seedPos = 0;
    int placed = 0;
    while (placed < n) {
        if (head == queue.size()) {
            while (seen[seeds[seedPos]]) ++seedPos;
            seen[seeds[seedPos]] = 1;
            queue.push_back(seeds[seedPos]);
        }
        const int u = queue[head++];
        std::fill(score.begin(), score.end(), 0.0);
        for (int i = H.incOff[u]; i < H.incOff[u + 1]; ++i) {
            const int e = H.inc[i];
            const int pins = H.numPins(e);
            const double w = H.netW[e];
            const int src = H.netSrc[e];
            if (src == u) {
                // u drives the net: every assigned sink FPGA costs D(f, sinkFpga) once.
                if (pins > ratingNetLimit) continue;
                for (int j = H.sinkOff[e]; j < H.sinkOff[e + 1]; ++j) {
                    const int p = part[H.sinks[j]];
                    if (p < 0) continue;
                    for (int f = 0; f < K; ++f) score[f] += w * T.D[f * K + p] / std::max(1, pins - 1) * 2;
                }
            } else {
                const int p = part[src];
                if (p >= 0)
                    for (int f = 0; f < K; ++f) score[f] += w * T.D[p * K + f];
            }
            if (pins <= ratingNetLimit) {
                if (!seen[src]) {
                    seen[src] = 1;
                    queue.push_back(src);
                }
                for (int j = H.sinkOff[e]; j < H.sinkOff[e + 1]; ++j) {
                    const int x = H.sinks[j];
                    if (!seen[x]) {
                        seen[x] = 1;
                        queue.push_back(x);
                    }
                }
            }
        }
        const int f = pickFpga(T, score, l1, l2, H.w1[u], H.w2[u]);
        part[u] = f;
        l1[f] += H.w1[u];
        l2[f] += H.w2[u];
        ++placed;
    }
    return part;
}

std::vector<int> randomInit(const Hypergraph& H, const Topology& T, Rng& rng) {
    const int n = H.n, K = T.K;
    std::vector<int> part(n, -1);
    std::vector<long long> l1(K, 0), l2(K, 0);
    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    // Place large nodes first.
    rng.shuffle(order);
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return H.w1[a] + H.w2[a] > H.w1[b] + H.w2[b]; });
    std::vector<double> score(K);
    for (int u : order) {
        for (int f = 0; f < K; ++f) score[f] = rng.uniform();
        const int f = pickFpga(T, score, l1, l2, H.w1[u], H.w2[u]);
        part[u] = f;
        l1[f] += H.w1[u];
        l2[f] += H.w2[u];
    }
    return part;
}

std::vector<int> Hierarchy::projectDown(int l, const std::vector<int>& part) const {
    const std::vector<int>& cmap = maps[l - 1];
    std::vector<int> fp(cmap.size());
    for (size_t v = 0; v < cmap.size(); ++v) fp[v] = part[cmap[v]];
    return fp;
}

void buildHierarchy(Hierarchy& hier, const Hypergraph& H0, const Topology& T, const std::vector<int>* constraint,
                    int stopN, const Params& prm, Rng& rng, const Timer& timer, double deadline) {
    const int K = T.K;
    int minCap1 = std::numeric_limits<int>::max(), minCap2 = std::numeric_limits<int>::max();
    for (int f = 0; f < K; ++f) {
        if (T.cap1[f] > 0) minCap1 = std::min(minCap1, T.cap1[f]);
        if (T.cap2[f] > 0) minCap2 = std::min(minCap2, T.cap2[f]);
    }
    if (minCap1 == std::numeric_limits<int>::max()) minCap1 = 1;
    if (minCap2 == std::numeric_limits<int>::max()) minCap2 = 1;
    const int maxW1 = std::max(1, (int)(minCap1 * prm.clusterFrac));
    const int maxW2 = std::max(1, (int)(minCap2 * prm.clusterFrac));

    hier.base = &H0;
    hier.levels.clear();
    hier.maps.clear();
    hier.cons.clear();
    if (constraint) hier.cons.push_back(*constraint);
    const Hypergraph* cur = &H0;
    while (cur->n > stopN) {
        std::vector<int> cmap;
        const int nc = clusterNodes(*cur, maxW1, maxW2, constraint ? &hier.cons.back() : nullptr, prm.ratingNetLimit,
                                    rng, cmap);
        if (nc > cur->n * 0.97) break;
        hier.levels.push_back(std::make_unique<Hypergraph>(contract(*cur, cmap, nc)));
        if (constraint) {
            std::vector<int> c(nc);
            for (int v = 0; v < cur->n; ++v) c[cmap[v]] = hier.cons.back()[v];
            hier.cons.push_back(std::move(c));
        }
        hier.maps.push_back(std::move(cmap));
        cur = hier.levels.back().get();
        if (timer.elapsed() > deadline) break;
    }
}

std::vector<int> multilevel(const Hypergraph& H0, const Topology& T, const std::vector<int>* constraint,
                            const Params& prm, Rng& rng, const Timer& timer, double deadline) {
    const int K = T.K;
    Hierarchy hier;
    buildHierarchy(hier, H0, T, constraint, std::max(prm.coarsestPerFpga * K, 200), prm, rng, timer, deadline);
    const int L = hier.depth();

    // Initial partition at the coarsest level.
    const Hypergraph& C = hier.graph(L);
    std::vector<int> part;
    {
        Refiner R(C, T);
        if (constraint) {
            part = hier.cons.back();
            R.init(part);
            R.greedyRefine(rng, true, prm.refinePasses, deadline, timer);
            part = R.part();
        } else {
            long long bestJ = std::numeric_limits<long long>::max();
            for (int t = 0; t < prm.initialTrials; ++t) {
                std::vector<int> p = (t % 3 == 2) ? randomInit(C, T, rng) : greedyGrowInit(C, T, prm.ratingNetLimit, rng);
                R.init(p);
                R.greedyRefine(rng, true, 4 * prm.refinePasses, deadline, timer);
                const long long J = R.capacityFeasible() ? R.objective() : std::numeric_limits<long long>::max() / 2;
                if (J < bestJ || part.empty()) {
                    bestJ = J;
                    part = R.part();
                }
                if (timer.elapsed() > deadline) break;
            }
        }
    }

    // Uncoarsening with refinement (the finest level is refined by the caller).
    for (int l = L; l > 0; --l) {
        part = hier.projectDown(l, part);
        if (l - 1 == 0) break;
        Refiner R(hier.graph(l - 1), T);
        R.init(part);
        R.greedyRefine(rng, false, prm.refinePasses, deadline, timer);
        part = R.part();
    }
    return part;
}
