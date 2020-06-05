#include "input_windows.hpp"

#include <Xinput.h>

#include <vector>

#pragma comment(lib, "xinput9_1_0")

using namespace DirectX;

static std::vector<Input::KeyCode> g_keyMap(256);

WindowsInput::WindowsInput(HWND hwnd)
	: m_hwnd(hwnd)
	, m_isMouseCaptured(false)
	, m_mouseDeltaX(0)
	, m_mouseDeltaY(0)
{
	std::fill(g_keyMap.begin(), g_keyMap.end(), (Input::KeyCode)-1);

	g_keyMap['A'] = Key_A;
	g_keyMap['B'] = Key_B;
	g_keyMap['C'] = Key_C;
	g_keyMap['D'] = Key_D;
	g_keyMap['E'] = Key_E;
	g_keyMap['F'] = Key_F;
	g_keyMap['G'] = Key_G;
	g_keyMap['H'] = Key_H;
	g_keyMap['I'] = Key_I;
	g_keyMap['J'] = Key_J;
	g_keyMap['K'] = Key_K;
	g_keyMap['L'] = Key_L;
	g_keyMap['M'] = Key_M;
	g_keyMap['N'] = Key_N;
	g_keyMap['O'] = Key_O;
	g_keyMap['P'] = Key_P;
	g_keyMap['Q'] = Key_Q;
	g_keyMap['R'] = Key_R;
	g_keyMap['S'] = Key_S;
	g_keyMap['T'] = Key_T;
	g_keyMap['U'] = Key_U;
	g_keyMap['V'] = Key_V;
	g_keyMap['W'] = Key_W;
	g_keyMap['X'] = Key_X;
	g_keyMap['Y'] = Key_Y;
	g_keyMap['Z'] = Key_Z;

	g_keyMap[VK_F1] = Key_F1;
	g_keyMap[VK_F2] = Key_F2;
	g_keyMap[VK_F3] = Key_F3;
	g_keyMap[VK_F4] = Key_F4;
	g_keyMap[VK_F5] = Key_F5;
	g_keyMap[VK_F6] = Key_F6;
	g_keyMap[VK_F7] = Key_F7;
	g_keyMap[VK_F8] = Key_F8;
	g_keyMap[VK_F9] = Key_F9;
	g_keyMap[VK_F10] = Key_F10;
	g_keyMap[VK_F11] = Key_F11;
	g_keyMap[VK_F12] = Key_F12;

	g_keyMap[VK_LEFT] = Key_LeftArrow;
	g_keyMap[VK_RIGHT] = Key_RightArrow;
	g_keyMap[VK_UP] = Key_UpArrow;
	g_keyMap[VK_DOWN] = Key_DownArrow;
	g_keyMap[VK_SPACE] = Key_Space;
	g_keyMap[VK_LCONTROL] = Key_LeftControl;
	g_keyMap[VK_RCONTROL] = Key_RightControl;
	g_keyMap[VK_CONTROL] = Key_LeftControl;
	g_keyMap[VK_LSHIFT] = Key_LeftShift;
	g_keyMap[VK_RSHIFT] = Key_RightShift;
	g_keyMap[VK_SHIFT] = Key_LeftShift;
	g_keyMap[VK_MENU] = Key_LeftAlt;
}

static Input::KeyCode getKeyCodeFromVKey(USHORT vkey)
{
	return g_keyMap[vkey];
}

bool WindowsInput::wndProc(HWND /*hwnd*/, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		case WM_INPUT:
		{
			UINT dwSize;
			if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER)) == (UINT)-1)
			{
				return false;
			}

			std::vector<BYTE> lpb(dwSize);

			if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, lpb.data(), &dwSize, sizeof(RAWINPUTHEADER)) != dwSize)
			{
				OutputDebugString(TEXT("GetRawInputData does not return correct size !\n"));
				return false;
			}

			RAWINPUT* raw = (RAWINPUT*)lpb.data();

			if (raw->header.dwType == RIM_TYPEMOUSE)
			{
				if (raw->data.mouse.usFlags == MOUSE_MOVE_RELATIVE)
				{
					const long dx = raw->data.mouse.lLastX;
					const long dy = raw->data.mouse.lLastY;

					m_mouseDeltaX += dx;
					m_mouseDeltaY += dy;
				}
			}
			else if (raw->header.dwType == RIM_TYPEKEYBOARD)
			{
				const KeyCode keyCode = getKeyCodeFromVKey(raw->data.keyboard.VKey);
				if (keyCode == (KeyCode)-1)
				{
					return false;
				}

				if (raw->data.keyboard.Flags == RI_KEY_MAKE)
				{
					if (raw->data.keyboard.VKey == VK_F4)
					{
						ExitProcess(1);
					}

					m_keyState.set(keyCode, true);
				}
				else if (raw->data.keyboard.Flags == RI_KEY_BREAK)
				{
					m_keyState.set(keyCode, false);
				}
			}

			return true;
		}

		case WM_INPUT_DEVICE_CHANGE:
		{
			if (wParam == GIDC_REMOVAL)
			{
				releaseMouse();
			}
			return true;
		}

		case WM_LBUTTONDOWN:
		{
			captureMouse();
			return true;
		}

		case WM_KILLFOCUS:
		{
			releaseMouse();
			return true;
		}
	}

	return false;
}

void WindowsInput::resetMouseDelta()
{
	for (size_t i = 0; i < m_keyState.size(); ++i)
	{
		m_prevKeyState.set(i, m_keyState[i]);
	}

	m_mouseDeltaX = 0;
	m_mouseDeltaY = 0;
}

void WindowsInput::captureMouse()
{
	if (m_isMouseCaptured)
	{
		return;
	}

	SetForegroundWindow(m_hwnd);

	RAWINPUTDEVICE rid[2];

	// Mouse.
	rid[0].usUsagePage = 0x01;
	rid[0].usUsage = 0x02;
	rid[0].dwFlags = RIDEV_NOLEGACY | RIDEV_CAPTUREMOUSE | RIDEV_DEVNOTIFY;
	rid[0].hwndTarget = m_hwnd;

	// Keyboard.
	rid[1].usUsagePage = 0x01;
	rid[1].usUsage = 0x06;
	rid[1].dwFlags = RIDEV_NOLEGACY | RIDEV_DEVNOTIFY;
	rid[1].hwndTarget = m_hwnd;

	if (FALSE == RegisterRawInputDevices(rid, _countof(rid), sizeof(RAWINPUTDEVICE)))
	{
		return;
	}

	ShowCursor(FALSE);
	m_isMouseCaptured = true;
}

void WindowsInput::releaseMouse()
{
	if (!m_isMouseCaptured)
	{
		return;
	}

	RAWINPUTDEVICE rid[2];

	// Mouse.
	rid[0].usUsagePage = 0x01;
	rid[0].usUsage = 0x02;
	rid[0].dwFlags = RIDEV_REMOVE;
	rid[0].hwndTarget = NULL;

	// Keyboard.
	rid[1].usUsagePage = 0x01;
	rid[1].usUsage = 0x06;
	rid[1].dwFlags = RIDEV_REMOVE;
	rid[1].hwndTarget = NULL;

	RegisterRawInputDevices(rid, _countof(rid), sizeof(RAWINPUTDEVICE));

	ShowCursor(TRUE);

	RECT windowRect;
	GetWindowRect(m_hwnd, &windowRect);
	SetCursorPos((windowRect.left + windowRect.right) / 2, (windowRect.top + windowRect.bottom) / 2);

	m_isMouseCaptured = false;
}

bool WindowsInput::isMouseCaptured()
{
	return m_isMouseCaptured;
}

int32_t WindowsInput::getMouseDeltaX()
{
	return m_mouseDeltaX;
}

int32_t WindowsInput::getMouseDeltaY()
{
	return m_mouseDeltaY;
}

bool WindowsInput::keyPressed(KeyCode key)
{
	return m_keyState[key] && !m_prevKeyState[key];
}

bool WindowsInput::keyReleased(KeyCode key)
{
	return !m_keyState[key] && m_prevKeyState[key];
}

bool WindowsInput::isKeyDown(KeyCode key)
{
	return m_keyState[key];
}

bool WindowsInput::isGamepadConnected()
{
	XINPUT_STATE xstate;
	if (XInputGetState(0, &xstate) == ERROR_DEVICE_NOT_CONNECTED)
	{
		return false;
	}

	return true;
}

bool WindowsInput::getGamepadState(GamepadState& outState)
{
	XINPUT_STATE xstate;
	if (FAILED(XInputGetState(0, &xstate))) {
		return false;
	}

	// Left stick.
	XMVECTOR v_ls{ xstate.Gamepad.sThumbLX / (float)0xffff, xstate.Gamepad.sThumbLY / (float)0xffff };
	float ls_len = XMVectorGetX(XMVector2Length(v_ls));
	if (ls_len <= XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE / (float)0xffff)
	{
		v_ls = XMVectorZero();
	}

	// Right stick.
	XMVECTOR v_rs{ xstate.Gamepad.sThumbRX / (float)0xffff, xstate.Gamepad.sThumbRY / (float)0xffff };
	float rs_len = XMVectorGetX(XMVector2Length(v_rs));
	if (rs_len <= XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE / (float)0xffff)
	{
		v_rs = XMVectorZero();
	}

	XMStoreFloat2(&outState.leftStick, v_ls);
	XMStoreFloat2(&outState.rightStick, v_rs);

	outState.m_buttonState[(size_t)GamepadButton::A] = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_A);
	outState.m_buttonState[(size_t)GamepadButton::B] = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_B);
	outState.m_buttonState[(size_t)GamepadButton::X] = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_X);
	outState.m_buttonState[(size_t)GamepadButton::Y] = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_Y);

	outState.m_buttonState[(size_t)GamepadButton::LeftShoulder] = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER);
	outState.m_buttonState[(size_t)GamepadButton::RightShoulder] = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER);

	outState.m_buttonState[(size_t)GamepadButton::DPadLeft] = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT);
	outState.m_buttonState[(size_t)GamepadButton::DPadRight] = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT);
	outState.m_buttonState[(size_t)GamepadButton::DPadUp] = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP);
	outState.m_buttonState[(size_t)GamepadButton::DPadDown] = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN);

	return true;
}
