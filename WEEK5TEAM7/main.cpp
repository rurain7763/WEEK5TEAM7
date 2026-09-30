#include <windows.h>
#include <nvapi.h>
#include <NvApiDriverSettings.h>
#include "NvapiHelpers.h"

#include "Sphere.h"
#include "Renderer.h"
#include "WindowApplication.h"
#include "LaunchEngineLoop.h"
#include "Object.h"
#include "EngineStatics.h"

//#pragma comment(linker, "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")
extern "C" {
	__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
	__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

enum : UINT_PTR
{
	RESIZE_TIMER_ID = 1,
};

void* operator new(size_t size);
void operator delete(void* deleteObject, size_t size);

void* operator new(size_t size)
{
	++UEngineStatics::sTotalAllocationCount;
	UEngineStatics::sTotalAllocationBytes += static_cast<uint32>(size);

	void* newObject = malloc(size);

	return newObject;
}

void operator delete(void* deleteObject, size_t size)
{
	assert(deleteObject);

	--UEngineStatics::sTotalAllocationCount;
	UEngineStatics::sTotalAllocationBytes -= static_cast<uint32>(size);

	free(deleteObject);
}

//FEngineLoop GEngineLoop;

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
	{
		return true;
	}

	switch (message)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		break;

	//다음 메세지들은 입력 지연
	case WM_KEYDOWN: case WM_KEYUP:
	case WM_LBUTTONDOWN: case WM_LBUTTONUP:
	case WM_RBUTTONDOWN: case WM_RBUTTONUP:
	case WM_MOUSEMOVE:   case WM_MOUSEWHEEL:
	case WM_KILLFOCUS:
		WindowApplication.Defer({ hWnd, message, wParam, lParam });
		return 0;

	//마우스가 얼마정도 이동했나
	case WM_INPUT:
	{
		FDeferredMessage M{ hWnd, message, wParam, lParam };

		BYTE  buf[sizeof(RAWINPUT)];
		UINT  size = sizeof(buf);
		if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, buf, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1)
		{
			const RAWINPUT* ri = (const RAWINPUT*)buf;
			if (ri->header.dwType == RIM_TYPEMOUSE &&
				(ri->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0)
			{
				M.RawMouseDX = ri->data.mouse.lLastX;
				M.RawMouseDY = ri->data.mouse.lLastY;
			}
		}
		WindowApplication.Defer(M);
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	//SYS_ : Alt가 눌린 상태의 입력
	case WM_SYSKEYDOWN: case WM_SYSKEYUP:
		WindowApplication.Defer({ hWnd, message, wParam, lParam });
		return DefWindowProc(hWnd, message, wParam, lParam);

	//창 크기 변경
	case WM_SIZE:
		if (wParam != SIZE_MINIMIZED)
		{
			WindowApplication.PendingWidth = LOWORD(lParam);
			WindowApplication.PendingHeight = HIWORD(lParam);
			WindowApplication.bPendingResize = true;
		}
		break;

	case WM_ENTERSIZEMOVE:
		SetTimer(hWnd, RESIZE_TIMER_ID, 16, nullptr);
		return 0;

	case WM_EXITSIZEMOVE:
		KillTimer(hWnd, RESIZE_TIMER_ID);
		return 0;

	case WM_TIMER:
		if (wParam == RESIZE_TIMER_ID)
		{
			GEngineLoop.Tick(false);
		}
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	return 0;
}

void ProcessMessage(bool& bIsExit)
{
	MSG msg;

	while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);

		//WinProc 호출
		DispatchMessage(&msg);

		if (msg.message == WM_QUIT)
		{
			bIsExit = true;
			break;
		}
	}
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	// nvAPI >>>>>>>>>>>>>>>>>>>>>>
	NvAPI_Status status = NvAPI_Initialize();
	if (status == NVAPI_OK)
	{
		auto ApplySetting = [](const char* name, NvAPI_Status res) {
			if (res != NVAPI_OK) {
				NvAPI_ShortString errDesc = { 0 };
				NvAPI_GetErrorMessage(res, errDesc);
				char buf[256];
				sprintf_s(buf, "[NVAPI FAIL] %s failed! Code: %d (%s)\n", name, res, errDesc);
				OutputDebugStringA(buf);
			}
			else {
				char buf[256];
				sprintf_s(buf, "[NVAPI OK] %s applied.\n", name);
				OutputDebugStringA(buf);
			}
			};
		// 1. 최고 클럭 고정
		ApplySetting("PSTATE", nvapi_example::WriteDwordSettingForCurrentExecutable(
			PREFERRED_PSTATE_STRING, PREFERRED_PSTATE_PREFER_MAX));
		// 2. 드라이버 워커 스레드 즉시 활성화
		ApplySetting("OGL_THREAD_CONTROL", nvapi_example::WriteDwordSettingForCurrentExecutable(
			OGL_THREAD_CONTROL_STRING, OGL_THREAD_CONTROL_ENABLE));
		// 3. 사전 렌더링 프레임 2로 확장
		ApplySetting("PRERENDERLIMIT", nvapi_example::WriteDwordSettingForCurrentExecutable(
			PRERENDERLIMIT_STRING, 2));
		// 4. PowerThrottle 해제 (0 = DISABLED 스로틀링 해제!)
		ApplySetting("PowerThrottle(OFF)", nvapi_example::WriteDwordSettingByIdForCurrentExecutable(
			0x00AE785C, 0));
		// 5. 외장 GPU 강제 (Optimus Shim)
		ApplySetting("SHIM_MCCOMPAT", nvapi_example::WriteDwordSettingByIdForCurrentExecutable(
			0x10F9DC80, 1));
		ApplySetting("SHIM_RENDERING_MODE", nvapi_example::WriteDwordSettingByIdForCurrentExecutable(
			0x10F9DC81, 1));
		// 6. Perf Strategy - Load Balance Mode 강제 활성화 (0x008F14F5)
		ApplySetting("PerfStrategy_LoadBalance", nvapi_example::WriteDwordSettingByIdForCurrentExecutable(
			0x008F14F5, 1));
		// 7. PS Reduction Load Balanced 강제 활성화 (0x00DB834A)
		ApplySetting("PSReduction_LoadBalance", nvapi_example::WriteDwordSettingByIdForCurrentExecutable(
			0x00DB834A, 1));
		// ----------------------------------------------------
		// [검증] 드라이버 DB에 실제 저장된 값 다시 읽어오기
		// ----------------------------------------------------
		NvU32 checkVal = 0;
		if (nvapi_example::ReadDwordSettingByIdForCurrentExecutable(0x008F14F5, &checkVal) == NVAPI_OK)
		{
			char buf[256];
			sprintf_s(buf, "[NVAPI VERIFY] PerfStrategy_LoadBalance actual value in driver: %u\n", checkVal);
			OutputDebugStringA(buf);
		}
	}
	// nvAPI <<<<<<<<<<<<<<<<<<<<<<<

	GEngineLoop.Init(hInstance, WndProc);

	// Main Loop
	bool bIsExit = false;
	while (bIsExit == false)
	{
		ProcessMessage(bIsExit);
		GEngineLoop.Tick(false);
	}

	GEngineLoop.End();
	return 0;
}
