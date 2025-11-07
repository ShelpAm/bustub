//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// count_min_sketch.cpp
//
// Identification: src/primer/count_min_sketch.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "primer/count_min_sketch.h"

#include <stdexcept>
#include <string>

namespace bustub {

/**
 * Constructor for the count-min sketch.
 *
 * @param width The width of the sketch matrix.
 * @param depth The depth of the sketch matrix.
 * @throws std::invalid_argument if width or depth are zero.
 */
template <typename KeyType>
CountMinSketch<KeyType>::CountMinSketch(uint32_t width, uint32_t depth)
    : width_(width), depth_(depth), buckets_(depth, std::vector<std::uint32_t>(width, 0)), mutexes_(depth) {
  if (width_ == 0 || depth_ == 0) {
    throw std::invalid_argument{"width and depth can't be zero"};
  }

  /** @fall2025 PLEASE DO NOT MODIFY THE FOLLOWING */
  // Initialize seeded hash functions
  hash_functions_.reserve(depth_);
  for (size_t i = 0; i < depth_; i++) {
    hash_functions_.push_back(this->HashFunction(i));
  }
}

template <typename KeyType>
CountMinSketch<KeyType>::CountMinSketch(CountMinSketch &&other) noexcept
    : width_(other.width_),
      depth_(other.depth_),
      hash_functions_(std::move(other.hash_functions_)),
      buckets_(std::move(other.buckets_)),
      mutexes_(std::move(other.mutexes_)) {}

template <typename KeyType>
auto CountMinSketch<KeyType>::operator=(CountMinSketch &&other) noexcept -> CountMinSketch & {
  width_ = other.width_;
  depth_ = other.depth_;
  hash_functions_ = std::move(other.hash_functions_);
  buckets_ = std::move(other.buckets_);
  mutexes_ = std::move(other.mutexes_);
  return *this;
}

template <typename KeyType>
void CountMinSketch<KeyType>::Insert(const KeyType &item) {
  for (std::size_t i{}; i != depth_; ++i) {
    std::scoped_lock lck{mutexes_[i]};
    auto slot = hash_functions_[i](item) % width_;
    ++buckets_[i][slot];
  }
}

template <typename KeyType>
void CountMinSketch<KeyType>::Merge(const CountMinSketch<KeyType> &other) {
  if (width_ != other.width_ || depth_ != other.depth_) {
    throw std::invalid_argument("Incompatible CountMinSketch dimensions for merge.");
  }

  for (std::size_t i{}; i != depth_; ++i) {
    for (std::size_t j{}; j != width_; ++j) {
      buckets_[i][j] += other.buckets_[i][j];
    }
  }
}

template <typename KeyType>
auto CountMinSketch<KeyType>::Count(const KeyType &item) const -> uint32_t {
  auto res = std::numeric_limits<uint32_t>::max();
  for (std::size_t i{}; i != depth_; ++i) {
    auto slot = hash_functions_[i](item) % width_;
    res = std::min(res, buckets_[i][slot]);
  }
  return res;
}

template <typename KeyType>
void CountMinSketch<KeyType>::Clear() {
  for (auto &bucket : buckets_) {
    std::for_each(bucket.begin(), bucket.end(), [](auto &slot) { slot = 0; });
  }
}

template <typename KeyType>
auto CountMinSketch<KeyType>::TopK(uint16_t k, const std::vector<KeyType> &candidates)
    -> std::vector<std::pair<KeyType, uint32_t>> {
  std::vector<std::pair<KeyType, uint32_t>> res;
  res.reserve(candidates.size());
  for (auto const &candid : candidates) {
    res.emplace_back(candid, Count(candid));
  }
  std::sort(res.begin(), res.end(), [](auto const &lhs, auto const rhs) {
    return std::tie(lhs.second, lhs.first) > std::tie(rhs.second, rhs.first);
  });
  while (res.size() > k) {
    res.pop_back();
  }
  return res;
}

// Explicit instantiations for all types used in tests
template class CountMinSketch<std::string>;
template class CountMinSketch<int64_t>;  // For int64_t tests
template class CountMinSketch<int>;      // This covers both int and int32_t
}  // namespace bustub
