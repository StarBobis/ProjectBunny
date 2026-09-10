// DX12WicTextureDecodeUtils.h: WIC-based decode of non-DDS custom resource
// images (PNG/JPG/BMP/...). DX11 loads these through DirectXTK's
// CreateWICTextureFromFileEx; the DX12 runtime only understood DDS, which
// silently zeroed every UI panel texture (bad_magic on the PNG bytes). This
// decoder is self-contained: it returns straight-alpha RGBA32 pixels plus
// dimensions, and the caller shapes them into whatever upload layout it
// needs (matching the DX11 WIC loader's 32bpp RGBA default).
#pragma once

#include <wincodec.h>

#include <string>
#include <vector>

class DX12WicTextureDecodeUtils
{
public:
	// Decodes an image file's first frame into 32bpp RGBA (straight alpha).
	// On success returns true with `width`/`height` set and `rgbaBytes`
	// holding width*4*height tightly packed pixels.
	static bool DecodeToRgba32(
		const std::vector<unsigned char> &fileBytes,
		std::vector<unsigned char> *rgbaBytes,
		unsigned *width,
		unsigned *height,
		std::string *reason)
	{
		if (reason)
			reason->clear();
		if (!rgbaBytes || !width || !height) {
			if (reason)
				*reason = "missing_output";
			return false;
		}
		rgbaBytes->clear();
		if (fileBytes.size() < 8) {
			if (reason)
				*reason = "too_small";
			return false;
		}

		// WIC needs COM; the game may not have initialised it on the loading
		// thread. Repeated CoInitializeEx is safe (S_FALSE when already up).
		CoInitializeEx(nullptr, COINIT_MULTITHREADED);

		IWICImagingFactory *factory = nullptr;
		HRESULT hr = CoCreateInstance(
			CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&factory));
		if (FAILED(hr) || !factory) {
			if (reason)
				*reason = "wic_factory_unavailable";
			return false;
		}

		IWICStream *stream = nullptr;
		IWICBitmapDecoder *decoder = nullptr;
		IWICBitmapFrameDecode *frame = nullptr;
		IWICFormatConverter *converter = nullptr;
		bool ok = false;
		std::string failReason;
		UINT decodedWidth = 0;
		UINT decodedHeight = 0;

		hr = factory->CreateStream(&stream);
		if (SUCCEEDED(hr) && stream) {
			hr = stream->InitializeFromMemory(
				const_cast<unsigned char*>(fileBytes.data()),
				static_cast<DWORD>(fileBytes.size()));
		}
		if (SUCCEEDED(hr)) {
			hr = factory->CreateDecoderFromStream(
				stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
		}
		if (FAILED(hr) || !decoder) {
			failReason = "wic_no_decoder";
		} else if (FAILED(decoder->GetFrame(0, &frame)) || !frame) {
			failReason = "wic_no_frame";
		} else {
			frame->GetSize(&decodedWidth, &decodedHeight);
			if (!decodedWidth || !decodedHeight ||
			    decodedWidth > 16384 || decodedHeight > 16384) {
				failReason = "wic_bad_size";
			} else if (
				FAILED(factory->CreateFormatConverter(&converter)) ||
				!converter ||
				FAILED(converter->Initialize(
					frame, GUID_WICPixelFormat32bppRGBA,
					WICBitmapDitherTypeNone, nullptr, 0.0,
					WICBitmapPaletteTypeMedianCut))) {
				failReason = "wic_convert_failed";
			} else {
				const UINT64 rowBytes = static_cast<UINT64>(decodedWidth) * 4;
				const UINT64 totalBytes = rowBytes * decodedHeight;
				rgbaBytes->resize(static_cast<size_t>(totalBytes));
				if (SUCCEEDED(converter->CopyPixels(
					    nullptr, static_cast<UINT>(rowBytes),
					    static_cast<UINT>(totalBytes), rgbaBytes->data()))) {
					ok = true;
				} else {
					failReason = "wic_copy_failed";
				}
			}
		}

		if (converter)
			converter->Release();
		if (frame)
			frame->Release();
		if (decoder)
			decoder->Release();
		if (stream)
			stream->Release();
		factory->Release();

		if (!ok) {
			rgbaBytes->clear();
			if (reason)
				*reason = failReason.empty() ? "wic_decode_failed" : failReason;
			return false;
		}
		*width = decodedWidth;
		*height = decodedHeight;
		return true;
	}
};
