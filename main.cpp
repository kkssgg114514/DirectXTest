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
ComPtr<IDXGISwapChain3> swapChain;//交换链
ComPtr<ID3D12Device> device;//3d设备
ComPtr<ID3D12Resource> renderTargets[FrameCount];//渲染目标
ComPtr<ID3D12CommandAllocator> commandAllocator;//命令管理器
ComPtr<ID3D12CommandQueue> commandQueue;//命令队列
ComPtr<ID3D12DescriptorHeap> rtvHeap;//渲染目标视图堆（）
ComPtr<ID3D12PipelineState> pipelineState;//管线状态对象
ComPtr<ID3D12GraphicsCommandList> commandList;//命令列表
UINT rtvDescriptorSize;

// 同步对象
//都是围栏点
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

	/*-----------------------------------------------------------------------------------------------------------------------------*/
	//枚举显示适配器
	ComPtr<IDXGIFactory4> mDxgifactory;
	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(mDxgifactory.GetAddressOf())));

	/*-----------------------------------------------------------------------------------------------------------------------------*/
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

	/*-----------------------------------------------------------------------------------------------------------------------------*/
	//创建设备接口，用来表示设备
	if (adapter != nullptr)
	{
		D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(device.GetAddressOf()));
	}

	/*-----------------------------------------------------------------------------------------------------------------------------*/
	//创建命令队列
	//先描述队列的各个属性，然后创建
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	//创建
	ThrowIfFailed(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));

	/*-----------------------------------------------------------------------------------------------------------------------------*/
	//创建交换链描述
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = FrameCount;
	swapChainDesc.Width = width;
	swapChainDesc.Height = height;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;

	//创建交换链
	ComPtr<IDXGISwapChain1> swapChain1;
	ThrowIfFailed(mDxgifactory->CreateSwapChainForHwnd(
		commandQueue.Get(),
		hwnd,
		&swapChainDesc,
		nullptr,
		nullptr,
		&swapChain1
	));

	ThrowIfFailed(swapChain1.As(&swapChain));
	frameIndex = swapChain->GetCurrentBackBufferIndex();

	/*-----------------------------------------------------------------------------------------------------------------------------*/
	//创建描述符堆描述
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = FrameCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	//创建描述符堆
	ThrowIfFailed(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap)));
	rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	/*-----------------------------------------------------------------------------------------------------------------------------*/

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