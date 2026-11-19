// Multilevel driver: coarsening, initial partitioning and uncoarsening.
#pragma once

#include <memory>
#include <vector>

#include "hypergraph.hpp"
#include "util.hpp"

struct Params {
    int ratingNetLimit = 64;     // nets with more pins are ignored while clustering
    double clusterFrac = 0.06;   // max cluster weight as fraction of the smallest capacity
    int coarsestPerFpga = 24;    // stop coarsening below coarsestPerFpga * K nodes
    int initialTrials = 12;      // initial partitioning attempts at the coarsest level
    int refinePasses = 10;       // greedy passes per level
};

// Clusters the nodes of H (first-choice heavy-edge rating with a size
// penalty). If `constraint` is given only nodes with equal constraint value
// are merged. Returns the number of clusters; fills `cmap`.
int clusterNodes(const Hypergraph& H, int maxW1, int maxW2, const std::vector<int>* constraint, int ratingNetLimit,
                 Rng& rng, std::vector<int>& cmap);

// Initial partitions of a (small) hypergraph.
std::vector<int> greedyGrowInit(const Hypergraph& H, const Topology& T, int ratingNetLimit, Rng& rng);
std::vector<int> randomInit(const Hypergraph& H, const Topology& T, Rng& rng);

// A coarsening hierarchy. Level 0 is the input hypergraph.
struct Hierarchy {
    const Hypergraph* base = nullptr;
    std::vector<std::unique_ptr<Hypergraph>> levels;  // levels[i] is level i+1
    std::vector<std::vector<int>> maps;               // maps[i]: node of level i -> node of level i+1
    std::vector<std::vector<int>> cons;               // per level partition constraint (optional)

    int depth() const { return (int)levels.size(); }
    const Hypergraph& graph(int l) const { return l == 0 ? *base : *levels[l - 1]; }
    // Projects a partition of level l to level l-1.
    std::vector<int> projectDown(int l, const std::vector<int>& part) const;
};

// Coarsens H until it has at most `stopN` nodes (or coarsening stalls).
void buildHierarchy(Hierarchy& hier, const Hypergraph& H, const Topology& T, const std::vector<int>* constraint,
                    int stopN, const Params& prm, Rng& rng, const Timer& timer, double deadline);

// One complete multilevel run. If `constraint` (a complete partition of H)
// is given, it is used as the initial partition of the coarsest level and
// coarsening never merges nodes of different FPGAs (V-cycle).
std::vector<int> multilevel(const Hypergraph& H, const Topology& T, const std::vector<int>* constraint,
                            const Params& prm, Rng& rng, const Timer& timer, double deadline);
