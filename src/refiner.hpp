// Incremental partition state and move-based refinement.
#pragma once

#include <cstdlib>
#include <utility>
#include <vector>

#include "hypergraph.hpp"
#include "util.hpp"

// Objective used during optimisation:
//   J = TLC' + lambdaIO * (sum over FPGAs of I/O overflow)
// where TLC' is the total link cost in which an unlinked FPGA pair costs
// Topology::big per signal. A partition is legal iff it satisfies the
// capacities (always enforced as a hard constraint by every move) and J
// contains no penalty term.
class Refiner {
public:
    Refiner(const Hypergraph& H, const Topology& T);

    // Loads a complete partition and builds every incremental structure.
    void init(const std::vector<int>& part);

    // Change of J if node v moves to FPGA b (b != part[v]).
    long long delta(int v, int b) { return eval<false>(v, b); }
    // Moves node v to FPGA b and returns the change of J.
    long long apply(int v, int b) { return eval<true>(v, b); }

    bool capacityOk(int v, int b) const {
        return load1_[b] + H_.w1[v] <= T_.cap1[b] && load2_[b] + H_.w2[v] <= T_.cap2[b];
    }

    long long objective() const { return cost_ + lambdaIO_ * overflow_; }
    long long cost() const { return cost_; }            // TLC incl. big penalties
    long long bad() const { return bad_; }              // signals over missing links
    long long overflow() const { return overflow_; }    // total I/O overflow
    long long unlinkedPins() const;                     // signals over missing links
    long long tlc() const;                              // exact TLC (only meaningful if legal)
    bool legal() const;
    bool capacityFeasible() const;

    const std::vector<int>& part() const { return part_; }

    // One greedy pass over the nodes in `order`. If `allTargets` every FPGA
    // is tried, otherwise only FPGAs containing neighbours. Moves with
    // strictly negative delta are applied. Returns the total delta.
    long long greedyPass(const std::vector<int>& order, bool allTargets);

    // Repeats greedy passes until the relative improvement is tiny.
    void greedyRefine(Rng& rng, bool allTargets, int maxPasses, double deadline, const Timer& timer);

    // Pairwise swaps between FPGAs to escape capacity-blocked local minima.
    long long swapPass(Rng& rng, double deadline, const Timer& timer);

    // Simulated annealing on single node moves (cost-only, legality kept by
    // the penalty terms). Keeps track of the best legal partition found.
    // On return the state holds the best partition seen (see key()).
    void anneal(Rng& rng, double deadline, const Timer& timer);

    // Lexicographic quality: (violations, TLC). Lower is better.
    using Key = std::pair<long long, long long>;
    Key key() const { return {bad_ + overflow_, cost_ - (long long)T_.big * bad_}; }

    bool isBoundary(int v);
    void collectCandidates(int v, std::vector<int>& out);

    void setLambda(long long l);
    void setTempScale(double t) { tempScale_ = t; }
    long long statEval_ = 0, statAcc_ = 0, statBlocked_ = 0;
    double statT0_ = 0;

private:
    template <bool COMMIT>
    long long eval(int v, int b);

    // Sink pin-count of net e in FPGA f.
    int cnt(int e, int f) const {
        const int off = pcOff_[e];
        if (dense_[e]) return pc_[off + f];
        const int u = nUsed_[e];
        for (int i = 0; i < u; ++i)
            if (pc_[off + 2 * i] == f) return pc_[off + 2 * i + 1];
        return 0;
    }
    void incCnt(int e, int f);
    void decCnt(int e, int f);

    template <class F>
    void forEachOcc(int e, F&& fn) const {
        const int off = pcOff_[e];
        if (dense_[e]) {
            for (int f = 0; f < T_.K; ++f)
                if (pc_[off + f] > 0) fn(f);
        } else {
            const int u = nUsed_[e];
            for (int i = 0; i < u; ++i) fn(pc_[off + 2 * i]);
        }
    }

    void addIO(int f, long long d) {
        if (dio_[f] == 0 && !dioMark_[f]) {
            dioMark_[f] = 1;
            dioTouched_.push_back(f);
        }
        dio_[f] += d;
    }

    const Hypergraph& H_;
    const Topology& T_;
    long long lambdaIO_;
    double tempScale_ = 0.3;

public:
    bool randTargets_ = std::getenv("NO_RT") == nullptr;

private:

    std::vector<int> part_;
    std::vector<int> pcOff_;
    std::vector<char> dense_;
    std::vector<int> pc_;
    std::vector<int> nUsed_;
    std::vector<int> occ_;  // number of FPGAs holding at least one sink

    std::vector<long long> io_;
    std::vector<long long> load1_, load2_;
    long long cost_ = 0;
    long long bad_ = 0;
    long long overflow_ = 0;
    std::vector<char> unlinked_;

    // scratch
    std::vector<long long> dio_;
    std::vector<char> dioMark_;
    std::vector<int> dioTouched_;
    std::vector<int> stamp_;
    int stampId_ = 0;
};
