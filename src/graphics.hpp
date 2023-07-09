#pragma once

#include <string>

#include <vulkan/vulkan_core.h>

namespace graphics
{

extern VkExtent2D resolution;

extern VkDevice device;
extern VkInstance instance;
extern VkPhysicalDevice physicalDevice;

void init(void* hwnd);

void beginFrame();
void endFrame();

uint32_t currentFrameId();
VkCommandBuffer currentCommandBuffer();

uint32_t currentSwapChainImageIndex();
VkImageView swapChainImageView(int index);
VkImage currentSwapChainImage();
VkImageView currentSwapChainImageView();

uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

void allocateAndBindDedicatedImageMemory(VkImage image, VkDeviceMemory* memory);

void setObjectDebugName(VkImage image, std::string name);
void setObjectDebugName(VkImageView imageView, std::string name);
void setObjectDebugName(VkBuffer buffer, std::string name);
void setObjectDebugName(VkPipeline pipeline, std::string name);
void setObjectDebugName(VkPipelineLayout pipelineLayout, std::string name);
void setObjectDebugName(VkDescriptorSet descriptorSet, std::string name);
void setObjectDebugName(VkDescriptorSetLayout descriptorSetLayout, std::string name);

}