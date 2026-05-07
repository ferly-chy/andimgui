/**
 * Dobby - VTable Hooking Interface (C++26)
 *
 * @maintainer: Gemini CLI
 * @version: 1.0
 * @date: 2026-03-24
 *
 * @description:
 * Provides a professional, type-safe C++26 interface for Virtual Method Table (VTable) manipulation.
 * Includes features for auto-calculating VTable indices (Itanium ABI), replacing single entries,
 * and replacing entire VTables for instance-specific hooking.
 */

#ifndef dobby_vtable_h
#define dobby_vtable_h

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <type_traits>
#include <expected>
#include <memory>
#include <sys/mman.h>
#include <unistd.h>
#include "dobby.h"

namespace dobby::vtable {

    /**
     * @brief Error codes for VTable operations.
     */
    enum class Error {
        Success = 0,
        NotVirtualFunction = -1,
        MemoryProtectionFailed = -2,
        InvalidInstance = -3,
        AllocationFailed = -4
    };

    /**
     * @brief Internal utilities for Itanium ABI PTMF parsing.
     */
    namespace detail {
        struct PtmfItaniumStandard {
            ptrdiff_t ptr;
            ptrdiff_t adj;
        };

        // Standard Itanium ABI (x86, x64, AArch64)
        // LSB of ptr is 1 if virtual. ptr - 1 is the byte offset.
        // ARM32 Itanium ABI
        // LSB of adj is 1 if virtual. ptr is the byte offset.
        template <typename T>
        inline std::expected<size_t, Error> CalculateVTableIndex(T method) {
            static_assert(std::is_member_function_pointer_v<T>, "Must be a pointer to member function");
            
            union {
                T pmf;
                PtmfItaniumStandard itanium;
            } u;
            u.pmf = method;

#if defined(__arm__) || defined(__aarch64__) || defined(__arm64__)
            // ARM/AArch64 Itanium ABI variation
            if (u.itanium.adj & 1) {
                // ptr is the byte offset, index = offset / sizeof(void*)
                return static_cast<size_t>(u.itanium.ptr / sizeof(void*));
            }
#endif
            // Standard Itanium ABI (x86, x64) or fallback
            if (u.itanium.ptr & 1) {
                // ptr - 1 is the byte offset, index = offset / sizeof(void*)
                return static_cast<size_t>((u.itanium.ptr - 1) / sizeof(void*));
            }
            return std::unexpected(Error::NotVirtualFunction);
        }

        inline bool UnprotectMemory(void* addr, size_t size) {
            size_t page_size = sysconf(_SC_PAGESIZE);
            uintptr_t align_addr = reinterpret_cast<uintptr_t>(addr) & ~(page_size - 1);
            size_t align_size = (reinterpret_cast<uintptr_t>(addr) + size - align_addr + page_size - 1) & ~(page_size - 1);
            return mprotect(reinterpret_cast<void*>(align_addr), align_size, PROT_READ | PROT_WRITE | PROT_EXEC) == 0;
        }
    }

    /**
     * @brief Auto-calculates the VTable index of a virtual member function.
     * @tparam Method The type of the pointer-to-member function.
     * @param method The member function pointer (e.g., &MyClass::MyVirtualFunc).
     * @return std::expected containing the index, or an error.
     */
    template <typename Method>
    std::expected<size_t, Error> AutoCalculateIndex(Method method) {
        return detail::CalculateVTableIndex(method);
    }

    /**
     * @brief Modifies a single entry in a class VTable globally.
     * @warning This affects all instances of the class. Ensure the memory is writable.
     * @param instance A pointer to an instance of the class (to get the vptr).
     * @param index The index in the VTable to replace.
     * @param replace The replacement function pointer.
     * @param backup Pointer to store the original function pointer.
     * @return Success or error code.
     */
    inline std::expected<void, Error> HookEntry(void* instance, size_t index, void* replace, void** backup) {
        if (!instance) return std::unexpected(Error::InvalidInstance);

        void** vptr = *reinterpret_cast<void***>(instance);
        void** entry_addr = &vptr[index];

        if (!detail::UnprotectMemory(entry_addr, sizeof(void*))) {
            return std::unexpected(Error::MemoryProtectionFailed);
        }

        if (backup) {
            *backup = *entry_addr;
        }

        *entry_addr = replace;

        return {};
    }

    /**
     * @brief Combines auto-calculation and entry hooking.
     * @tparam Method The type of the pointer-to-member function.
     * @param instance A pointer to an instance of the class.
     * @param method The member function pointer to hook.
     * @param replace The replacement function pointer.
     * @param backup Pointer to store the original function pointer.
     * @return Success or error code.
     */
    template <typename Method>
    std::expected<void, Error> HookVirtualMethod(void* instance, Method method, void* replace, void** backup) {
        auto index_res = AutoCalculateIndex(method);
        if (!index_res.has_value()) {
            return std::unexpected(index_res.error());
        }
        return HookEntry(instance, index_res.value(), replace, backup);
    }

    /**
     * @brief Helper class to replace the entire VTable for a single instance safely.
     * Clones the original VTable and updates the instance's vptr to point to the clone.
     */
    class InstanceHook {
    private:
        void* _instance;
        void** _original_vptr;
        std::unique_ptr<void*[]> _cloned_vtable;
        size_t _vtable_size;

    public:
        /**
         * @param instance Pointer to the object.
         * @param vtable_size Number of pointers in the VTable.
         */
        InstanceHook(void* instance, size_t vtable_size)
            : _instance(instance), _vtable_size(vtable_size) {
            if (_instance) {
                _original_vptr = *reinterpret_cast<void***>(_instance);
                _cloned_vtable = std::make_unique<void*[]>(_vtable_size);
                std::memcpy(_cloned_vtable.get(), _original_vptr, _vtable_size * sizeof(void*));
                
                // Swap the vptr
                *reinterpret_cast<void***>(_instance) = _cloned_vtable.get();
            }
        }

        ~InstanceHook() {
            Restore();
        }

        bool IsValid() const { return _instance != nullptr && _cloned_vtable != nullptr; }

        /**
         * @brief Hooks an entry in the cloned VTable.
         */
        std::expected<void, Error> Hook(size_t index, void* replace, void** backup = nullptr) {
            if (!IsValid() || index >= _vtable_size) return std::unexpected(Error::InvalidInstance);
            
            if (backup) {
                *backup = _cloned_vtable[index];
            }
            _cloned_vtable[index] = replace;
            return {};
        }

        /**
         * @brief Hooks an entry in the cloned VTable using auto calculation.
         */
        template <typename Method>
        std::expected<void, Error> HookMethod(Method method, void* replace, void** backup = nullptr) {
            auto idx = AutoCalculateIndex(method);
            if (!idx) return std::unexpected(idx.error());
            return Hook(idx.value(), replace, backup);
        }

        /**
         * @brief Restores the instance's vptr to the original VTable.
         */
        void Restore() {
            if (_instance && _original_vptr) {
                *reinterpret_cast<void***>(_instance) = _original_vptr;
                _original_vptr = nullptr; // Prevent double restore
            }
        }
        
        void* GetOriginalFunction(size_t index) const {
            if (index < _vtable_size && _original_vptr) {
                return _original_vptr[index];
            }
            return nullptr;
        }
        
        template <typename Method>
        void* GetOriginalFunction(Method method) const {
            auto idx = AutoCalculateIndex(method);
            if (idx) return GetOriginalFunction(idx.value());
            return nullptr;
        }
    };

} // namespace dobby::vtable

#endif // dobby_vtable_h
