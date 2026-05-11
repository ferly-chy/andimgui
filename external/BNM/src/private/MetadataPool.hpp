#pragma once

#include "../include/BNM/UserSettings/GlobalSettings.hpp"
#include <mutex>
#include <vector>

namespace BNM::Internal {

/**
 * @brief A simple but fast Pool Allocator for small metadata objects.
 * Reduces heap fragmentation and allocation overhead.
 */
class MetadataPool {
public:
  // Block size for the pool (64KB chunks)
  static constexpr size_t BLOCK_SIZE = 64 * 1024;

  static void *Allocate(size_t size) {
    static MetadataPool instance;
    return instance.AllocateInternal(size);
  }

  static void FreeAll() {
    static MetadataPool instance;
    instance.FreeAllInternal();
  }

private:
  struct Block {
    uint8_t *data;
    size_t used;
  };

  std::vector<Block> _blocks;
  std::mutex _mutex;

  void *AllocateInternal(size_t size) {
    // Align to pointer size
    size = (size + sizeof(void *) - 1) & ~(sizeof(void *) - 1);

    std::lock_guard lock(_mutex);

    if (_blocks.empty() || _blocks.back().used + size > BLOCK_SIZE) {
      // Allocate new block if requested size is larger than remaining space
      size_t allocSize = (size > BLOCK_SIZE) ? size : BLOCK_SIZE;
      uint8_t *newData = (uint8_t *)BNM_malloc(allocSize);
      if (!newData)
        return nullptr;
      try {
        _blocks.push_back({newData, 0});
      } catch (...) {
        BNM_free(newData);
        return nullptr;
      }
    }

    Block &current = _blocks.back();
    void *ptr = current.data + current.used;
    current.used += size;
    return ptr;
  }

  void FreeAllInternal() {
    std::lock_guard lock(_mutex);
    for (auto &block : _blocks) {
      BNM_free(block.data);
    }
    _blocks.clear();
  }
};

} // namespace BNM::Internal
