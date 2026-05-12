# TODO - BNM Reliability & Feature Improvement Roadmap

## Konteks

BNM bukan "ampas" secara konsep, tetapi implementasi saat ini masih terasa rapuh di beberapa case karena banyak area penting masih bergantung pada heuristic layout IL2CPP, validasi pointer minimal, dan diagnostic yang belum terstruktur.

Perubahan terbaru pada resolver `Class::Init` sudah menjadi contoh arah yang benar:

- menambah kandidat lintas source (`P4 object_new`, `P5 array_class_get`)
- memakai consensus lintas source
- menjaga Path 1 sebagai safe compatibility fallback
- membuat non-Path 1 selection harus opt-in explicit
- menambah log offset dan alasan pemilihan

Roadmap ini berisi rencana staged agar BNM lebih reliable sebelum menambah fitur besar lain.

---

## Prinsip pengerjaan

1. Jangan rewrite besar sekaligus.
2. Jangan mengubah behavior public API tanpa alasan kuat.
3. Default harus safe dan backward-compatible.
4. Experimental behavior harus opt-in via macro/CMake option.
5. Resolver internal harus punya alasan pemilihan yang jelas.
6. Log success jangan warning; warning hanya untuk fallback/ambigu/non-safe case.
7. Semua stage wajib build setelah perubahan.
8. Hindari menyentuh dependency external dan area unrelated.

---

# Phase 1 - Resolver hardening

## [P0] 1. Extract reusable consensus resolver helper

Status: [DONE]

Fix diterapkan:
- Helper lokal `ClassInitCandidate`/`FindClassInitMajorityCandidate` diekstrak menjadi helper resolver reusable di `Loading.cpp`:
  - `ResolverCandidate`
  - `ResolverResult`
  - `ResolveByConsensus(...)`
- Candidate sekarang membawa name, raw address, source group, priority, valid flag, dan invalid reason.
- Result sekarang membawa selected address, vote count, candidate names, reason, confirmed legacy/P1 flag, dan non-legacy flag.
- `Class::Init` sekarang memakai helper generic tersebut, tetap mempertahankan Path 1 sebagai safe default.

Verifikasi:
- Build default sukses: `cmake -S . -B build/dev-p1-default -DBNM_ENABLE_LTO=OFF && cmake --build build/dev-p1-default -j2`.
- Build extended-off dan non-P1 opt-in diverifikasi setelah perubahan ini.
- Runtime dua game tetap perlu dites oleh user karena environment lokal tidak menjalankan target Android/game.


Rencana:
- Buat helper reusable untuk candidate-based resolution.
- Minimal struktur:
  - `ResolverCandidate`
  - `ResolverResult`
  - `ResolveByConsensus(...)`
- Candidate harus membawa:
  - name
  - raw address
  - source group
  - priority
  - validity
- Result harus membawa:
  - selected address
  - vote count
  - candidate names
  - reason
  - apakah confirmed legacy/P1
  - apakah non-legacy

Verifikasi:
- Build default.
- Build extended-off.
- Build non-P1 opt-in.
- Pastikan output `Class::Init` tetap sama pada dua game yang sudah dites.

---

## [P0] 2. Add executable/text-range validation for resolved addresses

Status: [DONE]

Fix diterapkan:
- Tambah helper validasi resolver internal di `Loading.cpp`:
  - `IsExecutableAddress(ptr)` berbasis `/proc/self/maps` untuk memastikan mapping executable (`r-x`).
  - `IsInsideLibIl2CppText(ptr)` berbasis `dladdr` untuk memastikan alamat berasal dari mapping `libil2cpp`.
  - `LooksLikeFunctionStart(ptr)` sebagai validasi ringan allocated + executable.
  - `ValidateResolvedAddress(ptr)` untuk alasan invalid yang dipakai diagnostic.
- Candidate `Class::Init` sekarang dinilai valid/invalid dengan reason, bukan hanya `IsAllocated`.
- Final sanity check `Class::Init` tetap memakai legacy hard-fail (`IsAllocated`) agar default backward-compatible, tetapi menambah warning diagnostic jika selected address gagal executable/text check.

Verifikasi:
- Candidate null/outside/non-executable akan ditolak dengan invalid reason.
- Build default sukses.
- Build extended-off dan non-P1 opt-in diverifikasi setelah perubahan ini.


Rencana:
- Tambah helper validasi:
  - `IsExecutableAddress(ptr)`
  - `IsInsideLibIl2CppText(ptr)` jika data segment text bisa diperoleh dari dl_iterate_phdr/proc maps.
  - `LooksLikeFunctionStart(ptr)` untuk validasi ringan.
- Jangan langsung fail hard untuk semua resolver di tahap awal; mulai dari diagnostic warning.
- Gunakan pada kandidat resolver internal.

Verifikasi:
- Candidate valid lama tetap lolos.
- Kandidat null/outside range ditolak/log warning.
- Build semua mode.

---

## [P0] 3. Keep Class::Init safe mode as baseline

Status: [DONE]

Yang sudah diterapkan:
- Extended consensus default ON.
- P1 tetap safe default.
- Non-P1 selection butuh `BNM_ALLOW_NON_P1_CLASS_INIT_CONSENSUS=ON`.
- P4/P5 diagnostic chain ditambahkan.
- Success P1 confirmation memakai debug log.

Catatan:
Pattern yang sudah terbukti pada dua game:
- `P1 == P4D3`
- `P2 == P5D1`
- `P1 - P2 == 0x22c`
- `Class::Init - Class::FromIl2CppType == 0x4d8`

Verifikasi sudah dilakukan:
- Build default extended safe mode sukses.
- Build extended OFF sukses.
- Build non-P1 opt-in sukses.
- Runtime log dua game menunjukkan `P1 == P4D3`.

---

## [P1] 4. Apply consensus pattern to other internal resolvers

Status: [TODO]

Target awal:
- `Class::FromIl2CppType`
- `Type::GetClassOrElementClass`
- `Image::FromName`
- `Assembly::GetAllAssemblies`
- `Image::GetTypes` fallback path

Masalah:
Resolver lain masih rawan bergantung pada fixed jump depth/call index.

Rencana:
- Audit setiap resolver di `Loading.cpp`.
- Catat source path yang dipakai sekarang.
- Tambah kandidat alternatif hanya jika ada source IL2CPP API yang masuk akal.
- Default jangan mengubah target lama kecuali confirmed oleh consensus aman.

Verifikasi:
- Build.
- Runtime log resolver pada minimal dua game.
- Pastikan selected target sama atau punya alasan kuat jika berbeda.

---

# Phase 2 - Diagnostics & logging

## [P1] 5. Add structured resolver diagnostic mode

Status: [DONE]

Fix diterapkan:
- Tambah CMake option `BNM_ENABLE_RESOLVER_DIAGNOSTICS` default OFF.
- Tambah structured diagnostic helpers:
  - `LogResolverCandidate(...)`
  - `LogResolverSummary(...)`
- Saat diagnostics ON, log berisi resolver name, candidate name, raw address, lib offset, valid/invalid reason, selected target, vote count, reason, mode, dan fallback.
- Saat diagnostics OFF, log tidak terlalu ramai; hanya invalid non-null candidate yang diperingatkan.

Verifikasi:
- Build default sukses.
- Build extended-off dan non-P1 opt-in diverifikasi setelah perubahan ini.
- Runtime debug log lengkap perlu dites pada target game dengan `BNM_ENABLE_RESOLVER_DIAGNOSTICS=ON`.


Rencana:
- Tambah macro/CMake option:
  - `BNM_ENABLE_RESOLVER_DIAGNOSTICS`
- Format log standar:
  - resolver name
  - candidate list
  - raw address
  - offset lib
  - valid/invalid reason
  - vote count
  - selected target
  - reason
  - mode safe/experimental/legacy

Contoh output ideal:

```text
BNM: Resolver Class::Init
  P1 array_new_specific.d2 = 0x169f4d0 valid yes
  P2 array_new.d2          = 0x169f2a4 valid yes
  P4 object_new.d3         = 0x169f4d0 valid yes
  selected                 = 0x169f4d0
  reason                   = Path 1 confirmed by independent P4D3
  mode                     = safe extended consensus
```

Verifikasi:
- Debug build menampilkan log lengkap.
- Non-debug build tidak terlalu ramai.
- Warning hanya muncul untuk no-majority/non-P1/fallback berisiko.

---

## [P1] 6. Clean log levels

Status: [DONE]

Fix diterapkan:
- Success/confirmed Path 1 tetap `BNM_LOG_DEBUG`.
- No-majority/fallback tetap `BNM_LOG_WARN`.
- Non-P1 consensus ditemukan tetap `BNM_LOG_WARN`.
- Non-P1 consensus dipakai karena opt-in tetap `BNM_LOG_WARN`.
- Fatal missing/invalid resolved address tetap `BNM_LOG_ERR` dengan reason.
- Structured diagnostics default OFF agar non-debug/non-diagnostic runtime tidak terlalu noisy.

Verifikasi:
- Build default sukses.
- Warning normal success path tidak ditambah; warning tersisa untuk invalid candidate, fallback, non-P1, atau fatal-adjacent actionable case.


Rencana:
- Success/confirmed path => DEBUG atau INFO.
- Fallback tanpa confirmation => WARN.
- Non-P1 candidate ditemukan => WARN.
- Non-P1 candidate dipakai karena opt-in => WARN.
- Fatal missing API => ERR.

Verifikasi:
- Runtime normal tidak dipenuhi warning palsu.
- Warning yang tersisa memang actionable.

---

# Phase 3 - Hook backend audit

## [P1] 7. Audit BasicHook and trampoline safety

Status: [TODO]

Masalah:
BNM sangat bergantung pada hook backend. Jika hook backend lemah, resolver yang benar tetap bisa crash.

Area audit:
- ARM64 branch range.
- ARMv7/thumb handling.
- Trampoline allocation.
- Instruction relocation.
- Cache flush.
- Duplicate hook handling.
- Failure rollback.
- Thread-safety saat hook install.

Rencana:
- Audit tanpa refactor besar dulu.
- Catat bug/risiko dengan file/line evidence.
- Fix staged per risiko.

Verifikasi:
- Build.
- Hook install success/failure log jelas.
- Tidak ada regression pada Class::Init hook.

---

# Phase 4 - Metadata and wrapper safety

## [P1] 8. Harden method/class/field wrapper validation

Status: [TODO]

Masalah:
Wrapper yang langsung cast/invoke native pointer rawan crash kalau metadata tidak valid atau signature tidak cocok.

Rencana:
- Safe invoke harus fail closed:
  - method valid
  - klass valid
  - methodPointer valid
  - static/instance cocok
  - receiver object valid untuk instance method
  - arg count/signature assumption cocok
- Error log harus menyebut alasan gagal.

Verifikasi:
- Invalid method tidak crash.
- Wrong receiver tidak crash.
- Valid call tetap jalan.

---

## [P1] 9. Improve lookup failure diagnostics

Status: [TODO]

Masalah:
Kalau class/method/field lookup gagal, user sering hanya mendapat null tanpa alasan detail.

Rencana:
- Tambah optional last-error/context log:
  - class name
  - method/field name
  - arg count
  - overload candidates jika ada
  - reason mismatch

Contoh:

```text
BNM: Method lookup failed:
  class: PlayerController
  method: TakeDamage
  args: 1
  reason: overload mismatch
  candidates: TakeDamage(float), TakeDamage(int,bool)
```

Verifikasi:
- Lookup valid tetap tidak noisy.
- Lookup gagal memberi reason yang actionable.

---

# Phase 5 - Version-aware behavior

## [P2] 10. Add Unity/IL2CPP version-aware resolver strategy

Status: [TODO]

Masalah:
Unity/IL2CPP layout bisa beda antar versi. Satu heuristic untuk semua versi rawan gagal.

Rencana:
- Deteksi Unity version/metadata version jika tersedia.
- Kelompokkan resolver strategy per versi/range.
- Default tetap safe fallback.
- Tambah log version info saat diagnostics ON.

Verifikasi:
- Minimal test pada beberapa Unity version berbeda.
- Resolver memilih strategy yang sesuai.

---

# Phase 6 - Tests

## [P1] 11. Add unit tests for resolver consensus logic

Status: [TODO]

Masalah:
Consensus/tie/safe-mode behavior bisa regress tanpa test.

Test wajib:
- P1 confirmed by independent source => selected P1.
- No majority => fallback P1.
- Non-P1 majority with safe mode => keep P1.
- Non-P1 majority with opt-in => select non-P1.
- Same source group duplicate tidak dihitung sebagai independent consensus.
- Invalid/null candidate ignored.

Verifikasi:
- Test lokal sukses.
- Build tetap sukses.

---

## [P2] 12. Add tests for AssemblerUtils branch decoding

Status: [TODO]

Masalah:
`FindNextJump` adalah fondasi resolver. Kalau decoder salah, semua resolver bisa salah.

Rencana:
- Unit test untuk ARM64 B/BL.
- Unit test untuk BR/BLR thunk pattern jika memungkinkan.
- Unit test untuk x86 call/jmp jika target masih didukung.
- Mock memory buffer agar tidak perlu runtime IL2CPP.

Verifikasi:
- Decoder menghasilkan target expected.
- Invalid instruction tidak menghasilkan bogus target.

---

# Phase 7 - Documentation

## [P2] 13. Document resolver modes and failure meanings

Status: [TODO]

Masalah:
User bisa salah paham terhadap log resolver, terutama kata fallback/consensus/Path.

Rencana dokumentasi:
- Arti P1/P2/P3/P4/P5.
- Kenapa P1 tetap safe default.
- Cara enable/disable extended consensus.
- Cara enable non-P1 experimental mode.
- Log normal vs log berbahaya.
- Cara mengumpulkan log untuk report.

Verifikasi:
- Ada dokumen ringkas di repo.
- Contoh log dari dua game dimasukkan sebagai referensi pattern aman.

---

# Backlog - Kandidat fitur tambahan

## [P2] 14. P6/P7 diagnostic candidates

Status: [BACKLOG]

Kandidat:
- `il2cpp_field_static_get_value`
- `il2cpp_field_static_set_value`

Catatan:
Jangan ditambahkan sebelum consensus helper reusable dan safe-mode behavior matang. Static field path bisa conditional/noisy.

---

## [P3] 15. Better public API ergonomics

Status: [BACKLOG]

Ide:
- Result/error type untuk lookup.
- Last error context.
- Typed wrapper yang lebih aman.
- Cache invalidation yang jelas.
- Example usage yang lengkap.

---

# Merge guidance

Perubahan layak merge jika:
- default behavior backward-compatible
- build default sukses
- build legacy/off sukses jika ada option
- build experimental option sukses jika ada
- `git diff --check` bersih
- runtime log minimal dua target tidak menunjukkan non-P1 selected diam-diam
- warning hanya muncul untuk kondisi actionable

Perubahan tidak layak merge jika:
- default bisa memilih target baru tanpa opt-in
- resolver non-P1 dipakai tanpa warning
- hanya `IsAllocated` menjadi bukti utama kebenaran function
- menambah banyak kandidat tanpa scoring/consensus yang jelas
- refactor menyentuh area unrelated
