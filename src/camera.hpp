#pragma once

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

class Input;

class Camera
{
public:
	Camera();

	void update(Input& input, float deltaTime);

	inline const glm::mat4& getWorldMatrix() const { return m_worldMatrix; };
	inline const glm::mat4& getWorldToViewMatrix() const { return m_worldToViewMatrix; }
	inline const glm::mat4& getViewToNDCMatrix() const { return m_viewToNDCMatrix; }
	inline const glm::mat4& getWorldToNDCMatrix() const { return m_worldToNDCMatrix; }

	inline const glm::vec3& getPosition() const { return m_position; }

	inline const float getFarClip() const { return m_farClip; }

private:
	glm::mat4 m_worldMatrix;
	glm::mat4 m_worldToViewMatrix;
	glm::mat4 m_viewToNDCMatrix;
	glm::mat4 m_worldToNDCMatrix;

	glm::vec3 m_position;
	float m_yaw;
	float m_pitch;
	float m_fovInDegrees;
	float m_nearClip;
	float m_farClip;
};