#pragma once

#include <Windows.h>
#include <d3d11.h>

struct D3D11CreationExports {
    PFN_D3D11_CREATE_DEVICE create_device = nullptr;
    PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN create_swap_chain = nullptr;

    bool valid() const noexcept { return create_device && create_swap_chain; }
};

inline D3D11CreationExports ResolveD3D11CreationExports(HMODULE module,
    PFN_D3D11_CREATE_DEVICE existing_device = nullptr,
    PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN existing_swap_chain = nullptr) noexcept
{
    return {
        existing_device ? existing_device : reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(
            GetProcAddress(module, "D3D11CreateDevice")),
        existing_swap_chain ? existing_swap_chain : reinterpret_cast<PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN>(
            GetProcAddress(module, "D3D11CreateDeviceAndSwapChain"))
    };
}
