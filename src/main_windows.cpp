#include <game.hpp>
#include <input_windows.hpp>
#include <graphics.hpp>

struct WindowData
{

	WindowsInput* input;
};

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_CREATE)
	{
		const CREATESTRUCT* createStruct = reinterpret_cast<CREATESTRUCT*>(lParam);
		SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
		return 0;
	}

	WindowData* window = reinterpret_cast<WindowData*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
	if (!window)
	{
		return DefWindowProc(hwnd, message, wParam, lParam);
	}

	WindowsInput* input = window->input;

	if (message == WM_SIZE)
	{
		if (wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED)
		{
			const uint32_t width = LOWORD(lParam);
			const uint32_t height = HIWORD(lParam);

			// renderer->resizeSwapChain(width, height);
			return 0;
		}
	}

	if (input)
	{
		if (input->wndProc(hwnd, message, wParam, lParam))
		{
			return 0;
		}
	}

	return DefWindowProc(hwnd, message, wParam, lParam);
}

#if defined(CONFIG_RETAIL)
INT WINAPI WinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/, PSTR /*pCmdLine*/, INT /*nCmdShow*/)
#else
int main(int /*argc*/, char* /*argv*/[])
#endif
{
	WNDCLASS wc;
	ZeroMemory(&wc, sizeof(wc));
	wc.lpszClassName = TEXT("surface");
	wc.lpfnWndProc = WndProc;

	if (0 == RegisterClass(&wc))
	{
		MessageBox(NULL, TEXT("RegisterClass failed!"), NULL, MB_ICONERROR);
		ExitProcess(1);
	}

	const uint32_t windowWidth = graphics::resolution.width;
	const uint32_t windowHeight = graphics::resolution.height;

	WindowData windowData;
	windowData.input = nullptr;

	RECT wr{ 0, 0, (LONG)windowWidth, (LONG)windowHeight };
	AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
	HWND hwnd = CreateWindow(
		wc.lpszClassName,
		TEXT("cozy"),
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		wr.right - wr.left,
		wr.bottom - wr.top,
		NULL,
		NULL,
		NULL,
		&windowData);

	if (NULL == hwnd)
	{
		MessageBox(NULL, TEXT("CreateWindow failed!"), NULL, MB_ICONERROR);
		ExitProcess(1);
	}

	// Init renderer.
	graphics::init(hwnd);
	
	// Init input.
	std::unique_ptr<WindowsInput> input = std::make_unique<WindowsInput>(hwnd);

	// Set window data.
	// windowData.renderer = renderer.get();
	windowData.input = input.get();

	// Create game instance.
	std::unique_ptr<Game> game = std::make_unique<Game>();
	game->init();
	game->resizeBuffers(windowWidth, windowHeight);

	ShowWindow(hwnd, SW_SHOW);

	auto prevFrameTime = std::chrono::high_resolution_clock::now();
	while (IsWindowVisible(hwnd))
	{
		// Calculate delta time.
		const auto currentFrameTime = std::chrono::high_resolution_clock::now();
		const std::chrono::duration<float> deltaTime = currentFrameTime - prevFrameTime;
		prevFrameTime = currentFrameTime;
		const float dt = deltaTime.count();

		input->resetMouseDelta();

		MSG msg;
		while (PeekMessage(&msg, NULL, NULL, NULL, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		game->update(*input, dt);

		graphics::beginFrame();

		game->draw();

		graphics::endFrame();
	}
}