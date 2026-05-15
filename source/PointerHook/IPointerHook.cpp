#include "IPointerHook.h"

#include <sys/mman.h>
#include <unistd.h>
#include <atomic>
#include <cerrno>
#include <cinttypes>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <vector>

#include <android/log.h>

#ifndef kANDROID_LOG_TAG
#define kANDROID_LOG_TAG "PtrHook"
#endif

#define PH_LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO,  kANDROID_LOG_TAG, __VA_ARGS__))
#define PH_LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, kANDROID_LOG_TAG, __VA_ARGS__))

namespace {

// Per-hook trampoline layout (32 B):
//   [+0]  bti c                ; 0xD503245F
//   [+4]  ldr x17, [pc, #+12]  ; load IPointerHook* (literal @ +16)
//   [+8]  ldr x16, [pc, #+16]  ; load glue addr    (literal @ +24)
//   [+12] br  x16              ; 0xD61F0200
//   [+16] .quad hook_ptr
//   [+24] .quad glue_addr
// kTrampolineSize must agree with ph_trampoline_pool.s `.rept` block.
constexpr size_t kTrampolineSize = 32;

inline void EncodeTrampoline(void* rw_addr, IPointerHook* hook) {
    uint32_t* code = reinterpret_cast<uint32_t*>(rw_addr);
    code[0] = 0xD503245Fu;  // bti c
    code[1] = 0x58000071u;  // ldr x17, [pc, #+12]
    code[2] = 0x58000090u;  // ldr x16, [pc, #+16]
    code[3] = 0xD61F0200u;  // br  x16

    uint64_t* data = reinterpret_cast<uint64_t*>(code + 4);
    data[0] = reinterpret_cast<uint64_t>(hook);
    data[1] = reinterpret_cast<uint64_t>(&_ph_glue_entry);
}

inline uintptr_t PageAlign(uintptr_t addr) {
    static const uintptr_t pageSize = sysconf(_SC_PAGESIZE);
    return addr & ~(pageSize - 1);
}

// -1 means addr falls in no mapped segment (stale offset, not RELRO etc.).
inline int GetPageProt(uintptr_t addr) {
    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) return -1;
    char line[512];
    int prot = -1;
    while (fgets(line, sizeof(line), fp)) {
        uintptr_t start, end;
        char perms[5];
        if (sscanf(line, "%" SCNxPTR "-%" SCNxPTR " %4s", &start, &end, perms) == 3) {
            if (addr >= start && addr < end) {
                prot = 0;
                if (perms[0] == 'r') prot |= PROT_READ;
                if (perms[1] == 'w') prot |= PROT_WRITE;
                if (perms[2] == 'x') prot |= PROT_EXEC;
                break;
            }
        }
    }
    fclose(fp);
    return prot;
}

inline bool MemProtectRead(uintptr_t address, void* buffer, size_t len) {
    int origProt = GetPageProt(address);
    if (origProt < 0) {
        PH_LOGE("MemProtectRead: %p not in /proc/self/maps (stale offset?)", (void*)address);
        return false;
    }
    if (origProt & PROT_READ) {
        std::memcpy(buffer, reinterpret_cast<const void*>(address), len);
        return true;
    }
    uintptr_t pageStart = PageAlign(address);
    size_t totalSize = (address - pageStart) + len;
    if (mprotect(reinterpret_cast<void*>(pageStart), totalSize, origProt | PROT_READ) != 0) {
        PH_LOGE("MemProtectRead: mprotect(+R) failed at page %p (orig=0x%x): %s",
             (void*)pageStart, origProt, strerror(errno));
        return false;
    }
    std::memcpy(buffer, reinterpret_cast<const void*>(address), len);
    mprotect(reinterpret_cast<void*>(pageStart), totalSize, origProt);
    return true;
}

inline bool MemProtectWrite(uintptr_t address, const void* data, size_t len) {
    int origProt = GetPageProt(address);
    if (origProt < 0) {
        PH_LOGE("MemProtectWrite: %p not in /proc/self/maps (stale offset?)", (void*)address);
        return false;
    }
    auto do_write = [&]() {
        // Atomic store for the single-pointer-install fast path — prevents
        // memcpy from decomposing into torn byte stores visible mid-write.
        if (len == sizeof(uintptr_t) && (address & 7) == 0) {
            uintptr_t v;
            std::memcpy(&v, data, sizeof(v));
            __atomic_store_n(reinterpret_cast<uintptr_t*>(address), v, __ATOMIC_RELEASE);
        } else {
            std::memcpy(reinterpret_cast<void*>(address), data, len);
        }
    };

    if (origProt & PROT_WRITE) {
        do_write();
        return true;
    }
    uintptr_t pageStart = PageAlign(address);
    size_t totalSize = (address - pageStart) + len;
    if (mprotect(reinterpret_cast<void*>(pageStart), totalSize, origProt | PROT_WRITE) != 0) {
        PH_LOGE("MemProtectWrite: mprotect(+W) failed at page %p (orig=0x%x): %s",
             (void*)pageStart, origProt, strerror(errno));
        return false;
    }
    do_write();
    mprotect(reinterpret_cast<void*>(pageStart), totalSize, origProt);
    return true;
}

// Linker-reserved RX region in libdfmhook .text (see ph_trampoline_pool.s).
// 2048 × 32 B slots, brk-prefilled, bumped monotonically, never recycled.

extern "C" {
    extern uint8_t __ph_trampoline_pool_start[];
    extern uint8_t __ph_trampoline_pool_end[];
}

class InBinaryTrampolinePool {
public:
    static InBinaryTrampolinePool& Instance() {
        static InBinaryTrampolinePool inst;
        return inst;
    }

    uintptr_t Alloc(IPointerHook* hook) {
        const uintptr_t base = reinterpret_cast<uintptr_t>(__ph_trampoline_pool_start);
        const uintptr_t end  = reinterpret_cast<uintptr_t>(__ph_trampoline_pool_end);
        const size_t capacity = (end - base) / kTrampolineSize;

        size_t slot;
        {
            std::lock_guard<std::mutex> g(slot_mtx_);
            if (next_slot_ >= capacity) {
                PH_LOGE("[InBinaryTrampolinePool] exhausted (max %zu slots)", capacity);
                return 0;
            }
            slot = next_slot_++;
        }

        uintptr_t tramp = base + slot * kTrampolineSize;
        WriteSlotSealed(tramp, hook);
        return tramp;
    }

private:
    InBinaryTrampolinePool() = default;

    void WriteSlotSealed(uintptr_t tramp, IPointerHook* hook) {
        // Page-wide W-window; serialise concurrent installers on the same page.
        std::lock_guard<std::mutex> guard(write_mtx_);

        uintptr_t pg_lo = PageAlign(tramp);
        size_t    pg_sz = sysconf(_SC_PAGESIZE);

        if (mprotect(reinterpret_cast<void*>(pg_lo), pg_sz,
                     PROT_READ | PROT_WRITE) != 0) {
            PH_LOGE("[InBinaryTrampolinePool] mprotect(+W) failed at %p: %s",
                 reinterpret_cast<void*>(pg_lo), strerror(errno));
            return;
        }

        EncodeTrampoline(reinterpret_cast<void*>(tramp), hook);
        // Required for self-modifying code (ARM ARM B2.4.4).
        __builtin___clear_cache(reinterpret_cast<char*>(tramp),
                                reinterpret_cast<char*>(tramp + kTrampolineSize));

        if (mprotect(reinterpret_cast<void*>(pg_lo), pg_sz,
                     PROT_READ | PROT_EXEC) != 0) {
            PH_LOGE("[InBinaryTrampolinePool] mprotect(RX) failed at %p: %s",
                 reinterpret_cast<void*>(pg_lo), strerror(errno));
        }
    }

    std::mutex slot_mtx_;
    std::mutex write_mtx_;
    size_t     next_slot_ = 0;
};

// ─── SelfTest ─────────────────────────────────────────────────────────────

constexpr uint32_t kExpectedGluePrefix[4] = {
    0xD50324DFu,  // bti jc
    0xD10C43FFu,  // sub sp, sp, #0x310
    0xF90187F1u,  // str x17, [sp, #0x308]
    0xD53B4210u,  // mrs x16, nzcv
};

bool CheckGluePrefix() {
    const uint32_t* code = reinterpret_cast<const uint32_t*>(&_ph_glue_entry);
    for (int i = 0; i < 4; ++i) {
        if (code[i] != kExpectedGluePrefix[i]) {
            PH_LOGE("[SelfTest] glue prefix mismatch at +%d: got %08x want %08x",
                 i * 4, code[i], kExpectedGluePrefix[i]);
            return false;
        }
    }
    return true;
}

bool CheckEncodeTrampoline() {
    alignas(16) uint8_t buf[kTrampolineSize] = {0};
    constexpr uintptr_t kSentinelHook = 0xC0FFEEC0FFEEC0FFULL;
    EncodeTrampoline(buf, reinterpret_cast<IPointerHook*>(kSentinelHook));

    const uint32_t* code = reinterpret_cast<const uint32_t*>(buf);
    const uint64_t* data = reinterpret_cast<const uint64_t*>(buf + 16);

    if (code[0] != 0xD503245Fu ||
        code[1] != 0x58000071u ||
        code[2] != 0x58000090u ||
        code[3] != 0xD61F0200u)
    {
        PH_LOGE("[SelfTest] EncodeTrampoline instr mismatch: %08x %08x %08x %08x",
             code[0], code[1], code[2], code[3]);
        return false;
    }
    if (data[0] != kSentinelHook) {
        PH_LOGE("[SelfTest] EncodeTrampoline hook literal mismatch: %llx vs %llx",
             (unsigned long long)data[0], (unsigned long long)kSentinelHook);
        return false;
    }
    if (data[1] != reinterpret_cast<uint64_t>(&_ph_glue_entry)) {
        PH_LOGE("[SelfTest] EncodeTrampoline glue literal mismatch: %llx vs %llx",
             (unsigned long long)data[1],
             (unsigned long long)reinterpret_cast<uint64_t>(&_ph_glue_entry));
        return false;
    }
    return true;
}

using SelfTestFn = void (*)(uint64_t, uint64_t, uint64_t, uint64_t,
                            uint64_t, uint64_t, uint64_t, uint64_t,
                            double, double, double, double);

// `noinline` + asm constraints force every arg through the real calling
// convention — that's the codegen we're verifying.
[[gnu::noinline]] void SelfTestTarget(
    uint64_t a, uint64_t b, uint64_t c, uint64_t d,
    uint64_t e, uint64_t f, uint64_t g, uint64_t h,
    double da, double db, double dc, double dd)
{
    __asm__ volatile(""
        :
        : "r"(a), "r"(b), "r"(c), "r"(d),
          "r"(e), "r"(f), "r"(g), "r"(h),
          "w"(da), "w"(db), "w"(dc), "w"(dd)
        : "memory");
}

SelfTestFn volatile g_self_test_fn = &SelfTestTarget;

class SelfTestHook : public IPointerHook {
public:
    bool                     fired = false;
    std::vector<std::string> errors;

    std::string GetName() const override { return "[SelfTest]"; }
    uintptr_t   GetElfBase() const override { return 0; }
    uintptr_t   GetSlotAddr() const override {
        return reinterpret_cast<uintptr_t>(&g_self_test_fn);
    }
    uintptr_t   GetTargetAddr() const override { return 0; }

    uintptr_t OnCall(RegContext* ctx) override {
        fired = true;

        constexpr uint64_t kPattern = 0x1111111111111111ULL;
        for (int i = 0; i < 8; ++i) {
            uint64_t expected = kPattern * (i + 1);
            if (ctx->general.x[i] != expected) {
                errors.push_back(std::format("x{}: got {:#018x} want {:#018x}",
                                              i, ctx->general.x[i], expected));
            }
        }
        for (int i = 0; i < 4; ++i) {
            double expected = (i + 1) * 1.5;
            if (ctx->floating.d[i] != expected) {
                errors.push_back(std::format("d{}: got {} want {}",
                                              i, ctx->floating.d[i], expected));
            }
        }
        if (ctx->_pad != 0) {
            errors.push_back(std::format("_pad: got {:#x} want 0", ctx->_pad));
        }

        return GetOrigAddr();
    }
};

bool CheckEndToEnd() {
    SelfTestHook hook;
    hook.Resolve();
    hook.Install();

    constexpr uint64_t kPattern = 0x1111111111111111ULL;
    g_self_test_fn(
        kPattern * 1, kPattern * 2, kPattern * 3, kPattern * 4,
        kPattern * 5, kPattern * 6, kPattern * 7, kPattern * 8,
        1.5, 3.0, 4.5, 6.0);

    hook.Restore();

    if (!hook.fired) {
        PH_LOGE("[SelfTest] OnCall did not fire — dispatch path is broken");
        return false;
    }
    if (!hook.errors.empty()) {
        for (const auto& e : hook.errors) {
            PH_LOGE("[SelfTest] ctx mismatch: %s", e.c_str());
        }
        return false;
    }
    return true;
}

std::atomic<bool> g_self_test_passed{false};

} // anonymous namespace

#define MAKE_CRASH()     \
    __asm__ volatile (   \
        "mov x0, xzr;"   \
        "mov x29, x0;"   \
        "mov sp, x0;"    \
        "br x0;"         \
        : : :            \
    );

// asm/C++ bridge. ph_arm64_glue.s `bl _ph_dispatcher` lands here after
// register save; OnCall's return drives the glue's BR/RET epilogue.
extern "C" uintptr_t _ph_dispatcher(RegContext* ctx, IPointerHook* hook) {
    return hook->OnCall(ctx);
}

std::string RegContext::ToString() const
{
    std::string result;
    for (int i = 0; i < 29; i++) {
        result += std::format("x{}: {:#016x}\n", i, general.x[i]);
    }
    result += std::format("fp: {:#016x}\n", fp);
    result += std::format("lr: {:#016x}\n", lr);
    result += std::format("sp: {:#016x}\n", sp);
    result += std::format("nzcv: {:#016x}\n", nzcv);
    return result;
}

IPointerHook::IPointerHook() = default;

IPointerHook::~IPointerHook()
{
    // Residual UAF: an in-flight caller that LDR'd trampoline_ before this
    // restore will still reach OnCall on a partly-destroyed derived object.
    // Trampolines are permanent (ROADMAP §3); the C++ object isn't. Typical
    // workload (install-once for process lifetime) doesn't exercise this.
    Restore();
}

void IPointerHook::Resolve()
{
    slot_ = GetSlotAddr();

    if (uintptr_t target = GetTargetAddr(); target) {
        orig_ = target;
    } else {
        uintptr_t temp = 0;
        if (MemProtectRead(slot_, &temp, sizeof(uintptr_t)) && temp != 0) {
            orig_ = StripPAC(temp);
        } else {
            PH_LOGE("[%s] Resolve failed: orig is null", GetName().c_str());
            slot_ = 0;
            return;
        }
    }

    PH_LOGI("[%s] Resolved slot=%p orig=%p",
         GetName().c_str(), (void*)slot_, (void*)orig_);
}

uintptr_t IPointerHook::AllocTrampoline()
{
    return InBinaryTrampolinePool::Instance().Alloc(this);
}

bool IPointerHook::PrepareTrampoline()
{
    if (trampoline_ != 0) return true;
    if (slot_ == 0) {
        PH_LOGE("[%s] PrepareTrampoline: Resolve() not called yet", GetName().c_str());
        return false;
    }

    uintptr_t tramp = AllocTrampoline();
    if (tramp == 0) {
        PH_LOGE("[%s] PrepareTrampoline: AllocTrampoline returned 0", GetName().c_str());
        return false;
    }

    trampoline_ = tramp;
    PH_LOGI("[%s] trampoline @ %p", GetName().c_str(), (void*)trampoline_);
    return true;
}

void IPointerHook::Install()
{
    if (!PrepareTrampoline()) return;

    if (!MemProtectWrite(slot_, &trampoline_, sizeof(uintptr_t))) {
        PH_LOGE("[%s] Install failed: MemProtectWrite error at %p",
             GetName().c_str(), (void*)slot_);
        return;
    }

    installed_ = true;

    PH_LOGI("[%s] Install: slot=%p orig=%p tramp=%p",
        GetName().c_str(), (void*)slot_, (void*)orig_, (void*)trampoline_);
}

void IPointerHook::Restore()
{
    if (!installed_) return;

    if (!MemProtectWrite(slot_, &orig_, sizeof(uintptr_t))) {
        PH_LOGE("[%s] Restore failed: MemProtectWrite error at %p",
             GetName().c_str(), (void*)slot_);
    }
    installed_ = false;
    PH_LOGI("[%s] Restore", GetName().c_str());
}

bool IPointerHook::SelfTest()
{
    bool ok = CheckGluePrefix()
           && CheckEncodeTrampoline()
           && CheckEndToEnd();
    g_self_test_passed.store(ok, std::memory_order_release);
    if (ok) {
        PH_LOGI("[SelfTest] all checks passed");
    } else {
        PH_LOGE("[SelfTest] FAILED — PointerHookManager will refuse Adds");
    }
    return ok;
}

bool IPointerHook::PassedSelfTest()
{
    return g_self_test_passed.load(std::memory_order_acquire);
}
