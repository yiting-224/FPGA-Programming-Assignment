// Clustering-based coarsening.
#include <algorithm>
#include <numeric>

#include "partitioner.hpp"

int clusterNodes(const Hypergraph& H, int maxW1, int maxW2, const std::vector<int>* constraint, int ratingNetLimit,
                 Rng& rng, std::vector<int>& cmap) {
    const int n = H.n;
    cmap.assign(n, -1);
    std::vector<int> cw1, cw2;
    cw1.reserve(n);
    cw2.reserve(n);
    std::vector<double> rateC(n, 0.0), rateN(n, 0.0);
    std::vector<int> touchedC, touchedN;
    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    rng.shuffle(order);
    int nc = 0;

    for (int u : order) {
        if (cmap[u] != -1) continue;
        const int cu = constraint ? (*constraint)[u] : 0;
        auto addRate = [&](int x, double r) {
            if (x == u) return;
            if (constraint && (*constraint)[x] != cu) return;
            const int c = cmap[x];
            if (c >= 0) {
                if (rateC[c] == 0.0) touchedC.push_back(c);
                rateC[c] += r;
            } else {
                if (rateN[x] == 0.0) touchedN.push_back(x);
                rateN[x] += r;
            }
        };
        for (int i = H.incOff[u]; i < H.incOff[u + 1]; ++i) {
            const int e = H.inc[i];
            const int pins = H.numPins(e);
            if (pins > ratingNetLimit) continue;
            const double r = (double)H.netW[e] / (pins - 1);
            addRate(H.netSrc[e], r);
            for (int j = H.sinkOff[e]; j < H.sinkOff[e + 1]; ++j) addRate(H.sinks[j], r);
        }
        const double su = H.w1[u] + H.w2[u];
        double best = 0.0;
        int bestC = -1, bestN = -1;
        for (int c : touchedC) {
            if (cw1[c] + H.w1[u] <= maxW1 && cw2[c] + H.w2[u] <= maxW2) {
                const double sc = rateC[c] / (su * (double)(cw1[c] + cw2[c]));
                if (sc > best) {
                    best = sc;
                    bestC = c;
                    bestN = -1;
                }
            }
            rateC[c] = 0.0;
        }
        for (int x : touchedN) {
            if (H.w1[x] + H.w1[u] <= maxW1 && H.w2[x] + H.w2[u] <= maxW2) {
                const double sc = rateN[x] / (su * (double)(H.w1[x] + H.w2[x]));
                if (sc > best) {
                    best = sc;
                    bestN = x;
                    bestC = -1;
                }
            }
            rateN[x] = 0.0;
        }
        touchedC.clear();
        touchedN.clear();
        if (bestC >= 0) {
            cmap[u] = bestC;
            cw1[bestC] += H.w1[u];
            cw2[bestC] += H.w2[u];
        } else if (bestN >= 0) {
            cmap[u] = cmap[bestN] = nc++;
            cw1.push_back(H.w1[u] + H.w1[bestN]);
            cw2.push_back(H.w2[u] + H.w2[bestN]);
        } else {
            cmap[u] = nc++;
            cw1.push_back(H.w1[u]);
            cw2.push_back(H.w2[u]);
        }
    }
    return nc;
}
