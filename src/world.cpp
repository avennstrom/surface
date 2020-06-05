#include "world.hpp"

#include <terrain.hpp>
#include <error.hpp>

#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <DirectXCollision.h>
#include <strsafe.h>
#include <tracy/Tracy.hpp>

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/ext/matrix_transform.hpp>

//#include <BulletCollision/BroadphaseCollision/btDbvtBroadphase.h>

#include <Windows.h>

#include <vector>
#include <future>
#include <iostream>
#include <fstream>

using namespace DirectX;

constexpr uint32_t DrawDistance = 18;

constexpr uint32_t ChunkSideSize = 32;
constexpr uint32_t ChunkSideHalfSize = ChunkSideSize / 2;
constexpr uint32_t ChunkMaxLOD = 5;

static void initChunkBuffers(
	uint32_t lodLevel, 
	const glm::i32vec3& origin,
	glm::vec3** outPositions,
	glm::vec3** outNormals,
	size_t* outVertexCount);

bool g_cullingEnabled = true;
bool g_gpuCullingEnabled = false;

struct TerrainConstantBuffer
{
	glm::mat4 u_localToNDCMatrix;
	//
	glm::vec3 u_eyePos;
	float u_fogStart;
	//
	glm::vec3 u_lightDir;
	float u_fogEnd;
	//
	glm::vec3 u_fogColor;
	float u_fogRangeInv;
};

static size_t getCoreCount()
{
	DWORD returnLength = 0;
	GetLogicalProcessorInformation(nullptr, &returnLength);

	const DWORD count = returnLength / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
	std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> logicalProcessorInformation(count);
	if (GetLogicalProcessorInformation(logicalProcessorInformation.data(), &returnLength) == FALSE)
	{
		fatalError("%s", "GetLogicalProcessorInformation failed");
	}

	size_t coreCount = 0;
	for (SYSTEM_LOGICAL_PROCESSOR_INFORMATION& processorInfo : logicalProcessorInformation)
	{
		switch (processorInfo.Relationship)
		{
			case RelationProcessorCore:
				coreCount++;
				break;
		}
	}

	return coreCount;
}

void World::_workerThreadEP(size_t workerIndex, size_t workerCount)
{
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

	//const std::string threadName = "Worker " + std::to_string(workerIndex);
	//rmt_SetCurrentThreadName(threadName.c_str());

	struct Work
	{
		glm::i32vec3 position;
	};

	while (m_isRunning)
	{
		bool hasWork = false;
		Work work;

		size_t closestGridIndex = (size_t)-1;
		float closestDistance = 1000000.0f;

		m_gridMutex.lock_shared();
		{
			ZoneScopedN("Aquire Work");

			// Check if any chunks need updating.
			const size_t gridSize = DrawDistance * DrawDistance * DrawDistance;
			for (size_t gridIndex = workerIndex; gridIndex < gridSize; gridIndex += workerCount)
			{
				if (m_chunkGrid.occupation[gridIndex] == 0)
				{
					const uint32_t grid_x = (uint32_t)gridIndex % DrawDistance;
					const uint32_t grid_y = ((uint32_t)gridIndex / DrawDistance) % DrawDistance;
					const uint32_t grid_z = ((uint32_t)gridIndex / DrawDistance) / DrawDistance;

					const float gridCenter = (float)(DrawDistance / 2) + 0.5f;

					const float dx = grid_x - gridCenter;
					const float dy = grid_y - gridCenter;
					const float dz = grid_z - gridCenter;

					const float distance = dx * dx + dy * dy + dz * dz;
					if (distance < closestDistance)
					{
						closestDistance = distance;
						closestGridIndex = gridIndex;

						work.position.x = m_chunkGrid.regionMin.x + grid_x;
						work.position.y = m_chunkGrid.regionMin.y + grid_y;
						work.position.z = m_chunkGrid.regionMin.z + grid_z;
					}

					hasWork = true;
				}
			}
		}
		m_gridMutex.unlock_shared();

		if (hasWork)
		{
			m_chunkGrid.occupation[closestGridIndex] = 1;

			size_t vertexCount;
			glm::vec3* positionBuffer;
			glm::vec3* normalBuffer;
			initChunkBuffers(0, work.position, &positionBuffer, &normalBuffer, &vertexCount);

			VisualChunk visualChunk;
			_initVisualChunk(visualChunk, vertexCount);

			WorkItem outWork;
			outWork.type = WorkItemType::ChunkLoaded;
			outWork.chunkLoaded.chunkVertexCount = vertexCount;
			outWork.chunkLoaded.chunkPositionBuffer = positionBuffer;
			outWork.chunkLoaded.chunkNormalBuffer = normalBuffer;
			outWork.chunkLoaded.visualChunk = visualChunk;
			outWork.chunkLoaded.position = work.position;
			while (!m_mainThreadWorkQueue.enqueue(outWork));
		}
		else
		{
			using namespace std::chrono_literals;
			std::this_thread::sleep_for(16ms); // take a chill pill
		}
	}
}

void World::_createSamplers()
{
	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.magFilter = VK_FILTER_NEAREST;
	samplerInfo.minFilter = VK_FILTER_NEAREST;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

	if (vkCreateSampler(graphics::device, &samplerInfo, nullptr, &m_pointSampler) != VK_SUCCESS) {
		throw std::runtime_error("failed to create sampler!");
	}
}

static std::vector<char> readFile(const std::string& filename) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);

	if (!file.is_open()) {
		throw std::runtime_error("failed to open file!");
	}

	size_t fileSize = (size_t)file.tellg();
	std::vector<char> buffer(fileSize);

	file.seekg(0);
	file.read(buffer.data(), fileSize);

	file.close();

	return buffer;
}

static VkShaderModule createShaderModule(const std::vector<char>& code) {
	VkShaderModuleCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = code.size();
	createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

	VkShaderModule shaderModule;
	if (vkCreateShaderModule(graphics::device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
		throw std::runtime_error("failed to create shader module!");
	}

	return shaderModule;
}

void World::_createChunkPipeline()
{
	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &m_chunkDescriptorSetLayout;

	if (vkCreatePipelineLayout(graphics::device, &pipelineLayoutInfo, nullptr, &m_chunkPipelineLayout) != VK_SUCCESS) {
		throw std::runtime_error("failed to create pipeline layout!");
	}

	auto vertShaderCode = readFile("shaders/terrain_vs");
	auto fragShaderCode = readFile("shaders/terrain_ps");

	VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
	VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

	VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
	vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertShaderStageInfo.module = vertShaderModule;
	vertShaderStageInfo.pName = "main";

	VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
	fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragShaderStageInfo.module = fragShaderModule;
	fragShaderStageInfo.pName = "main";

	VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

	VkVertexInputBindingDescription vertexBindings[2]{};
	VkVertexInputAttributeDescription vertexAttributes[2]{};

	vertexBindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexBindings[0].binding = 0;
	vertexBindings[0].stride = 12;
	vertexAttributes[0].binding = 0;
	vertexAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	vertexAttributes[0].location = 0;

	vertexBindings[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexBindings[1].binding = 1;
	vertexBindings[1].stride = 12;
	vertexAttributes[1].binding = 1;
	vertexAttributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
	vertexAttributes[1].location = 1;

	VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount = 2;
	vertexInputInfo.pVertexBindingDescriptions = vertexBindings;
	vertexInputInfo.vertexAttributeDescriptionCount = 2;
	vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes;

	VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssembly.primitiveRestartEnable = VK_FALSE;

	VkViewport viewport{};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)graphics::resolution.width / 2;
	viewport.height = (float)graphics::resolution.height / 2;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor{};
	scissor.offset = { 0, 0 };
	scissor.extent = { graphics::resolution.width / 2, graphics::resolution.height / 2};

	VkPipelineViewportStateCreateInfo viewportState{};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.pViewports = &viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rasterizer{};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.depthClampEnable = VK_FALSE;
	rasterizer.rasterizerDiscardEnable = VK_FALSE;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rasterizer.depthBiasEnable = VK_FALSE;

	VkPipelineMultisampleStateCreateInfo multisampling{};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.sampleShadingEnable = VK_TRUE;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_2_BIT;

	VkPipelineColorBlendAttachmentState colorBlendAttachment{};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_FALSE;

	VkPipelineColorBlendStateCreateInfo colorBlending{};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &colorBlendAttachment;
	colorBlending.blendConstants[0] = 0.0f;
	colorBlending.blendConstants[1] = 0.0f;
	colorBlending.blendConstants[2] = 0.0f;
	colorBlending.blendConstants[3] = 0.0f;

	VkPipelineDepthStencilStateCreateInfo depthStencilState{};
	depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencilState.stencilTestEnable = VK_FALSE;
	depthStencilState.depthTestEnable = VK_TRUE;
	depthStencilState.depthWriteEnable = VK_TRUE;
	depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	depthStencilState.minDepthBounds = 0.0f;
	depthStencilState.maxDepthBounds = 1.0f;

	VkGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = shaderStages;
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDepthStencilState = &depthStencilState;
	pipelineInfo.layout = m_chunkPipelineLayout;
	pipelineInfo.renderPass = graphics::colorPass;
	pipelineInfo.subpass = 0;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

	if (vkCreateGraphicsPipelines(graphics::device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_chunkPipeline) != VK_SUCCESS) {
		throw std::runtime_error("failed to create graphics pipeline!");
	}

	vkDestroyShaderModule(graphics::device, fragShaderModule, nullptr);
	vkDestroyShaderModule(graphics::device, vertShaderModule, nullptr);
}

void World::_createDescriptorPool()
{
	VkDescriptorPoolSize poolSizes[] = {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1024 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024 },
	};

	VkDescriptorPoolCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	createInfo.maxSets = 1024;
	createInfo.poolSizeCount = _countof(poolSizes);
	createInfo.pPoolSizes = poolSizes;

	if (vkCreateDescriptorPool(graphics::device, &createInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
		throw new std::runtime_error("failed to create descriptor pool!");
	}
}

void World::_createDescriptorStuff()
{
	VkDescriptorSetLayoutBinding bindings[1]{};
	bindings[0].binding = 0;
	bindings[0].descriptorCount = 1;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo descriptorCreateInfo{};
	descriptorCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descriptorCreateInfo.bindingCount = 1;
	descriptorCreateInfo.pBindings = bindings;

	if (vkCreateDescriptorSetLayout(graphics::device, &descriptorCreateInfo, nullptr, &m_chunkDescriptorSetLayout) != VK_SUCCESS) {
		throw new std::runtime_error("failed to create descriptor set layout!");
	}

	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = m_descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &m_chunkDescriptorSetLayout;

	if (vkAllocateDescriptorSets(graphics::device, &allocInfo, &m_chunkDescriptorSet) != VK_SUCCESS) {
		throw new std::runtime_error("failed to allocate descriptor sets!");
	}
}

void World::_createChunkAllocator()
{
	VmaAllocatorCreateInfo createInfo{};
	createInfo.device = graphics::device;
	createInfo.physicalDevice = graphics::physicalDevice;
	createInfo.instance = graphics::instance;
	//createInfo.flags = VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT;

	if (vmaCreateAllocator(&createInfo, &m_chunkAllocator) != VK_SUCCESS) {
		throw std::runtime_error("failed to create VMA allocator!");
	}
}

void World::_createUniformBuffer()
{
	VkBufferCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	createInfo.size = sizeof(TerrainConstantBuffer);
	createInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	if (vkCreateBuffer(graphics::device, &createInfo, nullptr, &m_perFrameUniformBuffer) != VK_SUCCESS) {
		throw std::runtime_error("failed to create uniform buffer!");
	}

	VkMemoryRequirements memoryRequirements;
	vkGetBufferMemoryRequirements(graphics::device, m_perFrameUniformBuffer, &memoryRequirements);

	VkMemoryAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memoryRequirements.size;
	allocInfo.memoryTypeIndex = graphics::findMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	if (vkAllocateMemory(graphics::device, &allocInfo, nullptr, &m_perFrameUniformBufferMemory) != VK_SUCCESS) {
		throw std::runtime_error("failed to allocate uniform buffer memory!");
	}

	if (vkBindBufferMemory(graphics::device, m_perFrameUniformBuffer, m_perFrameUniformBufferMemory, 0) != VK_SUCCESS) {
		throw std::runtime_error("failed to bind uniform buffer memory!");
	}

	VkDescriptorBufferInfo writeBufferInfo;
	writeBufferInfo.buffer = m_perFrameUniformBuffer;
	writeBufferInfo.offset = 0;
	writeBufferInfo.range = sizeof(TerrainConstantBuffer);

	VkWriteDescriptorSet descriptorWrites[1]{};
	descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[0].dstBinding = 0;
	descriptorWrites[0].dstSet = m_chunkDescriptorSet;
	descriptorWrites[0].descriptorCount = 1;
	descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descriptorWrites[0].pBufferInfo = &writeBufferInfo;

	vkUpdateDescriptorSets(graphics::device, 1, descriptorWrites, 0, nullptr);
}

void World::_createStagingBuffer()
{
	for (size_t i = 0; i < 5; ++i) {
		VkBufferCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		createInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		createInfo.size = m_chunkStagingBufferSize;

		if (vkCreateBuffer(graphics::device, &createInfo, nullptr, &m_chunkStagingBuffer[i]) != VK_SUCCESS) {
			throw std::runtime_error("failed to create staging buffer!");
		}

		VkMemoryRequirements memoryRequirements;
		vkGetBufferMemoryRequirements(graphics::device, m_chunkStagingBuffer[i], &memoryRequirements);

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memoryRequirements.size;
		allocInfo.memoryTypeIndex = graphics::findMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		if (vkAllocateMemory(graphics::device, &allocInfo, nullptr, &m_chunkStagingBufferMemory[i]) != VK_SUCCESS) {
			throw std::runtime_error("failed to allocate staging buffer memory!");
		}

		if (vkBindBufferMemory(graphics::device, m_chunkStagingBuffer[i], m_chunkStagingBufferMemory[i], 0) != VK_SUCCESS) {
			throw std::runtime_error("failed to bind staging buffer memory!");
		}
	}
}

void World::_createResolveDescriptorSet()
{
	VkDescriptorSetLayoutBinding bindings[2]{};

	bindings[0].binding = 0;
	bindings[0].descriptorCount = 1;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	bindings[1].binding = 1;
	bindings[1].descriptorCount = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	VkDescriptorSetLayoutCreateInfo descriptorCreateInfo{};
	descriptorCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descriptorCreateInfo.bindingCount = 2;
	descriptorCreateInfo.pBindings = bindings;

	if (vkCreateDescriptorSetLayout(graphics::device, &descriptorCreateInfo, nullptr, &m_resolveDescriptorSetLayout) != VK_SUCCESS) {
		throw new std::runtime_error("failed to create descriptor set layout!");
	}

	for (int i = 0; i < 3; ++i) {
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_resolveDescriptorSetLayout;

		if (vkAllocateDescriptorSets(graphics::device, &allocInfo, &m_resolveDescriptorSet[i]) != VK_SUCCESS) {
			throw new std::runtime_error("failed to allocate descriptor sets!");
		}

		VkWriteDescriptorSet descriptorWrites[2]{};

		// Input color
		VkDescriptorImageInfo writeImageInfo0{};
		writeImageInfo0.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		writeImageInfo0.imageView = graphics::mainColorImageView;
		writeImageInfo0.sampler = m_pointSampler;

		descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[0].dstBinding = 0;
		descriptorWrites[0].dstSet = m_resolveDescriptorSet[i];
		descriptorWrites[0].descriptorCount = 1;
		descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrites[0].pImageInfo = &writeImageInfo0;

		// Resolved color
		VkDescriptorImageInfo writeImageInfo1{};
		writeImageInfo1.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		writeImageInfo1.imageView = graphics::swapChainImageView(i);

		descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[1].dstBinding = 1;
		descriptorWrites[1].dstSet = m_resolveDescriptorSet[i];
		descriptorWrites[1].descriptorCount = 1;
		descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		descriptorWrites[1].pImageInfo = &writeImageInfo1;

		vkUpdateDescriptorSets(graphics::device, _countof(descriptorWrites), descriptorWrites, 0, nullptr);
	}
}

void World::_createResolvePipeline()
{
	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &m_resolveDescriptorSetLayout;

	if (vkCreatePipelineLayout(graphics::device, &pipelineLayoutInfo, nullptr, &m_resolvePipelineLayout) != VK_SUCCESS) {
		throw std::runtime_error("failed to create pipeline layout!");
	}

	auto compShaderCode = readFile("shaders/resolve_cs");
	VkShaderModule compShaderModule = createShaderModule(compShaderCode);

	VkPipelineShaderStageCreateInfo stageInfo{};
	stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stageInfo.module = compShaderModule;
	stageInfo.pName = "main";

	VkComputePipelineCreateInfo pipelineCreateInfo{};
	pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipelineCreateInfo.layout = m_resolvePipelineLayout;
	pipelineCreateInfo.stage = stageInfo;

	if (vkCreateComputePipelines(graphics::device, nullptr, 1, &pipelineCreateInfo, nullptr, &m_resolvePipeline) != VK_SUCCESS) {
		throw std::runtime_error("failed to create pipeline!");
	}
}

World::World()
	: m_isRunning(true)
	, m_freezeFrustum(false)
	, m_mainThreadWorkQueue(64 * 1024)
	, m_prevCameraPosChunkSpace(INT32_MAX, INT32_MAX, INT32_MAX)
	, m_frameIndex(0)
	, m_chunkStagingBufferSize(4 * 1024 * 1024)
	, m_chunks(*this)
{
	Terrain::init(static_cast<int>(time(nullptr)));
}

World::~World()
{
	m_isRunning = false;
	for (auto&& t : m_threads)
	{
		if (t.joinable())
		{
			t.join();
		}
	}

	for (size_t i = 0; i < 5; ++i) {
		vkFreeMemory(graphics::device, m_chunkStagingBufferMemory[i], nullptr);
		vkDestroyBuffer(graphics::device, m_chunkStagingBuffer[i], nullptr);
	}

	vkFreeMemory(graphics::device, m_perFrameUniformBufferMemory, nullptr);
	vkDestroyBuffer(graphics::device, m_perFrameUniformBuffer, nullptr);

	for (size_t i = 0; i < m_chunks.count(); ++i) {
		auto&& vchunk = m_chunks.visuals[i];
		if (vchunk.vertexBuffer != VK_NULL_HANDLE && vchunk.normalBuffer != VK_NULL_HANDLE) {
			vmaDestroyBuffer(m_chunkAllocator, vchunk.vertexBuffer, vchunk.vertexBufferAlloc);
			vmaDestroyBuffer(m_chunkAllocator, vchunk.normalBuffer, vchunk.normalBufferAlloc);
		}
	}

	vmaDestroyAllocator(m_chunkAllocator);

	vkDestroyPipeline(graphics::device, m_chunkPipeline, nullptr);
	vkDestroyPipelineLayout(graphics::device, m_chunkPipelineLayout, nullptr);

	vkFreeDescriptorSets(graphics::device, m_descriptorPool, 1, &m_chunkDescriptorSet);
	vkDestroyDescriptorSetLayout(graphics::device, m_chunkDescriptorSetLayout, nullptr);

	vkDestroyDescriptorPool(graphics::device, m_descriptorPool, nullptr);
}

void World::init()
{
	//m_btBroadphase = new btDbvtBroadphase();
	//
	//m_btCollisionConfiguration = new btDefaultCollisionConfiguration();
	//m_btDispatcher = new btCollisionDispatcher(m_btCollisionConfiguration);
	//
	//m_btSolver = new btSequentialImpulseConstraintSolver();
	//
	//m_btWorld = new btDiscreteDynamicsWorld(m_btDispatcher, m_btBroadphase, m_btSolver, m_btCollisionConfiguration);
	//
	//m_btWorld->setGravity(btVector3(0.0f, -9.8f, 0.0f));

	_createSamplers();

	_createDescriptorPool();
	_createDescriptorStuff();

	_createChunkPipeline();

	_createChunkAllocator();
	_createStagingBuffer();

	_createUniformBuffer();

	_createResolveDescriptorSet();
	_createResolvePipeline();

	m_debugRenderer.reset(new DebugRenderer(m_descriptorPool));

	const size_t gridSize = DrawDistance * DrawDistance * DrawDistance;
	m_chunkGrid.occupation.reset(new uint8_t[gridSize]);
	std::fill_n(m_chunkGrid.occupation.get(), gridSize, (uint8_t)0);

	m_chunkGrid.regionMin = glm::i32vec3(
		0 - (DrawDistance / 2),
		0 - (DrawDistance / 2),
		0 - (DrawDistance / 2)
	);

	m_chunkGrid.regionMax = glm::i32vec3(
		0 + (DrawDistance / 2),
		0 + (DrawDistance / 2),
		0 + (DrawDistance / 2)
	);

	// Init workers
	const size_t workerCount = getCoreCount() - 1;

	m_threads.resize(workerCount);
	for (size_t i = 0; i < workerCount; ++i)
	{
		m_threads[i] = std::thread([this, workerCount] (size_t i) {
			_workerThreadEP(i, workerCount);
		}, i);
	}
}

void World::update(float dt, Input& input)
{
	ZoneScoped;

	TracyPlot("Chunk Count", (int64_t)m_chunks.count());
	TracyPlot("Main Thread Work Amount", (int64_t)m_mainThreadWorkQueue.unsafe_size());

	//m_btWorld->stepSimulation(dt);

	GamepadState gamepad;
	if (input.getGamepadState(gamepad))
	{
		static bool yButtonPrev = false;

		if (gamepad.isButtonDown(GamepadButton::Y) && !yButtonPrev)
		{
			m_freezeFrustum = !m_freezeFrustum;
		}

		yButtonPrev = gamepad.isButtonDown(GamepadButton::Y);
	}

	{
		ZoneScopedN("Perform Work");

		m_stagingCopies.clear();
		size_t chunkStagingBufferOffset = 0;

		bool cancelWork = false;

		WorkItem work;
		while (!cancelWork && m_mainThreadWorkQueue.dequeue(work))
		{
			switch (work.type)
			{
				case WorkItemType::ChunkLoaded:
				{
					const size_t vertexCount = work.chunkLoaded.chunkVertexCount;
					const glm::vec3* chunkPositionBuffer = work.chunkLoaded.chunkPositionBuffer;
					const glm::vec3* chunkNormalBuffer = work.chunkLoaded.chunkNormalBuffer;
					const VisualChunk& vchunk = work.chunkLoaded.visualChunk;

					if (vchunk.vertexCount > 0) {
						const size_t positionDataSize = vertexCount * sizeof(glm::vec3);
						const size_t normalDataSize = vertexCount * sizeof(glm::vec3);
						const size_t totalDataSize = positionDataSize + normalDataSize;

						const size_t vertexDataOffset = 0;
						const size_t normalDataOffset = positionDataSize;

						if ((chunkStagingBufferOffset + totalDataSize) > m_chunkStagingBufferSize) {
							cancelWork = true;
							break;
						}

						void* mappedMemory;
						if (vkMapMemory(graphics::device, m_chunkStagingBufferMemory[m_frameIndex], chunkStagingBufferOffset, totalDataSize, 0, &mappedMemory) != VK_SUCCESS) {
							throw std::runtime_error("failed to map chunk staging buffer!");
						}

						memcpy(((char*)mappedMemory) + vertexDataOffset, chunkPositionBuffer, positionDataSize);
						memcpy(((char*)mappedMemory) + normalDataOffset, chunkNormalBuffer, normalDataSize);

						vkUnmapMemory(graphics::device, m_chunkStagingBufferMemory[m_frameIndex]);

						StagingCopy copy{};

						copy.size = positionDataSize;
						copy.dstBuffer = vchunk.vertexBuffer;
						copy.srcOffset = chunkStagingBufferOffset + vertexDataOffset;
						m_stagingCopies.push_back(copy);

						copy.size = normalDataSize;
						copy.dstBuffer = vchunk.normalBuffer;
						copy.srcOffset = chunkStagingBufferOffset + normalDataOffset;
						m_stagingCopies.push_back(copy);

						chunkStagingBufferOffset += totalDataSize;
					}

					delete[] chunkPositionBuffer;
					delete[] chunkNormalBuffer;

					ChunkHandle chunkHandle = m_chunks.add();
					const uint32_t chunkIndex = m_chunks.lookup(chunkHandle);

					m_chunks.visuals[chunkIndex] = work.chunkLoaded.visualChunk;
					m_chunks.positions[chunkIndex] = work.chunkLoaded.position;

					// Set AABB
					/*m_chunks.boundingBoxes[chunkIndex].Center.x = (float)(work.chunkLoaded.position.x * (int32_t)ChunkSideSize);
					m_chunks.boundingBoxes[chunkIndex].Center.y = (float)(work.chunkLoaded.position.y * (int32_t)ChunkSideSize);
					m_chunks.boundingBoxes[chunkIndex].Center.z = (float)(work.chunkLoaded.position.z * (int32_t)ChunkSideSize);
					m_chunks.boundingBoxes[chunkIndex].Extents.x = (float)ChunkSideSize;
					m_chunks.boundingBoxes[chunkIndex].Extents.y = (float)ChunkSideSize;
					m_chunks.boundingBoxes[chunkIndex].Extents.z = (float)ChunkSideSize;*/

					break;
				}
			}
		}

		TracyPlot("Staging Buffer Usage", (int64_t)chunkStagingBufferOffset)
	}

	m_camera.update(input, dt);

	const glm::vec3& cameraPos = m_camera.getPosition();
	const glm::i32vec3 cameraPosChunkSpace(
		(int32_t)cameraPos.x / (int32_t)ChunkSideSize,
		(int32_t)cameraPos.y / (int32_t)ChunkSideSize,
		(int32_t)cameraPos.z / (int32_t)ChunkSideSize
	);

	if (cameraPosChunkSpace.x != m_prevCameraPosChunkSpace.x || 
		cameraPosChunkSpace.y != m_prevCameraPosChunkSpace.y ||
		cameraPosChunkSpace.z != m_prevCameraPosChunkSpace.z)
	{
		ZoneScopedN("Update Grid");
		
		m_gridMutex.lock();

		m_chunkGrid.regionMin = glm::i32vec3(
			cameraPosChunkSpace.x - (DrawDistance / 2),
			cameraPosChunkSpace.y - (DrawDistance / 2),
			cameraPosChunkSpace.z - (DrawDistance / 2)
		);

		m_chunkGrid.regionMax = glm::i32vec3(
			cameraPosChunkSpace.x + (DrawDistance / 2),
			cameraPosChunkSpace.y + (DrawDistance / 2),
			cameraPosChunkSpace.z + (DrawDistance / 2)
		);

		{
			// Mark which tiles are occupied.

			ZoneScopedN("Mark Occupation And Remove");

			const size_t gridSize = DrawDistance * DrawDistance * DrawDistance;
			std::fill_n(m_chunkGrid.occupation.get(), gridSize, (uint8_t)0);

			for (size_t chunkIt = 0; chunkIt < m_chunks.count();)
			{
				const glm::i32vec3& position = m_chunks.positions[chunkIt];
				
				if (position.x >= m_chunkGrid.regionMin.x &&
					position.y >= m_chunkGrid.regionMin.y &&
					position.z >= m_chunkGrid.regionMin.z &&
					position.x < m_chunkGrid.regionMax.x &&
					position.y < m_chunkGrid.regionMax.y &&
					position.z < m_chunkGrid.regionMax.z)
				{
					const glm::i32vec3 occupationPos(
						position.x - m_chunkGrid.regionMin.x, 
						position.y - m_chunkGrid.regionMin.y,
						position.z - m_chunkGrid.regionMin.z
					);

					const int occupationIndex = (occupationPos.z * DrawDistance * DrawDistance) + (occupationPos.y * DrawDistance) + occupationPos.x;
					assert(occupationIndex < gridSize);
					m_chunkGrid.occupation[occupationIndex] = 1;

					++chunkIt;
				}
				else
				{
					ChunkHandle handle = m_chunks.reverseLookup(static_cast<uint32_t>(chunkIt));
					m_chunks.remove(handle);
				}
			}
		}

		m_gridMutex.unlock();

		m_prevCameraPosChunkSpace = cameraPosChunkSpace;
	}
}

inline void XM_CALLCONV CreateBoundingFrustumRH(BoundingFrustum& Out, FXMMATRIX Projection)
{
	// Corners of the projection frustum in homogenous space.
	static XMVECTORF32 HomogenousPoints[6] =
	{
		{ {  { 1.0f,  0.0f, -1.0f, 1.0f } } },   // right (at far plane)
		{ { { -1.0f,  0.0f, -1.0f, 1.0f } } },   // left
		{ { {  0.0f,  1.0f, -1.0f, 1.0f } } },   // top
		{ { {  0.0f, -1.0f, -1.0f, 1.0f } } },   // bottom

		{ { { 0.0f, 0.0f, 1.0f, 1.0f } } },     // near
		{ { { 0.0f, 0.0f, 0.0f, 1.0f } } }      // far
	};

	XMVECTOR Determinant;
	XMMATRIX matInverse = XMMatrixInverse(&Determinant, Projection);

	// Compute the frustum corners in world space.
	XMVECTOR Points[6];

	for (size_t i = 0; i < 6; ++i)
	{
		// Transform point.
		Points[i] = XMVector4Transform(HomogenousPoints[i], matInverse);
	}

	Out.Origin = XMFLOAT3(0.0f, 0.0f, 0.0f);
	Out.Orientation = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);

	// Compute the slopes.
	Points[0] = XMVectorMultiply(Points[0], XMVectorReciprocal(XMVectorSplatZ(Points[0])));
	Points[1] = XMVectorMultiply(Points[1], XMVectorReciprocal(XMVectorSplatZ(Points[1])));
	Points[2] = XMVectorMultiply(Points[2], XMVectorReciprocal(XMVectorSplatZ(Points[2])));
	Points[3] = XMVectorMultiply(Points[3], XMVectorReciprocal(XMVectorSplatZ(Points[3])));

	Out.RightSlope = XMVectorGetX(Points[0]);
	Out.LeftSlope = XMVectorGetX(Points[1]);
	Out.TopSlope = XMVectorGetY(Points[2]);
	Out.BottomSlope = XMVectorGetY(Points[3]);

	// Compute near and far.
	Points[4] = XMVectorMultiply(Points[4], XMVectorReciprocal(XMVectorSplatW(Points[4])));
	Points[5] = XMVectorMultiply(Points[5], XMVectorReciprocal(XMVectorSplatW(Points[5])));

	Out.Near = XMVectorGetZ(Points[4]);
	Out.Far = XMVectorGetZ(Points[5]);
}

void World::draw()
{
	ZoneScopedC(0x555500);

	VkCommandBuffer cb = graphics::currentCommandBuffer();

	{
		ZoneScopedN("Generate HZB");


	}

	std::vector<uint64_t> cullingBitset((m_chunks.count() + 63) / 64);
#if 0
	if (g_cullingEnabled)
	{
		ZoneScopedN("Cull Chunks");

		std::fill(cullingBitset.begin(), cullingBitset.end(), (uint64_t)0);

		static BoundingFrustum cameraFrustum;

		if (!m_freezeFrustum)
		{
			//CreateBoundingFrustumRH(cameraFrustum, XMLoadFloat4x4A(&m_camera.getViewToNDCMatrix()));
			//cameraFrustum.Transform(cameraFrustum, XMLoadFloat4x4A(&m_camera.getWorldMatrix()));
		}

		for (size_t chunkIt = 0; chunkIt < m_chunks.count(); ++chunkIt)
		{
			//const BoundingBox& bbox = m_chunks.boundingBoxes[chunkIt];

			//const uint64_t isVisible = cameraFrustum.Contains(bbox) ? 1 : 0;
			
			const glm::i32vec3& pos = m_chunks.positions[chunkIt];



			// Convert AABB to center-extents representation
			const float e = static_cast<float>(ChunkSideHalfSize);
			const glm::vec3 c = glm::vec3(pos) + e;

			// Compute the projection interval radius of b onto L(t) = b.c + t * p.n
			float r = e * abs(p.n[0]) + e * abs(p.n[1]) + e * abs(p.n[2]);

			// Compute distance of box center from plane
			float s = Dot(p.n, c) - p.d;

			// Intersection occurs when distance s falls within [-r,+r] interval
			return Abs(s) <= r;

			const size_t bitSegment = chunkIt / 64;
			const uint64_t bitIndex = static_cast<uint64_t>(chunkIt % 64);

			cullingBitset[bitSegment] |= (isVisible << bitIndex);
		}
	}
	else
#endif
	{
		std::fill(cullingBitset.begin(), cullingBitset.end(), (uint64_t)-1);
	}

	{
		ZoneScopedN("Fill Chunk Buffers");

		for (const auto& copy : m_stagingCopies) {
			VkBufferCopy region;
			region.srcOffset = copy.srcOffset;
			region.dstOffset = copy.dstOffset;
			region.size = copy.size;
			vkCmdCopyBuffer(cb, m_chunkStagingBuffer[m_frameIndex], copy.dstBuffer, 1, &region);
		}
	}

	{
		ZoneScopedN("Update Chunk Uniform Buffer");

		TerrainConstantBuffer constants;
		constants.u_fogStart = 32.0f;
		constants.u_fogEnd = m_camera.getFarClip();
		constants.u_fogRangeInv = 1.0f / (constants.u_fogEnd - constants.u_fogStart);
		constants.u_fogColor.x = 0.0f;
		constants.u_fogColor.y = 0.0f;
		constants.u_fogColor.z = 0.0f;
		constants.u_localToNDCMatrix = m_camera.getWorldToNDCMatrix();
		constants.u_eyePos = m_camera.getPosition();
		constants.u_lightDir = glm::normalize(glm::vec3(0.2f, 1.0f, 0.1f));
		vkCmdUpdateBuffer(cb, m_perFrameUniformBuffer, 0, sizeof(TerrainConstantBuffer), &constants);
	}

#if 0
	// Draw chunk boundaries
	for (size_t chunkIt = 0; chunkIt < m_chunks.count(); ++chunkIt)
	{
		const glm::i32vec3& ipos = m_chunks.positions[chunkIt];
		const glm::vec3 pos(ipos * (int32_t)ChunkSideSize);
		const float s = (float)ChunkSideSize;

		// Bottom
		_debugDrawLine3D(pos + glm::vec3(0.0f, 0.0f, 0.0f), pos + glm::vec3(s,    0.0f, 0.0f), 0xaaaaaa);
		_debugDrawLine3D(pos + glm::vec3(s,    0.0f, 0.0f), pos + glm::vec3(s,    0.0f, s   ), 0xaaaaaa);
		_debugDrawLine3D(pos + glm::vec3(s,    0.0f, s   ), pos + glm::vec3(0.0f, 0.0f, s   ), 0xaaaaaa);
		_debugDrawLine3D(pos + glm::vec3(0.0f, 0.0f, s   ), pos + glm::vec3(0.0f, 0.0f, 0.0f), 0xaaaaaa);

		// Top
		_debugDrawLine3D(pos + glm::vec3(0.0f, s,    0.0f), pos + glm::vec3(s,    s,    0.0f), 0xaaaaaa);
		_debugDrawLine3D(pos + glm::vec3(s,    s,    0.0f), pos + glm::vec3(s,    s,    s   ), 0xaaaaaa);
		_debugDrawLine3D(pos + glm::vec3(s,    s,    s   ), pos + glm::vec3(0.0f, s,    s   ), 0xaaaaaa);
		_debugDrawLine3D(pos + glm::vec3(0.0f, s,    s   ), pos + glm::vec3(0.0f, s,    0.0f), 0xaaaaaa);

		// Vertical edges
		_debugDrawLine3D(pos + glm::vec3(0.0f, 0.0f, 0.0f), pos + glm::vec3(0.0f, s,    0.0f), 0xaaaaaa);
		_debugDrawLine3D(pos + glm::vec3(s,    0.0f, 0.0f), pos + glm::vec3(s,    s,    0.0f), 0xaaaaaa);
		_debugDrawLine3D(pos + glm::vec3(s,    0.0f, s   ), pos + glm::vec3(s,    s,    s   ), 0xaaaaaa);
		_debugDrawLine3D(pos + glm::vec3(0.0f, 0.0f, s   ), pos + glm::vec3(0.0f, s,    s   ), 0xaaaaaa);
	}
#endif

	// Draw XYZ
	m_debugRenderer->drawLine3D(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(5.0f, 0.0f, 0.0f), 0x0000ff);
	m_debugRenderer->drawLine3D(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 5.0f, 0.0f), 0x00ff00);
	m_debugRenderer->drawLine3D(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 5.0f), 0xff0000);

	_debugDrawChunkAllocator();

	m_debugRenderer->updateBuffers(cb, m_camera);

	VkRenderPassBeginInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = graphics::colorPass;
	renderPassInfo.framebuffer = graphics::colorPassFramebuffer;
	renderPassInfo.renderArea.offset = { 0, 0 };
	renderPassInfo.renderArea.extent = { graphics::resolution.width / 2, graphics::resolution.height / 2 };

	VkClearValue depthClearValue{};
	depthClearValue.depthStencil.depth = 1.0f;
	depthClearValue.depthStencil.stencil = 0x00;

	VkClearValue colorClearValue{};
	colorClearValue.color.float32[0] = 0.0f;
	colorClearValue.color.float32[1] = 0.0f;
	colorClearValue.color.float32[2] = 0.0f;
	colorClearValue.color.float32[3] = 0.0f;

	VkClearValue clearValues[] = {
		depthClearValue,
		colorClearValue,
	};

	renderPassInfo.clearValueCount = _countof(clearValues);
	renderPassInfo.pClearValues = clearValues;

	vkCmdBeginRenderPass(cb, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

	{
		ZoneScopedN("Draw Chunks");

		vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_chunkPipeline);

		VkDescriptorSet descriptorSets[] = {
			m_chunkDescriptorSet,
		};

		vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_chunkPipelineLayout, 0, 1, descriptorSets, 0, nullptr);

		for (size_t chunkIt = 0; chunkIt < m_chunks.count(); ++chunkIt)
		{
			const size_t bitSegment = chunkIt / 64;
			const uint64_t bitIndex = static_cast<uint64_t>(chunkIt % 64);

			const bool isVisible = (cullingBitset[bitSegment] >> bitIndex) & 1;
			if (isVisible)
			{
				const VisualChunk& chunk = m_chunks.visuals[chunkIt];
				if (chunk.vertexCount > 0)
				{
					VkBuffer vertexBuffers[2] = { 
						chunk.vertexBuffer, 
						chunk.normalBuffer,
					};
					VkDeviceSize vertexBufferOffsets[2] = { 
						0, 
						0, 
					};
					vkCmdBindVertexBuffers(cb, 0, 2, vertexBuffers, vertexBufferOffsets);

					vkCmdDraw(cb, chunk.vertexCount, 1, 0, 0);
				}
			}
		}
	}

	m_debugRenderer->draw(cb);

	vkCmdEndRenderPass(cb);

	{
		VkImageMemoryBarrier resolveImageBarrier{};
		resolveImageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		resolveImageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		resolveImageBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		resolveImageBarrier.srcAccessMask = 0;
		resolveImageBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		resolveImageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		resolveImageBarrier.subresourceRange.layerCount = 1;
		resolveImageBarrier.subresourceRange.levelCount = 1;
		resolveImageBarrier.image = graphics::currentSwapChainImage();

		vkCmdPipelineBarrier(
			cb,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
			0,
			0,
			nullptr,
			0,
			nullptr,
			1,
			&resolveImageBarrier);
	}

	vkCmdBindDescriptorSets(
		cb,
		VK_PIPELINE_BIND_POINT_COMPUTE,
		m_resolvePipelineLayout,
		0,
		1,
		&m_resolveDescriptorSet[graphics::currentFrameIndex()],
		0,
		nullptr);
	vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_resolvePipeline);

	const uint32_t threadGroupSizeX = 8;
	const uint32_t threadGroupSizeY = 8;

	const uint32_t threadCountX = ((graphics::resolution.width / 2) + (threadGroupSizeX - 1)) / threadGroupSizeX;
	const uint32_t threadCountY = (graphics::resolution.height + (threadGroupSizeY - 1)) / threadGroupSizeY;

	vkCmdDispatch(cb, threadCountX, threadCountY, 1);

	{
		VkImageMemoryBarrier imageBarrier{};
		imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		imageBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		imageBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		imageBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageBarrier.subresourceRange.layerCount = 1;
		imageBarrier.subresourceRange.levelCount = 1;
		imageBarrier.image = graphics::currentSwapChainImage();

		vkCmdPipelineBarrier(
			cb,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
			0,
			0,
			nullptr,
			0,
			nullptr,
			1,
			&imageBarrier);
	}

	//vkCmdEndRenderPass(cb);

	m_frameIndex = (m_frameIndex + 1) % 5;

	{
		ZoneScopedN("Deferred Deletes");

		for (auto&& dd : m_deferredDeletes[m_frameIndex])
		{
			vmaDestroyBuffer(m_chunkAllocator, dd.buffer, dd.allocation);
		}
		m_deferredDeletes[m_frameIndex].clear();
	}
}

/*static uint32_t roundUpToNearestPowerOf2(uint32_t value)
{
	uint32_t result = value;
	result--;
	result |= result >> 1;
	result |= result >> 2;
	result |= result >> 4;
	result |= result >> 8;
	result |= result >> 16;
	result++;
	return result;
}

static uint32_t log2(uint32_t value)
{
	unsigned long idx;
	_BitScanReverse(&idx, value);
	return idx;
}*/

void World::resizeBuffers(uint32_t /*width*/, uint32_t /*height*/)
{
	/* const uint32_t hzbMinWidth = roundUpToNearestPowerOf2((width + 7) / 8);
	const uint32_t hzbMinHeight = roundUpToNearestPowerOf2((height + 7) / 8);

	m_hzbSize = std::max(hzbMinWidth, hzbMinHeight);
	m_hzbMipLevels = log2(m_hzbSize);

	Texture2DDesc hzbDesc;
	hzbDesc.width = m_hzbSize;
	hzbDesc.height = m_hzbSize;
	hzbDesc.format = RenderFormat::R32_FLOAT;
	hzbDesc.mipLevels = m_hzbMipLevels;
	hzbDesc.usage = RenderTextureUsage_ShaderResource | RenderTextureUsage_UnorderedAccess;
	m_HZBTexture = renderer.createTexture2D(hzbDesc);

	Texture2DDesc depthBufferDesc;
	depthBufferDesc.width = width;
	depthBufferDesc.height = height;
	depthBufferDesc.format = RenderFormat::R32_FLOAT;
	depthBufferDesc.usage = RenderTextureUsage_DepthStencil | RenderTextureUsage_ShaderResource;
	m_depthBuffers[0] = renderer.createTexture2D(depthBufferDesc);
	m_depthBuffers[1] = renderer.createTexture2D(depthBufferDesc); */
}

/*
Linearly interpolate the position where an isosurface cuts
an edge between two vertices, each with their own scalar value
*/
static glm::vec3 vertexInterp(
	float isolevel, 
	glm::vec3 p1,
	glm::vec3 p2,
	float valp1, 
	float valp2)
{
	if (abs(isolevel - valp1) < 0.00001f)
		return(p1);
	if (abs(isolevel - valp2) < 0.00001f)
		return(p2);
	if (abs(valp1 - valp2) < 0.00001f)
		return(p1);

	float mu = (isolevel - valp1) / (valp2 - valp1);

	glm::vec3 p;
	p.x = p1.x + mu * (p2.x - p1.x);
	p.y = p1.y + mu * (p2.y - p1.y);
	p.z = p1.z + mu * (p2.z - p1.z);
	return(p);
}

struct GridCell
{
	glm::vec3 p[8];
	float val[8];
};

/*
Given a grid cell and an isolevel, calculate the triangular
facets required to represent the isosurface through the cell.
Return the number of triangular facets, the array "triangles"
will be loaded up with the vertices at most 5 triangular facets.
0 will be returned if the grid cell is either totally above
of totally below the isolevel.
*/
static void polygonise(
	GridCell grid, 
	float isolevel, 
	std::vector<glm::vec3>& vertices,
	std::vector<glm::vec3>& normals)
{
	constexpr int edgeTable[256] = {
		0x0  , 0x109, 0x203, 0x30a, 0x406, 0x50f, 0x605, 0x70c,
		0x80c, 0x905, 0xa0f, 0xb06, 0xc0a, 0xd03, 0xe09, 0xf00,
		0x190, 0x99 , 0x393, 0x29a, 0x596, 0x49f, 0x795, 0x69c,
		0x99c, 0x895, 0xb9f, 0xa96, 0xd9a, 0xc93, 0xf99, 0xe90,
		0x230, 0x339, 0x33 , 0x13a, 0x636, 0x73f, 0x435, 0x53c,
		0xa3c, 0xb35, 0x83f, 0x936, 0xe3a, 0xf33, 0xc39, 0xd30,
		0x3a0, 0x2a9, 0x1a3, 0xaa , 0x7a6, 0x6af, 0x5a5, 0x4ac,
		0xbac, 0xaa5, 0x9af, 0x8a6, 0xfaa, 0xea3, 0xda9, 0xca0,
		0x460, 0x569, 0x663, 0x76a, 0x66 , 0x16f, 0x265, 0x36c,
		0xc6c, 0xd65, 0xe6f, 0xf66, 0x86a, 0x963, 0xa69, 0xb60,
		0x5f0, 0x4f9, 0x7f3, 0x6fa, 0x1f6, 0xff , 0x3f5, 0x2fc,
		0xdfc, 0xcf5, 0xfff, 0xef6, 0x9fa, 0x8f3, 0xbf9, 0xaf0,
		0x650, 0x759, 0x453, 0x55a, 0x256, 0x35f, 0x55 , 0x15c,
		0xe5c, 0xf55, 0xc5f, 0xd56, 0xa5a, 0xb53, 0x859, 0x950,
		0x7c0, 0x6c9, 0x5c3, 0x4ca, 0x3c6, 0x2cf, 0x1c5, 0xcc ,
		0xfcc, 0xec5, 0xdcf, 0xcc6, 0xbca, 0xac3, 0x9c9, 0x8c0,
		0x8c0, 0x9c9, 0xac3, 0xbca, 0xcc6, 0xdcf, 0xec5, 0xfcc,
		0xcc , 0x1c5, 0x2cf, 0x3c6, 0x4ca, 0x5c3, 0x6c9, 0x7c0,
		0x950, 0x859, 0xb53, 0xa5a, 0xd56, 0xc5f, 0xf55, 0xe5c,
		0x15c, 0x55 , 0x35f, 0x256, 0x55a, 0x453, 0x759, 0x650,
		0xaf0, 0xbf9, 0x8f3, 0x9fa, 0xef6, 0xfff, 0xcf5, 0xdfc,
		0x2fc, 0x3f5, 0xff , 0x1f6, 0x6fa, 0x7f3, 0x4f9, 0x5f0,
		0xb60, 0xa69, 0x963, 0x86a, 0xf66, 0xe6f, 0xd65, 0xc6c,
		0x36c, 0x265, 0x16f, 0x66 , 0x76a, 0x663, 0x569, 0x460,
		0xca0, 0xda9, 0xea3, 0xfaa, 0x8a6, 0x9af, 0xaa5, 0xbac,
		0x4ac, 0x5a5, 0x6af, 0x7a6, 0xaa , 0x1a3, 0x2a9, 0x3a0,
		0xd30, 0xc39, 0xf33, 0xe3a, 0x936, 0x83f, 0xb35, 0xa3c,
		0x53c, 0x435, 0x73f, 0x636, 0x13a, 0x33 , 0x339, 0x230,
		0xe90, 0xf99, 0xc93, 0xd9a, 0xa96, 0xb9f, 0x895, 0x99c,
		0x69c, 0x795, 0x49f, 0x596, 0x29a, 0x393, 0x99 , 0x190,
		0xf00, 0xe09, 0xd03, 0xc0a, 0xb06, 0xa0f, 0x905, 0x80c,
		0x70c, 0x605, 0x50f, 0x406, 0x30a, 0x203, 0x109, 0x0 
	};

	constexpr int triTable[256][16] =
	{ 
		{ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 0, 8, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 0, 1, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 1, 8, 3, 9, 8, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 1, 2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 0, 8, 3, 1, 2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 9, 2, 10, 0, 2, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 2, 8, 3, 2, 10, 8, 10, 9, 8, -1, -1, -1, -1, -1, -1, -1 },
		{ 3, 11, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 0, 11, 2, 8, 11, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 1, 9, 0, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 1, 11, 2, 1, 9, 11, 9, 8, 11, -1, -1, -1, -1, -1, -1, -1 },
		{ 3, 10, 1, 11, 10, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 0, 10, 1, 0, 8, 10, 8, 11, 10, -1, -1, -1, -1, -1, -1, -1 },
		{ 3, 9, 0, 3, 11, 9, 11, 10, 9, -1, -1, -1, -1, -1, -1, -1 },
		{ 9, 8, 10, 10, 8, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 4, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 4, 3, 0, 7, 3, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 0, 1, 9, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 4, 1, 9, 4, 7, 1, 7, 3, 1, -1, -1, -1, -1, -1, -1, -1 },
		{ 1, 2, 10, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 3, 4, 7, 3, 0, 4, 1, 2, 10, -1, -1, -1, -1, -1, -1, -1 },
		{ 9, 2, 10, 9, 0, 2, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1 },
		{ 2, 10, 9, 2, 9, 7, 2, 7, 3, 7, 9, 4, -1, -1, -1, -1 },
		{ 8, 4, 7, 3, 11, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 11, 4, 7, 11, 2, 4, 2, 0, 4, -1, -1, -1, -1, -1, -1, -1 },
		{ 9, 0, 1, 8, 4, 7, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1 },
		{ 4, 7, 11, 9, 4, 11, 9, 11, 2, 9, 2, 1, -1, -1, -1, -1 },
		{ 3, 10, 1, 3, 11, 10, 7, 8, 4, -1, -1, -1, -1, -1, -1, -1 },
		{ 1, 11, 10, 1, 4, 11, 1, 0, 4, 7, 11, 4, -1, -1, -1, -1 },
		{ 4, 7, 8, 9, 0, 11, 9, 11, 10, 11, 0, 3, -1, -1, -1, -1 },
		{ 4, 7, 11, 4, 11, 9, 9, 11, 10, -1, -1, -1, -1, -1, -1, -1 },
		{ 9, 5, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 9, 5, 4, 0, 8, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 0, 5, 4, 1, 5, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 8, 5, 4, 8, 3, 5, 3, 1, 5, -1, -1, -1, -1, -1, -1, -1 },
		{ 1, 2, 10, 9, 5, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 3, 0, 8, 1, 2, 10, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1 },
		{ 5, 2, 10, 5, 4, 2, 4, 0, 2, -1, -1, -1, -1, -1, -1, -1 },
		{ 2, 10, 5, 3, 2, 5, 3, 5, 4, 3, 4, 8, -1, -1, -1, -1 },
		{ 9, 5, 4, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 0, 11, 2, 0, 8, 11, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1 },
		{ 0, 5, 4, 0, 1, 5, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1 },
		{ 2, 1, 5, 2, 5, 8, 2, 8, 11, 4, 8, 5, -1, -1, -1, -1 },
		{ 10, 3, 11, 10, 1, 3, 9, 5, 4, -1, -1, -1, -1, -1, -1, -1 },
		{ 4, 9, 5, 0, 8, 1, 8, 10, 1, 8, 11, 10, -1, -1, -1, -1 },
		{ 5, 4, 0, 5, 0, 11, 5, 11, 10, 11, 0, 3, -1, -1, -1, -1 },
		{ 5, 4, 8, 5, 8, 10, 10, 8, 11, -1, -1, -1, -1, -1, -1, -1 },
		{ 9, 7, 8, 5, 7, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 9, 3, 0, 9, 5, 3, 5, 7, 3, -1, -1, -1, -1, -1, -1, -1 },
		{ 0, 7, 8, 0, 1, 7, 1, 5, 7, -1, -1, -1, -1, -1, -1, -1 },
		{ 1, 5, 3, 3, 5, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 9, 7, 8, 9, 5, 7, 10, 1, 2, -1, -1, -1, -1, -1, -1, -1 },
		{ 10, 1, 2, 9, 5, 0, 5, 3, 0, 5, 7, 3, -1, -1, -1, -1 },
		{ 8, 0, 2, 8, 2, 5, 8, 5, 7, 10, 5, 2, -1, -1, -1, -1 },
		{ 2, 10, 5, 2, 5, 3, 3, 5, 7, -1, -1, -1, -1, -1, -1, -1 },
		{ 7, 9, 5, 7, 8, 9, 3, 11, 2, -1, -1, -1, -1, -1, -1, -1 },
		{ 9, 5, 7, 9, 7, 2, 9, 2, 0, 2, 7, 11, -1, -1, -1, -1 },
		{ 2, 3, 11, 0, 1, 8, 1, 7, 8, 1, 5, 7, -1, -1, -1, -1 },
		{ 11, 2, 1, 11, 1, 7, 7, 1, 5, -1, -1, -1, -1, -1, -1, -1 },
		{ 9, 5, 8, 8, 5, 7, 10, 1, 3, 10, 3, 11, -1, -1, -1, -1 },
		{ 5, 7, 0, 5, 0, 9, 7, 11, 0, 1, 0, 10, 11, 10, 0, -1 },
		{ 11, 10, 0, 11, 0, 3, 10, 5, 0, 8, 0, 7, 5, 7, 0, -1 },
		{ 11, 10, 5, 7, 11, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 10, 6, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 0, 8, 3, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 9, 0, 1, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 1, 8, 3, 1, 9, 8, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1 },
		{ 1, 6, 5, 2, 6, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 1, 6, 5, 1, 2, 6, 3, 0, 8, -1, -1, -1, -1, -1, -1, -1 },
		{ 9, 6, 5, 9, 0, 6, 0, 2, 6, -1, -1, -1, -1, -1, -1, -1 },
		{ 5, 9, 8, 5, 8, 2, 5, 2, 6, 3, 2, 8, -1, -1, -1, -1 },
		{ 2, 3, 11, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 11, 0, 8, 11, 2, 0, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1 },
		{ 0, 1, 9, 2, 3, 11, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1 },
		{ 5, 10, 6, 1, 9, 2, 9, 11, 2, 9, 8, 11, -1, -1, -1, -1 },
		{ 6, 3, 11, 6, 5, 3, 5, 1, 3, -1, -1, -1, -1, -1, -1, -1 },
		{ 0, 8, 11, 0, 11, 5, 0, 5, 1, 5, 11, 6, -1, -1, -1, -1 },
		{ 3, 11, 6, 0, 3, 6, 0, 6, 5, 0, 5, 9, -1, -1, -1, -1 },
		{ 6, 5, 9, 6, 9, 11, 11, 9, 8, -1, -1, -1, -1, -1, -1, -1 },
		{ 5, 10, 6, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 4, 3, 0, 4, 7, 3, 6, 5, 10, -1, -1, -1, -1, -1, -1, -1 },
		{ 1, 9, 0, 5, 10, 6, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1 },
		{ 10, 6, 5, 1, 9, 7, 1, 7, 3, 7, 9, 4, -1, -1, -1, -1 },
		{ 6, 1, 2, 6, 5, 1, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1 },
		{ 1, 2, 5, 5, 2, 6, 3, 0, 4, 3, 4, 7, -1, -1, -1, -1 },
		{ 8, 4, 7, 9, 0, 5, 0, 6, 5, 0, 2, 6, -1, -1, -1, -1 },
		{ 7, 3, 9, 7, 9, 4, 3, 2, 9, 5, 9, 6, 2, 6, 9, -1 },
		{ 3, 11, 2, 7, 8, 4, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1 },
		{ 5, 10, 6, 4, 7, 2, 4, 2, 0, 2, 7, 11, -1, -1, -1, -1 },
		{ 0, 1, 9, 4, 7, 8, 2, 3, 11, 5, 10, 6, -1, -1, -1, -1 },
		{ 9, 2, 1, 9, 11, 2, 9, 4, 11, 7, 11, 4, 5, 10, 6, -1 },
		{ 8, 4, 7, 3, 11, 5, 3, 5, 1, 5, 11, 6, -1, -1, -1, -1 },
		{ 5, 1, 11, 5, 11, 6, 1, 0, 11, 7, 11, 4, 0, 4, 11, -1 },
		{ 0, 5, 9, 0, 6, 5, 0, 3, 6, 11, 6, 3, 8, 4, 7, -1 },
		{ 6, 5, 9, 6, 9, 11, 4, 7, 9, 7, 11, 9, -1, -1, -1, -1 },
		{ 10, 4, 9, 6, 4, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 4, 10, 6, 4, 9, 10, 0, 8, 3, -1, -1, -1, -1, -1, -1, -1 },
		{ 10, 0, 1, 10, 6, 0, 6, 4, 0, -1, -1, -1, -1, -1, -1, -1 },
		{ 8, 3, 1, 8, 1, 6, 8, 6, 4, 6, 1, 10, -1, -1, -1, -1 },
		{ 1, 4, 9, 1, 2, 4, 2, 6, 4, -1, -1, -1, -1, -1, -1, -1 },
		{ 3, 0, 8, 1, 2, 9, 2, 4, 9, 2, 6, 4, -1, -1, -1, -1 },
		{ 0, 2, 4, 4, 2, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 8, 3, 2, 8, 2, 4, 4, 2, 6, -1, -1, -1, -1, -1, -1, -1 },
		{ 10, 4, 9, 10, 6, 4, 11, 2, 3, -1, -1, -1, -1, -1, -1, -1 },
		{ 0, 8, 2, 2, 8, 11, 4, 9, 10, 4, 10, 6, -1, -1, -1, -1 },
		{ 3, 11, 2, 0, 1, 6, 0, 6, 4, 6, 1, 10, -1, -1, -1, -1 },
		{ 6, 4, 1, 6, 1, 10, 4, 8, 1, 2, 1, 11, 8, 11, 1, -1 },
		{ 9, 6, 4, 9, 3, 6, 9, 1, 3, 11, 6, 3, -1, -1, -1, -1 },
		{ 8, 11, 1, 8, 1, 0, 11, 6, 1, 9, 1, 4, 6, 4, 1, -1 },
		{ 3, 11, 6, 3, 6, 0, 0, 6, 4, -1, -1, -1, -1, -1, -1, -1 },
		{ 6, 4, 8, 11, 6, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 7, 10, 6, 7, 8, 10, 8, 9, 10, -1, -1, -1, -1, -1, -1, -1 },
		{ 0, 7, 3, 0, 10, 7, 0, 9, 10, 6, 7, 10, -1, -1, -1, -1 },
		{ 10, 6, 7, 1, 10, 7, 1, 7, 8, 1, 8, 0, -1, -1, -1, -1 },
		{ 10, 6, 7, 10, 7, 1, 1, 7, 3, -1, -1, -1, -1, -1, -1, -1 },
		{ 1, 2, 6, 1, 6, 8, 1, 8, 9, 8, 6, 7, -1, -1, -1, -1 },
		{ 2, 6, 9, 2, 9, 1, 6, 7, 9, 0, 9, 3, 7, 3, 9, -1 },
		{ 7, 8, 0, 7, 0, 6, 6, 0, 2, -1, -1, -1, -1, -1, -1, -1 },
		{ 7, 3, 2, 6, 7, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 2, 3, 11, 10, 6, 8, 10, 8, 9, 8, 6, 7, -1, -1, -1, -1 },
		{ 2, 0, 7, 2, 7, 11, 0, 9, 7, 6, 7, 10, 9, 10, 7, -1 },
		{ 1, 8, 0, 1, 7, 8, 1, 10, 7, 6, 7, 10, 2, 3, 11, -1 },
		{ 11, 2, 1, 11, 1, 7, 10, 6, 1, 6, 7, 1, -1, -1, -1, -1 },
		{ 8, 9, 6, 8, 6, 7, 9, 1, 6, 11, 6, 3, 1, 3, 6, -1 },
		{ 0, 9, 1, 11, 6, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 7, 8, 0, 7, 0, 6, 3, 11, 0, 11, 6, 0, -1, -1, -1, -1 },
		{ 7, 11, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 7, 6, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 3, 0, 8, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 0, 1, 9, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 8, 1, 9, 8, 3, 1, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1 },
		{ 10, 1, 2, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 1, 2, 10, 3, 0, 8, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1 },
		{ 2, 9, 0, 2, 10, 9, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1 },
		{ 6, 11, 7, 2, 10, 3, 10, 8, 3, 10, 9, 8, -1, -1, -1, -1 },
		{ 7, 2, 3, 6, 2, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 7, 0, 8, 7, 6, 0, 6, 2, 0, -1, -1, -1, -1, -1, -1, -1 },
		{ 2, 7, 6, 2, 3, 7, 0, 1, 9, -1, -1, -1, -1, -1, -1, -1 },
		{ 1, 6, 2, 1, 8, 6, 1, 9, 8, 8, 7, 6, -1, -1, -1, -1 },
		{ 10, 7, 6, 10, 1, 7, 1, 3, 7, -1, -1, -1, -1, -1, -1, -1 },
		{ 10, 7, 6, 1, 7, 10, 1, 8, 7, 1, 0, 8, -1, -1, -1, -1 },
		{ 0, 3, 7, 0, 7, 10, 0, 10, 9, 6, 10, 7, -1, -1, -1, -1 },
		{ 7, 6, 10, 7, 10, 8, 8, 10, 9, -1, -1, -1, -1, -1, -1, -1 },
		{ 6, 8, 4, 11, 8, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 3, 6, 11, 3, 0, 6, 0, 4, 6, -1, -1, -1, -1, -1, -1, -1 },
		{ 8, 6, 11, 8, 4, 6, 9, 0, 1, -1, -1, -1, -1, -1, -1, -1 },
		{ 9, 4, 6, 9, 6, 3, 9, 3, 1, 11, 3, 6, -1, -1, -1, -1 },
		{ 6, 8, 4, 6, 11, 8, 2, 10, 1, -1, -1, -1, -1, -1, -1, -1 },
		{ 1, 2, 10, 3, 0, 11, 0, 6, 11, 0, 4, 6, -1, -1, -1, -1 },
		{ 4, 11, 8, 4, 6, 11, 0, 2, 9, 2, 10, 9, -1, -1, -1, -1 },
		{ 10, 9, 3, 10, 3, 2, 9, 4, 3, 11, 3, 6, 4, 6, 3, -1 },
		{ 8, 2, 3, 8, 4, 2, 4, 6, 2, -1, -1, -1, -1, -1, -1, -1 },
		{ 0, 4, 2, 4, 6, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 1, 9, 0, 2, 3, 4, 2, 4, 6, 4, 3, 8, -1, -1, -1, -1 },
		{ 1, 9, 4, 1, 4, 2, 2, 4, 6, -1, -1, -1, -1, -1, -1, -1 },
		{ 8, 1, 3, 8, 6, 1, 8, 4, 6, 6, 10, 1, -1, -1, -1, -1 },
		{ 10, 1, 0, 10, 0, 6, 6, 0, 4, -1, -1, -1, -1, -1, -1, -1 },
		{ 4, 6, 3, 4, 3, 8, 6, 10, 3, 0, 3, 9, 10, 9, 3, -1 },
		{ 10, 9, 4, 6, 10, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 4, 9, 5, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 0, 8, 3, 4, 9, 5, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1 },
		{ 5, 0, 1, 5, 4, 0, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1 },
		{ 11, 7, 6, 8, 3, 4, 3, 5, 4, 3, 1, 5, -1, -1, -1, -1 },
		{ 9, 5, 4, 10, 1, 2, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1 },
		{ 6, 11, 7, 1, 2, 10, 0, 8, 3, 4, 9, 5, -1, -1, -1, -1 },
		{ 7, 6, 11, 5, 4, 10, 4, 2, 10, 4, 0, 2, -1, -1, -1, -1 },
		{ 3, 4, 8, 3, 5, 4, 3, 2, 5, 10, 5, 2, 11, 7, 6, -1 },
		{ 7, 2, 3, 7, 6, 2, 5, 4, 9, -1, -1, -1, -1, -1, -1, -1 },
		{ 9, 5, 4, 0, 8, 6, 0, 6, 2, 6, 8, 7, -1, -1, -1, -1 },
		{ 3, 6, 2, 3, 7, 6, 1, 5, 0, 5, 4, 0, -1, -1, -1, -1 },
		{ 6, 2, 8, 6, 8, 7, 2, 1, 8, 4, 8, 5, 1, 5, 8, -1 },
		{ 9, 5, 4, 10, 1, 6, 1, 7, 6, 1, 3, 7, -1, -1, -1, -1 },
		{ 1, 6, 10, 1, 7, 6, 1, 0, 7, 8, 7, 0, 9, 5, 4, -1 },
		{ 4, 0, 10, 4, 10, 5, 0, 3, 10, 6, 10, 7, 3, 7, 10, -1 },
		{ 7, 6, 10, 7, 10, 8, 5, 4, 10, 4, 8, 10, -1, -1, -1, -1 },
		{ 6, 9, 5, 6, 11, 9, 11, 8, 9, -1, -1, -1, -1, -1, -1, -1 },
		{ 3, 6, 11, 0, 6, 3, 0, 5, 6, 0, 9, 5, -1, -1, -1, -1 },
		{ 0, 11, 8, 0, 5, 11, 0, 1, 5, 5, 6, 11, -1, -1, -1, -1 },
		{ 6, 11, 3, 6, 3, 5, 5, 3, 1, -1, -1, -1, -1, -1, -1, -1 },
		{ 1, 2, 10, 9, 5, 11, 9, 11, 8, 11, 5, 6, -1, -1, -1, -1 },
		{ 0, 11, 3, 0, 6, 11, 0, 9, 6, 5, 6, 9, 1, 2, 10, -1 },
		{ 11, 8, 5, 11, 5, 6, 8, 0, 5, 10, 5, 2, 0, 2, 5, -1 },
		{ 6, 11, 3, 6, 3, 5, 2, 10, 3, 10, 5, 3, -1, -1, -1, -1 },
		{ 5, 8, 9, 5, 2, 8, 5, 6, 2, 3, 8, 2, -1, -1, -1, -1 },
		{ 9, 5, 6, 9, 6, 0, 0, 6, 2, -1, -1, -1, -1, -1, -1, -1 },
		{ 1, 5, 8, 1, 8, 0, 5, 6, 8, 3, 8, 2, 6, 2, 8, -1 },
		{ 1, 5, 6, 2, 1, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 1, 3, 6, 1, 6, 10, 3, 8, 6, 5, 6, 9, 8, 9, 6, -1 },
		{ 10, 1, 0, 10, 0, 6, 9, 5, 0, 5, 6, 0, -1, -1, -1, -1 },
		{ 0, 3, 8, 5, 6, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 10, 5, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 11, 5, 10, 7, 5, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 11, 5, 10, 11, 7, 5, 8, 3, 0, -1, -1, -1, -1, -1, -1, -1 },
		{ 5, 11, 7, 5, 10, 11, 1, 9, 0, -1, -1, -1, -1, -1, -1, -1 },
		{ 10, 7, 5, 10, 11, 7, 9, 8, 1, 8, 3, 1, -1, -1, -1, -1 },
		{ 11, 1, 2, 11, 7, 1, 7, 5, 1, -1, -1, -1, -1, -1, -1, -1 },
		{ 0, 8, 3, 1, 2, 7, 1, 7, 5, 7, 2, 11, -1, -1, -1, -1 },
		{ 9, 7, 5, 9, 2, 7, 9, 0, 2, 2, 11, 7, -1, -1, -1, -1 },
		{ 7, 5, 2, 7, 2, 11, 5, 9, 2, 3, 2, 8, 9, 8, 2, -1 },
		{ 2, 5, 10, 2, 3, 5, 3, 7, 5, -1, -1, -1, -1, -1, -1, -1 },
		{ 8, 2, 0, 8, 5, 2, 8, 7, 5, 10, 2, 5, -1, -1, -1, -1 },
		{ 9, 0, 1, 5, 10, 3, 5, 3, 7, 3, 10, 2, -1, -1, -1, -1 },
		{ 9, 8, 2, 9, 2, 1, 8, 7, 2, 10, 2, 5, 7, 5, 2, -1 },
		{ 1, 3, 5, 3, 7, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 0, 8, 7, 0, 7, 1, 1, 7, 5, -1, -1, -1, -1, -1, -1, -1 },
		{ 9, 0, 3, 9, 3, 5, 5, 3, 7, -1, -1, -1, -1, -1, -1, -1 },
		{ 9, 8, 7, 5, 9, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 5, 8, 4, 5, 10, 8, 10, 11, 8, -1, -1, -1, -1, -1, -1, -1 },
		{ 5, 0, 4, 5, 11, 0, 5, 10, 11, 11, 3, 0, -1, -1, -1, -1 },
		{ 0, 1, 9, 8, 4, 10, 8, 10, 11, 10, 4, 5, -1, -1, -1, -1 },
		{ 10, 11, 4, 10, 4, 5, 11, 3, 4, 9, 4, 1, 3, 1, 4, -1 },
		{ 2, 5, 1, 2, 8, 5, 2, 11, 8, 4, 5, 8, -1, -1, -1, -1 },
		{ 0, 4, 11, 0, 11, 3, 4, 5, 11, 2, 11, 1, 5, 1, 11, -1 },
		{ 0, 2, 5, 0, 5, 9, 2, 11, 5, 4, 5, 8, 11, 8, 5, -1 },
		{ 9, 4, 5, 2, 11, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 2, 5, 10, 3, 5, 2, 3, 4, 5, 3, 8, 4, -1, -1, -1, -1 },
		{ 5, 10, 2, 5, 2, 4, 4, 2, 0, -1, -1, -1, -1, -1, -1, -1 },
		{ 3, 10, 2, 3, 5, 10, 3, 8, 5, 4, 5, 8, 0, 1, 9, -1 },
		{ 5, 10, 2, 5, 2, 4, 1, 9, 2, 9, 4, 2, -1, -1, -1, -1 },
		{ 8, 4, 5, 8, 5, 3, 3, 5, 1, -1, -1, -1, -1, -1, -1, -1 },
		{ 0, 4, 5, 1, 0, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 8, 4, 5, 8, 5, 3, 9, 0, 5, 0, 3, 5, -1, -1, -1, -1 },
		{ 9, 4, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 4, 11, 7, 4, 9, 11, 9, 10, 11, -1, -1, -1, -1, -1, -1, -1 },
		{ 0, 8, 3, 4, 9, 7, 9, 11, 7, 9, 10, 11, -1, -1, -1, -1 },
		{ 1, 10, 11, 1, 11, 4, 1, 4, 0, 7, 4, 11, -1, -1, -1, -1 },
		{ 3, 1, 4, 3, 4, 8, 1, 10, 4, 7, 4, 11, 10, 11, 4, -1 },
		{ 4, 11, 7, 9, 11, 4, 9, 2, 11, 9, 1, 2, -1, -1, -1, -1 },
		{ 9, 7, 4, 9, 11, 7, 9, 1, 11, 2, 11, 1, 0, 8, 3, -1 },
		{ 11, 7, 4, 11, 4, 2, 2, 4, 0, -1, -1, -1, -1, -1, -1, -1 },
		{ 11, 7, 4, 11, 4, 2, 8, 3, 4, 3, 2, 4, -1, -1, -1, -1 },
		{ 2, 9, 10, 2, 7, 9, 2, 3, 7, 7, 4, 9, -1, -1, -1, -1 },
		{ 9, 10, 7, 9, 7, 4, 10, 2, 7, 8, 7, 0, 2, 0, 7, -1 },
		{ 3, 7, 10, 3, 10, 2, 7, 4, 10, 1, 10, 0, 4, 0, 10, -1 },
		{ 1, 10, 2, 8, 7, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 4, 9, 1, 4, 1, 7, 7, 1, 3, -1, -1, -1, -1, -1, -1, -1 },
		{ 4, 9, 1, 4, 1, 7, 0, 8, 1, 8, 7, 1, -1, -1, -1, -1 },
		{ 4, 0, 3, 7, 4, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 4, 8, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 9, 10, 8, 10, 11, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 3, 0, 9, 3, 9, 11, 11, 9, 10, -1, -1, -1, -1, -1, -1, -1 },
		{ 0, 1, 10, 0, 10, 8, 8, 10, 11, -1, -1, -1, -1, -1, -1, -1 },
		{ 3, 1, 10, 11, 3, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 1, 2, 11, 1, 11, 9, 9, 11, 8, -1, -1, -1, -1, -1, -1, -1 },
		{ 3, 0, 9, 3, 9, 11, 1, 2, 9, 2, 11, 9, -1, -1, -1, -1 },
		{ 0, 2, 11, 8, 0, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 3, 2, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 2, 3, 8, 2, 8, 10, 10, 8, 9, -1, -1, -1, -1, -1, -1, -1 },
		{ 9, 10, 2, 0, 9, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 2, 3, 8, 2, 8, 10, 0, 1, 8, 1, 10, 8, -1, -1, -1, -1 },
		{ 1, 10, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 1, 3, 8, 9, 1, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 0, 9, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ 0, 3, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
		{ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 } 
	};

	/*
	Determine the index into the edge table which
	tells us which vertices are inside of the surface
	*/
	int cubeindex = 0;
	if (grid.val[0] < isolevel) cubeindex |= 1;
	if (grid.val[1] < isolevel) cubeindex |= 2;
	if (grid.val[2] < isolevel) cubeindex |= 4;
	if (grid.val[3] < isolevel) cubeindex |= 8;
	if (grid.val[4] < isolevel) cubeindex |= 16;
	if (grid.val[5] < isolevel) cubeindex |= 32;
	if (grid.val[6] < isolevel) cubeindex |= 64;
	if (grid.val[7] < isolevel) cubeindex |= 128;

	/* Cube is entirely in/out of the surface */
	if (edgeTable[cubeindex] == 0)
		return;

	/* Find the vertices where the surface intersects the cube */
	glm::vec3 vertlist[12];
	if (edgeTable[cubeindex] & 1)
		vertlist[0] = vertexInterp(isolevel, grid.p[0], grid.p[1], grid.val[0], grid.val[1]);
	if (edgeTable[cubeindex] & 2)
		vertlist[1] = vertexInterp(isolevel, grid.p[1], grid.p[2], grid.val[1], grid.val[2]);
	if (edgeTable[cubeindex] & 4)
		vertlist[2] = vertexInterp(isolevel, grid.p[2], grid.p[3], grid.val[2], grid.val[3]);
	if (edgeTable[cubeindex] & 8)
		vertlist[3] = vertexInterp(isolevel, grid.p[3], grid.p[0], grid.val[3], grid.val[0]);
	if (edgeTable[cubeindex] & 16)
		vertlist[4] = vertexInterp(isolevel, grid.p[4], grid.p[5], grid.val[4], grid.val[5]);
	if (edgeTable[cubeindex] & 32)
		vertlist[5] = vertexInterp(isolevel, grid.p[5], grid.p[6], grid.val[5], grid.val[6]);
	if (edgeTable[cubeindex] & 64)
		vertlist[6] = vertexInterp(isolevel, grid.p[6], grid.p[7], grid.val[6], grid.val[7]);
	if (edgeTable[cubeindex] & 128)
		vertlist[7] = vertexInterp(isolevel, grid.p[7], grid.p[4], grid.val[7], grid.val[4]);
	if (edgeTable[cubeindex] & 256)
		vertlist[8] = vertexInterp(isolevel, grid.p[0], grid.p[4], grid.val[0], grid.val[4]);
	if (edgeTable[cubeindex] & 512)
		vertlist[9] = vertexInterp(isolevel, grid.p[1], grid.p[5], grid.val[1], grid.val[5]);
	if (edgeTable[cubeindex] & 1024)
		vertlist[10] = vertexInterp(isolevel, grid.p[2], grid.p[6], grid.val[2], grid.val[6]);
	if (edgeTable[cubeindex] & 2048)
		vertlist[11] = vertexInterp(isolevel, grid.p[3], grid.p[7], grid.val[3], grid.val[7]);

	/* Create the triangle */
	for (int i = 0; triTable[cubeindex][i] != -1; i += 3)
	{
		const glm::vec3& v0 = vertlist[triTable[cubeindex][i]];
		const glm::vec3& v1 = vertlist[triTable[cubeindex][i + 1]];
		const glm::vec3& v2 = vertlist[triTable[cubeindex][i + 2]];

		glm::vec3 normal = glm::normalize(glm::cross(v1 - v0, v2 - v0));

		vertices.push_back(v0);
		vertices.push_back(v1);
		vertices.push_back(v2);

		normals.push_back(normal);
		normals.push_back(normal);
		normals.push_back(normal);
	}
}

void initChunkBuffers(
	uint32_t lodLevel,
	const glm::i32vec3& origin,
	glm::vec3** outPositions,
	glm::vec3** outNormals,
	size_t* outVertexCount)
{
	ZoneScoped;

	const uint32_t lodSideSize = ChunkSideSize >> lodLevel;
	const uint32_t lodBlockCount = lodSideSize * lodSideSize * lodSideSize;
	const float sizeMultiplier = (float)(1 << lodLevel);

	const uint32_t sampleGridSideSize = lodSideSize + 1;
	const uint32_t sampleCount = sampleGridSideSize * sampleGridSideSize * sampleGridSideSize;

	// TODO: consider using bitmask if we want to skip the intersection point approximation in the triangulation
	std::unique_ptr<float[]> terrainSamples(new float[sampleCount]);

	{
		ZoneScopedN("Sample Terrain");

		const int32_t x = origin.z * (int32_t)lodSideSize;
		const int32_t y = origin.y * (int32_t)lodSideSize;
		const int32_t z = origin.x * (int32_t)lodSideSize;

		Terrain::sample(terrainSamples.get(), x, y, z, sampleGridSideSize, sampleGridSideSize, sampleGridSideSize, sizeMultiplier);
	}

	{
		ZoneScopedN("Triangulate");

		std::vector<glm::vec3> vertices;
		std::vector<glm::vec3> normals;

		vertices.reserve(lodBlockCount * 3);
		normals.reserve(lodBlockCount * 3);

		const uint32_t terrainSampleOffsetX = 1;
		const uint32_t terrainSampleOffsetY = sampleGridSideSize;
		const uint32_t terrainSampleOffsetZ = sampleGridSideSize * sampleGridSideSize;

		for (uint32_t i = 0; i < lodBlockCount; ++i)
		{
			const uint32_t ix = (i % lodSideSize);
			const uint32_t iy = (i / lodSideSize) % lodSideSize;
			const uint32_t iz = (i / lodSideSize) / lodSideSize;

			const float fl = sizeMultiplier;
			const float fx = (float)origin.x * (int32_t)ChunkSideSize + ix * sizeMultiplier;
			const float fy = (float)origin.y * (int32_t)ChunkSideSize + iy * sizeMultiplier;
			const float fz = (float)origin.z * (int32_t)ChunkSideSize + iz * sizeMultiplier;

			GridCell grid;
			grid.p[0] = glm::vec3(fx,      fy,      fz);
			grid.p[1] = glm::vec3(fx + fl, fy,      fz);
			grid.p[2] = glm::vec3(fx + fl, fy + fl, fz);
			grid.p[3] = glm::vec3(fx,      fy + fl, fz);
			grid.p[4] = glm::vec3(fx,      fy,      fz + fl);
			grid.p[5] = glm::vec3(fx + fl, fy,      fz + fl);
			grid.p[6] = glm::vec3(fx + fl, fy + fl, fz + fl);
			grid.p[7] = glm::vec3(fx,      fy + fl, fz + fl);

			const uint32_t terrainSampleIndex = (iz * sampleGridSideSize * sampleGridSideSize) + (iy * sampleGridSideSize) + ix;

			grid.val[0] = terrainSamples[terrainSampleIndex];
			grid.val[1] = terrainSamples[terrainSampleIndex + terrainSampleOffsetX];
			grid.val[2] = terrainSamples[terrainSampleIndex + terrainSampleOffsetX + terrainSampleOffsetY];
			grid.val[3] = terrainSamples[terrainSampleIndex +                        terrainSampleOffsetY];
			grid.val[4] = terrainSamples[terrainSampleIndex +                                               terrainSampleOffsetZ];
			grid.val[5] = terrainSamples[terrainSampleIndex + terrainSampleOffsetX +                        terrainSampleOffsetZ];
			grid.val[6] = terrainSamples[terrainSampleIndex + terrainSampleOffsetX + terrainSampleOffsetY + terrainSampleOffsetZ];
			grid.val[7] = terrainSamples[terrainSampleIndex +                        terrainSampleOffsetY + terrainSampleOffsetZ];

			polygonise(grid, 0.0, vertices, normals);
		}

		const auto vertexCount = vertices.size();

		*outVertexCount = vertexCount;

		if (vertexCount > 0) 
		{
			*outPositions = new glm::vec3[vertexCount];
			*outNormals = new glm::vec3[vertexCount];

			memcpy(*outPositions, vertices.data(), vertexCount * sizeof(glm::vec3));
			memcpy(*outNormals, normals.data(), vertexCount * sizeof(glm::vec3));
		}
		else 
		{
			*outPositions = nullptr;
			*outNormals = nullptr;
		}
	}
}

void World::_initVisualChunk(
	VisualChunk& vchunk,
	size_t vertexCount)
{
	ZoneScoped;

	vchunk.vertexCount = static_cast<uint32_t>(vertexCount);

	// Create vertex buffers
	if (vertexCount > 0)
	{
		{
			VkBufferCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			createInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			createInfo.size = vertexCount * sizeof(glm::vec3);

			VmaAllocationCreateInfo allocInfo{};
			allocInfo.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			
			if (vmaCreateBuffer(m_chunkAllocator, &createInfo, &allocInfo, &vchunk.vertexBuffer, &vchunk.vertexBufferAlloc, nullptr) != VK_SUCCESS) {
				throw std::runtime_error("failed to create chunk vertex buffer!");
			}
		}
		{
			VkBufferCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			createInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			createInfo.size = vertexCount * sizeof(glm::vec3);

			VmaAllocationCreateInfo allocInfo{};
			allocInfo.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

			if (vmaCreateBuffer(m_chunkAllocator, &createInfo, &allocInfo, &vchunk.normalBuffer, &vchunk.normalBufferAlloc, nullptr) != VK_SUCCESS) {
				throw std::runtime_error("failed to create chunk vertex buffer!");
			}
		}
	}
}

void World::_freeChunkBuffers(VisualChunk& vchunk)
{
	vchunk.vertexCount = 0;

	m_deferredDeletes[m_frameIndex].push_back(DeferredChunkBufferDelete{ vchunk.vertexBuffer, vchunk.vertexBufferAlloc });
	m_deferredDeletes[m_frameIndex].push_back(DeferredChunkBufferDelete{ vchunk.normalBuffer, vchunk.normalBufferAlloc });
}

void World::_debugDrawChunkAllocator()
{
	VmaStats stats;
	vmaCalculateStats(m_chunkAllocator, &stats);

	const VkDeviceSize totalBytes = stats.total.usedBytes + stats.total.unusedBytes;
	const float usedBytesRatio = static_cast<float>(stats.total.usedBytes) / static_cast<float>(totalBytes);

	m_debugRenderer->drawRectangle2D(glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 16.0f / 720.0f), 0x222222);
	m_debugRenderer->drawRectangle2D(glm::vec2(0.0f, 0.0f), glm::vec2(usedBytesRatio, 16.0f / 720.0f), 0x00aa00);

	m_debugRenderer->drawRectangle2D(glm::vec2(0.0f, 16.0f / 720.0f), glm::vec2(1.0f, 32.0f / 720.0f), 0x111111);

	const float pixelWidth = 1.0f / static_cast<float>(graphics::resolution.width);

	const size_t chunkCount = m_chunks.count();
	for (size_t i = 0; i < chunkCount; ++i) 
	{
		const auto& vchunk = m_chunks.visuals[i];
		if (vchunk.vertexCount > 0) {
			{
				VmaAllocationInfo allocInfo;
				vmaGetAllocationInfo(m_chunkAllocator, vchunk.vertexBufferAlloc, &allocInfo);

				const float fracOffset = static_cast<float>(allocInfo.offset) / static_cast<float>(totalBytes);
				const float fracSize = std::max(static_cast<float>(allocInfo.size) / static_cast<float>(totalBytes), pixelWidth);

				m_debugRenderer->drawRectangle2D(glm::vec2(fracOffset, 16.0f / 720.0f), glm::vec2(fracOffset + fracSize, 32.0f / 720.0f), 0xffff00);
			}
			{
				VmaAllocationInfo allocInfo;
				vmaGetAllocationInfo(m_chunkAllocator, vchunk.normalBufferAlloc, &allocInfo);

				const float fracOffset = static_cast<float>(allocInfo.offset) / static_cast<float>(totalBytes);
				const float fracSize = std::max(static_cast<float>(allocInfo.size) / static_cast<float>(totalBytes), pixelWidth);

				m_debugRenderer->drawRectangle2D(glm::vec2(fracOffset, 16.0f / 720.0f), glm::vec2(fracOffset + fracSize, 32.0f / 720.0f), 0x00ffff);
			}
		}
	}
}
