#include <d3d11_1.h>
#include <d3d11sdklayers.h>
#include <stddef.h>

size_t dx11_audit_context1_vtable_entries(void)
{
    return sizeof(ID3D11DeviceContext1Vtbl) / sizeof(void*);
}
size_t dx11_audit_finish_command_slot(void)
{
    return offsetof(ID3D11DeviceContext1Vtbl, FinishCommandList) / sizeof(void*);
}
size_t dx11_audit_map_slot(void)
{
    return offsetof(ID3D11DeviceContext1Vtbl, Map) / sizeof(void*);
}
size_t dx11_audit_unmap_slot(void)
{
    return offsetof(ID3D11DeviceContext1Vtbl, Unmap) / sizeof(void*);
}
size_t dx11_audit_device1_vtable_entries(void)
{
    return sizeof(ID3D11Device1Vtbl) / sizeof(void*);
}
size_t dx11_audit_create_texture2d_slot(void)
{
    return offsetof(ID3D11Device1Vtbl, CreateTexture2D) / sizeof(void*);
}
size_t dx11_audit_report_live_device_slot(void)
{
    return offsetof(ID3D11DebugVtbl, ReportLiveDeviceObjects) / sizeof(void*);
}
size_t dx11_candidate_ps_get_slot(void)
{
    return offsetof(ID3D11DeviceContext1Vtbl, PSGetShaderResources) / sizeof(void*);
}
size_t dx11_candidate_vb_get_slot(void)
{
    return offsetof(ID3D11DeviceContext1Vtbl, IAGetVertexBuffers) / sizeof(void*);
}
size_t dx11_candidate_ib_get_slot(void)
{
    return offsetof(ID3D11DeviceContext1Vtbl, IAGetIndexBuffer) / sizeof(void*);
}
