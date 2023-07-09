#pragma once

#include <stdint.h>

class DynamicBufferAllocator
{
public:
	DynamicBufferAllocator() = delete;
	DynamicBufferAllocator(const DynamicBufferAllocator&) = delete;
	DynamicBufferAllocator(uint32_t capacityPerFrame, uint32_t frameCount);
private:
};