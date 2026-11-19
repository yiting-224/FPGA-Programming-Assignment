// Topology-aware multi-FPGA partitioner.
//
// Usage: ./partitioner <input_file> <output_file> [time_limit_seconds]
//
// The time limit defaults to 250 s (the official limit is 300 s); it can
// also be set with the environment variable PARTITIONER_TIME_LIMIT.
// Other optional environment variables (for experiments):
//   PARTITIONER_THREADS   number of worker threads (default: min(4, #cores))
//   PARTITIONER_SEED      base random seed
//   PARTITIONER_VERBOSE   print progress to stderr
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "hypergraph.hpp"
#include "partitioner.hpp"
#include "refiner.hpp"
#include "util.hpp"

namespace {

bool verbose = false;
std::mutex logMutex;

void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void logf(const char* fmt, ...) {
    if (!verbose) return;
    std::lock_guard<std::mutex> lock(logMutex);
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
}

double envDouble(const char* name, double def) {
    const char* e = std::getenv(name);
    return e ? std::atof(e) : def;
}

// Best solution shared by all workers.
struct SharedBest {
    std::mutex mu;
    std::vector<int> part;
    Refiner::Key key{std::numeric_limits<long long>::max(), std::numeric_limits<long long>::max()};

    void offer(const std::vector<int>& p, Refiner::Key k) {
        std::lock_guard<std::mutex> lock(mu);
        if (part.empty() || k < key) {
            part = p;
            key = k;
        }
    }
};

struct Config {
    double tsHigh = 2.0;   // temperature scale on the coarsest annealed level
    double decay = 0.05;   // temperature scale factor per finer level
    double beta = -1.0;    // time share of a level is proportional to n^beta
    int saTop = 5000;      // coarsest annealed level has at most this many nodes
};

// One optimisation attempt:
//  1. multilevel partitioning (coarsen, initial partition, uncoarsen with
//     greedy refinement);
//  2. multilevel simulated annealing: a hierarchy that preserves the
//     partition of step 1 is built, then every level from the coarsest to
//     the finest is annealed (hot at the coarsest level, cooler below).
Refiner::Key attempt(const Hypergraph& H, const Topology& T, const Params& prm, const Config& cfg, Rng& rng,
                     const Timer& timer, double deadline, Refiner& R, std::vector<int>& out) {
    std::vector<int> part = multilevel(H, T, nullptr, prm, rng, timer, deadline);
    R.init(part);
    R.greedyRefine(rng, false, prm.refinePasses * 2, deadline, timer);
    part = R.part();
    logf("  ml t=%.1f J=%lld legal=%d\n", timer.elapsed(), R.objective(), (int)R.legal());

    Hierarchy hier;
    buildHierarchy(hier, H, T, &part, cfg.saTop, prm, rng, timer, deadline);
    const int L = hier.depth();
    std::vector<double> w(L + 1);
    for (int l = 0; l <= L; ++l) w[l] = std::pow((double)hier.graph(l).n, cfg.beta);
    part = hier.cons.back();
    for (int l = L; l >= 0; --l) {
        double wsum = 0;
        for (int j = 0; j <= l; ++j) wsum += w[j];
        const double now = timer.elapsed();
        const double levelEnd = now + std::max(0.0, deadline - now) * (w[l] / wsum);
        std::unique_ptr<Refiner> own;
        Refiner* Rl = &R;
        if (l > 0) {
            own = std::make_unique<Refiner>(hier.graph(l), T);
            Rl = own.get();
        }
        Rl->init(part);
        Rl->setTempScale(cfg.tsHigh * std::pow(cfg.decay, L - l));
        Rl->anneal(rng, levelEnd, timer);
        Rl->greedyRefine(rng, false, prm.refinePasses, deadline + 0.5, timer);
        logf("  level %d n=%d evals=%lld t0=%.2f J=%lld t=%.1f\n", l, hier.graph(l).n, Rl->statEval_, Rl->statT0_,
             Rl->objective(), timer.elapsed());
        part = Rl->part();
        if (l > 0) part = hier.projectDown(l, part);
    }
    out = R.part();
    return R.key();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "Usage: %s <input_file> <output_file> [time_limit_seconds]\n", argv[0]);
        return 1;
    }
    Timer timer;
    double timeLimit = envDouble("PARTITIONER_TIME_LIMIT", 250.0);
    if (argc >= 4) timeLimit = std::atof(argv[3]);
    if (std::getenv("PARTITIONER_VERBOSE")) verbose = true;

    Hypergraph H;
    Topology T;
    std::vector<int> nodeType;
    readInput(argv[1], H, T, nodeType);
    logf("parsed: n=%d m=%d K=%d (%.2fs)\n", H.n, H.m, T.K, timer.elapsed());

    // Leave a margin for writing the output.
    const double endTime = std::max(0.5, timeLimit - 2.0 - H.n * 2e-6);

    int threads = (int)std::thread::hardware_concurrency();
    if (threads <= 0) threads = 1;
    threads = std::min(threads, 4);
    threads = (int)envDouble("PARTITIONER_THREADS", threads);
    threads = std::max(1, threads);
    // Large inputs: limit the number of workers to keep the memory bounded.
    const double sizeMB = (double)(H.sinks.size() + H.inc.size() + 4.0 * H.n + 4.0 * H.m) * 4.0 / 1e6;
    while (threads > 1 && sizeMB * 12 * threads > 6000) --threads;

    const uint64_t baseSeed = (uint64_t)envDouble("PARTITIONER_SEED", 20261119.0);
    Config base;
    base.tsHigh = envDouble("PARTITIONER_TSHIGH", base.tsHigh);
    base.decay = envDouble("PARTITIONER_DECAY", base.decay);
    base.beta = envDouble("PARTITIONER_BETA", base.beta);
    base.saTop = (int)envDouble("PARTITIONER_SATOP", base.saTop);
    // Length of a single attempt: small inputs profit from many restarts,
    // large inputs from one long annealing schedule.
    const double attemptLen = std::max(3.0, envDouble("PARTITIONER_ATTEMPT_PER_NODE", 2e-3) * H.n);
    const double tsMul[] = {1.0, 0.5, 2.0, 0.25};

    SharedBest best;
    Params prm;
    auto worker = [&](int id) {
        Rng rng(baseSeed * 1000003ULL + (uint64_t)id * 7919ULL + 17ULL);
        Refiner R(H, T);
        std::vector<int> part;
        int k = 0;
        while (true) {
            const double now = timer.elapsed();
            const double left = endTime - now;
            if (left <= 0.05 && k > 0) break;
            // Spread the remaining time evenly over the remaining attempts.
            const int nAtt = std::max(1, (int)std::floor(left / attemptLen));
            const double deadline = now + std::max(0.05, left / nAtt);
            Config cfg = base;
            cfg.tsHigh *= tsMul[(id + k) % 4];
            // Alternate the size of the coarsest annealed level; small inputs
            // otherwise would never be annealed on a coarse level.
            const int variant = (id + k / 4) % 3;
            if (variant > 0) cfg.saTop = std::max(8 * T.K, std::min(cfg.saTop, H.n / (variant == 1 ? 3 : 8)));
            const Refiner::Key key = attempt(H, T, prm, cfg, rng, timer, deadline, R, part);
            logf("worker %d attempt %d ts=%.2f top=%d: viol=%lld TLC=%lld t=%.1f\n", id, k, cfg.tsHigh, cfg.saTop, key.first, key.second,
                 timer.elapsed());
            best.offer(part, key);
            ++k;
        }
    };
    if (threads == 1) {
        worker(0);
    } else {
        std::vector<std::thread> pool;
        for (int i = 0; i < threads; ++i) pool.emplace_back(worker, i);
        for (auto& th : pool) th.join();
    }

    // Final verification.
    {
        Refiner R(H, T);
        R.init(best.part);
        logf("final: TLC=%lld legal=%d bad=%lld ovf=%lld threads=%d time=%.1fs\n", R.tlc(), (int)R.legal(), R.bad(),
             R.overflow(), threads, timer.elapsed());
        if (!R.legal()) std::fprintf(stderr, "warning: no legal solution found\n");
    }
    if (!writeOutput(argv[2], best.part)) {
        std::fprintf(stderr, "error: cannot write output\n");
        return 1;
    }
    return 0;
}
