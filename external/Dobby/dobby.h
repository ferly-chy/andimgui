/**
 * Dobby - Lightweight multi-platform exploit hook framework
 *
 * @maintainer: Gemini CLI
 * @version: 1.0 (Refactored to C23/C++26)
 * @date: 2026-03-24
 *
 * @description:
 * Dobby provides a modular set of tools for runtime code manipulation, 
 * including function hooking, memory patching, and instruction instrumentation.
 * This version has been modernized to adhere to C23 and C++26 standards.
 */

#ifndef dobby_h
#define dobby_h

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/**
 * @typedef addr_t
 * @brief Represents a memory address.
 */
typedef uintptr_t addr_t;
typedef uint32_t addr32_t;
typedef uint64_t addr64_t;

typedef void *dobby_func_t;
typedef void *asm_func_t;

/**
 * @enum DobbyStatus
 * @brief Return status codes for Dobby functions.
 */
typedef enum {
  kDobbySuccess = 0,
  kDobbyFailed = -1,
  RS_SUCCESS = 0, // Alias for compatibility
  RS_FAILED = -1
} DobbyStatus;

#if defined(__arm__)
/**
 * @struct DobbyRegisterContext
 * @brief Register context for ARM architecture.
 */
typedef struct {
  uint32_t dummy_0;
  uint32_t dummy_1;

  uint32_t dummy_2;
  uint32_t sp;

  union {
    uint32_t r[13];
    struct {
      uint32_t r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12;
    } regs;
  } general;

  uint32_t lr;
} DobbyRegisterContext;
#elif defined(__arm64__) || defined(__aarch64__)
#define ARM64_TMP_REG_NDX_0 17

typedef union _FPReg {
  __int128_t q;
  struct {
    double d1;
    double d2;
  } d;
  struct {
    float f1;
    float f2;
    float f3;
    float f4;
  } f;
} FPReg;

/**
 * @struct DobbyRegisterContext
 * @brief Register context for ARM64 architecture.
 */
typedef struct {
  uint64_t dmmpy_0; // dummy placeholder
  uint64_t sp;

  uint64_t dmmpy_1; // dummy placeholder
  union {
    uint64_t x[29];
    struct {
      uint64_t x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17, x18, x19, x20, x21, x22,
          x23, x24, x25, x26, x27, x28;
    } regs;
  } general;

  uint64_t fp;
  uint64_t lr;

  union {
    FPReg q[32];
    struct {
      FPReg q0, q1, q2, q3, q4, q5, q6, q7;
      // [!!! READ ME !!!]
      // for Arm64, can't access q8 - q31, unless you enable full floating-point register pack
      FPReg q8, q9, q10, q11, q12, q13, q14, q15, q16, q17, q18, q19, q20, q21, q22, q23, q24, q25, q26, q27, q28, q29,
          q30, q31;
    } regs;
  } floating;
} DobbyRegisterContext;
#elif defined(_M_IX86) || defined(__i386__)
/**
 * @struct DobbyRegisterContext
 * @brief Register context for X86 architecture.
 */
typedef struct _RegisterContext {
  uint32_t dummy_0;
  uint32_t esp;

  uint32_t dummy_1;
  uint32_t flags;

  union {
    struct {
      uint32_t eax, ebx, ecx, edx, ebp, esp, edi, esi;
    } regs;
  } general;

} DobbyRegisterContext;
#elif defined(_M_X64) || defined(__x86_64__)
/**
 * @struct DobbyRegisterContext
 * @brief Register context for X64 architecture.
 */
typedef struct {
  uint64_t dummy_0;
  uint64_t rsp;

  union {
    struct {
      uint64_t rax, rbx, rcx, rdx, rbp, rsp, rdi, rsi, r8, r9, r10, r11, r12, r13, r14, r15;
    } regs;
  } general;

  uint64_t dummy_1;
  uint64_t flags;
} DobbyRegisterContext;
#endif

/**
 * @brief Macro to define a hook helper.
 */
#define install_hook_name(name, fn_ret_t, fn_args_t...)                                                                \
  static fn_ret_t fake_##name(fn_args_t);                                                                              \
  static fn_ret_t (*orig_##name)(fn_args_t);                                                                           \
  /* __attribute__((constructor)) */ static void install_hook_##name(void *sym_addr) {                                 \
    DobbyHook(sym_addr, (dobby_func_t)fake_##name, (dobby_func_t *)&orig_##name);                          \
    return;                                                                                                            \
  }                                                                                                                    \
  fn_ret_t fake_##name(fn_args_t)

/**
 * @brief Patch code at a given address.
 * 
 * @param address The target address to patch.
 * @param buffer The new code buffer.
 * @param buffer_size Size of the buffer.
 * @return kDobbySuccess on success.
 */
DobbyStatus DobbyCodePatch(void *address, uint8_t *buffer, uint32_t buffer_size);

/**
 * @brief Perform an inline hook on a function.
 * 
 * @param address The address of the target function.
 * @param replace_func The address of the replacement function.
 * @param origin_func Pointer to store the original function's address.
 * @return kDobbySuccess on success.
 */
DobbyStatus DobbyHook(void *address, dobby_func_t replace_func, dobby_func_t *origin_func);

/**
 * @brief Instrument binary instructions at a given address.
 * 
 * @param address The target address.
 * @param pre_handler The callback to execute before the instruction.
 * @return kDobbySuccess on success.
 */
typedef void (*dobby_instrument_callback_t)(void *address, DobbyRegisterContext *ctx);
DobbyStatus DobbyInstrument(void *address, dobby_instrument_callback_t pre_handler);

/**
 * @brief Destroy a hook and restore the original code.
 * 
 * @param address The address where the hook was installed.
 * @return kDobbySuccess on success.
 */
DobbyStatus DobbyDestroy(void *address);

/**
 * @brief Get the Dobby framework version string.
 * @return Version string.
 */
const char *DobbyGetVersion();

/**
 * @brief Resolve a symbol from a given image.
 * 
 * @param image_name The name of the image (library).
 * @param symbol_name The name of the symbol to resolve.
 * @return Address of the symbol, or NULL if not found.
 */
void *DobbySymbolResolver(const char *image_name, const char *symbol_name);

/**
 * @brief Replace an entry in the import table.
 * 
 * @param image_name Target image.
 * @param symbol_name Symbol to replace.
 * @param fake_func Address of the fake function.
 * @param orig_func Pointer to store the original function's address.
 * @return kDobbySuccess on success.
 */
DobbyStatus DobbyImportTableReplace(char *image_name, char *symbol_name, dobby_func_t fake_func,
                            dobby_func_t *orig_func);

/**
 * @brief Enable near branch trampoline support.
 */
void dobby_enable_near_branch_trampoline();

/**
 * @brief Disable near branch trampoline support.
 */
void dobby_disable_near_branch_trampoline();

#ifdef __cplusplus
}
#endif

#endif
