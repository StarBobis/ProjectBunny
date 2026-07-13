#pragma once

class DX12ThreadLocalCommandListOriginal
{
public:
	void *target = nullptr;
	void *original = nullptr;
	bool resolved = false;
};
