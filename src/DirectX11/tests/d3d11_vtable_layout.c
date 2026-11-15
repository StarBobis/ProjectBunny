#include <d3d11_1.h>
#include <stddef.h>

size_t dx11_audit_context1_vtable_entries(void)
{
    return sizeof(ID3D11DeviceContext1Vtbl) / sizeof(void*);
}
size_t dx11_audit_finish_command_slot(void)
{
    return offsetof(ID3D11DeviceContext1Vtbl, FinishCommandList) / sizeof(void*);
}
