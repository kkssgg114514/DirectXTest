#include <Windows.h>
//#include <d3d12.h>
//#include <dxgi1_4.h>
//#include <wrl.h>
#include "d3dutil.h"//�Ѿ�����������
#include <iostream>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

using namespace Microsoft::WRL;//��Ҫ�İ����������ռ�

const UINT FrameCount = 2;
UINT width = 800;
UINT height = 600;
HWND hwnd;

//���߶���
ComPtr<IDXGISwapChain3> swapChain;//������
ComPtr<ID3D12Device> device;//3d�豸
ComPtr<ID3D12Resource> renderTargets[FrameCount];//��ȾĿ��
ComPtr<ID3D12CommandAllocator> commandAllocator;//���������
ComPtr<ID3D12CommandQueue> commandQueue;//�������
ComPtr<ID3D12DescriptorHeap> rtvHeap;//��ȾĿ����ͼ�ѣ���
ComPtr<ID3D12PipelineState> pipelineState;//����״̬����
ComPtr<ID3D12GraphicsCommandList> commandList;//�����б�
UINT rtvDescriptorSize;

// ͬ������
//����Χ����
UINT frameIndex;
HANDLE fenceEvent;
ComPtr<ID3D12Fence> fence;
UINT64 fenceValue;

IDXGIAdapter1* GetSupportedAdapter(ComPtr<IDXGIFactory4>& dxgiFactory, const D3D_FEATURE_LEVEL featureLevel);

void LoadPipeline()
{
	//���Բ㣬����debugʱ����
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
	//ö����ʾ������
	ComPtr<IDXGIFactory4> mDxgifactory;
	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(mDxgifactory.GetAddressOf())));

	/*-----------------------------------------------------------------------------------------------------------------------------*/
	//���ÿ�ͷ�����ĺ���
	//�汾�Ӹߵ��ͣ����Ƚ��и߰汾����
	D3D_FEATURE_LEVEL featureLevels[] =
	{
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0,
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
	};

	IDXGIAdapter1* adapter = nullptr;
	//�����Ϸ���D3D_FEATURE_LEVEL�������飬���ڵõ���������
	for (std::uint32_t i = 0U; i < _countof(featureLevels); ++i)
	{
		adapter = GetSupportedAdapter(mDxgifactory, featureLevels[i]);
		//adapter��Ϊ�ձ�ʾ�ɹ�ƥ���˰汾��������ѭ��
		if (adapter != nullptr)
		{
			break;
		}
	}

	/*-----------------------------------------------------------------------------------------------------------------------------*/
	//�����豸�ӿڣ�������ʾ�豸
	if (adapter != nullptr)
	{
		D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(device.GetAddressOf()));
	}

	/*-----------------------------------------------------------------------------------------------------------------------------*/
	//�����������
	//���������еĸ������ԣ�Ȼ�󴴽�
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	//����
	ThrowIfFailed(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));

	/*-----------------------------------------------------------------------------------------------------------------------------*/
	//��������������
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = FrameCount;
	swapChainDesc.Width = width;
	swapChainDesc.Height = height;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;

	//����������
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
	//����������������
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = FrameCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	//������������
	ThrowIfFailed(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap)));
	rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	/*-----------------------------------------------------------------------------------------------------------------------------*/

}

IDXGIAdapter1* GetSupportedAdapter(ComPtr<IDXGIFactory4>& dxgiFactory, const D3D_FEATURE_LEVEL featureLevel)
{
	IDXGIAdapter1* adapter = nullptr;
	//���ô����õ�IDXGIFactoryָ����ö�����е���ʾ�豸
	for (std::uint32_t adapterIndex = 0U; ; ++adapterIndex)
	{
		IDXGIAdapter1* currentAdapter = nullptr;
		//���δ�ҵ�����������ֹͣ����
		if (DXGI_ERROR_NOT_FOUND == dxgiFactory->EnumAdapters1(adapterIndex, &currentAdapter))
		{
			break;
		}

		//����ָ����D3D�汾�����豸���ڳ����еĶ��󣩣�����һ�����
		const HRESULT hres = D3D12CreateDevice(currentAdapter, featureLevel, __uuidof(ID3D12Device), nullptr);
		//���÷��ض�����ж��Ƿ񴴽��ɹ�������汾ƥ�䣬��ֹͣ����
		if (SUCCEEDED(hres))
		{
			adapter = currentAdapter;
			break;
		}

		//ÿ��ѭ���ͷſ�ͷ������ָ��
		currentAdapter->Release();
	}
	//����һ����ʾ������
	return adapter;
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)//�ص�����
{
	switch (message)
	{

	case WM_DESTROY:
		PostQuitMessage(0);//������Ϣ
		return 0;
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}

int CALLBACK WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
	WNDCLASSEX windowClass = { 0 };
	windowClass.cbSize = sizeof(WNDCLASSEX);
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = WindowProc;//���ûص������ĵط�
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