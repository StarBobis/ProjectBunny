#pragma once

#include <Windows.h>

class DX12CommandListHooks
{
public:
	static void HookCommandListCreation(IUnknown *device);
	static void HookCommandList(IUnknown *commandList);
	static void HookCommandQueue(IUnknown *commandQueue);
	static void RememberCommandQueueForHotPath(IUnknown *commandQueue);
	static void ActivateCommandQueueHotPathHooks();
};
