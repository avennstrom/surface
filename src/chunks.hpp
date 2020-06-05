#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.hpp>

#include <glm/vec3.hpp>

#include <vector>
#include <memory>

class World;

/*struct Chunk
{
	size_t vertexCount;
	DirectX::XMFLOAT3* vertices;
	DirectX::XMFLOAT3* normals;
};*/

struct VisualChunk
{
	VkBuffer vertexBuffer = VK_NULL_HANDLE;
	VkBuffer normalBuffer = VK_NULL_HANDLE;

	VmaAllocation vertexBufferAlloc = VK_NULL_HANDLE;
	VmaAllocation normalBufferAlloc = VK_NULL_HANDLE;

	uint32_t vertexCount = 0;
};

struct ChunkHandle
{
	uint32_t id;
};

struct ChunkIndex
{
	uint32_t id;
	uint16_t index;
	uint16_t next;
};

class Chunks
{
public:
	Chunks(World& world);

	bool has(ChunkHandle handle) const;
	uint32_t lookup(ChunkHandle handle) const;
	ChunkHandle reverseLookup(uint32_t index) const;

	ChunkHandle add();
	void remove(ChunkHandle handle);

	std::unique_ptr<glm::i32vec3[]> positions;
	//std::unique_ptr<DirectX::BoundingBox[]> boundingBoxes;
	std::unique_ptr<VisualChunk[]> visuals;

	__forceinline size_t count() const { return m_count; }

private:
	void _move(size_t dst, size_t src);

	size_t m_count;
	size_t m_capacity;

	std::unique_ptr<ChunkIndex[]> m_indices;
	std::unique_ptr<uint32_t[]> m_chunkIDs;
	uint16_t m_freelistEnqueue;
	uint16_t m_freelistDequeue;

private:
	World& m_world;
};
