#pragma once

#include <d3d11_1.h>

//
// Forward declerations
//
extern FILE *LogFile;

// Exported by name from D3DCompiler_46.def (@2). C linkage keeps the symbol
// unique: d3dcompiler.lib also carries a C++-decorated DebugSetMute(int), and
// an undecorated .def entry must not be resolved by decoration search.
extern "C" void WINAPI DebugSetMute();
