#pragma once

#include <camera.hpp>
#include <world.hpp>

#include <memory>

class Input;

class Game
{
public:
	void init();
	void update(Input& input, float deltaTime);
	void draw();
	void resizeBuffers(uint32_t width, uint32_t height);

private:
	Camera m_camera;

	std::unique_ptr<World> m_world;
};