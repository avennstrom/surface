#pragma once

#include <camera.hpp>
#include <input.hpp>
#include <graphics.hpp>
#include <chunks.hpp>
#include <debug_renderer.hpp>
#include <descriptor_set_cache.hpp>

#include <vk_mem_alloc.hpp>

#include <mpmc_bounded_queue.hpp>
#include <tracy/Tracy.hpp>

#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolver.h>
#include <BulletCollision/BroadphaseCollision/btBroadphaseInterface.h>
#include <BulletCollision/CollisionDispatch/btDefaultCollisionConfiguration.h>
#include <BulletCollision/CollisionDispatch/btCollisionDispatcher.h>

#include <vulkan/vulkan.h>

#include <cinttypes>
#include <vector>
#include <atomic>
#include <thread>
#include <concurrent_unordered_map.h>
#include <mutex>
#include <shared_mutex>

struct ChunkGrid
{
	std::unique_ptr<uint8_t[]> occupation;
	glm::i32vec3 regionMin;
	glm::i32vec3 regionMax;
};

class World
{
	friend class Chunks;

public:
	World();
	~World();

	void init();
	void update(float dt, Input& input);
	void draw();
	void resizeBuffers(uint32_t width, uint32_t height);

private:
	void _workerThreadEP(size_t workerIndex, size_t workerCount);
	void _createSamplers();
	void _createChunkPipeline();
	void _createChunkAllocator();
	void _createUniformBuffer();
	void _createStagingBuffer();

	void _createResolvePipeline();

	void _initVisualChunk(
		VisualChunk& vchunk, 
		size_t vertexCount);

	void _freeChunkBuffers(VisualChunk& vchunk);

	void _debugDrawChunkAllocator();


	std::vector<std::thread> m_threads;

	bool m_isRunning;

	enum class WorkItemType
	{
		LoadChunk,
		ChunkLoaded,
	};

	struct WorkItemData_ChunkLoaded
	{
		size_t chunkVertexCount;
		glm::vec3* chunkPositionBuffer;
		glm::vec3* chunkNormalBuffer;
		VisualChunk visualChunk;
		glm::i32vec3 position;
		uint8_t lodLevel;
	};

	struct WorkItem
	{
		WorkItemType type;
		//union 
		//{
			WorkItemData_ChunkLoaded chunkLoaded;
		//} data;
	};

	mpmc_bounded_queue<WorkItem> m_mainThreadWorkQueue;

	Camera m_camera;

	glm::i32vec3 m_prevCameraPosChunkSpace;

	Chunks m_chunks;

	std::shared_mutex m_gridMutex;
	ChunkGrid m_chunkGrid;

	VkDeviceSize m_uniformBufferSize{ 64u * 1024u };
	VkBuffer m_uniformBuffer;
	VkDeviceMemory m_uniformBufferMemory;

	VmaAllocator m_chunkAllocator;

	struct StagingCopy
	{
		VkBuffer dstBuffer;
		VkDeviceAddress srcOffset;
		VkDeviceAddress dstOffset;
		VkDeviceSize size;
	};

	VkDeviceSize m_chunkStagingBufferSize;
	VkBuffer m_chunkStagingBuffer[5];
	VkDeviceMemory m_chunkStagingBufferMemory[5];
	void* m_chunkStagingBufferData[5];

	struct DeferredChunkBufferDelete
	{
		VkBuffer buffer;
		VmaAllocation allocation;
	};

	std::vector<DeferredChunkBufferDelete> m_deferredDeletes[5];

	std::vector<StagingCopy> m_stagingCopies;

	VkDescriptorSetLayout m_chunkDescriptorSetLayout;
	VkPipelineLayout m_chunkPipelineLayout;
	VkPipeline m_chunkPipeline;

	VkDescriptorSetLayout m_resolveDescriptorSetLayout;
	VkPipelineLayout m_resolvePipelineLayout;
	VkPipeline m_resolvePipeline;

	VkDescriptorSetLayout m_compositeDescriptorSetLayout;
	VkPipelineLayout m_compositePipelineLayout;
	VkPipeline m_compositePipeline;

	VkSampler m_pointSampler;

	VkDescriptorPool m_descriptorPool;
	VkDescriptorPool m_dynamicDescriptorPool;
	DescriptorSetCache m_descriptorSetCache;

	VkExtent2D m_resolution;
	VkExtent2D m_checkerboardResolution;

	//VkBuffer m_dynamicStagingBuffer;
	//VkBuffer m_dynamicBuffer;
	//VkDeviceMemory m_dynamicBufferMemory;
	//uint32_t m_dynamicBufferOffset;


	// Render targets
	VkImage m_checkerboardDepth;
	VkImageView m_checkerboardDepthView;
	VkDeviceMemory m_checkerboardDepthMemory;
	VkImage m_checkerboardColor;
	VkImageView m_checkerboardColorView;
	VkDeviceMemory m_checkerboardColorMemory;
	VkImage m_resolvedColor[2];
	VkImageView m_resolvedColorView[2];
	VkDeviceMemory m_resolvedColorMemory[2];
	bool m_isHistoryValid;


	uint32_t m_frameIndex;
	uint32_t m_temporalTargetIndex;

	bool m_freezeFrustum;

	std::unique_ptr<DebugRenderer> m_debugRenderer;

	btBroadphaseInterface* m_btBroadphase;
	btDefaultCollisionConfiguration* m_btCollisionConfiguration;
	btCollisionDispatcher* m_btDispatcher;
	btSequentialImpulseConstraintSolver* m_btSolver;
	btDiscreteDynamicsWorld* m_btWorld;
};
