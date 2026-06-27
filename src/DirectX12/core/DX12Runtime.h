#pragma once

#include <Windows.h>

typedef HMODULE (*DX12LoadRealD3D12Fn)();

void BunnyDX12RuntimeInitialize(HINSTANCE module, DX12LoadRealD3D12Fn loadRealD3D12);
void BunnyDX12RuntimeShutdown();
