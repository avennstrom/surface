#pragma once

#include <input.hpp>

#include <Windows.h>

#include <bitset>

class WindowsInput final : public Input
{
public:
	WindowsInput(HWND hwnd);

	bool wndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

	void resetMouseDelta();
	void captureMouse();
	void releaseMouse();

	bool isMouseCaptured() override;
	int32_t getMouseDeltaX() override;
	int32_t getMouseDeltaY() override;

	bool keyPressed(KeyCode key) override;
	bool keyReleased(KeyCode key) override;
	bool isKeyDown(KeyCode key) override;

	bool isGamepadConnected() override;
	bool getGamepadState(GamepadState& outState) override;

private:
	HWND m_hwnd;
	bool m_isMouseCaptured;
	int32_t m_mouseDeltaX;
	int32_t m_mouseDeltaY;

	std::bitset<KeyCount> m_keyState;
	std::bitset<KeyCount> m_prevKeyState;
};