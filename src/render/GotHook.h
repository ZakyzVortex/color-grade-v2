#pragma once

namespace color_grade::got {

// Looks for the loaded library whose path ends with `libSuffix` and rewrites every GOT/PLT slot
// that imports `symbol`.
//
//  - expectedCurrent == nullptr: patch every slot that imports `symbol`; the first original value
//    found is stored in *oldValue (before the slot is written).
//  - expectedCurrent != nullptr: only patch slots that currently hold that exact pointer (used to undo
//    a previous patch).
//
// Returns the number of slots changed. Only implemented for 64-bit ARM; elsewhere it returns 0.
int replaceSymbol(const char *libSuffix, const char *symbol, void *expectedCurrent, void *newValue,
                  void **oldValue);

} // namespace color_grade::got
