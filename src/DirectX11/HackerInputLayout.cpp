#include <algorithm>
#include <cctype>
#include <new>
#include "util.h"

#include "HackerInputLayout.h"

HackerInputLayout::HackerInputLayout(ID3D11InputLayout* orig, const D3D11_INPUT_ELEMENT_DESC* pElements, UINT numElements,
	const void* shaderSignature, SIZE_T signatureSize, uint64_t hash) : mOrigLayout(orig)
{
	mElements.resize(numElements);
	mSemanticNames.resize(numElements);

	for (UINT i = 0; i < numElements; ++i)
	{
		mElements[i] = pElements[i];

		if (pElements[i].SemanticName)
		{
			mSemanticNames[i] = pElements[i].SemanticName;
			std::transform(mSemanticNames[i].begin(), mSemanticNames[i].end(), mSemanticNames[i].begin(),
				[](unsigned char c) { return static_cast<char>(std::toupper(c)); });
			mElements[i].SemanticName = mSemanticNames[i].c_str();
		}
		else
		{
			mElements[i].SemanticName = nullptr;
		}
	}

	if (signatureSize) {
		const auto* bytes = static_cast<const uint8_t*>(shaderSignature);
		mShaderSignature.assign(bytes, bytes + signatureSize);
	}
	mLayoutHash = hash ? hash : CalculateInputLayoutHash(pElements, numElements, shaderSignature, signatureSize);
}

HackerInputLayout::~HackerInputLayout()
{
}

ID3D11InputLayout* HackerInputLayout::GetOrigInputLayout() const
{
	return mOrigLayout;
}

UINT HackerInputLayout::GetElementCount() const
{
	return static_cast<UINT>(mElements.size());
}

const D3D11_INPUT_ELEMENT_DESC* HackerInputLayout::GetElements() const
{
	return mElements.data();
}

uint64_t HackerInputLayout::GetLayoutHash() const
{
	return mLayoutHash;
}

const void* HackerInputLayout::GetShaderSignature() const
{
	return mShaderSignature.empty() ? nullptr : mShaderSignature.data();
}

SIZE_T HackerInputLayout::GetShaderSignatureSize() const
{
	return mShaderSignature.size();
}

uint64_t CalculateInputLayoutHash(const D3D11_INPUT_ELEMENT_DESC* elements, UINT count,
	const void* signature, SIZE_T size)
{
	uint32_t hash = crc32c_hw(0, &count, sizeof(count));
	for (UINT i = 0; i < count; ++i) {
		const auto& element = elements[i];
		if (element.SemanticName) {
			for (const char* p = element.SemanticName; *p; ++p) {
				const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
				hash = crc32c_hw(hash, &c, sizeof(c));
			}
		}
		hash = crc32c_hw(hash, &element.SemanticIndex, sizeof(element.SemanticIndex));
		hash = crc32c_hw(hash, &element.Format, sizeof(element.Format));
		hash = crc32c_hw(hash, &element.InputSlot, sizeof(element.InputSlot));
		hash = crc32c_hw(hash, &element.AlignedByteOffset, sizeof(element.AlignedByteOffset));
		hash = crc32c_hw(hash, &element.InputSlotClass, sizeof(element.InputSlotClass));
		hash = crc32c_hw(hash, &element.InstanceDataStepRate, sizeof(element.InstanceDataStepRate));
	}
	const uint32_t signatureHash = signature && size ? crc32c_hw(0, signature, size) : 0;
	return (static_cast<uint64_t>(signatureHash) << 32) | hash;
}

bool HackerInputLayout::Matches(const D3D11_INPUT_ELEMENT_DESC* elements, UINT count,
	const void* signature, SIZE_T size) const
{
	if (count != mElements.size() || size != mShaderSignature.size() ||
		(size && std::memcmp(signature, mShaderSignature.data(), size)))
		return false;
	for (UINT i = 0; i < count; ++i) {
		const auto& a = elements[i];
		const auto& b = mElements[i];
		if ((a.SemanticName == nullptr) != (b.SemanticName == nullptr) ||
			(a.SemanticName && _stricmp(a.SemanticName, b.SemanticName)) ||
			a.SemanticIndex != b.SemanticIndex || a.Format != b.Format || a.InputSlot != b.InputSlot ||
			a.AlignedByteOffset != b.AlignedByteOffset || a.InputSlotClass != b.InputSlotClass ||
			a.InstanceDataStepRate != b.InstanceDataStepRate)
			return false;
	}
	return true;
}

HRESULT HackerInputLayoutCache::Create(ID3D11Device* device, const D3D11_INPUT_ELEMENT_DESC* elements,
	UINT count, const void* signature, SIZE_T size, ID3D11InputLayout** result, bool use_cache)
{
	// Preserve D3D11 validation-only calls and let the runtime validate malformed inputs.
	if (!result || count > D3D11_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT ||
		(!elements && count) || (!signature && size))
		return device->CreateInputLayout(elements, count, signature, size, result);
	*result = nullptr;
	const uint64_t hash = CalculateInputLayoutHash(elements, count, signature, size);
	std::unique_lock<std::mutex> lock(mMutex, std::defer_lock);
	if (use_cache) {
		lock.lock();
		const auto range = mLayouts.equal_range(hash);
		for (auto it = range.first; it != range.second; ++it) {
			if (it->second->Matches(elements, count, signature, size)) {
				it->second->AddRef();
				*result = it->second;
				return S_OK;
			}
		}
	}
	ID3D11InputLayout* original = nullptr;
	const HRESULT hr = device->CreateInputLayout(elements, count, signature, size, &original);
	if (FAILED(hr) || !original)
		return hr;
	HackerInputLayout* layout = nullptr;
	try {
		layout = new HackerInputLayout(original, elements, count, signature, size, hash);
		if (use_cache) {
			mLayouts.emplace(hash, layout);
			layout->mCache = shared_from_this();
		}
	} catch (const std::bad_alloc&) {
		if (layout) delete layout;
		original->Release();
		return E_OUTOFMEMORY;
	}
	*result = layout;
	return hr;
}

#pragma region IUnknown
HRESULT STDMETHODCALLTYPE HackerInputLayout::QueryInterface(REFIID riid, void** ppvObject)
{
	if (!ppvObject)
		return E_POINTER;
	*ppvObject = nullptr;
	if (riid == __uuidof(HackerInputLayout) || riid == __uuidof(ID3D11InputLayout) ||
		riid == __uuidof(ID3D11DeviceChild) ||
		riid == __uuidof(IUnknown))
	{
		*ppvObject = this;
		AddRef();
		return S_OK;
	}

	return mOrigLayout->QueryInterface(riid, ppvObject);
}

ULONG STDMETHODCALLTYPE HackerInputLayout::AddRef()
{
	return ++mRefCount;
}

ULONG STDMETHODCALLTYPE HackerInputLayout::Release()
{
	// Serialize final release with cache lookup, so a weak entry cannot resurrect us.
	const auto cache = mCache;
	std::unique_lock<std::mutex> lock;
	if (cache)
		lock = std::unique_lock<std::mutex>(cache->mMutex);
	const ULONG ref = --mRefCount;
	if (ref == 0) {
		if (cache) {
			const auto range = cache->mLayouts.equal_range(mLayoutHash);
			for (auto it = range.first; it != range.second; ++it) {
				if (it->second == this) { cache->mLayouts.erase(it); break; }
			}
			lock.unlock();
		}
		mOrigLayout->Release();
		delete this;
	}
	return ref;
}
#pragma endregion IUnknown

#pragma region ID3D11DeviceChild
void STDMETHODCALLTYPE HackerInputLayout::GetDevice(ID3D11Device** ppDevice)
{
	mOrigLayout->GetDevice(ppDevice);
}

HRESULT STDMETHODCALLTYPE HackerInputLayout::GetPrivateData(REFGUID guid, UINT* pDataSize, void* pData)
{
	return mOrigLayout->GetPrivateData(guid, pDataSize, pData);
}

HRESULT STDMETHODCALLTYPE HackerInputLayout::SetPrivateData(REFGUID guid, UINT DataSize, const void* pData)
{
	return mOrigLayout->SetPrivateData(guid, DataSize, pData);
}

HRESULT STDMETHODCALLTYPE HackerInputLayout::SetPrivateDataInterface(REFGUID guid, const IUnknown* pData)
{
	return mOrigLayout->SetPrivateDataInterface(guid, pData);
}
#pragma endregion ID3D11DeviceChild
