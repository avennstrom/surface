#pragma once

#include <vulkan/vulkan.h>

namespace graphics
{

extern VkExtent2D resolution;

extern VkDevice device;
extern VkInstance instance;
extern VkPhysicalDevice physicalDevice;

extern VkRenderPass colorPass;
extern VkRenderPass resolvePass;

extern VkImage mainColorImage;
extern VkImageView mainColorImageView;
extern VkFramebuffer colorPassFramebuffer;

void init(void* hwnd);

void beginFrame();
void endFrame();

uint32_t currentFrameIndex();
VkCommandBuffer currentCommandBuffer();

VkImageView swapChainImageView(int index);
VkImage currentSwapChainImage();
VkFramebuffer currentSwapChainFramebuffer();

uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

}