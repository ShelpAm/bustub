
//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// arc_replacer.h
//
// Identification: src/include/buffer/arc_replacer.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <list>
#include <memory>
#include <mutex>  // NOLINT
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "common/config.h"
#include "common/macros.h"

namespace bustub {

enum class AccessType { Unknown = 0, Lookup, Scan, Index };

// enum class ArcStatus { MRU, MFU, MRU_GHOST, MFU_GHOST };

// TODO(student): You can modify or remove this struct as you like.
struct FrameStatus {
  page_id_t page_id_;

  frame_id_t frame_id_;  // When in ghost, this is invalid. DON'T use it.

  bool evictable_{};  // When in ghost, this must be `false`.
  // ArcStatus arc_status_;
  FrameStatus(page_id_t pid, frame_id_t fid
              // , bool ev, std::list<int> *owner, std::list<int>::iterator iter
              // ,  ArcStatus st
              )
      : page_id_(pid),
        frame_id_(fid)
  // , evictable_(ev),
  // owner_(owner),
  // iter_(iter)
  // , arc_status_(st)
  {}

  [[nodiscard]] std::list<int> *owner_queue() const { return owner_queue_; }
  void erase_from_owner() {
    if (owner_queue_ != nullptr) {
      owner_queue_->erase(queue_iter_);
      owner_queue_ = nullptr;
    }
  }
  void bind_to_queue_front(std::list<int> *neo_owner) {
    assert(neo_owner != nullptr);
    owner_queue_ = neo_owner;
    queue_iter_ = neo_owner->begin();
  }

 private:
  std::list<int> *owner_queue_{};
  std::list<int>::iterator queue_iter_;
};

/**
 * ArcReplacer implements the ARC replacement policy.
 */
class ArcReplacer {
 public:
  explicit ArcReplacer(size_t num_frames);

  DISALLOW_COPY_AND_MOVE(ArcReplacer)

  /**
   * @brief Destroys the LRUReplacer.
   */
  ~ArcReplacer() = default;

  auto Evict() -> std::optional<frame_id_t>;
  void RecordAccess(frame_id_t frame_id, page_id_t page_id, AccessType access_type = AccessType::Unknown);
  void SetEvictable(frame_id_t frame_id, bool set_evictable);
  void Remove(frame_id_t frame_id);
  auto Size() const -> size_t;

  void summary(int i) const;

 private:
  static void move_frame_to_hot(FrameStatus &fs, std::list<int> &list);
  static void move_frame_to_ghost(FrameStatus &fs, std::list<int> &list);

  // implement me! You can replace or remove these member variables as you like.
  std::list<frame_id_t> mru_;
  std::list<frame_id_t> mfu_;
  std::list<page_id_t> mru_ghost_;
  std::list<page_id_t> mfu_ghost_;

  /* record entries in mru_ and mfu_
   * this uses frame_id_t to guarantee no duplicate records for the same
   * frame when they are alive */
  std::unordered_map<frame_id_t, std::shared_ptr<FrameStatus>> alive_map_;
  /* record entries in mru_ghost_ and mfu_ghost_
   * this uses page_id_t but not frame_id_t because page_id is the unique
   * identifier in ghost lists */
  std::unordered_map<page_id_t, std::shared_ptr<FrameStatus>> ghost_map_;

  /* alive, evictable entries count */
  [[maybe_unused]] size_t curr_size_{0};
  /* p as in original paper */
  [[maybe_unused]] size_t mru_target_size_{0};
  /* c as in original paper */
  [[maybe_unused]] size_t replacer_size_;
  std::mutex latch_;

  // You can add member variables / functions as you like.
};

}  // namespace bustub
