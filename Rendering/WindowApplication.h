#pragma once

#include <windows.h>

// Win32ウィンドウと描画実装を疎結合にし、移行中も同じWndProcを共有する。
class window_application
{
public:
	virtual ~window_application() = default;
	virtual int run() = 0;
	virtual LRESULT handle_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) = 0;
};
