#pragma once

// Internal: speaker clustering for the Diarizer. Not part of the public API.
// It needs nothing but the standard library, so tests/test_translate_text.cpp
// compiles speaker_clustering.cpp directly.

#include "muninn/diarization.h"

#include <vector>

namespace muninn {
namespace detail {

// Average-linkage agglomerative clustering by cosine similarity.
//
// Gives the same clusters as the old greedy loop (repeatedly merge the pair of
// clusters with the highest average similarity while it is at least
// `threshold`; then, if there are more than `max_speakers` clusters, keep
// merging the closest pair until there aren't; max_speakers <= 0 = no limit),
// but in O(n^2) time instead of O(n^3): the similarity matrix is computed once,
// a nearest-neighbour chain builds the full average-linkage tree with
// Lance-Williams updates, and the tree is then cut where the greedy loop
// would have stopped.
//
// Memory: n*(n-1)/2 floats (about 100 MB for an hour at a 0.5 s step).
//
// Returns one label per embedding, numbered from 0 in order of each
// cluster's first embedding.
std::vector<int> cluster_embeddings(const std::vector<SpeakerEmbedding>& embeddings,
                                    float threshold,
                                    int max_speakers);

} // namespace detail
} // namespace muninn
