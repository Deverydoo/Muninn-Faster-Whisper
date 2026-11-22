#include "speaker_clustering.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace muninn {
namespace detail {

namespace {

// Upper-triangle storage of a symmetric n x n matrix without its diagonal.
class CondensedMatrix {
public:
    explicit CondensedMatrix(size_t n) : n_(n), data_(n < 2 ? 0 : n * (n - 1) / 2) {}

    float& at(size_t i, size_t j) { return data_[index(i, j)]; }
    float at(size_t i, size_t j) const { return data_[index(i, j)]; }

private:
    size_t index(size_t i, size_t j) const {
        if (i > j) std::swap(i, j);
        // Row i starts after rows 0..i-1, which hold (n-1) + (n-2) + ... entries.
        return i * n_ - i * (i + 1) / 2 + (j - i - 1);
    }

    size_t n_;
    std::vector<float> data_;
};

struct Merge {
    int a;           // a point in one cluster
    int b;           // a point in the other
    double height;   // average similarity of the merge, kept non-increasing up the tree
};

int find_root(std::vector<int>& parent, int x) {
    while (parent[x] != x) {
        parent[x] = parent[parent[x]];
        x = parent[x];
    }
    return x;
}

} // namespace

std::vector<int> cluster_embeddings(const std::vector<SpeakerEmbedding>& embeddings,
                                    float threshold,
                                    int max_speakers) {
    const size_t n = embeddings.size();
    if (n == 0) return {};
    if (n == 1) return {0};

    const size_t dim = embeddings[0].features.size();
    for (const auto& e : embeddings) {
        if (e.features.size() != dim) throw std::runtime_error("Embedding size mismatch");
    }

    // ── Similarity matrix, once ───────────────────────────────────────────
    // Same formula as Diarizer::cosine_similarity (0 for a zero vector).
    std::vector<float> norms(n);
    for (size_t i = 0; i < n; ++i) {
        float s = 0.0f;
        for (float v : embeddings[i].features) s += v * v;
        norms[i] = std::sqrt(s);
    }

    CondensedMatrix sim(n);
    for (size_t i = 0; i < n; ++i) {
        const float* fi = embeddings[i].features.data();
        for (size_t j = i + 1; j < n; ++j) {
            const float* fj = embeddings[j].features.data();
            float dot = 0.0f;
            for (size_t d = 0; d < dim; ++d) dot += fi[d] * fj[d];
            sim.at(i, j) = (norms[i] == 0.0f || norms[j] == 0.0f) ? 0.0f : dot / (norms[i] * norms[j]);
        }
    }

    // ── Average-linkage tree by nearest-neighbour chain ───────────────────
    // Each cluster lives in the slot of one of its points. Merging a and b
    // keeps the lower slot and updates its row with Lance-Williams:
    //   s(a+b, k) = (|a| s(a,k) + |b| s(b,k)) / (|a| + |b|)
    std::vector<char> active(n, 1);
    std::vector<double> size(n, 1.0);
    std::vector<double> height(n, std::numeric_limits<double>::infinity());
    std::vector<int> chain;
    chain.reserve(n);
    std::vector<Merge> merges;
    merges.reserve(n - 1);

    size_t remaining = n;
    size_t first_active = 0;
    while (remaining > 1) {
        if (chain.empty()) {
            while (!active[first_active]) ++first_active;
            chain.push_back(static_cast<int>(first_active));
        }
        const int a = chain.back();
        const int prev = chain.size() >= 2 ? chain[chain.size() - 2] : -1;

        // The most similar other cluster; on a tie, prefer the previous link
        // so the chain always ends in a reciprocal pair.
        int b = prev;
        double best = prev >= 0 ? sim.at(a, prev) : -std::numeric_limits<double>::infinity();
        for (size_t k = 0; k < n; ++k) {
            if (!active[k] || static_cast<int>(k) == a) continue;
            const double s = sim.at(a, k);
            if (s > best) {
                best = s;
                b = static_cast<int>(k);
            }
        }

        if (b != prev) {
            chain.push_back(b);
            continue;
        }

        // a and b are each other's nearest neighbours: merge them.
        chain.pop_back();
        chain.pop_back();
        const int keep = std::min(a, b);
        const int drop = std::max(a, b);
        const double na = size[a], nb = size[b];
        for (size_t k = 0; k < n; ++k) {
            if (!active[k] || static_cast<int>(k) == a || static_cast<int>(k) == b) continue;
            const double updated = (na * sim.at(a, k) + nb * sim.at(b, k)) / (na + nb);
            sim.at(keep, k) = static_cast<float>(updated);
        }
        // Rounding can put a merge a hair above one of its children; clamp so
        // the tree stays monotonic and cutting it matches the greedy order.
        const double h = std::min({best, height[a], height[b]});
        merges.push_back({a, b, h});
        active[drop] = 0;
        size[keep] = na + nb;
        height[keep] = h;
        --remaining;
    }

    // ── Cut the tree where the greedy loop would stop ─────────────────────
    // Merges in order of decreasing similarity (children before parents on
    // ties, since they were recorded first).
    std::stable_sort(merges.begin(), merges.end(),
                     [](const Merge& x, const Merge& y) { return x.height > y.height; });

    std::vector<int> parent(n);
    std::iota(parent.begin(), parent.end(), 0);
    size_t clusters = n;
    for (const Merge& m : merges) {
        const bool above_threshold = m.height >= threshold;
        const bool too_many = max_speakers > 0 && clusters > static_cast<size_t>(max_speakers);
        if (!above_threshold && !too_many) break;
        const int ra = find_root(parent, m.a);
        const int rb = find_root(parent, m.b);
        if (ra == rb) continue;
        parent[std::max(ra, rb)] = std::min(ra, rb);
        --clusters;
    }

    // Number clusters by their first point.
    std::vector<int> labels(n, -1);
    std::vector<int> label_of_root(n, -1);
    int next = 0;
    for (size_t i = 0; i < n; ++i) {
        const int r = find_root(parent, static_cast<int>(i));
        if (label_of_root[r] < 0) label_of_root[r] = next++;
        labels[i] = label_of_root[r];
    }
    return labels;
}

} // namespace detail
} // namespace muninn
