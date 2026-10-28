#include <time.h>

#include "DirectX12/NativePreview.h"
namespace
{
	window_application* active_application = nullptr;
	constexpr wchar_t application_name[] = L"Jisaku Engine";
	constexpr LONG initial_width = 1280;
	constexpr LONG initial_height = 720;
}

LRESULT CALLBACK window_procedure(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	window_application* p{ reinterpret_cast<window_application*>(GetWindowLongPtr(hwnd, GWLP_USERDATA)) };
	return p ? p->handle_message(hwnd, msg, wparam, lparam) : DefWindowProc(hwnd, msg, wparam, lparam);
}

int WINAPI WinMain(_In_ HINSTANCE instance, _In_opt_  HINSTANCE prev_instance, _In_ LPSTR cmd_line, _In_ int cmd_show)
{
	srand(static_cast<unsigned int>(time(nullptr)));
	// WIC画像読込みなどのCOM機能をReleaseでも確実に使えるよう、メインスレッドを明示的に初期化する。
	const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
	//_CrtSetBreakAlloc(####);
#endif

	WNDCLASSEXW wcex{};
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = window_procedure;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = instance;
	wcex.hIcon = 0;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = application_name;
	wcex.hIconSm = 0;
	RegisterClassExW(&wcex);

	RECT rc{ 0, 0, initial_width, initial_height };
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
	HWND hwnd = CreateWindowExW(0, application_name, L"Jisaku Engine - DirectX 12", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
		CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, instance, NULL);
	ShowWindow(hwnd, cmd_show);

	std::unique_ptr<window_application> application;
	// 標準起動をネイティブD3D12へ統一する。D3D11/D3D11On12経路にはフォールバックしない。
	application = std::make_unique<native_preview12>(hwnd);
	active_application = application.get();
	SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(active_application));
	const int exit_code = application->run();
	active_application = nullptr;
	if (SUCCEEDED(com_result))
	{
		// 初期化に成功したスレッドだけ対応する終了処理を行う。
		CoUninitialize();
	}
	return exit_code;
}
