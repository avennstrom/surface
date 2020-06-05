#include "game.hpp"

void Game::init()
{
	m_world = std::make_unique<World>();
	m_world->init();
}

void Game::update(Input& input, float deltaTime)
{
	m_world->update(deltaTime, input);
}

void Game::draw()
{
    VkCommandBuffer cb = graphics::currentCommandBuffer();

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(cb, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }

	m_world->draw();

    if (vkEndCommandBuffer(cb) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }
}

void Game::resizeBuffers(uint32_t width, uint32_t height)
{
	m_world->resizeBuffers(width, height);
}
