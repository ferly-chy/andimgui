#include "BNM/AssemblerUtils.hpp"
#include "BNM/DebugMessages.hpp"
#include <cstdio>
#include <cstring>

namespace BNM {

namespace AssemblerUtils {

#if defined(__ARM_ARCH_7A__)

// Check if the address points to Thumb code (LSB is 1)
static bool IsThumb(BNM_PTR address) { return address & 1; }

static bool DecodeBranchOrCall(BNM_PTR address, BNM_PTR &outOffset) {
  bool thumb = IsThumb(address);
  BNM_PTR pcAddr = address & ~1;

  if (thumb) {
    uint16_t insn1 = *(uint16_t *)pcAddr;
    // Thumb-2 BL (32-bit): 11110... 11111...
    if ((insn1 & 0xF800) == 0xF000) {
      uint16_t insn2 = *(uint16_t *)(pcAddr + 2);
      if ((insn2 & 0xD000) == 0xD000) {
        int32_t s = (insn1 >> 10) & 1;
        int32_t j1 = (insn2 >> 13) & 1;
        int32_t j2 = (insn2 >> 11) & 1;
        int32_t i1 = !(j1 ^ s);
        int32_t i2 = !(j2 ^ s);
        int32_t imm10 = insn1 & 0x3FF;
        int32_t imm11 = insn2 & 0x7FF;
        int32_t offset =
            (s << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm11 << 1);
        if (s)
          offset |= 0xFE000000; // Sign extend
        outOffset = (address & ~1) + 4 + offset;
        outOffset |= 1; // Keep thumb bit
        return true;
      }
    }
    // Thumb B (16-bit): 11100...
    else if ((insn1 & 0xF800) == 0xE000) {
      int32_t offset = (int8_t)(insn1 & 0xFF) << 1;
      outOffset = (address & ~1) + 4 + offset;
      outOffset |= 1;
      return true;
    }
  } else {
    // ARM B/BL (32-bit)
    uint32_t insn = *(uint32_t *)address;
    if ((insn & 0x0A000000) == 0x0A000000) {
      int32_t offset = (int32_t)((insn & 0x00FFFFFF) << 8) >> 6;
      outOffset = address + 8 + offset;
      return true;
    }
  }
  return false;
}

#elif defined(__aarch64__)

enum class Arm64InsnType {
  UNKNOWN,
  B,
  BL,
  BR,
  BLR,
  ADRP,
  ADD_IMM,
  LDR_LIT,
  RET
};

struct Arm64Insn {
  Arm64InsnType type = Arm64InsnType::UNKNOWN;
  uint32_t rd = 0;
  uint32_t rn = 0;
  int64_t imm = 0;
  BNM_PTR address = 0;
};

static Arm64Insn DecodeArm64Insn(BNM_PTR address) {
  uint32_t insn = *(uint32_t *)address;
  Arm64Insn out;
  out.address = address;

  // B: 0001 01imm26
  if ((insn & 0xFC000000) == 0x14000000) {
    out.type = Arm64InsnType::B;
    int32_t imm26 = insn & 0x03FFFFFF;
    if (imm26 & 0x02000000)
      imm26 |= 0xFC000000; // Sign extend
    out.imm = (int64_t)imm26 << 2;
  }
  // BL: 1001 01imm26
  else if ((insn & 0xFC000000) == 0x94000000) {
    out.type = Arm64InsnType::BL;
    int32_t imm26 = insn & 0x03FFFFFF;
    if (imm26 & 0x02000000)
      imm26 |= 0xFC000000; // Sign extend
    out.imm = (int64_t)imm26 << 2;
  }
  // BR: 1101 0110 0001 1111 0000 00rn 0000 0000
  else if ((insn & 0xFFFFFC1F) == 0xD61F0000) {
    out.type = Arm64InsnType::BR;
    out.rn = (insn >> 5) & 0x1F;
  }
  // BLR: 1101 0110 0011 1111 0000 00rn 0000 0000
  else if ((insn & 0xFFFFFC1F) == 0xD63F0000) {
    out.type = Arm64InsnType::BLR;
    out.rn = (insn >> 5) & 0x1F;
  }
  // ADRP: 1 immlo:2 10000 immhi:19 rd:5
  else if ((insn & 0x9F000000) == 0x90000000) {
    out.type = Arm64InsnType::ADRP;
    int64_t imm = ((insn >> 29) & 0x03) | (((insn >> 5) & 0x7FFFF) << 2);
    if (insn & 0x800000)
      imm |= ~0x1FFFFF; // Sign extend
    out.imm = imm << 12;
    out.rd = insn & 0x1F;
  }
  // LDR (literal): 01 011 0 00 imm19 rd:5 (64-bit)
  else if ((insn & 0xFF000000) == 0x58000000) {
    out.type = Arm64InsnType::LDR_LIT;
    out.rd = insn & 0x1F;
    int32_t imm19 = (insn >> 5) & 0x7FFFF;
    if (imm19 & 0x40000)
      imm19 |= 0xFFF80000; // Sign extend
    out.imm = (int64_t)imm19 << 2;
  }
  // ADD (immediate): sf:1 0010001 sh:1 imm12:12 rn:5 rd:5
  else if ((insn & 0xFF000000) == 0x91000000) {
    out.type = Arm64InsnType::ADD_IMM;
    out.rd = insn & 0x1F;
    out.rn = (insn >> 5) & 0x1F;
    out.imm = (insn >> 10) & 0xFFF;
    if (insn & (1 << 22))
      out.imm <<= 12; // Shifted
  }
  // RET: 1101 0110 0101 1111 0000 00rn 0000 0000
  else if ((insn & 0xFFFFFC1F) == 0xD65F0000) {
    out.type = Arm64InsnType::RET;
    out.rn = (insn >> 5) & 0x1F;
  }

  return out;
}

static bool DecodeBranchOrCall(BNM_PTR address, BNM_PTR &outOffset) {
  auto insn = DecodeArm64Insn(address);
  if (insn.type == Arm64InsnType::B || insn.type == Arm64InsnType::BL) {
    outOffset = address + insn.imm;
    return true;
  }

  if (insn.type == Arm64InsnType::BR || insn.type == Arm64InsnType::BLR) {
    uint32_t targetReg = insn.rn;
    BNM_PTR adrpAddr = 0;
    BNM_PTR addAddr = 0;
    BNM_PTR ldrAddr = 0;
    int64_t adrpImm = 0;
    int64_t addImm = 0;
    int64_t ldrImm = 0;

    // Scan backward up to 16 instructions to find ADRP and ADD or LDR
    for (int i = 1; i <= 16; ++i) {
      auto prevInsn = DecodeArm64Insn(address - i * 4);
      if (prevInsn.rd != targetReg)
        continue;

      if (prevInsn.type == Arm64InsnType::ADRP) {
        adrpAddr = address - i * 4;
        adrpImm = prevInsn.imm;
      } else if (prevInsn.type == Arm64InsnType::ADD_IMM &&
                 prevInsn.rn == targetReg) {
        addAddr = address - i * 4;
        addImm = prevInsn.imm;
      } else if (prevInsn.type == Arm64InsnType::LDR_LIT) {
        ldrAddr = address - i * 4;
        ldrImm = prevInsn.imm;
      }

      if ((adrpAddr && addAddr) || ldrAddr)
        break;
    }

    if (ldrAddr) {
      outOffset = *(BNM_PTR *)(ldrAddr + ldrImm);
      return true;
    } else if (adrpAddr && addAddr) {
      outOffset = (adrpAddr & ~0xFFFULL) + adrpImm + addImm;
      return true;
    }
  }
  return false;
}

#elif defined(__i386__) || defined(__x86_64__)

static bool DecodeBranchOrCall(BNM_PTR address, BNM_PTR &outOffset) {
  uint8_t op = *(uint8_t *)address;
  if (op == 0xE8 || op == 0xE9) { // CALL or JMP rel32
    outOffset = address + *(int32_t *)(address + 1) + 5;
    return true;
  }
  return false;
}
#endif

void DiagnosticDump(BNM_PTR address, size_t size) {
  if (!address)
    return;
  // Use fixed size buffer for diagnostics
  char buf[1024];
  size_t actualSize = size > 256 ? 256 : size;
  BNM_PTR pcAddr = address & ~1;
  for (size_t i = 0; i < actualSize; ++i) {
    sprintf(buf + i * 3, "%02X ", *(uint8_t *)(pcAddr + i));
  }
  BNM_LOG_DEBUG("BNM: Diagnostic dump at %p: %s", (void *)address, buf);
}

BNM_PTR FindNextJump(BNM_PTR start, uint8_t index) {
  BNM_PTR outOffset = 0;
  BNM_PTR currentPos = start;

  for (uint8_t i = 0; i < index; ++i) {
    bool found = false;
    // Scan up to 256 bytes for the next jump
    for (int j = 0; j < 256; j += (sizeof(BNM_PTR) == 8 ? 4 : 2)) {
      if (DecodeBranchOrCall(currentPos + j, outOffset)) {
        currentPos = outOffset;
        found = true;
        break;
      }
    }
    if (!found) {
      BNM_LOG_WARN("BNM: Failed to find jump #%d starting from %p", i + 1,
                   (void *)currentPos);
      DiagnosticDump(currentPos, 32);
      return 0;
    }
  }
  return currentPos;
}

} // namespace AssemblerUtils

} // namespace BNM
