#include "chunks.hpp"

#include <world.hpp>

#define MAX_CHUNKS 64 * 1024
#define INDEX_MASK 0xffff
#define NEW_CHUNK_ID_ADD 0x10000

Chunks::Chunks(World& world)
	: m_world(world)
	, m_capacity(MAX_CHUNKS)
	, m_count(0)
{
	m_indices.reset(new ChunkIndex[m_capacity]);
	for (size_t i = 0; i < m_capacity; ++i) {
		m_indices[i].id = static_cast<uint32_t>(i);
		m_indices[i].next = static_cast<uint16_t>(i + 1);
	}

	m_freelistDequeue = 0;
	m_freelistEnqueue = static_cast<uint16_t>(m_capacity - 1);

	m_chunkIDs.reset(new uint32_t[m_capacity]);

	positions.reset(new glm::i32vec3[m_capacity]);
	//boundingBoxes.reset(new DirectX::BoundingBox[m_capacity]);
	visuals.reset(new VisualChunk[m_capacity]);
}

bool Chunks::has(ChunkHandle handle) const
{
	const ChunkIndex& in = m_indices[handle.id & INDEX_MASK];
	return in.id == handle.id && in.index != USHRT_MAX;
}

uint32_t Chunks::lookup(ChunkHandle handle) const
{
	const ChunkIndex& in = m_indices[handle.id & INDEX_MASK];
	return in.index;
}

ChunkHandle Chunks::reverseLookup(uint32_t index) const
{
	const uint32_t id = m_chunkIDs[index];
	return ChunkHandle{ id };
}

ChunkHandle Chunks::add()
{
	ChunkIndex& in = m_indices[m_freelistDequeue];
	m_freelistDequeue = in.next;
	in.id += NEW_CHUNK_ID_ADD;
	in.index = static_cast<uint16_t>(m_count++);
	uint32_t& chunkId = m_chunkIDs[in.index];
	chunkId = in.id;
	return ChunkHandle{ chunkId };
}

void Chunks::remove(ChunkHandle handle)
{
	ChunkIndex& in = m_indices[handle.id & INDEX_MASK];

	uint32_t& chunkId = m_chunkIDs[in.index];
	const size_t lastIndex = --m_count;
	chunkId = m_chunkIDs[lastIndex];
	
	m_world._freeChunkBuffers(visuals[in.index]);
	_move(in.index, lastIndex);

	m_indices[chunkId & INDEX_MASK].index = in.index;

	in.index = USHRT_MAX;
	m_indices[m_freelistEnqueue].next = handle.id & INDEX_MASK;
	m_freelistEnqueue = handle.id & INDEX_MASK;

	/*positions[index] = positions[count - 1];
	boundingBoxes[index] = boundingBoxes[count - 1];
	m_world._freeChunkBuffers(visuals[index]);
	visuals[index] = visuals[count - 1];
	lodLevel[index] = lodLevel[count - 1];*/
}

void Chunks::_move(size_t dst, size_t src)
{
	positions[dst] = positions[src];
	//boundingBoxes[dst] = boundingBoxes[src];
	visuals[dst] = visuals[src];

}
