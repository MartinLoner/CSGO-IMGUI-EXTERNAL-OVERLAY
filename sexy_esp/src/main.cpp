#include <iostream>
#include <format>

#include "memory.hpp"

#include <Windows.h>
#include <dwmapi.h>
#include <d3d11.h>

#include <imgui/imgui.h>
#include <imgui/imgui_impl_dx11.h>
#include <imgui/imgui_impl_win32.h>

namespace offsets {
	constexpr auto local_player = 0xDE6964;
	constexpr auto entitiy_list = 0x4DFBE54;
	constexpr auto view_matrix = 0x4DECC84;

	constexpr auto bone_matrix = 0x26A8;
	constexpr auto team_num = 0xF4;
	constexpr auto life_state = 0x25F;
	constexpr auto origin = 0x138;
	constexpr auto dormant = 0xED;
}

struct Vector {
	Vector() noexcept
		: x(), y(), z() {}

	Vector(float x, float y, float z) noexcept
		: x(x), y(y), z(z) {}

	Vector& operator+(const Vector& v) noexcept {
		x += v.x;
		y += v.y;
		z += v.z;
		return *this;
	}

	Vector& operator-(const Vector& v) noexcept {
		x -= v.x;
		y -= v.y;
		z -= v.z;
		return *this;
	}
	
	float x, y, z;
};

struct ViewMatrix {
	ViewMatrix() noexcept
		: data() {}

	float* operator[](int index) noexcept {
		return data[index];
	}

	const float* operator[](int index) const noexcept {
		return data[index];
	}

	float data[4][4];
};

static bool world_to_screen(const Vector& world, Vector& screen, const ViewMatrix& vm) noexcept {
	float w = vm[3][0] * world.x + vm[3][1] * world.y + vm[3][2] * world.z + vm[3][3];

	if (w < 0.001f) {
		return false;
	}

	const float x = world.x * vm[0][0] + world.y * vm[0][1] + world.z * vm[0][2] + vm[0][3];
	const float y = world.x * vm[1][0] + world.y * vm[1][1] + world.z * vm[1][2] + vm[1][3];

	w = 1.f / w;
	float nx = x * w;
	float ny = y * w;

	const ImVec2 size = ImGui::GetIO().DisplaySize;

	screen.x = (size.x * 0.5f * nx) + (nx + size.x * 0.5f);
	screen.y = -(size.y * 0.5f * ny) + (ny + size.y * 0.5f);

	return true;
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
	if (ImGui_ImplWin32_WndProcHandler(window, message, w_param, l_param)) {
		return 0L;
	}

	switch (message) {
	   case WM_DESTROY: {
		PostQuitMessage(0);
		return 0L;
	   }
	}

	return DefWindowProc(window, message, w_param, l_param);
}

bool create_directx(HWND window) {

}

// application entry point
INT APIENTRY WinMain(HINSTANCE instance, HINSTANCE, PSTR, INT cmd_show) {
	// allocate this program a console
	if (!AllocConsole()) {
		return FALSE;
	}

	// redirect stream IO
	FILE* file{ nullptr };
	freopen_s(&file, "CONIN$", "r", stdin);
	freopen_s(&file, "CONOUT$", "w", stdout);
	freopen_s(&file, "CONOUT$", "w", stderr);

	// try get the process ID
	DWORD pid = memory::get_process_id(L"csgo.exe");

	// wait for the game
	if (!pid) {
		std::cout << "Waiting for CS:GO...\n";

		do {
			pid = memory::get_process_id(L"csgo.exe");
			Sleep(200UL);
		} while (!pid);
	}

	std::cout << std::format("Game found. Process ID = {}\n", pid);

	const DWORD client = memory::get_module_address(pid, L"client.dll");

	if (!client) {
		std::cout << "Failed to get game module.\n";
		FreeConsole();
		return FALSE;
	}

	std::cout << std::format("Client -> {:#x}\n", client);

	if (!FreeConsole()) {
		return FALSE;
	}

	const HANDLE handle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);

	if (!handle) {
		return FALSE;
	}

	// create the window class to specify options for the window
	WNDCLASSEXW wc{
	.cbSize = sizeof(WNDCLASSEXW),
	.style = CS_HREDRAW | CS_VREDRAW,
	.lpfnWndProc = window_procedure,
	.cbClsExtra = 0,
	.cbWndExtra = 0,
	.hInstance = instance,
	.hIcon = nullptr,
	.hCursor = nullptr,
	.hbrBackground = nullptr,
	.lpszMenuName = nullptr,
	.lpszClassName = L"External Overlay Class",
	.hIconSm = nullptr
	};

	// register it and make sure it succeeded
	if (!RegisterClassExW(&wc)) {
		return FALSE;
	}

	// create the window
	const HWND window = CreateWindowExW(
		WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED,
		wc.lpszClassName,
		L"External Overlay",
		WS_POPUP,
		0,
		0,
		1920,
		1080,
		nullptr,
		nullptr,
		wc.hInstance,
		nullptr
	);

	// make sure the window was created successfully
	if (!window) {
		UnregisterClassW(wc.lpszClassName, wc.hInstance);
		return FALSE;
	}

	// set the window's opacity
	if (!SetLayeredWindowAttributes(window, RGB(0, 0, 0), BYTE(255), LWA_ALPHA)) {
		DestroyWindow(window);
		UnregisterClassW(wc.lpszClassName, wc.hInstance);
		return FALSE;
	}

	{
		RECT client_area{};
		if (!GetClientRect(window, &client_area)) {
			DestroyWindow(window);
			UnregisterClassW(wc.lpszClassName, wc.hInstance);
			return FALSE;
		}

		RECT window_area{};
		if (!GetClientRect(window, &window_area)) {
			DestroyWindow(window);
			UnregisterClassW(wc.lpszClassName, wc.hInstance);
			return FALSE;
		}

		POINT diff{};
		if (!ClientToScreen(window, &diff)) {
			DestroyWindow(window);
			UnregisterClassW(wc.lpszClassName, wc.hInstance);
			return FALSE;
		}

		const MARGINS margins{
			window_area.left + (diff.x - window_area.left),
			window_area.top + (diff.y - window_area.top),
			client_area.right,
			client_area.bottom
		};

		if (FAILED(DwmExtendFrameIntoClientArea(window, &margins))) {
			DestroyWindow(window);
			UnregisterClassW(wc.lpszClassName, wc.hInstance);
			return FALSE;
		}
	}

	// create the directx swap chain description
	DXGI_SWAP_CHAIN_DESC sd{};
	ZeroMemory(&sd, sizeof(sd));

	sd.BufferDesc.Width = 0U;
	sd.BufferDesc.Height = 0U;
	sd.BufferDesc.RefreshRate.Numerator = 60U;
	sd.BufferDesc.RefreshRate.Denominator = 1U;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

	sd.SampleDesc.Count = 1U;
	sd.SampleDesc.Quality = 0U;

	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = 2U;
	sd.OutputWindow = window;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	constexpr D3D_FEATURE_LEVEL levels[2]{
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_0
	};

	// directx variables
	ID3D11Device* device{ nullptr };
	ID3D11DeviceContext* device_context{ nullptr };
	IDXGISwapChain* swap_chain{ nullptr };
	ID3D11RenderTargetView* render_target_view{ nullptr };
	D3D_FEATURE_LEVEL level{};
	
	// create the swap chain & device
	if (FAILED(D3D11CreateDeviceAndSwapChain(
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
		0U,
		levels,
		2U,
		D3D11_SDK_VERSION,
		&sd,
		&swap_chain,
		&device,
		&level,
		&device_context))) {
		DestroyWindow(window);
		UnregisterClassW(wc.lpszClassName, wc.hInstance);
		return FALSE;
	}

	ID3D11Texture2D* back_buffer{ nullptr };

	if (FAILED(swap_chain->GetBuffer(0U, IID_PPV_ARGS(&back_buffer)))) {
		return FALSE;
	}

	// create the render target
	if (FAILED(device->CreateRenderTargetView(back_buffer, nullptr, &render_target_view))) {
		return FALSE;
	}

	if (back_buffer) {
		back_buffer->Release();
	}

	// tell windows to show this window
	ShowWindow(window, cmd_show);
	UpdateWindow(window);

	ImGui::CreateContext();
	ImGui::StyleColorsDark();

	ImGui_ImplWin32_Init(window);
	ImGui_ImplDX11_Init(device, device_context);

	bool running = true;
	while (running) {
		MSG msg;
		while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);

			if (msg.message == WM_QUIT) {
				running = false;
			}
		}

		if (!running) {
			break;
		}

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();

		ImGui::NewFrame();

		const auto local_player = memory::read<DWORD>(handle, client + offsets::local_player);

		if (local_player) {
			const auto local_team = memory::read<int>(handle, local_player + offsets::team_num);
			const auto view_matrix = memory::read<ViewMatrix>(handle, client + offsets::view_matrix);

			for (int i = 1; i < 32; ++i) {
				const auto player = memory::read<DWORD>(handle, client + offsets::entitiy_list + i * 0x10);

				if (!player) {
					continue;
				}
				
				if (memory::read<bool>(handle, player + offsets::dormant)) {
					continue;
				}

				if (memory::read<int>(handle, player + offsets::team_num) == local_team) {
					continue;
				}

				if (memory::read<int>(handle, player + offsets::life_state) != 0) {
					continue;
				}

				const auto bones = memory::read<DWORD>(handle, player + offsets::bone_matrix);

				if (!bones) {
					continue;
				}

				Vector head_pos{
					memory::read<float>(handle, bones + 0x30 * 8 + 0x0C),
					memory::read<float>(handle, bones + 0x30 * 8 + 0x1C),
					memory::read<float>(handle, bones + 0x30 * 8 + 0x2C)
				};

				auto feet_pos = memory::read<Vector>(handle, player + offsets::origin);

				Vector top;
				Vector bottom;
				if (world_to_screen(head_pos + Vector{ 0, 0, 11.f }, top, view_matrix) && world_to_screen(feet_pos + Vector{ 0, 0, 9.f }, bottom, view_matrix));
					const float h = bottom.y - top.y;
					const float w = h * 0.35f;

					ImGui::GetBackgroundDrawList()->AddRect({ top.x - w, top.y }, { top.x + w, bottom.y }, ImColor(165, 23, 69));
			}
		}

		// rendering goes here

		ImGui::Render();

		constexpr float color[4]{ 0.f, 0.f, 0.f, 0.f };
		device_context->OMSetRenderTargets(1U, &render_target_view, nullptr);
		device_context->ClearRenderTargetView(render_target_view, color);

		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		swap_chain->Present(1U, 0U);
	}

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();

	ImGui::DestroyContext();

	if (swap_chain) {
		swap_chain->Release();
	}

	if (device_context) {
		device_context->Release();
	}

	if (device) {
		device->Release();
	}

	if (render_target_view) {
		render_target_view->Release();
	}

	DestroyWindow(window);
	UnregisterClassW(wc.lpszClassName, wc.hInstance);

	return 0;
}
