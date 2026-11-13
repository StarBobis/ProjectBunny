#pragma once

#include <d3d11.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>

class HackerInputLayout;

// Weak entries avoid retaining a device through its child input layouts.
class HackerInputLayoutCache : public std::enable_shared_from_this<HackerInputLayoutCache>
{
public:
	HRESULT Create(ID3D11Device* device, const D3D11_INPUT_ELEMENT_DESC* elements, UINT count,
		const void* signature, SIZE_T size, ID3D11InputLayout** result, bool use_cache = true);
private:
	friend class HackerInputLayout;
	std::mutex mMutex;
	std::unordered_multimap<uint64_t, HackerInputLayout*> mLayouts;
};

uint64_t CalculateInputLayoutHash(const D3D11_INPUT_ELEMENT_DESC* elements, UINT count,
	const void* signature, SIZE_T size);

class __declspec(uuid("82BB64D9-44D0-4F6B-903C-C1477343AE6D")) HackerInputLayout final : public ID3D11InputLayout
{
public:
	HackerInputLayout(ID3D11InputLayout* orig, const D3D11_INPUT_ELEMENT_DESC* pElements, UINT numElements,
		const void* shaderSignature = nullptr, SIZE_T signatureSize = 0, uint64_t hash = 0);
	~HackerInputLayout();

	ID3D11InputLayout* GetOrigInputLayout() const;

	UINT GetElementCount() const;
	const D3D11_INPUT_ELEMENT_DESC* GetElements() const;
	uint64_t GetLayoutHash() const;
	const void* GetShaderSignature() const;
	SIZE_T GetShaderSignatureSize() const;
	bool Matches(const D3D11_INPUT_ELEMENT_DESC* elements, UINT count, const void* signature, SIZE_T size) const;

#pragma region IUnknown
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
	ULONG STDMETHODCALLTYPE AddRef() override;
	ULONG STDMETHODCALLTYPE Release() override;
#pragma endregion

#pragma region ID3D11DeviceChild
	void STDMETHODCALLTYPE GetDevice(ID3D11Device** ppDevice) override;
	HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT* pDataSize, void* pData) override;
	HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void* pData) override;
	HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown* pData) override;
#pragma endregion

private:
	friend class HackerInputLayoutCache;

private:
	std::atomic<ULONG> mRefCount = 1;
	ID3D11InputLayout* mOrigLayout = nullptr;
	uint64_t mLayoutHash = 0;
	std::vector<uint8_t> mShaderSignature;
	std::shared_ptr<HackerInputLayoutCache> mCache;

	std::vector<D3D11_INPUT_ELEMENT_DESC> mElements;
	std::vector<std::string> mSemanticNames;
};
