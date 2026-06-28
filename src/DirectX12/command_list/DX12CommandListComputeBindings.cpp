static void STDMETHODCALLTYPE HookedSetDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList, UINT count,
	ID3D12DescriptorHeap *const *heaps)
{
	if (DX12HotPathState::Get().skipAll) {
		PFN_SET_DESCRIPTOR_HEAPS original =
			DX12_CL_ORIG(commandList, 28, PFN_SET_DESCRIPTOR_HEAPS, SetDescriptorHeaps);
		if (original)
			original(commandList, count, heaps);
		return;
	}
	DX12_PROFILE_SCOPE(SetDescriptorHeaps);
	if (DX12InternalReplayState::IsActive()) {
		PFN_SET_DESCRIPTOR_HEAPS original =
			DX12_CL_ORIG(commandList, 28, PFN_SET_DESCRIPTOR_HEAPS, SetDescriptorHeaps);
		if (original)
			original(commandList, count, heaps);
		return;
	}

	ID3D12DescriptorHeap *cbvSrvUav = nullptr;
	ID3D12DescriptorHeap *sampler = nullptr;
	if (heaps) {
		for (UINT i = 0; i < count; ++i) {
			if (!heaps[i])
				continue;
			D3D12_DESCRIPTOR_HEAP_DESC desc = heaps[i]->GetDesc();
			if (desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
				cbvSrvUav = heaps[i];
			else if (desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
				sampler = heaps[i];
		}
	}
	DX12CommandListRuntimeSetDescriptorHeaps(commandList, cbvSrvUav, sampler);

	if (DX12HotPathState::Get().skipBindings && !DX12CommandListCaptureShouldTrackBindings()) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_SET_DESCRIPTOR_HEAPS original =
			DX12_CL_ORIG(commandList, 28, PFN_SET_DESCRIPTOR_HEAPS, SetDescriptorHeaps);
		if (original)
			original(commandList, count, heaps);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::SetDescriptorHeaps", commandList, " count=%u", count);
	DX12CommandListCaptureDescriptorHeaps(commandList, cbvSrvUav, sampler);
	DX12CommandListRuntimeBumpComputeBindingSerial(commandList);
	PFN_SET_DESCRIPTOR_HEAPS original =
		DX12_CL_ORIG(commandList, 28, PFN_SET_DESCRIPTOR_HEAPS, SetDescriptorHeaps);
	if (original)
		original(commandList, count, heaps);
}

static void STDMETHODCALLTYPE HookedSetComputeRootSignature(
	ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature)
{
	if (DX12HotPathState::Get().skipAll) {
		PFN_SET_ROOT_SIGNATURE original =
			DX12_CL_ORIG(commandList, 29, PFN_SET_ROOT_SIGNATURE, SetComputeRootSignature);
		if (original)
			original(commandList, rootSignature);
		return;
	}
	DX12_PROFILE_SCOPE(SetComputeRootSignature);

	if (DX12HotPathState::Get().skipBindings && !DX12CommandListCaptureShouldTrackBindings()) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_SET_ROOT_SIGNATURE original =
			DX12_CL_ORIG(commandList, 29, PFN_SET_ROOT_SIGNATURE, SetComputeRootSignature);
		if (original)
			original(commandList, rootSignature);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::SetComputeRootSignature", commandList,
		" rootSignature=%p", rootSignature);
	DX12CommandListCaptureComputeRootSignature(commandList, rootSignature);
	DX12CommandListRuntimeBumpComputeBindingSerial(commandList);
	PFN_SET_ROOT_SIGNATURE original =
		DX12_CL_ORIG(commandList, 29, PFN_SET_ROOT_SIGNATURE, SetComputeRootSignature);
	if (original)
		original(commandList, rootSignature);
}

static void STDMETHODCALLTYPE HookedSetGraphicsRootSignature(
	ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature)
{
	if (DX12HotPathState::Get().skipAll) {
		PFN_SET_ROOT_SIGNATURE original =
			DX12_CL_ORIG(commandList, 30, PFN_SET_ROOT_SIGNATURE, SetGraphicsRootSignature);
		if (original)
			original(commandList, rootSignature);
		return;
	}
	DX12_PROFILE_SCOPE(SetGraphicsRootSignature);

	if (DX12HotPathState::Get().skipGraphicsBindings && !DX12CommandListCaptureShouldTrackBindings()) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_SET_ROOT_SIGNATURE original =
			DX12_CL_ORIG(commandList, 30, PFN_SET_ROOT_SIGNATURE, SetGraphicsRootSignature);
		if (original)
			original(commandList, rootSignature);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::SetGraphicsRootSignature", commandList,
		" rootSignature=%p", rootSignature);
	DX12CommandListCaptureGraphicsRootSignature(commandList, rootSignature);
	PFN_SET_ROOT_SIGNATURE original =
		DX12_CL_ORIG(commandList, 30, PFN_SET_ROOT_SIGNATURE, SetGraphicsRootSignature);
	if (original)
		original(commandList, rootSignature);
}

static void STDMETHODCALLTYPE HookedSetComputeRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	if (DX12HotPathState::Get().skipAll) {
		PFN_SET_ROOT_DESCRIPTOR_TABLE original =
			DX12_CL_ORIG(commandList, 31, PFN_SET_ROOT_DESCRIPTOR_TABLE, SetComputeRootDescriptorTable);
		if (original)
			original(commandList, rootParameterIndex, baseDescriptor);
		return;
	}
	DX12_PROFILE_SCOPE(SetComputeRootDescriptorTable);
	if (DX12InternalReplayState::IsActive()) {
		PFN_SET_ROOT_DESCRIPTOR_TABLE original =
			DX12_CL_ORIG(commandList, 31, PFN_SET_ROOT_DESCRIPTOR_TABLE, SetComputeRootDescriptorTable);
		if (original)
			original(commandList, rootParameterIndex, baseDescriptor);
		return;
	}

	if (DX12HotPathState::Get().skipBindings && !DX12CommandListCaptureShouldTrackBindings()) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_SET_ROOT_DESCRIPTOR_TABLE original =
			DX12_CL_ORIG(commandList, 31, PFN_SET_ROOT_DESCRIPTOR_TABLE, SetComputeRootDescriptorTable);
		if (original)
			original(commandList, rootParameterIndex, baseDescriptor);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::SetComputeRootDescriptorTable", commandList,
		" root=%u gpu=0x%llx", rootParameterIndex,
		static_cast<unsigned long long>(baseDescriptor.ptr));
	DX12CommandListCaptureComputeRootDescriptorTable(
		commandList, rootParameterIndex, baseDescriptor);
	DX12CommandListRuntimeBumpComputeBindingSerial(commandList);
	PFN_SET_ROOT_DESCRIPTOR_TABLE original =
		DX12_CL_ORIG(commandList, 31, PFN_SET_ROOT_DESCRIPTOR_TABLE, SetComputeRootDescriptorTable);
	if (original)
		original(commandList, rootParameterIndex, baseDescriptor);
}

static void STDMETHODCALLTYPE HookedSetGraphicsRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	if (DX12HotPathState::Get().skipAll) {
		PFN_SET_ROOT_DESCRIPTOR_TABLE original =
			DX12_CL_ORIG(commandList, 32, PFN_SET_ROOT_DESCRIPTOR_TABLE, SetGraphicsRootDescriptorTable);
		if (original)
			original(commandList, rootParameterIndex, baseDescriptor);
		return;
	}
	DX12_PROFILE_SCOPE(SetGraphicsRootDescriptorTable);

	if (DX12HotPathState::Get().skipGraphicsBindings && !DX12CommandListCaptureShouldTrackBindings()) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_SET_ROOT_DESCRIPTOR_TABLE original =
			DX12_CL_ORIG(commandList, 32, PFN_SET_ROOT_DESCRIPTOR_TABLE, SetGraphicsRootDescriptorTable);
		if (original)
			original(commandList, rootParameterIndex, baseDescriptor);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable", commandList,
		" root=%u gpu=0x%llx", rootParameterIndex,
		static_cast<unsigned long long>(baseDescriptor.ptr));
	DX12CommandListCaptureGraphicsRootDescriptorTable(
		commandList, rootParameterIndex, baseDescriptor);
	PFN_SET_ROOT_DESCRIPTOR_TABLE original =
		DX12_CL_ORIG(commandList, 32, PFN_SET_ROOT_DESCRIPTOR_TABLE, SetGraphicsRootDescriptorTable);
	if (original)
		original(commandList, rootParameterIndex, baseDescriptor);
}

static void STDMETHODCALLTYPE HookedSetComputeRoot32BitConstant(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT srcData, UINT destOffset)
{
	if (DX12HotPathState::Get().skipAll) {
		PFN_SET_ROOT_32BIT_CONSTANT original =
			DX12_CL_ORIG(commandList, 33, PFN_SET_ROOT_32BIT_CONSTANT, SetComputeRoot32BitConstant);
		if (original)
			original(commandList, rootParameterIndex, srcData, destOffset);
		return;
	}
	if (DX12HotPathState::Get().skipBindings) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_SET_ROOT_32BIT_CONSTANT original =
			DX12_CL_ORIG(commandList, 33, PFN_SET_ROOT_32BIT_CONSTANT, SetComputeRoot32BitConstant);
		if (original)
			original(commandList, rootParameterIndex, srcData, destOffset);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::SetComputeRoot32BitConstant", commandList,
		" root=%u destOffset=%u", rootParameterIndex, destOffset);
	DX12CommandListCaptureComputeRoot32BitConstant(
		commandList, rootParameterIndex, srcData, destOffset);
	DX12CommandListRuntimeBumpComputeBindingSerial(commandList);
	PFN_SET_ROOT_32BIT_CONSTANT original =
		DX12_CL_ORIG(commandList, 33, PFN_SET_ROOT_32BIT_CONSTANT, SetComputeRoot32BitConstant);
	if (original)
		original(commandList, rootParameterIndex, srcData, destOffset);
}

static void STDMETHODCALLTYPE HookedSetGraphicsRoot32BitConstant(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT srcData, UINT destOffset)
{
	if (DX12HotPathState::Get().skipAll) {
		PFN_SET_ROOT_32BIT_CONSTANT original =
			DX12_CL_ORIG(commandList, 34, PFN_SET_ROOT_32BIT_CONSTANT, SetGraphicsRoot32BitConstant);
		if (original)
			original(commandList, rootParameterIndex, srcData, destOffset);
		return;
	}
	if (DX12HotPathState::Get().skipGraphicsBindings) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_SET_ROOT_32BIT_CONSTANT original =
			DX12_CL_ORIG(commandList, 34, PFN_SET_ROOT_32BIT_CONSTANT, SetGraphicsRoot32BitConstant);
		if (original)
			original(commandList, rootParameterIndex, srcData, destOffset);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstant", commandList,
		" root=%u destOffset=%u", rootParameterIndex, destOffset);
	DX12CommandListCaptureGraphicsRoot32BitConstant(
		commandList, rootParameterIndex, srcData, destOffset);
	PFN_SET_ROOT_32BIT_CONSTANT original =
		DX12_CL_ORIG(commandList, 34, PFN_SET_ROOT_32BIT_CONSTANT, SetGraphicsRoot32BitConstant);
	if (original)
		original(commandList, rootParameterIndex, srcData, destOffset);
}

static void STDMETHODCALLTYPE HookedSetComputeRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT num32BitValuesToSet,
	const void *srcData, UINT destOffset)
{
	if (DX12HotPathState::Get().skipAll) {
		PFN_SET_ROOT_32BIT_CONSTANTS original =
			DX12_CL_ORIG(commandList, 35, PFN_SET_ROOT_32BIT_CONSTANTS, SetComputeRoot32BitConstants);
		if (original)
			original(commandList, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
		return;
	}
	if (DX12HotPathState::Get().skipBindings) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_SET_ROOT_32BIT_CONSTANTS original =
			DX12_CL_ORIG(commandList, 35, PFN_SET_ROOT_32BIT_CONSTANTS, SetComputeRoot32BitConstants);
		if (original)
			original(commandList, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::SetComputeRoot32BitConstants", commandList,
		" root=%u values=%u destOffset=%u", rootParameterIndex, num32BitValuesToSet, destOffset);
	DX12CommandListCaptureComputeRoot32BitConstants(
		commandList, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
	DX12CommandListRuntimeBumpComputeBindingSerial(commandList);
	PFN_SET_ROOT_32BIT_CONSTANTS original =
		DX12_CL_ORIG(commandList, 35, PFN_SET_ROOT_32BIT_CONSTANTS, SetComputeRoot32BitConstants);
	if (original)
		original(commandList, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
}

static void STDMETHODCALLTYPE HookedSetGraphicsRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT num32BitValuesToSet,
	const void *srcData, UINT destOffset)
{
	if (DX12HotPathState::Get().skipAll) {
		PFN_SET_ROOT_32BIT_CONSTANTS original =
			DX12_CL_ORIG(commandList, 36, PFN_SET_ROOT_32BIT_CONSTANTS, SetGraphicsRoot32BitConstants);
		if (original)
			original(commandList, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
		return;
	}
	if (DX12HotPathState::Get().skipGraphicsBindings) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_SET_ROOT_32BIT_CONSTANTS original =
			DX12_CL_ORIG(commandList, 36, PFN_SET_ROOT_32BIT_CONSTANTS, SetGraphicsRoot32BitConstants);
		if (original)
			original(commandList, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstants", commandList,
		" root=%u values=%u destOffset=%u", rootParameterIndex, num32BitValuesToSet, destOffset);
	DX12CommandListCaptureGraphicsRoot32BitConstants(
		commandList, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
	PFN_SET_ROOT_32BIT_CONSTANTS original =
		DX12_CL_ORIG(commandList, 36, PFN_SET_ROOT_32BIT_CONSTANTS, SetGraphicsRoot32BitConstants);
	if (original)
		original(commandList, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
}

static void STDMETHODCALLTYPE HookedSetComputeRootConstantBufferView(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	if (DX12HotPathState::Get().skipAll) {
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, 37, PFN_SET_ROOT_GPU_VA, SetComputeRootConstantBufferView);
		if (original)
			original(commandList, rootParameterIndex, address);
		return;
	}
	DX12_PROFILE_SCOPE(SetComputeRootConstantBufferView);

	if (DX12HotPathState::Get().skipBindings) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, 37, PFN_SET_ROOT_GPU_VA, SetComputeRootConstantBufferView);
		if (original)
			original(commandList, rootParameterIndex, address);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::SetComputeRootConstantBufferView", commandList,
		" root=%u gpu=0x%llx", rootParameterIndex, static_cast<unsigned long long>(address));
	DX12CommandListCaptureComputeRootDescriptor(
		commandList, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_CBV, address);
	DX12CommandListRuntimeBumpComputeBindingSerial(commandList);
	PFN_SET_ROOT_GPU_VA original =
		DX12_CL_ORIG(commandList, 37, PFN_SET_ROOT_GPU_VA, SetComputeRootConstantBufferView);
	if (original)
		original(commandList, rootParameterIndex, address);
}

static void STDMETHODCALLTYPE HookedSetGraphicsRootConstantBufferView(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	if (DX12HotPathState::Get().skipAll) {
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, 38, PFN_SET_ROOT_GPU_VA, SetGraphicsRootConstantBufferView);
		if (original)
			original(commandList, rootParameterIndex, address);
		return;
	}
	DX12_PROFILE_SCOPE(SetGraphicsRootConstantBufferView);

	if (DX12HotPathState::Get().skipGraphicsBindings) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, 38, PFN_SET_ROOT_GPU_VA, SetGraphicsRootConstantBufferView);
		if (original)
			original(commandList, rootParameterIndex, address);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::SetGraphicsRootConstantBufferView", commandList,
		" root=%u gpu=0x%llx", rootParameterIndex, static_cast<unsigned long long>(address));
	DX12CommandListCaptureGraphicsRootDescriptor(
		commandList, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_CBV, address);
	PFN_SET_ROOT_GPU_VA original =
		DX12_CL_ORIG(commandList, 38, PFN_SET_ROOT_GPU_VA, SetGraphicsRootConstantBufferView);
	if (original)
		original(commandList, rootParameterIndex, address);
}

static void STDMETHODCALLTYPE HookedSetComputeRootShaderResourceView(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	if (DX12HotPathState::Get().skipAll) {
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, 39, PFN_SET_ROOT_GPU_VA, SetComputeRootShaderResourceView);
		if (original)
			original(commandList, rootParameterIndex, address);
		return;
	}
	DX12_PROFILE_SCOPE(SetComputeRootShaderResourceView);

	if (DX12HotPathState::Get().skipBindings) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, 39, PFN_SET_ROOT_GPU_VA, SetComputeRootShaderResourceView);
		if (original)
			original(commandList, rootParameterIndex, address);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::SetComputeRootShaderResourceView", commandList,
		" root=%u gpu=0x%llx", rootParameterIndex, static_cast<unsigned long long>(address));
	DX12CommandListCaptureComputeRootDescriptor(
		commandList, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_SRV, address);
	DX12CommandListRuntimeBumpComputeBindingSerial(commandList);
	PFN_SET_ROOT_GPU_VA original =
		DX12_CL_ORIG(commandList, 39, PFN_SET_ROOT_GPU_VA, SetComputeRootShaderResourceView);
	if (original)
		original(commandList, rootParameterIndex, address);
}

static void STDMETHODCALLTYPE HookedSetGraphicsRootShaderResourceView(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	if (DX12HotPathState::Get().skipAll) {
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, 40, PFN_SET_ROOT_GPU_VA, SetGraphicsRootShaderResourceView);
		if (original)
			original(commandList, rootParameterIndex, address);
		return;
	}
	DX12_PROFILE_SCOPE(SetGraphicsRootShaderResourceView);

	if (DX12HotPathState::Get().skipGraphicsBindings) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, 40, PFN_SET_ROOT_GPU_VA, SetGraphicsRootShaderResourceView);
		if (original)
			original(commandList, rootParameterIndex, address);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::SetGraphicsRootShaderResourceView", commandList,
		" root=%u gpu=0x%llx", rootParameterIndex, static_cast<unsigned long long>(address));
	DX12CommandListCaptureGraphicsRootDescriptor(
		commandList, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_SRV, address);
	PFN_SET_ROOT_GPU_VA original =
		DX12_CL_ORIG(commandList, 40, PFN_SET_ROOT_GPU_VA, SetGraphicsRootShaderResourceView);
	if (original)
		original(commandList, rootParameterIndex, address);
}

static void STDMETHODCALLTYPE HookedSetComputeRootUnorderedAccessView(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	if (DX12HotPathState::Get().skipAll) {
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, 41, PFN_SET_ROOT_GPU_VA, SetComputeRootUnorderedAccessView);
		if (original)
			original(commandList, rootParameterIndex, address);
		return;
	}
	DX12_PROFILE_SCOPE(SetComputeRootUnorderedAccessView);

	if (DX12HotPathState::Get().skipBindings) {
		DX12_PROFILE_FAST_FORWARD();
		PFN_SET_ROOT_GPU_VA original =
			DX12_CL_ORIG(commandList, 41, PFN_SET_ROOT_GPU_VA, SetComputeRootUnorderedAccessView);
		if (original)
			original(commandList, rootParameterIndex, address);
		return;
	}

	LogDX12Call("ID3D12GraphicsCommandList::SetComputeRootUnorderedAccessView", commandList,
		" root=%u gpu=0x%llx", rootParameterIndex, static_cast<unsigned long long>(address));
	DX12CommandListCaptureComputeRootDescriptor(
		commandList, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_UAV, address);
	DX12CommandListRuntimeBumpComputeBindingSerial(commandList);
	PFN_SET_ROOT_GPU_VA original =
		DX12_CL_ORIG(commandList, 41, PFN_SET_ROOT_GPU_VA, SetComputeRootUnorderedAccessView);
	if (original)
		original(commandList, rootParameterIndex, address);
}

