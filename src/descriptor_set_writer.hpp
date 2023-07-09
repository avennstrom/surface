#pragma once

#include <vector>
#include <cassert>

#include <vulkan/vulkan_core.h>

class DescriptorWriter
{
public:
	DescriptorWriter() = delete;
	DescriptorWriter(const DescriptorWriter&) = delete;

	DescriptorWriter(VkDevice device, size_t capacity)
		: m_device(device)
	{
		assert(device != VK_NULL_HANDLE);
		m_writes.reserve(capacity);
		m_imageInfos.reserve(capacity);
		m_bufferInfos.reserve(capacity);
	}

	~DescriptorWriter()
	{
		flush();
	}

	void bindUniformBuffer(VkDescriptorSet dstSet, uint32_t dstBinding, VkDescriptorBufferInfo buffer)
	{
		assert(m_writes.size() < m_writes.capacity());
		assert(m_bufferInfos.size() < m_bufferInfos.capacity());

		m_bufferInfos.push_back(buffer);

		m_writes.push_back(VkWriteDescriptorSet{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = dstSet,
			.dstBinding = dstBinding,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &m_bufferInfos.back(),
		});
	}

	void bindUniformBuffer(VkDescriptorSet dstSet, uint32_t dstBinding, VkBuffer buffer, VkDeviceSize bufferOffset, VkDeviceSize bufferRange)
	{
		bindUniformBuffer(dstSet, dstBinding, VkDescriptorBufferInfo{buffer, bufferOffset, bufferRange});
	}

	void bindStorageBuffer(VkDescriptorSet dstSet, uint32_t dstBinding, VkBuffer buffer, VkDeviceSize bufferOffset, VkDeviceSize bufferRange)
	{
		assert(m_writes.size() < m_writes.capacity());
		assert(m_bufferInfos.size() < m_bufferInfos.capacity());

		m_bufferInfos.push_back(VkDescriptorBufferInfo{
			.buffer = buffer,
			.offset = bufferOffset,
			.range = bufferRange,
		});

		m_writes.push_back(VkWriteDescriptorSet{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = dstSet,
			.dstBinding = dstBinding,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo = &m_bufferInfos.back(),
		});
	}

	void bindCombinedImageSampler(VkDescriptorSet dstSet, uint32_t dstBinding, VkSampler sampler, VkImageView image)
	{
		assert(m_writes.size() < m_writes.capacity());
		assert(m_imageInfos.size() < m_imageInfos.capacity());

		m_imageInfos.push_back(VkDescriptorImageInfo{
			.sampler = sampler,
			.imageView = image,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		});

		m_writes.push_back(VkWriteDescriptorSet{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = dstSet,
			.dstBinding = dstBinding,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &m_imageInfos.back(),
		});
	}

	void bindStorageImage(VkDescriptorSet dstSet, uint32_t dstBinding, VkImageView image)
	{
		assert(m_writes.size() < m_writes.capacity());
		assert(m_imageInfos.size() < m_imageInfos.capacity());

		m_imageInfos.push_back(VkDescriptorImageInfo{
			.imageView = image,
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
		});

		m_writes.push_back(VkWriteDescriptorSet{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = dstSet,
			.dstBinding = dstBinding,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.pImageInfo = &m_imageInfos.back(),
		});
	}

	void flush()
	{
		if(!m_writes.empty()) {
			vkUpdateDescriptorSets(m_device, (uint32_t)m_writes.size(), m_writes.data(), 0, nullptr);
		}

		m_writes.clear();
		m_imageInfos.clear();
	}

private:
	VkDevice							m_device;
	std::vector<VkWriteDescriptorSet>	m_writes;
	std::vector<VkDescriptorImageInfo>	m_imageInfos;
	std::vector<VkDescriptorBufferInfo>	m_bufferInfos;
};