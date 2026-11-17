#pragma once

#include <d3d11_1.h>

ID3D11Device1* hook_device(ID3D11Device1 *orig_device, ID3D11Device1 *hacker_device);
ID3D11Device1* lookup_hooked_device(ID3D11Device1 *orig_device);

// Preserve the original GetDevice reference while selecting the unhooked API view.
ID3D11Device* get_passthrough_device_view(ID3D11Device* original);
