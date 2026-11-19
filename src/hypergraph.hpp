// Core data structures for the topology-aware multi-FPGA partitioner.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// A directed hypergraph: every net has exactly one source node and one or
// more (distinct) sink nodes. A sink is never equal to the source of its net.
struct Hypergraph {
    int n = 0;  // number of nodes
    int m = 0;  // number of nets

    std::vector<int> w1, w2;  // per node: number of type-1 / type-2 original nodes

    std::vector<int> netW;     // per net: weight (number of signals)
    std::vector<int> netSrc;   // per net: source node
    std::vector<int> sinkOff;  // CSR offsets into `sinks` (size m+1)
    std::vector<int> sinks;    // sink nodes of all nets

    std::vector<int> incOff;   // CSR offsets into `inc` (size n+1)
    std::vector<int> inc;      // nets incident to a node (as source or sink)

    int numSinks(int e) const { return sinkOff[e + 1] - sinkOff[e]; }
    int numPins(int e) const { return numSinks(e) + 1; }

    // Builds incOff/inc from netSrc/sinks.
    void buildIncidence();
};

// The multi-FPGA system.
struct Topology {
    int K = 0;
    std::vector<long long> ioLim;
    std::vector<int> cap1, cap2;
    // K*K matrix: link cost, 0 on the diagonal, `big` for unlinked pairs.
    std::vector<int> D;
    std::vector<char> adj;  // K*K: 1 if a link exists (or same FPGA)
    int big = 0;
    int maxCost = 0;

    int dist(int a, int b) const { return D[a * K + b]; }
    bool linked(int a, int b) const { return adj[a * K + b] != 0; }
};

// Parses the input file. Duplicate sinks and sinks equal to the source are
// removed (they never influence cost or I/O). Exits on malformed input.
void readInput(const std::string& path, Hypergraph& H, Topology& T, std::vector<int>& nodeType);

// Writes one FPGA id per line.
bool writeOutput(const std::string& path, const std::vector<int>& part);

// Contracts H according to `cmap` (node -> cluster id in [0, nc)).
// Sinks that fall into the source's cluster are dropped, duplicate sinks are
// merged, nets without sinks are removed and identical nets are merged
// (their weights are summed). This preserves both the cost and the I/O usage
// of every partition of the coarse hypergraph exactly.
Hypergraph contract(const Hypergraph& H, const std::vector<int>& cmap, int nc);
