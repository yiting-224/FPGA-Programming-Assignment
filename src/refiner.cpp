#include "refiner.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace {
// Nets with more sinks than this do not contribute candidate FPGAs (their
// sinks are everywhere anyway); their source FPGA still does.
constexpr int kCandNetLimit = 32;
}  // namespace

Refiner::Refiner(const Hypergraph& H, const Topology& T) : H_(H), T_(T) {
    lambdaIO_ = T.big;
    const int K = T.K;
    pcOff_.assign(H.m, 0);
    dense_.assign(H.m, 0);
    nUsed_.assign(H.m, 0);
    occ_.assign(H.m, 0);
    size_t total = 0;
    for (int e = 0; e < H.m; ++e) {
        const int ns = H.numSinks(e);
        const int slots = std::min(ns, K);
        pcOff_[e] = (int)total;
        if (ns >= K || slots > 8) {
            dense_[e] = 1;
            total += K;
        } else {
            total += 2 * slots;
        }
    }
    pc_.assign(total, 0);
    io_.assign(K, 0);
    load1_.assign(K, 0);
    load2_.assign(K, 0);
    dio_.assign(K, 0);
    dioMark_.assign(K, 0);
    stamp_.assign(K, 0);
    unlinked_.assign((size_t)K * K, 0);
    for (int i = 0; i < K * K; ++i) unlinked_[i] = T.adj[i] ? 0 : 1;
}

void Refiner::setLambda(long long l) {
    lambdaIO_ = l;
}

void Refiner::incCnt(int e, int f) {
    const int off = pcOff_[e];
    if (dense_[e]) {
        if (pc_[off + f]++ == 0) occ_[e]++;
        return;
    }
    const int u = nUsed_[e];
    for (int i = 0; i < u; ++i) {
        if (pc_[off + 2 * i] == f) {
            pc_[off + 2 * i + 1]++;
            return;
        }
    }
    pc_[off + 2 * u] = f;
    pc_[off + 2 * u + 1] = 1;
    nUsed_[e] = u + 1;
    occ_[e]++;
}

void Refiner::decCnt(int e, int f) {
    const int off = pcOff_[e];
    if (dense_[e]) {
        if (--pc_[off + f] == 0) occ_[e]--;
        return;
    }
    const int u = nUsed_[e];
    for (int i = 0; i < u; ++i) {
        if (pc_[off + 2 * i] == f) {
            if (--pc_[off + 2 * i + 1] == 0) {
                pc_[off + 2 * i] = pc_[off + 2 * (u - 1)];
                pc_[off + 2 * i + 1] = pc_[off + 2 * (u - 1) + 1];
                nUsed_[e] = u - 1;
                occ_[e]--;
            }
            return;
        }
    }
}

void Refiner::init(const std::vector<int>& part) {
    const int K = T_.K;
    part_ = part;
    std::fill(pc_.begin(), pc_.end(), 0);
    std::fill(nUsed_.begin(), nUsed_.end(), 0);
    std::fill(occ_.begin(), occ_.end(), 0);
    std::fill(io_.begin(), io_.end(), 0);
    std::fill(load1_.begin(), load1_.end(), 0);
    std::fill(load2_.begin(), load2_.end(), 0);
    for (int v = 0; v < H_.n; ++v) {
        load1_[part_[v]] += H_.w1[v];
        load2_[part_[v]] += H_.w2[v];
    }
    cost_ = 0;
    bad_ = 0;
    for (int e = 0; e < H_.m; ++e) {
        for (int i = H_.sinkOff[e]; i < H_.sinkOff[e + 1]; ++i) incCnt(e, part_[H_.sinks[i]]);
        const int s = part_[H_.netSrc[e]];
        const long long w = H_.netW[e];
        const int nR = occ_[e] - (cnt(e, s) > 0);
        if (nR > 0) {
            io_[s] += w;
            forEachOcc(e, [&](int f) {
                if (f != s) {
                    io_[f] += w;
                    cost_ += w * T_.D[s * K + f];
                    bad_ += w * unlinked_[s * K + f];
                }
            });
        }
    }
    overflow_ = 0;
    for (int f = 0; f < K; ++f) overflow_ += std::max(0LL, io_[f] - T_.ioLim[f]);
}

template <bool COMMIT>
long long Refiner::eval(int v, int b) {
    const int a = part_[v];
    const int K = T_.K;
    const int* D = T_.D.data();
    const char* U = unlinked_.data();
    long long dc = 0, dbad = 0;
    for (int i = H_.incOff[v]; i < H_.incOff[v + 1]; ++i) {
        const int e = H_.inc[i];
        const long long w = H_.netW[e];
        if (H_.netSrc[e] == v) {
            const int ca = cnt(e, a), cb = cnt(e, b);
            const int oc = occ_[e];
            const int oldNon = oc - (ca > 0), newNon = oc - (cb > 0);
            const int* Da = D + a * K;
            const int* Db = D + b * K;
            const char* Ua = U + a * K;
            const char* Ub = U + b * K;
            long long oldC = 0, newC = 0;
            int oldU = 0, newU = 0;
            forEachOcc(e, [&](int f) {
                oldC += Da[f];
                newC += Db[f];
                oldU += Ua[f];
                newU += Ub[f];
            });
            dc += w * (newC - oldC);
            dbad += w * (newU - oldU);
            if (oldNon > 0 && newNon > 0) {
                if (ca == 0) addIO(a, -w);
                if (cb == 0) addIO(b, w);
            } else {
                if (oldNon > 0) {
                    forEachOcc(e, [&](int f) { addIO(f, -w); });
                    if (ca == 0) addIO(a, -w);
                }
                if (newNon > 0) {
                    forEachOcc(e, [&](int f) { addIO(f, w); });
                    if (cb == 0) addIO(b, w);
                }
            }
        } else {
            const int s = part_[H_.netSrc[e]];
            const int ca = cnt(e, a), cb = cnt(e, b);
            const bool remA = (ca == 1 && a != s);
            const bool addB = (cb == 0 && b != s);
            if (remA || addB) {
                if (addB) {
                    dc += w * D[s * K + b];
                    dbad += w * U[s * K + b];
                }
                if (remA) {
                    dc -= w * D[s * K + a];
                    dbad -= w * U[s * K + a];
                }
                const int cs = (s == a) ? ca : (s == b) ? cb : cnt(e, s);
                const int nR = occ_[e] - (cs > 0);
                const int nR2 = nR - (int)remA + (int)addB;
                if (remA) addIO(a, -w);
                if (addB) addIO(b, w);
                if ((nR > 0) != (nR2 > 0)) addIO(s, nR2 > 0 ? w : -w);
            }
            if (COMMIT) {
                decCnt(e, a);
                incCnt(e, b);
            }
        }
    }
    long long dov = 0;
    for (int f : dioTouched_) {
        const long long cur = io_[f], nw = cur + dio_[f];
        const long long lim = T_.ioLim[f];
        dov += std::max(0LL, nw - lim) - std::max(0LL, cur - lim);
        if (COMMIT) io_[f] = nw;
        dio_[f] = 0;
        dioMark_[f] = 0;
    }
    dioTouched_.clear();
    if (COMMIT) {
        cost_ += dc;
        bad_ += dbad;
        overflow_ += dov;
        part_[v] = b;
        load1_[a] -= H_.w1[v];
        load2_[a] -= H_.w2[v];
        load1_[b] += H_.w1[v];
        load2_[b] += H_.w2[v];
    }
    return dc + lambdaIO_ * dov;
}

template long long Refiner::eval<true>(int, int);
template long long Refiner::eval<false>(int, int);

long long Refiner::unlinkedPins() const {
    const int K = T_.K;
    long long bad = 0;
    for (int e = 0; e < H_.m; ++e) {
        const int s = part_[H_.netSrc[e]];
        forEachOcc(e, [&](int f) {
            if (!T_.adj[s * K + f]) bad += H_.netW[e];
        });
    }
    return bad;
}

long long Refiner::tlc() const {
    const int K = T_.K;
    long long c = 0;
    for (int e = 0; e < H_.m; ++e) {
        const int s = part_[H_.netSrc[e]];
        forEachOcc(e, [&](int f) {
            if (f != s) c += (long long)H_.netW[e] * T_.D[s * K + f];
        });
    }
    return c;
}

bool Refiner::capacityFeasible() const {
    for (int f = 0; f < T_.K; ++f)
        if (load1_[f] > T_.cap1[f] || load2_[f] > T_.cap2[f]) return false;
    return true;
}

bool Refiner::legal() const {
    return overflow_ == 0 && bad_ == 0 && capacityFeasible();
}

bool Refiner::isBoundary(int v) {
    const int a = part_[v];
    for (int i = H_.incOff[v]; i < H_.incOff[v + 1]; ++i) {
        const int e = H_.inc[i];
        const int src = H_.netSrc[e];
        if (src == v) {
            if (occ_[e] - (cnt(e, a) > 0) > 0) return true;
        } else if (part_[src] != a) {
            return true;
        }
    }
    return false;
}

void Refiner::collectCandidates(int v, std::vector<int>& out) {
    out.clear();
    if (++stampId_ == 0x7fffffff) {
        std::fill(stamp_.begin(), stamp_.end(), 0);
        stampId_ = 1;
    }
    const int a = part_[v];
    stamp_[a] = stampId_;
    auto add = [&](int f) {
        if (stamp_[f] != stampId_) {
            stamp_[f] = stampId_;
            out.push_back(f);
        }
    };
    for (int i = H_.incOff[v]; i < H_.incOff[v + 1]; ++i) {
        const int e = H_.inc[i];
        const int src = H_.netSrc[e];
        if (src != v) add(part_[src]);
        if (H_.numSinks(e) <= kCandNetLimit) forEachOcc(e, add);
    }
}

long long Refiner::greedyPass(const std::vector<int>& order, bool allTargets) {
    long long total = 0;
    std::vector<int> cands;
    const int K = T_.K;
    for (int v : order) {
        if (!allTargets && !isBoundary(v)) continue;
        const int a = part_[v];
        if (allTargets) {
            cands.clear();
            for (int f = 0; f < K; ++f)
                if (f != a) cands.push_back(f);
        } else {
            collectCandidates(v, cands);
        }
        long long best = 0;
        int bb = -1;
        for (int b : cands) {
            if (!capacityOk(v, b)) continue;
            const long long d = delta(v, b);
            if (d < best) {
                best = d;
                bb = b;
            }
        }
        if (bb >= 0) total += apply(v, bb);
    }
    return total;
}

void Refiner::greedyRefine(Rng& rng, bool allTargets, int maxPasses, double deadline, const Timer& timer) {
    std::vector<int> order(H_.n);
    std::iota(order.begin(), order.end(), 0);
    for (int pass = 0; pass < maxPasses; ++pass) {
        if (timer.elapsed() > deadline) break;
        rng.shuffle(order);
        const long long before = objective();
        const long long d = greedyPass(order, allTargets);
        if (d == 0) break;
        if (-d * 10000 < before) break;  // < 0.01% improvement
    }
}

long long Refiner::swapPass(Rng& rng, double deadline, const Timer& timer) {
    // For every boundary node v whose best target b is blocked by capacity,
    // try to swap it with a node u currently in b that is willing to move to
    // part[v] (combined delta must be negative).
    (void)rng;
    const int K = T_.K;
    long long total = 0;
    // Index nodes by FPGA for quick partner search.
    std::vector<std::vector<int>> members(K);
    for (int v = 0; v < H_.n; ++v)
        if (isBoundary(v)) members[part_[v]].push_back(v);
    std::vector<int> cands;
    std::vector<int> order;
    for (int f = 0; f < K; ++f) order.insert(order.end(), members[f].begin(), members[f].end());
    rng.shuffle(order);
    int iter = 0;
    for (int v : order) {
        if ((++iter & 255) == 0 && timer.elapsed() > deadline) break;
        const int a = part_[v];
        collectCandidates(v, cands);
        for (int b : cands) {
            if (capacityOk(v, b)) continue;  // plain moves handled elsewhere
            const long long dv = delta(v, b);
            if (dv >= 0) continue;
            // find partner in b
            auto& mb = members[b];
            long long bestTot = 0;
            int bu = -1;
            int tries = 0;
            for (size_t k = 0; k < mb.size() && tries < 64; ++k) {
                const int u = mb[(rng.below((uint32_t)mb.size()))];
                if (part_[u] != b || u == v) continue;
                ++tries;
                if (H_.w1[u] != H_.w1[v] || H_.w2[u] != H_.w2[v]) {
                    // generic capacity check after swap
                    if (load1_[b] - H_.w1[u] + H_.w1[v] > T_.cap1[b] || load2_[b] - H_.w2[u] + H_.w2[v] > T_.cap2[b])
                        continue;
                    if (load1_[a] - H_.w1[v] + H_.w1[u] > T_.cap1[a] || load2_[a] - H_.w2[v] + H_.w2[u] > T_.cap2[a])
                        continue;
                }
                const long long du = delta(u, a);
                if (dv + du >= bestTot) continue;
                // exact combined delta
                const long long d1 = apply(v, b);
                const long long d2 = delta(u, a);
                apply(v, a);  // undo
                (void)d1;
                if (d1 + d2 < bestTot) {
                    bestTot = d1 + d2;
                    bu = u;
                }
            }
            if (bu >= 0) {
                // capacity: temporarily violated between the two moves, fine.
                total += apply(v, b);
                total += apply(bu, a);
                mb.push_back(v);
                members[a].push_back(bu);
                break;
            }
        }
    }
    return total;
}

void Refiner::anneal(Rng& rng, double deadline, const Timer& timer) {
    // Moves are sampled from boundary nodes (list rebuilt periodically).
    // The best state is tracked lazily: `dirty` holds the nodes whose FPGA
    // changed since `bestPart` was last synchronised with the state. On
    // return the state holds the best partition seen.
    std::vector<int> bnodes;
    std::vector<std::vector<int>> byPart(T_.K);  // boundary nodes per FPGA (may be stale)
    std::vector<int> cands;
    auto rebuild = [&]() {
        bnodes.clear();
        for (auto& l : byPart) l.clear();
        for (int v = 0; v < H_.n; ++v)
            if (isBoundary(v)) {
                bnodes.push_back(v);
                byPart[part_[v]].push_back(v);
            }
    };
    rebuild();
    if (bnodes.empty()) return;
    std::vector<int> bestPart = part_;
    Key bestKey = key();
    std::vector<char> isDirty(H_.n, 0);
    std::vector<int> dirty;
    auto touch = [&](int v) {
        if (!isDirty[v]) {
            isDirty[v] = 1;
            dirty.push_back(v);
        }
    };
    auto checkBest = [&]() {
        const Key k = key();
        if (k < bestKey) {
            bestKey = k;
            for (int y : dirty) {
                bestPart[y] = part_[y];
                isDirty[y] = 0;
            }
            dirty.clear();
        }
    };

    // Initial temperature from the average uphill delta of random moves.
    double sumUp = 0;
    int nUp = 0;
    for (int i = 0; i < 4000; ++i) {
        const int v = bnodes[rng.below((uint32_t)bnodes.size())];
        collectCandidates(v, cands);
        if (cands.empty()) continue;
        const int b = cands[rng.below((uint32_t)cands.size())];
        if (!capacityOk(v, b)) continue;
        const long long d = delta(v, b);
        if (d > 0 && d < T_.big) {
            sumUp += (double)d;
            ++nUp;
        }
    }
    const double t0 = std::max(0.05, nUp ? tempScale_ * sumUp / nUp : 1.0);
    statEval_ = statAcc_ = statBlocked_ = 0;
    statT0_ = t0;
    const double t1 = std::min(t0, 0.05);
    const double start = timer.elapsed();
    const double span = std::max(1e-3, deadline - start);
    double temp = t0;
    double lastRebuild = start;
    long long it = 0;
    while (true) {
        if ((++it & 1023) == 0) {
            const double now = timer.elapsed();
            if (now >= deadline) break;
            const double frac = (now - start) / span;
            temp = t0 * std::pow(t1 / t0, frac);
            if (now - lastRebuild > std::max(0.2, span / 100)) {
                rebuild();
                lastRebuild = now;
                if (bnodes.empty()) break;
            }
        }
        const int v = bnodes[rng.below((uint32_t)bnodes.size())];
        collectCandidates(v, cands);
        if (cands.empty()) continue;
        const int b = cands[rng.below((uint32_t)cands.size())];
        const int a = part_[v];
        if (!capacityOk(v, b)) {
            ++statBlocked_;
            // Swap move: find a boundary node u in b that can go to a.
            int u = -1;
            const std::vector<int>& pool = byPart[b];
            if (pool.empty()) continue;
            for (int t = 0; t < 8; ++t) {
                const int x = pool[rng.below((uint32_t)pool.size())];
                if (part_[x] != b) continue;
                if (load1_[b] - H_.w1[x] + H_.w1[v] > T_.cap1[b] || load2_[b] - H_.w2[x] + H_.w2[v] > T_.cap2[b])
                    continue;
                if (load1_[a] - H_.w1[v] + H_.w1[x] > T_.cap1[a] || load2_[a] - H_.w2[v] + H_.w2[x] > T_.cap2[a])
                    continue;
                u = x;
                break;
            }
            if (u < 0) continue;
            const long long d1 = apply(v, b);
            const long long d2 = delta(u, a);
            const long long d = d1 + d2;
            ++statEval_;
            if (d <= 0 || rng.uniform() < std::exp(-(double)d / temp)) {
                ++statAcc_;
                apply(u, a);
                touch(v);
                touch(u);
                byPart[b].push_back(v);
                byPart[a].push_back(u);
                if (d < 0) checkBest();
            } else {
                apply(v, a);
            }
            continue;
        }
        const long long d = delta(v, b);
        ++statEval_;
        if (d <= 0 || rng.uniform() < std::exp(-(double)d / temp)) {
            ++statAcc_;
            apply(v, b);
            touch(v);
            byPart[b].push_back(v);
            if (d < 0) checkBest();
        }
    }
    checkBest();
    if (!dirty.empty()) init(bestPart);
}
