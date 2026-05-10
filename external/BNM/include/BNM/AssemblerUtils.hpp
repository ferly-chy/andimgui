#pragma once

#include "Utils.hpp"

namespace BNM {

namespace AssemblerUtils {

// Goes through memory and tries to find b-, bl- or call instructions
// Then gets the address they go to
// index: 1 is the first, 2 is the second, etc.
BNM_PTR FindNextJump(BNM_PTR start, uint8_t index);

// Dump memory to hex for diagnostics
void DiagnosticDump(BNM_PTR address, size_t size);

} // namespace AssemblerUtils

} // namespace BNM
