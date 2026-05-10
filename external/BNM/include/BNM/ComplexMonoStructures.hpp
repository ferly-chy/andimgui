#pragma once

#include <map>
#include <vector>

#include "BasicMonoStructures.hpp"
#include "Class.hpp"
#include "Delegates.hpp"
#include "Il2CppHeaders.hpp"
#include "Method.hpp"
#include "UserSettings/GlobalSettings.hpp"

namespace BNM::Structures::Mono {
/**
    @brief System.Generic.Dictionary type implementation
    @tparam TKey Key type
    @tparam TValue Value type
*/
template <typename TKey, typename TValue>
struct Dictionary : BNM::IL2CPP::Il2CppObject {
  static inline Method<bool> _TryGetValue_method{};
  static inline Method<void> _Add_method{};
  static inline Method<void> _set_Item_method{};
  static inline Method<bool> _Remove_method{};
  static inline Method<bool> _ContainsKey_method{};
  static inline Method<bool> _ContainsValue_method{};
  static inline Method<void> _Clear_method{};

#ifdef BNM_DOTNET35
  /**
      @brief System.Generic.Dictionary.Link type implementation
  */
  struct Link {
    int HashCode{}, Next{};
  };
  Array<int> *table{};
  Array<Link> *linkSlots{};
  Array<TKey> *keys{};
  Array<TValue> *values{};
  int touchedSlots{};
  int emptySlot{};
  int count{};
  int threshold{};
  void *hcp{};
  void *serialization_info{};
  int version{};

  /**
      @brief Convert to std::map.
      @return Map of elements
  */
  std::map<TKey, TValue> ToMap() const {
    std::map<TKey, TValue> ret{};
    for (int i = 0; i < touchedSlots; i++)
      if ((linkSlots->m_Items[i].HashCode & -2147483648) != 0)
        ret.insert(std::make_pair(keys->m_Items[i], values->m_Items[i]));
    return ret;
  }

  /**
      @brief Convert to std::vector.
      @return Vector of keys
  */
  std::vector<TKey> GetKeys() const {
    std::vector<TKey> ret{};
    for (int i = 0; i < touchedSlots; i++)
      if ((linkSlots->m_Items[i].HashCode & -2147483648) != 0)
        ret.push_back(keys->m_Items[i]);
    return ret;
  }

  /**
      @brief Convert to std::vector.
      @return Vector of values
  */
  std::vector<TValue> GetValues() const {
    std::vector<TValue> ret{};
    for (int i = 0; i < touchedSlots; i++)
      if ((linkSlots->m_Items[i].HashCode & -2147483648) != 0)
        ret.push_back(values->m_Items[i]);
    return ret;
  }
#else
  /**
      @brief System.Generic.Dictionary.Entry type implementation
  */
  struct Entry {
    int hashCode{}, next{};
    TKey key{};
    TValue value{};
  };
  Array<int> *buckets{};
  Array<Entry> *entries{};
  int count{};
  int version{};
  int freeList{};
  int freeCount{};
  void *comparer{};
  Array<TKey> *keys{};
  Array<TValue> *values{};
  void *syncRoot{};

  /**
      @brief Convert to std::map.
      @return Map of elements
  */
  std::map<TKey, TValue> ToMap() const {
    std::map<TKey, TValue> ret{};
    for (auto it = (Entry *)&entries->m_Items;
         it != ((Entry *)&entries->m_Items + count); ++it)
      ret.emplace(std::make_pair(it->key, it->value));
    return ret;
  }

  /**
      @brief Convert to std::vector.
      @return Vector of keys
  */
  std::vector<TKey> GetKeys() const {
    std::vector<TKey> ret{};
    for (int i = 0; i < count; ++i)
      ret.emplace_back(entries->m_Items[i].key);
    return ret;
  }

  /**
      @brief Convert to std::vector.
      @return Vector of values
  */
  std::vector<TValue> GetValues() const {
    std::vector<TValue> ret{};
    for (int i = 0; i < count; ++i)
      ret.emplace_back(entries->m_Items[i].value);
    return ret;
  }
#endif

  /**
      @brief Get dictionary size.
      @return Dictionary entries count
  */
  [[nodiscard]] int GetSize() const { return count; }

  /**
      @brief Get dictionary version.
      @return Dictionary version
  */
  [[nodiscard]] int GetVersion() const { return version; }

  /**
      @brief Try to get value by key.
      @param key Target key
      @param value Out value
      @return True if value is found by key.
  */
  bool TryGet(TKey key, TValue *value) const {
    if (!_TryGetValue_method.IsValid())
      _TryGetValue_method =
          Class(klass).GetMethod(BNM_OBFUSCATE("TryGetValue"), 2);
    return _TryGetValue_method[(void *)this](key, value);
  }

  /**
      @brief Add value.
      @param key Target key
      @param value Target value
  */
  void Add(TKey key, TValue value) {
    if (!_Add_method.IsValid())
      _Add_method = Class(klass).GetMethod(BNM_OBFUSCATE("Add"), 2);
    return _Add_method[(void *)this](key, value);
  }

  /**
      @brief Insert value.
      @param key Target key
      @param value Target value
  */
  void Insert(TKey key, TValue value) {
    if (!_set_Item_method.IsValid())
      _set_Item_method = Class(klass).GetMethod(BNM_OBFUSCATE("set_Item"), 2);
    return _set_Item_method[(void *)this](key, value);
  }

  /**
      @brief Remove value.
      @param key Target key
      @return True if value was removed
  */
  bool Remove(TKey key) {
    if (!_Remove_method.IsValid())
      _Remove_method = Class(klass).GetMethod(BNM_OBFUSCATE("Remove"), 1);
    return _Remove_method[(void *)this](key);
  }

  /**
      @brief Check if key exists.
      @param key Target key
      @return True if key exists.
  */
  bool ContainsKey(TKey key) const {
    if (!_ContainsKey_method.IsValid())
      _ContainsKey_method =
          Class(klass).GetMethod(BNM_OBFUSCATE("ContainsKey"), 1);
    return _ContainsKey_method[(void *)this](key);
  }

  /**
      @brief Check if value exists.
      @param value Target value
      @return True if value exists.
  */
  bool ContainsValue(TValue value) const {
    if (!_ContainsValue_method.IsValid())
      _ContainsValue_method =
          Class(klass).GetMethod(BNM_OBFUSCATE("ContainsValue"), 1);
    return _ContainsValue_method[(void *)this](value);
  }

  /**
      @brief Clear dictionary.
  */
  void Clear() {
    if (!_Clear_method.IsValid())
      _Clear_method = Class(klass).GetMethod(BNM_OBFUSCATE("Clear"), 0);
    return _Clear_method[(void *)this]();
  }

  /**
      @brief Get value by key.
      @return Value if it's found, otherwise default value.
  */
  TValue Get(TKey key) const {
    TValue ret;
    if (TryGet(key, &ret))
      return ret;
    return {};
  }

  /**
      @brief Get value by key.

      Alias of Get().

      @return Value if it's found, otherwise default value.
  */
  TValue operator[](TKey key) const { return Get(key); }
};
} // namespace BNM::Structures::Mono