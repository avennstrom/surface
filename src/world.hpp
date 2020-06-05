#pragma once

#include <camera.hpp>
#include <input.hpp>
#include <graphics.hpp>
#include <chunks.hpp>
#include <debug_renderer.hpp>

#include <vk_mem_alloc.hpp>

#include <mpmc_bounded_queue.hpp>
#include <tracy/Tracy.hpp>

//#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
//#include <BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolver.h>
//#include <BulletCollision/BroadphaseCollision/btBroadphaseInterface.h>
//#include <BulletCollision/CollisionDispatch/btDefaultCollisionConfiguration.h>
//#include <BulletCollision/CollisionDispatch/btCollisionDispatcher.h>

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
	void _createDescriptorPool();
	void _createDescriptorStuff();
	void _createChunkAllocator();
	void _createUniformBuffer();
	void _createStagingBuffer();

	void _createResolveDescriptorSet();
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

	VkBuffer m_perFrameUniformBuffer;
	VkDeviceMemory m_perFrameUniformBufferMemory;

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

	struct DeferredChunkBufferDelete
	{
		VkBuffer buffer;
		VmaAllocation allocation;
	};

	std::vector<DeferredChunkBufferDelete> m_deferredDeletes[5];

	std::vector<StagingCopy> m_stagingCopies;

	VkDescriptorSetLayout m_chunkDescriptorSetLayout;
	VkDescriptorSet m_chunkDescriptorSet;
	VkPipelineLayout m_chunkPipelineLayout;
	VkPipeline m_chunkPipeline;

	VkDescriptorSetLayout m_resolveDescriptorSetLayout;
	VkDescriptorSet m_resolveDescriptorSet[3]; // 1 per swap chain image
	VkPipelineLayout m_resolvePipelineLayout;
	VkPipeline m_resolvePipeline;

	VkSampler m_pointSampler;

	VkDescriptorPool m_descriptorPool;

	uint32_t m_frameIndex;

	bool m_freezeFrustum;

	std::unique_ptr<DebugRenderer> m_debugRenderer;

	//btBroadphaseInterface* m_btBroadphase;
	//btDefaultCollisionConfiguration* m_btCollisionConfiguration;
	//btCollisionDispatcher* m_btDispatcher;
	//btSequentialImpulseConstraintSolver* m_btSolver;
	//btDiscreteDynamicsWorld* m_btWorld;
};
