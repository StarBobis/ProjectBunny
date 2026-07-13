#pragma once

#include "DX12IaCommandSignatures.hpp"

class DX12IaReplacementExecutorCallbacks
{
public:
	DX12IaCommandSignatures::DrawInstanced drawInstanced = nullptr;
	DX12IaCommandSignatures::DrawIndexedInstanced drawIndexedInstanced = nullptr;
	DX12IaCommandSignatures::Dispatch dispatch = nullptr;
	DX12IaCommandSignatures::SetIndexBuffer setIndexBuffer = nullptr;
	DX12IaCommandSignatures::SetVertexBuffers setVertexBuffers = nullptr;
	void (*recordReplacementDraw)(bool indexed) = nullptr;
	void (*recordReplacementDispatch)() = nullptr;
};
