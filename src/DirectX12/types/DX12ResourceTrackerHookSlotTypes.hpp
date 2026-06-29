#pragma once

#include <Windows.h>

class DX12ResourceTrackerHookSlotTypes
{
public:
	class ID3D12Device
	{
	public:
		static constexpr UINT CreateDescriptorHeap = 14;
		static constexpr UINT CreateRootSignature = 16;
		static constexpr UINT CreateConstantBufferView = 17;
		static constexpr UINT CreateShaderResourceView = 18;
		static constexpr UINT CreateUnorderedAccessView = 19;
		static constexpr UINT CreateRenderTargetView = 20;
		static constexpr UINT CreateDepthStencilView = 21;
		static constexpr UINT CreateSampler = 22;
		static constexpr UINT CopyDescriptors = 23;
		static constexpr UINT CopyDescriptorsSimple = 24;
		static constexpr UINT CreateCommittedResource = 27;
		static constexpr UINT CreatePlacedResource = 29;
		static constexpr UINT CreateReservedResource = 30;
	};

	class ID3D12Device4
	{
	public:
		static constexpr UINT CreateCommittedResource1 = 53;
		static constexpr UINT CreateReservedResource1 = 55;
	};

	class ID3D12Device8
	{
	public:
		static constexpr UINT CreateCommittedResource2 = 69;
		static constexpr UINT CreatePlacedResource1 = 70;
	};

	class ID3D12Device10
	{
	public:
		static constexpr UINT CreateCommittedResource3 = 76;
		static constexpr UINT CreatePlacedResource2 = 77;
		static constexpr UINT CreateReservedResource2 = 78;
	};
};
