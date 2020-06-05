#include "camera.hpp"

#include <input.hpp>

#include <tracy/Tracy.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>

#include <algorithm>

using namespace DirectX;

constexpr float MovementSpeed = 60.0f;
constexpr float LookSensitivity = 5.0f;

Camera::Camera()
	: m_position(0.0f, 0.0f, 0.0f)
	, m_yaw(0.0f)
	, m_pitch(0.0f)
	, m_fovInDegrees(90.0f)
	, m_nearClip(0.1f)
	, m_farClip(512.0f)
	, m_worldMatrix(1.0f)
	, m_worldToViewMatrix(1.0f)
	, m_viewToNDCMatrix(1.0f)
	, m_worldToNDCMatrix(1.0f)
{
}

void Camera::update(Input& input, float deltaTime)
{
	ZoneScoped;

	float input_move_right = 0.0f;
	float input_move_forward = 0.0f;

	float input_look_x = 0.0f;
	float input_look_y = 0.0f;

	float adjustedMovementSpeed = MovementSpeed;

	if (input.isMouseCaptured())
	{
		input_look_x = (float)input.getMouseDeltaX() / 300.0f;
		input_look_y = -(float)input.getMouseDeltaY() / 300.0f;

		if (input.isKeyDown(Input::Key_A) && !input.isKeyDown(Input::Key_D))
		{
			input_move_right = -1.0f;
		}
		else if (input.isKeyDown(Input::Key_D) && !input.isKeyDown(Input::Key_A))
		{
			input_move_right = 1.0f;
		}

		if (input.isKeyDown(Input::Key_W) && !input.isKeyDown(Input::Key_S))
		{
			input_move_forward = 1.0f;
		}
		else if (input.isKeyDown(Input::Key_S) && !input.isKeyDown(Input::Key_W))
		{
			input_move_forward = -1.0f;
		}

		if (input.isKeyDown(Input::Key_LeftControl))
		{
			adjustedMovementSpeed /= 5.0f;
		}
		if (input.isKeyDown(Input::Key_LeftShift))
		{
			adjustedMovementSpeed *= 5.0f;
		}
	}
	else if (input.isGamepadConnected())
	{
		GamepadState gamepad;
		input.getGamepadState(gamepad);

		input_move_right = gamepad.leftStick.x;
		input_move_forward = gamepad.leftStick.y;

		input_look_x = gamepad.rightStick.x * LookSensitivity * deltaTime;
		input_look_y = gamepad.rightStick.y * LookSensitivity * deltaTime;

		if (gamepad.isButtonDown(GamepadButton::LeftShoulder))
		{
			adjustedMovementSpeed /= 5.0f;
		}
		if (gamepad.isButtonDown(GamepadButton::RightShoulder))
		{
			adjustedMovementSpeed *= 5.0f;
		}
	}

	m_yaw -= input_look_x;
	m_pitch -= input_look_y;

	m_pitch = std::max(m_pitch, glm::radians(-90.0f));
	m_pitch = std::min(m_pitch, glm::radians(90.0f));

	glm::mat4 cameraRot(1.0f);
	cameraRot = glm::rotate(cameraRot, m_yaw, glm::vec3(0.0f, 1.0f, 0.0f));
	cameraRot = glm::rotate(cameraRot, m_pitch, glm::vec3(1.0f, 0.0f, 0.0f));

	m_position += glm::vec3(cameraRot[0]) * input_move_right * adjustedMovementSpeed * deltaTime;
	m_position += glm::vec3(cameraRot[2]) * -input_move_forward * adjustedMovementSpeed * deltaTime;

	const float fovInRadians = glm::radians(m_fovInDegrees);

	const uint32_t screenWidth = 1280;
	const uint32_t screenHeight = 720;

	m_worldMatrix = glm::translate(glm::mat4(1.0f), m_position) * cameraRot;

	m_worldToViewMatrix = glm::inverse(m_worldMatrix);
	m_viewToNDCMatrix = glm::perspectiveFov(
		fovInRadians, 
		static_cast<float>(screenWidth), 
		static_cast<float>(screenHeight),
		m_nearClip,
		m_farClip);
	m_worldToNDCMatrix = m_viewToNDCMatrix * m_worldToViewMatrix;
}
