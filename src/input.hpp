#pragma once

#include <DirectXMath.h>

#include <cinttypes>
#include <bitset>

enum class GamepadButton
{
	DPadUp,
	DPadDown,
	DPadLeft,
	DPadRight,
	Start,
	Back,
	LeftThumb,
	RightThumb,
	LeftShoulder,
	RightShoulder,
	A,
	B,
	X,
	Y,
};

struct GamepadState
{
public:
	DirectX::XMFLOAT2 leftStick;
	DirectX::XMFLOAT2 rightStick;

	bool isButtonDown(GamepadButton button)
	{
		return m_buttonState[static_cast<size_t>(button)];
	}

//private:
	std::bitset<14> m_buttonState;
};

class Input abstract
{
public:
	enum KeyCode
	{
		Key_A,
		Key_B,
		Key_C,
		Key_D,
		Key_E,
		Key_F,
		Key_G,
		Key_H,
		Key_I,
		Key_J,
		Key_K,
		Key_L,
		Key_M,
		Key_N,
		Key_O,
		Key_P,
		Key_Q,
		Key_R,
		Key_S,
		Key_T,
		Key_U,
		Key_V,
		Key_W,
		Key_X,
		Key_Y,
		Key_Z,
		Key_0,
		Key_1,
		Key_2,
		Key_3,
		Key_4,
		Key_5,
		Key_6,
		Key_7,
		Key_8,
		Key_9,
		Key_F1,
		Key_F2,
		Key_F3,
		Key_F4,
		Key_F5,
		Key_F6,
		Key_F7,
		Key_F8,
		Key_F9,
		Key_F10,
		Key_F11,
		Key_F12,
		Key_LeftShift,
		Key_RightShift,
		Key_LeftControl,
		Key_RightControl,
		Key_Space,
		Key_Backspace,
		Key_Escape,
		Key_Tab,
		Key_LeftAlt,
		Key_LeftArrow,
		Key_RightArrow,
		Key_UpArrow,
		Key_DownArrow,
		KeyCount,
	};

	virtual bool isMouseCaptured() = 0;
	virtual int32_t getMouseDeltaX() = 0;
	virtual int32_t getMouseDeltaY() = 0;

	virtual bool keyPressed(KeyCode key) = 0;
	virtual bool keyReleased(KeyCode key) = 0;
	virtual bool isKeyDown(KeyCode key) = 0;

	virtual bool isGamepadConnected() = 0;
	virtual bool getGamepadState(GamepadState& outState) = 0;
};