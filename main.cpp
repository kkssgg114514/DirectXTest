#include <Windows.h>
//#include <d3d12.h>
//#include <dxgi1_4.h>
//#include <wrl.h>
#include "d3dutil.h"//已经包含在里面
#include <iostream>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

using namespace Microsoft::WRL;//重要的包含，命名空间

const UINT FrameCount = 2;
UINT width = 800;
UINT height = 600;
HWND hwnd;

//管线对象
ComPtr<IDXGISwapChain3> swapChain;
ComPtr<ID3D12Device> device;
ComPtr<ID3D12Resource> renderTargets[FrameCount];
ComPtr<ID3D12CommandAllocator> commandAllocator;
ComPtr<ID3D12CommandQueue> commandQueue;
ComPtr<ID3D12DescriptorHeap> rtvHeap;
ComPtr<ID3D12PipelineState> pipelineState;
ComPtr<ID3D12GraphicsCommandList> commandList;
UINT rtvDescriptorSize;

// 同步对象
UINT frameIndex;
HANDLE fenceEvent;
ComPtr<ID3D12Fence> fence;
UINT64 fenceValue;

IDXGIAdapter1* GetSupportedAdapter(ComPtr<IDXGIFactory4>& dxgiFactory, const D3D_FEATURE_LEVEL featureLevel);

void LoadPipeline()
{
	//调试层，仅在debug时运行
#if defined(_DEBUG)
	{
		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))));
		{
			debugController->EnableDebugLayer();
		}
	}
#endif

	//枚举显示适配器
	ComPtr<IDXGIFactory4> mDxgifactory;
	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(mDxgifactory.GetAddressOf())));

	//利用开头声明的函数
	//版本从高到低，优先进行高版本适配
	D3D_FEATURE_LEVEL featureLevels[] =
	{
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0,
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
	};

	IDXGIAdapter1* adapter = nullptr;
	//遍历上方的D3D_FEATURE_LEVEL类型数组，对于得到的适配器
	for (std::uint32_t i = 0U; i < _countof(featureLevels); ++i)
	{
		adapter = GetSupportedAdapter(mDxgifactory, featureLevels[i]);
		//adapter不为空表示成功匹配了版本，则跳出循环
		if (adapter != nullptr)
		{
			break;
		}
	}

	//创建设备接口，用来表示设备
	if (adapter != nullptr)
	{
		D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(device.GetAddressOf()));
	}
}

IDXGIAdapter1* GetSupportedAdapter(ComPtr<IDXGIFactory4>& dxgiFactory, const D3D_FEATURE_LEVEL featureLevel)
{
	IDXGIAdapter1* adapter = nullptr;
	//利用创建好的IDXGIFactory指针来枚举所有的显示设备
	for (std::uint32_t adapterIndex = 0U; ; ++adapterIndex)
	{
		IDXGIAdapter1* currentAdapter = nullptr;
		//如果未找到适配器，则停止搜索
		if (DXGI_ERROR_NOT_FOUND == dxgiFactory->EnumAdapters1(adapterIndex, &currentAdapter))
		{
			break;
		}

		//利用指定的D3D版本创建设备（在程序中的对象），返回一个结果
		const HRESULT hres = D3D12CreateDevice(currentAdapter, featureLevel, __uuidof(ID3D12Device), nullptr);
		//利用返回而结果判断是否创建成功，如果版本匹配，则停止搜索
		if (SUCCEEDED(hres))
		{
			adapter = currentAdapter;
			break;
		}

		//每次循环释放开头创建的指针
		currentAdapter->Release();
	}
	//返回一个显示适配器
	return adapter;
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)//回调函数
{
	switch (message)
	{

	case WM_DESTROY:
		PostQuitMessage(0);//返回信息
		return 0;
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}

int CALLBACK WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
	WNDCLASSEX windowClass = { 0 };
	windowClass.cbSize = sizeof(WNDCLASSEX);
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = WindowProc;//设置回调函数的地方
	windowClass.hInstance = hInstance;
	windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	windowClass.lpszClassName = L"RenderClass";

	RegisterClassEx(&windowClass);

	HWND hwnd = CreateWindow(
		windowClass.lpszClassName,
		L"Render",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		800,
		600,
		nullptr,
		nullptr,
		hInstance,
		nullptr);

	ShowWindow(hwnd, SW_SHOW);


	MSG msg = {};
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}


	return 0;
}