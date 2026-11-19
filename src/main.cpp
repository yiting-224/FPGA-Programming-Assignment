// Topology-aware multi-FPGA partitioner.
//
// Usage: ./partitioner <input_file> <output_file> [time_limit_seconds]
//
// The time limit defaults to 240 s (the official limit is 300 s); it can
// also be set with the environment variable PARTITIONER_TIME_LIMIT.
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "hypergraph.hpp"
#include "partitioner.hpp"
#include "refiner.hpp"
#include "util.hpp"

namespace {

bool verbose = false;

void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void logf(const char* fmt, ...) {
    if (!verbose) return;
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
}

struct Best {
    std::vector<int> part;
    bool legal = false;
    long long J = std::numeric_limits<long long>::max();

    // Returns true if (legal, J) is better than the stored solution.
    bool better(bool lg, long long j) const {
        if (part.empty()) return true;
        if (lg != legal) return lg;
        return j < J;
    }
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "Usage: %s <input_file> <output_file> [time_limit_seconds]\n", argv[0]);
        return 1;
    }
    Timer timer;
    double timeLimit = 240.0;
    if (const char* env = std::getenv("PARTITIONER_TIME_LIMIT")) timeLimit = std::atof(env);
    if (argc >= 4) timeLimit = std::atof(argv[3]);
    if (std::getenv("PARTITIONER_VERBOSE")) verbose = true;

    Hypergraph H;
    Topology T;
    std::vector<int> nodeType;
    readInput(argv[1], H, T, nodeType);
    logf("parsed: n=%d m=%d K=%d (%.2fs)\n", H.n, H.m, T.K, timer.elapsed());

    // Reserve time for writing the output.
    const double endTime = std::max(1.0, timeLimit - 2.0 - H.n * 1e-6);

    Params prm;
    Rng rng(20261119ULL);
    double tsHigh = 1.0, tsLow = 0.05, coarseFrac = 0.55;
    int saMaxNodes = 100000;
    if (const char* e = std::getenv("PARTITIONER_TSHIGH")) tsHigh = std::atof(e);
    if (const char* e = std::getenv("PARTITIONER_TSLOW")) tsLow = std::atof(e);
    if (const char* e = std::getenv("PARTITIONER_COARSEFRAC")) coarseFrac = std::atof(e);
    if (const char* e = std::getenv("PARTITIONER_SAMAX")) saMaxNodes = std::atoi(e);

    Refiner R(H, T);
    Best best;
    auto record = [&](Refiner& Rf, const char* tag) {
        const bool lg = Rf.legal();
        const long long J = Rf.objective();
        logf("%-8s t=%.1fs J=%lld cost=%lld bad=%lld ovf=%lld legal=%d\n", tag, timer.elapsed(), J, Rf.cost(), Rf.bad(),
             Rf.overflow(), (int)lg);
        if (best.better(lg, J)) {
            best.part = Rf.part();
            best.legal = lg;
            best.J = J;
            return true;
        }
        return false;
    };

    // Phase 1: multilevel partitioning (coarsening, initial partitioning of
    // the coarsest hypergraph, uncoarsening with greedy refinement).
    {
        std::vector<int> p = multilevel(H, T, nullptr, prm, rng, timer, endTime);
        R.init(p);
        R.greedyRefine(rng, false, prm.refinePasses * 2, endTime, timer);
        record(R, "ml");
    }

    // Phase 2: multilevel simulated annealing. A partition-preserving
    // hierarchy is built from the best partition; the coarsest level (at most
    // saTop nodes) is annealed at a high temperature, then every finer level
    // is annealed in turn with a lower temperature.
    double beta = 0.5, decay = 0.6;
    int saTop = 5000, rounds = 1;
    if (const char* e = std::getenv("PARTITIONER_SATOP")) saTop = std::atoi(e);
    if (const char* e = std::getenv("PARTITIONER_BETA")) beta = std::atof(e);
    if (const char* e = std::getenv("PARTITIONER_DECAY")) decay = std::atof(e);
    if (const char* e = std::getenv("PARTITIONER_ROUNDS")) rounds = std::atoi(e);
    (void)saMaxNodes;
    (void)coarseFrac;
    for (int round = 0; round < rounds && timer.elapsed() < endTime; ++round) {
        const double roundEnd = timer.elapsed() + (endTime - timer.elapsed()) / (rounds - round);
        Hierarchy hier;
        buildHierarchy(hier, H, T, &best.part, saTop, prm, rng, timer, roundEnd);
        const int L = hier.depth();
        std::vector<double> w(L + 1);
        for (int l = 0; l <= L; ++l) w[l] = std::pow((double)hier.graph(l).n, beta);
        std::vector<int> part = hier.cons.back();
        for (int l = L; l >= 0; --l) {
            double wsum = 0;
            for (int j = 0; j <= l; ++j) wsum += w[j];
            const double now = timer.elapsed();
            const double levelEnd = now + std::max(0.0, roundEnd - now) * (w[l] / wsum);
            Refiner* Rl;
            std::unique_ptr<Refiner> own;
            if (l == 0) {
                Rl = &R;
            } else {
                own = std::make_unique<Refiner>(hier.graph(l), T);
                Rl = own.get();
            }
            Rl->init(part);
            Rl->setTempScale(tsHigh * std::pow(decay, L - l));
            Rl->anneal(rng, levelEnd, timer);
            Rl->greedyRefine(rng, false, prm.refinePasses, roundEnd + 1.0, timer);
            logf("  level %d n=%d evals=%lld t0=%.2f J=%lld t=%.1f\n", l, hier.graph(l).n, Rl->statEval_, Rl->statT0_,
                 Rl->objective(), timer.elapsed());
            part = Rl->part();
            if (l > 0) part = hier.projectDown(l, part);
        }
        record(R, "mlsa");
    }
    (void)tsLow;

    // Final report.
    R.init(best.part);
    logf("final: TLC=%lld legal=%d bad=%lld ovf=%lld time=%.1fs\n", R.tlc(), (int)R.legal(), R.bad(), R.overflow(),
         timer.elapsed());
    if (!writeOutput(argv[2], best.part)) {
        std::fprintf(stderr, "error: cannot write output\n");
        return 1;
    }
    return 0;
}
