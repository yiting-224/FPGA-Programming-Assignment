// Small utilities: timer and random number generator.
#pragma once

#include <chrono>
#include <cstdint>

class Timer {
public:
    Timer() : start_(std::chrono::steady_clock::now()) {}
    double elapsed() const {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    }

private:
    std::chrono::steady_clock::time_point start_;
};

// xoshiro256** style generator (public-domain algorithm by Blackman & Vigna),
// seeded through splitmix64.
class Rng {
public:
    explicit Rng(uint64_t seed = 0x9E3779B97F4A7C15ULL) { reseed(seed); }

    void reseed(uint64_t seed) {
        for (auto& x : s_) {
            seed += 0x9E3779B97F4A7C15ULL;
            uint64_t z = seed;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            x = z ^ (z >> 31);
        }
    }

    uint64_t next() {
        const uint64_t result = rotl(s_[1] * 5, 7) * 9;
        const uint64_t t = s_[1] << 17;
        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = rotl(s_[3], 45);
        return result;
    }

    // Uniform integer in [0, n).
    uint32_t below(uint32_t n) { return (uint32_t)(((next() >> 32) * (uint64_t)n) >> 32); }

    // Uniform double in [0, 1).
    double uniform() { return (next() >> 11) * (1.0 / 9007199254740992.0); }

    template <class Vec>
    void shuffle(Vec& v) {
        for (size_t i = v.size(); i > 1; --i) {
            size_t j = below((uint32_t)i);
            std::swap(v[i - 1], v[j]);
        }
    }

private:
    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    uint64_t s_[4];
};
