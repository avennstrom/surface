#include "debug_renderer.hpp"

#include <graphics.hpp>

#include <glm/ext/matrix_transform.hpp>

#include <tracy/Tracy.hpp>

#include <fstream>

#define BUFFERED_FRAMES 3

struct DebugConstantBuffer
{
	glm::mat4 u_localToNDCMatrix;
};

DebugRenderer::DebugRenderer(VkDescriptorPool descriptorPool)
	: m_debugVertexBufferSize(32 * 1024 * 1024)
{
	_createDebugUniformBuffer();
	_createDebugDescriptorSetLayout();
	_createDebugDescriptorSet(descriptorPool);
	_createDebugPipeline();
	_createDebugStagingBuffers();
	_createDebugVertexBuffer();
}

void DebugRenderer::updateBuffers(VkCommandBuffer cb, const Camera& camera)
{
	ZoneScoped;

	m_frameIndex = (m_frameIndex + 1) % 3;

	DebugConstantBuffer constants;
	constants.u_localToNDCMatrix = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f, -1.0f, 0.0f)), glm::vec3(2.0f, 2.0f, 1.0f));
	vkCmdUpdateBuffer(cb, m_debugUniformBuffer[DBG_UNIFORM_BUFFER_2D], 0, sizeof(DebugConstantBuffer), &constants);

	constants.u_localToNDCMatrix = camera.getWorldToNDCMatrix();
	vkCmdUpdateBuffer(cb, m_debugUniformBuffer[DBG_UNIFORM_BUFFER_3D], 0, sizeof(DebugConstantBuffer), &constants);

	VkBuffer stagingBuffer = m_debugStagingBuffer[m_frameIndex];
	VkDeviceMemory stagingBufferMemory = m_debugStagingBufferMemory[m_frameIndex];

	size_t totalVertexCount = 0;
	for (size_t i = 0; i < DBG_VERTEX_BUFFER_COUNT; ++i)
	{
		const auto& vertices = m_debugVertices[i];
		m_debugVertexOffset[i] = static_cast<uint32_t>(totalVertexCount);
		m_debugVertexCount[i] = static_cast<uint32_t>(vertices.size());
		totalVertexCount += vertices.size();
	}

	const size_t totalBufferSize = totalVertexCount * sizeof(DebugVertex);

	void* mappedMemory;
	if (vkMapMemory(graphics::device, stagingBufferMemory, 0, totalBufferSize, 0, &mappedMemory) != VK_SUCCESS) {
		throw std::runtime_error("failed to map chunk staging buffer!");
	}

	for (size_t i = 0; i < DBG_VERTEX_BUFFER_COUNT; ++i)
	{
		const auto& vertices = m_debugVertices[i];
		const uint32_t offset = m_debugVertexOffset[i] * sizeof(DebugVertex);
		const uint32_t size = m_debugVertexCount[i] * sizeof(DebugVertex);
		if (size > 0)
		{
			memcpy((char*)mappedMemory + offset, vertices.data(), size);
		}
	}

	vkUnmapMemory(graphics::device, stagingBufferMemory);

	VkBufferCopy vertexBufferCopy;
	vertexBufferCopy.srcOffset = 0;
	vertexBufferCopy.dstOffset = 0;
	vertexBufferCopy.size = totalBufferSize;
	vkCmdCopyBuffer(cb, stagingBuffer, m_debugVertexBuffer, 1, &vertexBufferCopy);
}

void DebugRenderer::draw(VkCommandBuffer cb)
{
	ZoneScoped;

	VkBuffer vertexBuffers[1] = {
		m_debugVertexBuffer,
	};

	VkDeviceSize vertexBufferOffsets[1] = {
		0,
	};

	vkCmdBindVertexBuffers(cb, 0, 1, vertexBuffers, vertexBufferOffsets);

	VkDescriptorSet descriptorSets[DBG_VERTEX_BUFFER_COUNT] = {
		m_debugDescriptorSet[DBG_UNIFORM_BUFFER_2D],
		m_debugDescriptorSet[DBG_UNIFORM_BUFFER_3D],
		m_debugDescriptorSet[DBG_UNIFORM_BUFFER_2D],
		m_debugDescriptorSet[DBG_UNIFORM_BUFFER_3D],
	};

	for (size_t i = 0; i < DBG_VERTEX_BUFFER_COUNT; ++i)
	{
		const uint32_t offset = m_debugVertexOffset[i];
		const uint32_t count = m_debugVertexCount[i];

		if (count > 0)
		{
			vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_debugPipelineLayout, 0, 1, &descriptorSets[i], 0, nullptr);
			vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_debugPipeline[i]);
			vkCmdDraw(cb, count, 1, offset, 0);
		}
	}

	for (auto&& vertices : m_debugVertices)
	{
		vertices.clear();
	}
}

void DebugRenderer::drawLine2D(const glm::vec2& from, const glm::vec2& to, uint32_t color)
{
	m_debugVertices[DBG_VERTEX_BUFFER_LINES_2D].push_back({ glm::vec3(from, 0.0f), color });
	m_debugVertices[DBG_VERTEX_BUFFER_LINES_2D].push_back({ glm::vec3(to, 0.0f), color });
}

void DebugRenderer::drawLine3D(const glm::vec3& from, const glm::vec3& to, uint32_t color)
{
	m_debugVertices[DBG_VERTEX_BUFFER_LINES_3D].push_back({ from, color });
	m_debugVertices[DBG_VERTEX_BUFFER_LINES_3D].push_back({ to, color });
}

void DebugRenderer::drawTriangle2D(const glm::vec2& v0, const glm::vec2& v1, const glm::vec2& v2, uint32_t color)
{
	m_debugVertices[DBG_VERTEX_BUFFER_TRIS_2D].push_back({ glm::vec3(v0, 0.0f), color });
	m_debugVertices[DBG_VERTEX_BUFFER_TRIS_2D].push_back({ glm::vec3(v1, 0.0f), color });
	m_debugVertices[DBG_VERTEX_BUFFER_TRIS_2D].push_back({ glm::vec3(v2, 0.0f), color });
}

void DebugRenderer::drawTriangle3D(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, uint32_t color)
{
	m_debugVertices[DBG_VERTEX_BUFFER_TRIS_3D].push_back({ v0, color });
	m_debugVertices[DBG_VERTEX_BUFFER_TRIS_3D].push_back({ v1, color });
	m_debugVertices[DBG_VERTEX_BUFFER_TRIS_3D].push_back({ v2, color });
}

void DebugRenderer::drawRectangle2D(const glm::vec2& min, const glm::vec2& max, uint32_t color)
{
	m_debugVertices[DBG_VERTEX_BUFFER_TRIS_2D].push_back({ glm::vec3(min.x, min.y, 0.0f), color });
	m_debugVertices[DBG_VERTEX_BUFFER_TRIS_2D].push_back({ glm::vec3(max.x, min.y, 0.0f), color });
	m_debugVertices[DBG_VERTEX_BUFFER_TRIS_2D].push_back({ glm::vec3(max.x, max.y, 0.0f), color });

	m_debugVertices[DBG_VERTEX_BUFFER_TRIS_2D].push_back({ glm::vec3(min.x, min.y, 0.0f), color });
	m_debugVertices[DBG_VERTEX_BUFFER_TRIS_2D].push_back({ glm::vec3(max.x, max.y, 0.0f), color });
	m_debugVertices[DBG_VERTEX_BUFFER_TRIS_2D].push_back({ glm::vec3(min.x, max.y, 0.0f), color });
}

void DebugRenderer::_createDebugUniformBuffer()
{
	for (size_t i = 0; i < DBG_UNIFORM_BUFFER_COUNT; ++i)
	{
		VkBufferCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		createInfo.size = sizeof(DebugConstantBuffer);
		createInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

		if (vkCreateBuffer(graphics::device, &createInfo, nullptr, &m_debugUniformBuffer[i]) != VK_SUCCESS) {
			throw std::runtime_error("failed to create uniform buffer!");
		}

		VkMemoryRequirements memoryRequirements;
		vkGetBufferMemoryRequirements(graphics::device, m_debugUniformBuffer[i], &memoryRequirements);

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memoryRequirements.size;
		allocInfo.memoryTypeIndex = graphics::findMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		if (vkAllocateMemory(graphics::device, &allocInfo, nullptr, &m_debugUniformBufferMemory[i]) != VK_SUCCESS) {
			throw std::runtime_error("failed to allocate uniform buffer memory!");
		}

		if (vkBindBufferMemory(graphics::device, m_debugUniformBuffer[i], m_debugUniformBufferMemory[i], 0) != VK_SUCCESS) {
			throw std::runtime_error("failed to bind uniform buffer memory!");
		}
	}
}

void DebugRenderer::_createDebugDescriptorSetLayout()
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

	if (vkCreateDescriptorSetLayout(graphics::device, &descriptorCreateInfo, nullptr, &m_debugDescriptorSetLayout) != VK_SUCCESS) {
		throw new std::runtime_error("failed to create descriptor set layout!");
	}
}

void DebugRenderer::_createDebugDescriptorSet(VkDescriptorPool descriptorPool)
{
	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &m_debugDescriptorSetLayout;

	for (size_t i = 0; i < DBG_UNIFORM_BUFFER_COUNT; ++i)
	{
		if (vkAllocateDescriptorSets(graphics::device, &allocInfo, &m_debugDescriptorSet[i]) != VK_SUCCESS) {
			throw new std::runtime_error("failed to allocate descriptor sets!");
		}

		VkDescriptorBufferInfo writeBufferInfo;
		writeBufferInfo.buffer = m_debugUniformBuffer[i];
		writeBufferInfo.offset = 0;
		writeBufferInfo.range = sizeof(DebugConstantBuffer);

		VkWriteDescriptorSet descriptorWrites[1]{};
		descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[0].dstBinding = 0;
		descriptorWrites[0].dstSet = m_debugDescriptorSet[i];
		descriptorWrites[0].descriptorCount = 1;
		descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorWrites[0].pBufferInfo = &writeBufferInfo;

		vkUpdateDescriptorSets(graphics::device, 1, descriptorWrites, 0, nullptr);
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

void DebugRenderer::_createDebugPipeline()
{
	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &m_debugDescriptorSetLayout;

	if (vkCreatePipelineLayout(graphics::device, &pipelineLayoutInfo, nullptr, &m_debugPipelineLayout) != VK_SUCCESS) {
		throw std::runtime_error("failed to create pipeline layout!");
	}

	auto vertShaderCode = readFile("shaders/debug_vs");
	auto fragShaderCode = readFile("shaders/debug_ps");

	VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
	VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

	const VkPrimitiveTopology topologies[DBG_VERTEX_BUFFER_COUNT] = {
		VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
		VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	};

	const VkBool32 depthTestEnable[DBG_VERTEX_BUFFER_COUNT] = {
		VK_FALSE,
		VK_TRUE,
		VK_FALSE,
		VK_TRUE,
	};

	for (size_t i = 0; i < DBG_VERTEX_BUFFER_COUNT; ++i)
	{
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

		VkVertexInputBindingDescription vertexBindings[1]{};
		VkVertexInputAttributeDescription vertexAttributes[2]{};

		vertexBindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		vertexBindings[0].binding = 0;
		vertexBindings[0].stride = sizeof(DebugVertex);

		vertexAttributes[0].binding = 0;
		vertexAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		vertexAttributes[0].location = 0;
		vertexAttributes[0].offset = 0;

		vertexAttributes[1].binding = 0;
		vertexAttributes[1].format = VK_FORMAT_A8B8G8R8_UNORM_PACK32;
		vertexAttributes[1].location = 1;
		vertexAttributes[1].offset = 12;

		VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = vertexBindings;
		vertexInputInfo.vertexAttributeDescriptionCount = 2;
		vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes;

		VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology = topologies[i];
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
		scissor.extent = { graphics::resolution.width / 2, graphics::resolution.height / 2 };

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
		rasterizer.cullMode = VK_CULL_MODE_NONE;
		rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
		rasterizer.depthBiasEnable = VK_FALSE;

		VkPipelineMultisampleStateCreateInfo multisampling{};
		multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_2_BIT; //TODO

		VkPipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
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
		depthStencilState.depthTestEnable = depthTestEnable[i];
		depthStencilState.depthWriteEnable = VK_FALSE;
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
		pipelineInfo.layout = m_debugPipelineLayout;
		pipelineInfo.renderPass = graphics::colorPass;
		pipelineInfo.subpass = 0;
		pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

		if (vkCreateGraphicsPipelines(graphics::device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_debugPipeline[i]) != VK_SUCCESS) {
			throw std::runtime_error("failed to create debug pipeline!");
		}
	}

	vkDestroyShaderModule(graphics::device, fragShaderModule, nullptr);
	vkDestroyShaderModule(graphics::device, vertShaderModule, nullptr);
}

void DebugRenderer::_createDebugStagingBuffers()
{
	for (size_t i = 0; i < BUFFERED_FRAMES; ++i) {
		VkBufferCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		createInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		createInfo.size = m_debugVertexBufferSize;

		if (vkCreateBuffer(graphics::device, &createInfo, nullptr, &m_debugStagingBuffer[i]) != VK_SUCCESS) {
			throw std::runtime_error("failed to create staging buffer!");
		}

		VkMemoryRequirements memoryRequirements;
		vkGetBufferMemoryRequirements(graphics::device, m_debugStagingBuffer[i], &memoryRequirements);

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memoryRequirements.size;
		allocInfo.memoryTypeIndex = graphics::findMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		if (vkAllocateMemory(graphics::device, &allocInfo, nullptr, &m_debugStagingBufferMemory[i]) != VK_SUCCESS) {
			throw std::runtime_error("failed to allocate staging buffer memory!");
		}

		if (vkBindBufferMemory(graphics::device, m_debugStagingBuffer[i], m_debugStagingBufferMemory[i], 0) != VK_SUCCESS) {
			throw std::runtime_error("failed to bind staging buffer memory!");
		}
	}
}

void DebugRenderer::_createDebugVertexBuffer()
{
	VkBufferCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	createInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	createInfo.size = m_debugVertexBufferSize;

	if (vkCreateBuffer(graphics::device, &createInfo, nullptr, &m_debugVertexBuffer) != VK_SUCCESS) {
		throw std::runtime_error("failed to create staging buffer!");
	}

	VkMemoryRequirements memoryRequirements;
	vkGetBufferMemoryRequirements(graphics::device, m_debugVertexBuffer, &memoryRequirements);

	VkMemoryAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memoryRequirements.size;
	allocInfo.memoryTypeIndex = graphics::findMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	if (vkAllocateMemory(graphics::device, &allocInfo, nullptr, &m_debugVertexBufferMemory) != VK_SUCCESS) {
		throw std::runtime_error("failed to allocate staging buffer memory!");
	}

	if (vkBindBufferMemory(graphics::device, m_debugVertexBuffer, m_debugVertexBufferMemory, 0) != VK_SUCCESS) {
		throw std::runtime_error("failed to bind staging buffer memory!");
	}
}
