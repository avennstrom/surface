#pragma once

#include <unordered_map>

#include <vulkan/vulkan_core.h>

struct DescriptorSetCache
{
	struct Item
	{
		uint32_t lastUsedFrameId;
		VkDescriptorSetLayout layout;
		VkDescriptorSet set;
	};

	std::vector<Item> items;
};

VkDescriptorSet allocateDescriptorSet(std::string name, DescriptorSetCache& cache, VkDescriptorPool pool, VkDescriptorSetLayout layout, uint32_t frameId);