#pragma once

// One-shot startup probe for the "descriptor direct read" premise.
//
// The descriptor shadow keeps a 264-byte DescriptorRecord per slot while the
// D3D12 descriptor it mirrors is 32 bytes, and the copy path measured 0.22-0.28
// microseconds per descriptor against 0.044 for a creation. The design that
// removes that traffic stores only a small identity per slot and recovers the
// view description by reading the 32 bytes at the CPU handle - descriptor heaps
// are CPU memory, and the game's own CopyDescriptors wrote those bytes into the
// destination heap, so they are authoritative there.
//
// The descriptor layout is deliberately undocumented, so this probe does not
// assume one: it creates two descriptors with distinctive known values in a
// scratch non-shader-visible heap, reads the bytes back, and reports whether -
// and at which offsets - the known values appear. Nothing depends on the result
// and nothing is drawn; the recorded pointer is never dereferenced.
//
// It runs on the raw (unwrapped) device, so it is invisible to the shadow.
inline void DX12ProbeDescriptorDirectRead(ID3D12Device *device)
{
	static std::atomic<bool> probed{false};
	if (!device || probed.exchange(true))
		return;

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.NumDescriptors = 2;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	ID3D12DescriptorHeap *heap = nullptr;
	if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap))) || !heap)
		return;

	const UINT increment = device->GetDescriptorHandleIncrementSize(
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	const SIZE_T base = heap->GetCPUDescriptorHandleForHeapStart().ptr;

	const UINT64 knownVa = 0x0000123456789A00ull;
	const UINT knownSize = 0x000001C0u;
	const UINT knownStride = 0x00000020u;

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbv = {};
	cbv.BufferLocation = knownVa;
	cbv.SizeInBytes = knownSize;
	D3D12_CPU_DESCRIPTOR_HANDLE cbvHandle = {};
	cbvHandle.ptr = base;
	device->CreateConstantBufferView(&cbv, cbvHandle);

	D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
	srv.Format = DXGI_FORMAT_R32G32B32_FLOAT;
	srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Buffer.FirstElement = 0;
	srv.Buffer.NumElements = 64;
	srv.Buffer.StructureByteStride = knownStride;
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = {};
	srvHandle.ptr = base + increment;
	device->CreateShaderResourceView(nullptr, &srv, srvHandle);

	const auto findU64 = [](const unsigned char *bytes, SIZE_T length, UINT64 value) -> int {
		for (SIZE_T offset = 0; offset + sizeof(UINT64) <= length; ++offset) {
			UINT64 candidate = 0;
			memcpy(&candidate, bytes + offset, sizeof(candidate));
			if (candidate == value)
				return static_cast<int>(offset);
		}
		return -1;
	};
	const auto findU32 = [](const unsigned char *bytes, SIZE_T length, UINT value) -> int {
		for (SIZE_T offset = 0; offset + sizeof(UINT) <= length; ++offset) {
			UINT candidate = 0;
			memcpy(&candidate, bytes + offset, sizeof(candidate));
			if (candidate == value)
				return static_cast<int>(offset);
		}
		return -1;
	};

	const unsigned char *cbvBytes = reinterpret_cast<const unsigned char *>(base);
	const unsigned char *srvBytes = reinterpret_cast<const unsigned char *>(base + increment);
	char cbvHex[3 * 32 + 1] = {};
	char srvHex[3 * 32 + 1] = {};
	for (SIZE_T i = 0; i < 32; ++i)
	{
		snprintf(cbvHex + i * 3, 4, "%02x ", cbvBytes[i]);
		snprintf(srvHex + i * 3, 4, "%02x ", srvBytes[i]);
	}

	DX12_LOG("DX12DescriptorDirectRead",
		"cbv_va_offset=%d,cbv_size_offset=%d,cbv_bytes=\"%s\","
		"srv_va_offset=%d,srv_stride_offset=%d,srv_bytes=\"%s\"",
		findU64(cbvBytes, 32, knownVa),
		findU32(cbvBytes, 32, knownSize),
		cbvHex,
		findU64(srvBytes, 32, knownVa),
		findU32(srvBytes, 32, knownStride),
		srvHex);

	heap->Release();
}
