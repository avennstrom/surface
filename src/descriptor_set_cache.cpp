#include "descriptor_set_cache.hpp"
#include "graphics.hpp"

#include <assert.h>
#include <stdexcept>

VkDescriptorSet allocateDescriptorSet(std::string name, DescriptorSetCache& cache, VkDescriptorPool pool, VkDescriptorSetLayout layout, uint32_t frameId)
{
	for(DescriptorSetCache::Item& item : cache.items) {
		if(item.layout != layout) {
			continue;
		}
		assert(item.lastUsedFrameId <= frameId);
		if((item.lastUsedFrameId + 3u) > frameId) {
			continue;
		}

		item.lastUsedFrameId = frameId;
		return item.set;
	}

	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = pool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &layout;

	VkDescriptorSet set;
	if (vkAllocateDescriptorSets(graphics::device, &allocInfo, &set) != VK_SUCCESS) {
		throw new std::runtime_error("failed to allocate descriptor sets!");
	}

	graphics::setObjectDebugName(set, name);

	cache.items.push_back(DescriptorSetCache::Item{
		.lastUsedFrameId = frameId,
		.layout = layout,
		.set = set,
	});

	return set;
}