#pragma once

#include <camera.hpp>

#include <vulkan/vulkan.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cinttypes>
#include <vector>

class DebugRenderer
{
public:
	explicit DebugRenderer(VkDescriptorPool descriptorPool);

	void updateBuffers(VkCommandBuffer cb, const Camera& camera);

	void draw(VkCommandBuffer cb);

	void drawLine2D(const glm::vec2& from, const glm::vec2& to, uint32_t color);
	void drawLine3D(const glm::vec3& from, const glm::vec3& to, uint32_t color);
	void drawTriangle2D(const glm::vec2& v0, const glm::vec2& v1, const glm::vec2& v2, uint32_t color);
	void drawTriangle3D(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, uint32_t color);
	void drawRectangle2D(const glm::vec2& min, const glm::vec2& max, uint32_t color);

private:
	void _createDebugUniformBuffer();
	void _createDebugDescriptorSetLayout();
	void _createDebugDescriptorSet(VkDescriptorPool descriptorPool);
	void _createDebugPipeline();
	void _createDebugStagingBuffers();
	void _createDebugVertexBuffer();

	struct DebugVertex
	{
		glm::vec3 pos;
		uint32_t color;
	};

	enum DebugVertexBufferType
	{
		DBG_VERTEX_BUFFER_LINES_2D,
		DBG_VERTEX_BUFFER_LINES_3D,
		DBG_VERTEX_BUFFER_TRIS_2D,
		DBG_VERTEX_BUFFER_TRIS_3D,
		DBG_VERTEX_BUFFER_COUNT,
	};

	enum DebugUniformBuffer
	{
		DBG_UNIFORM_BUFFER_2D,
		DBG_UNIFORM_BUFFER_3D,
		DBG_UNIFORM_BUFFER_COUNT,
	};

	uint32_t m_frameIndex;

	std::vector<DebugVertex> m_debugVertices[DBG_VERTEX_BUFFER_COUNT];
	uint32_t m_debugVertexOffset[DBG_VERTEX_BUFFER_COUNT];
	uint32_t m_debugVertexCount[DBG_VERTEX_BUFFER_COUNT];
	VkPipeline m_debugPipeline[DBG_VERTEX_BUFFER_COUNT];

	VkBuffer m_debugUniformBuffer[DBG_UNIFORM_BUFFER_COUNT];
	VkDeviceMemory m_debugUniformBufferMemory[DBG_UNIFORM_BUFFER_COUNT];
	VkDescriptorSet m_debugDescriptorSet[DBG_UNIFORM_BUFFER_COUNT];

	VkDeviceSize m_debugVertexBufferSize;
	VkBuffer m_debugStagingBuffer[5];
	VkDeviceMemory m_debugStagingBufferMemory[5];
	VkBuffer m_debugVertexBuffer;
	VkDeviceMemory m_debugVertexBufferMemory;
	VkDescriptorSetLayout m_debugDescriptorSetLayout;
	VkPipelineLayout m_debugPipelineLayout;
};