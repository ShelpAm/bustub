// :bustub-keep-private:
//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// arc_replacer.cpp
//
// Identification: src/buffer/arc_replacer.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/arc_replacer.h"
#include <algorithm>
#include <optional>
#include <utility>
#include "common/config.h"

namespace bustub {

/**
 * @brief a new ArcReplacer, with lists initialized to be empty and target size to 0
 * @param num_frames the maximum number of frames the ArcReplacer will be required to cache
 */
ArcReplacer::ArcReplacer(size_t num_frames) : replacer_size_(num_frames) {}

/**
 * @brief Performs the Replace operation as described by the writeup
 * that evicts from either mfu_ or mru_ into its corresponding ghost list
 * according to balancing policy.
 *
 * If you wish to refer to the original ARC paper, please note that there are
 * two changes in our implementation:
 * 1. When the size of mru_ equals the target size, we don't check
 * the last access as the paper did when deciding which list to evict from.
 * This is fine since the original decision is stated to be arbitrary.
 * 2. Entries that are not evictable are skipped. If all entries from the desired side
 * (mru_ / mfu_) are pinned, we instead try victimize the other side (mfu_ / mru_),
 * and move it to its corresponding ghost list (mfu_ghost_ / mru_ghost_).
 *
 * @return frame id of the evicted frame, or std::nullopt if cannot evict
 */
auto ArcReplacer::Evict() -> std::optional<frame_id_t> {
  auto [first, second] = std::pair(&mru_, &mfu_);
  auto [first_ghost, second_ghost] = std::pair(&mru_ghost_, &mfu_ghost_);
  if (mru_.size() < mru_target_size_) {
    std::swap(first, second);
    std::swap(first_ghost, second_ghost);
  }

  auto try_evict_from = [this](std::list<frame_id_t> &list, std::list<page_id_t> &ghost) -> std::optional<frame_id_t> {
    auto evictable = [this](frame_id_t f) {
      if (!alive_map_.count(f)) {
        std::cerr << "Invalid frame_id " << f << '\n';
        throw std::runtime_error{"Invalid frame_id"};
      }
      return alive_map_.at(f)->evictable_;
    };
    auto rit = std::find_if(list.rbegin(), list.rend(), evictable);
    if (rit == list.rend()) {
      return std::nullopt;
    }
    auto fid = *rit;
    auto node = alive_map_.extract(fid);
    auto const &fs_ptr = node.mapped();
    ghost_map_.insert({fs_ptr->page_id_, fs_ptr});
    move_frame_to_ghost(*fs_ptr, ghost);
    assert(fs_ptr->evictable_);
    fs_ptr->evictable_ = false;
    --curr_size_;
    return fid;
  };

  if (auto res = try_evict_from(*first, *first_ghost); res.has_value()) return res;
  if (auto res = try_evict_from(*second, *second_ghost); res.has_value()) return res;
  return std::nullopt;
}

/**
 * @brief Record access to a frame, adjusting ARC bookkeeping accordingly
 * by bring the accessed page to the front of mfu_ if it exists in any of the lists
 * or the front of mru_ if it does not.
 *
 * Performs the operations EXCEPT REPLACE described in original paper, which is
 * handled by `Evict()`.
 *
 * Consider the following four cases, handle accordingly:
 * 1. Access hits mru_ or mfu_
 * 2/3. Access hits mru_ghost_ / mfu_ghost_
 * 4. Access misses all the lists
 *
 * This routine performs all changes to the four lists as preperation
 * for `Evict()` to simply find and evict a victim into ghost lists.
 *
 * Note that frame_id is used as identifier for alive pages and
 * page_id is used as identifier for the ghost pages, since page_id is
 * the unique identifier to the page after it's dead.
 * Using page_id for alive pages should be the same since it's one to one mapping,
 * but using frame_id is slightly more intuitive.
 *
 * @param frame_id id of frame that received a new access.
 * @param page_id id of page that is mapped to the frame.
 * @param access_type type of access that was received. This parameter is only needed for
 * leaderboard tests.
 */
void ArcReplacer::RecordAccess(frame_id_t frame_id, page_id_t page_id, [[maybe_unused]] AccessType access_type) {
  if (auto it = alive_map_.find(frame_id); it != alive_map_.end()) {  // 1
    auto &fs = *it->second;
    assert(fs.page_id_ == page_id);
    assert(fs.owner_queue() != nullptr);
    move_frame_to_hot(fs, mfu_);
    return;
  }
  if (auto it = ghost_map_.find(page_id); it != ghost_map_.end()) {  // 2/3
                                                                     // same as 1 except using page_id
    auto node = ghost_map_.extract(it);
    auto const &fs_ptr = node.mapped();
    fs_ptr->frame_id_ = frame_id;
    assert(!fs_ptr->evictable_);
    assert(fs_ptr->owner_queue() != nullptr);
    if (fs_ptr->owner_queue() == &mru_ghost_) {
      assert(mru_ghost_.size());
      mru_target_size_ += std::max(1UL, mfu_ghost_.size() / mru_ghost_.size());
    } else {
      assert(fs_ptr->owner_queue() == &mfu_ghost_);
      assert(mfu_ghost_.size());
      mru_target_size_ -= std::max(1UL, mru_ghost_.size() / mfu_ghost_.size());
    }
    move_frame_to_hot(*fs_ptr, mfu_);
    alive_map_.insert({frame_id, fs_ptr});
    return;
  }

  // 4
  if (mru_.size() + mru_ghost_.size() == replacer_size_) {
    auto pid = mru_ghost_.back();
    ghost_map_.extract(pid);
    mru_ghost_.pop_back();
  } else {
    auto total_size = mru_.size() + mru_ghost_.size() + mfu_.size() + mfu_ghost_.size();
    assert(total_size <= 2 * replacer_size_);
    if (total_size == 2 * replacer_size_) {
      auto pid = mfu_ghost_.back();
      ghost_map_.extract(pid);
      mfu_ghost_.pop_back();
    }
  }
  auto fs_ptr = std::make_shared<FrameStatus>(page_id, frame_id);
  move_frame_to_hot(*fs_ptr, mru_);
  alive_map_.insert({frame_id, fs_ptr});
}

/**
 * @brief Toggle whether a frame is evictable or non-evictable. This function also
 * controls replacer's size. Note that size is equal to number of evictable entries.
 *
 * If a frame was previously evictable and is to be set to non-evictable, then size should
 * decrement. If a frame was previously non-evictable and is to be set to evictable,
 * then size should increment.
 *
 * If frame id is invalid, throw an exception or abort the process.
 *
 * For other scenarios, this function should terminate without modifying anything.
 *
 * @param frame_id id of frame whose 'evictable' status will be modified
 * @param set_evictable whether the given frame is evictable or not
 */
void ArcReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  auto it = alive_map_.find(frame_id);
  if (it == alive_map_.end()) {
    throw std::invalid_argument{"Invalid frame id " + std::to_string(frame_id)};
  }
  auto &frame_status = it->second;
  curr_size_ += static_cast<std::size_t>(set_evictable) - static_cast<std::size_t>(frame_status->evictable_);
  frame_status->evictable_ = set_evictable;
}

/**
 * @brief Remove an evictable frame from replacer.
 * This function should also decrement replacer's size if removal is successful.
 *
 * Note that this is different from evicting a frame, which always remove the frame
 * decided by the ARC algorithm.
 *
 * If Remove is called on a non-evictable frame, throw an exception or abort the
 * process.
 *
 * If specified frame is not found, directly return from this function.
 *
 * @param frame_id id of frame to be removed
 */
void ArcReplacer::Remove(frame_id_t frame_id) {
  auto it = alive_map_.find(frame_id);
  if (it == alive_map_.end()) {
    return;
  }

  auto const &fs_ptr = it->second;
  if (!fs_ptr->evictable_) {
    throw std::exception{};
  }

  --curr_size_;
  fs_ptr->erase_from_owner();
  alive_map_.extract(it);
}

/**
 *
 * @brief Return replacer's size, which tracks the number of evictable frames.
 *
 * @return size_t
 */
auto ArcReplacer::Size() const -> size_t { return curr_size_; }

void ArcReplacer::summary(int i) const {
  assert(false && "Only for debug use, don't use in production code.");
  std::cerr << i << '\n';

  std::cerr << "alive_map:\n";
  for (auto [k, v] : alive_map_) std::cerr << "  " << k << ' ' << v << '\n';
  std::cerr << "ghost_pages:\n";
  for (auto [k, _] : ghost_map_) std::cerr << "  " << ' ' << k << '\n';

  std::cerr << "[";
  for (auto it = mru_ghost_.rbegin(); it != mru_ghost_.rend(); ++it) std::cerr << "(" << *it << ",_)" << ' ';
  std::cerr << "]";
  std::cerr << "[";
  for (auto it = mru_.rbegin(); it != mru_.rend(); ++it)
    std::cerr << (alive_map_.at(*it)->evictable_ ? "" : "p") << "(" << alive_map_.at(*it)->page_id_ << ",f" << *it
              << ")" << ' ';
  std::cerr << "]";
  std::cerr << "[";
  for (auto e : mfu_)
    std::cerr << (alive_map_.at(e)->evictable_ ? "" : "p") << "(" << alive_map_.at(e)->page_id_ << ",f" << e << ")"
              << ' ';
  std::cerr << "]";
  std::cerr << "[";
  for (auto e : mfu_ghost_) std::cerr << "(" << e << ",_)" << ' ';
  std::cerr << "]" << '\n';
}

void ArcReplacer::move_frame_to_hot(FrameStatus &fs, std::list<int> &list) {
  fs.erase_from_owner();
  list.push_front(fs.frame_id_);
  fs.bind_to_queue_front(&list);
}

void ArcReplacer::move_frame_to_ghost(FrameStatus &fs, std::list<int> &list) {
  fs.erase_from_owner();
  list.push_front(fs.page_id_);
  fs.bind_to_queue_front(&list);
}

}  // namespace bustub
