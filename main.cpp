#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl.h>
#include <iostream>
#include <d3dcompiler.h>//编译着色器需要
#include <DirectXMath.h>//基础数学库

#include "d3dx12.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")//编译着色器需要

using namespace Microsoft::WRL;//重要的包含，命名空间
using namespace DirectX;//另一个重要包含

const UINT FrameCount = 2;
UINT width = 800;
UINT height = 600;
HWND hwnd;

//管线对象
CD3DX12_VIEWPORT viewport(0.0f, 0.0f, width, height);
CD3DX12_RECT scissorRect(0, 0, width, height);
ComPtr<IDXGISwapChain3> swapChain;//交换链
ComPtr<ID3D12Device> device;//3d设备
ComPtr<ID3D12Resource> renderTargets[FrameCount];//渲染目标
ComPtr<ID3D12CommandAllocator> commandAllocator;//命令管理器
ComPtr<ID3D12CommandQueue> commandQueue;//命令队列
ComPtr<ID3D12RootSignature> rootSignature;//根签名
ComPtr<ID3D12DescriptorHeap> rtvHeap;//渲染目标视图堆（）
ComPtr<ID3D12PipelineState> pipelineState;//管线状态对象
ComPtr<ID3D12GraphicsCommandList> commandList;//命令列表
UINT rtvDescriptorSize;

ComPtr<ID3D12Resource> vertexBuffer;
D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
ComPtr<ID3D12Resource> indexBuffer;
D3D12_INDEX_BUFFER_VIEW indexBufferView;

// 同步对象
//都是围栏点
UINT frameIndex;
HANDLE fenceEvent;
ComPtr<ID3D12Fence> fence;
UINT64 fenceValue;

//添加颜色标记（）
float color[3];
bool isRAdd = true;
bool isGAdd = true;
bool isBAdd = true;

//顶点的结构，这里使用位置和颜色
struct Vertex
{
	XMFLOAT3 position;
	XMFLOAT4 color;
};

std::string HrToString(HRESULT hr)
{
	char s_str[64] = {};
	sprintf_s(s_str, "HRESULT of 0x%08X", static_cast<UINT>(hr));
	return std::string(s_str);
}

class HrException : public std::runtime_error
{
public:
	HrException(HRESULT hr) : std::runtime_error(HrToString(hr)), m_hr(hr) {}
	HRESULT Error() const { return m_hr; }
private:
	const HRESULT m_hr;
};

void ThrowIfFailed(HRESULT hr)
{
	if (FAILED(hr))
	{
		throw HrException(hr);
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
		const HRESULT hres = D3D12CreateDevice(currentAdapter, featureLevel, _uuidof(ID3D12Device), nullptr);
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

void LoadPipeline()
{
	//调试层，仅在debug时运行
#if defined(_DEBUG)
	{
		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();
		}
	}
#endif 

	/*-----------------------------------------------------------------------------------------------------------------------------*/
	//枚举显示适配器
	ComPtr<IDXGIFactory4> mDxgiFactory;
	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(mDxgiFactory.GetAddressOf())));

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
		adapter = GetSupportedAdapter(mDxgiFactory, featureLevels[i]);
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
	ThrowIfFailed(mDxgiFactory->CreateSwapChainForHwnd(
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
	{
		//创建描述符堆描述
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = FrameCount;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		//创建描述符堆
		ThrowIfFailed(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap)));
		rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}

	/*-----------------------------------------------------------------------------------------------------------------------------*/
	//创建资源视图
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());
	//获取交换链缓存区的资源，然后为此创建视图
	for (UINT n = 0; n < FrameCount; n++)
	{
		ThrowIfFailed(swapChain->GetBuffer(n, IID_PPV_ARGS(&renderTargets[n])));
		device->CreateRenderTargetView(renderTargets[n].Get(), nullptr, rtvHandle);
		rtvHandle.Offset(1, rtvDescriptorSize);
	}

	/*-----------------------------------------------------------------------------------------------------------------------------*/
	//创建命令分配器存放命令
	ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));
}

//加载资源
void LoadAsset()
{
	//创建根签名描述
	CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	//创建根签名
	ComPtr<ID3DBlob> signature;
	ComPtr<ID3DBlob> error;

	ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
	ThrowIfFailed(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));

	/*-----------------------------------------------------------------------------------------------------------------------------*/
	//编译着色器
	ComPtr<ID3DBlob> vertexShader;
	ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
	//debug期间实现更好性能
	UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	UINT compileFlags = 0;
#endif // defined(_DEBUG)

	//从高级着色器语言文件编译
	ThrowIfFailed(D3DCompileFromFile(std::wstring(L"Assets/shaders.hlsl").c_str(), nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, nullptr));
	ThrowIfFailed(D3DCompileFromFile(std::wstring(L"Assets/shaders.hlsl").c_str(), nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, nullptr));

	/*-----------------------------------------------------------------------------------------------------------------------------*/
	//创建管线状态对象，先填写描述
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
	psoDesc.pRootSignature = rootSignature.Get();
	psoDesc.VS =
	{
		reinterpret_cast<BYTE*>(vertexShader->GetBufferPointer()),
		vertexShader->GetBufferSize()
	};
	psoDesc.PS =
	{
		reinterpret_cast<BYTE*>(pixelShader->GetBufferPointer()),
		pixelShader->GetBufferSize()
	};
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState.DepthEnable = FALSE;
	psoDesc.DepthStencilState.StencilEnable = FALSE;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.SampleDesc.Count = 1;
	//创建
	ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState)));

	/*-----------------------------------------------------------------------------------------------------------------------------*/
	//创建命令列表
	ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), pipelineState.Get(), IID_PPV_ARGS(&commandList)));

	//关闭命令列表
	ThrowIfFailed(commandList->Close());

	/*-----------------------------------------------------------------------------------------------------------------------------*/
	//填写三角形顶点数组，包括位置和颜色数组
	Vertex triangleVertices[] =
	{
			{ { -0.5f, 0.5f, 0.5f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
			{ { 0.5f, 0.5f, 0.5f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
			{ { -0.5f, -0.5f, 0.5f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
			{ { 0.5f, 0.5f, 0.5f }, { 1.0f, 0.0f, 0.0f, 1.0f } }
	};
	//索引数组
	DWORD triangleIndexs[]
	{
		0,1,2,
		0,3,1
	};
	//计算出顶点缓冲区大小
	const UINT vertexBufferSize = sizeof(triangleVertices);
	//计算索引缓冲区大小
	const UINT indexBufferSize = sizeof(triangleIndexs);

	//新建一个上传堆
	CD3DX12_HEAP_PROPERTIES heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

	CD3DX12_RESOURCE_DESC resourceDes = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);
	////新建一个资源堆，大小和顶点缓冲区一样大
	//CD3DX12_RESOURCE_DESC vertexResourceDes = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);
	////索引缓冲区资源
	//CD3DX12_RESOURCE_DESC indexResourceDes = CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize);

	ThrowIfFailed(device->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDes,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&vertexBuffer)
	));

	ThrowIfFailed(device->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDes,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&indexBuffer)
	));

	UINT8* pDataBegin;
	CD3DX12_RANGE readRange(0, 0);

	ThrowIfFailed(vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pDataBegin)));
	//将数据从三角形顶点拷贝金顶点数据起点处
	memcpy(pDataBegin, triangleVertices, sizeof(triangleVertices));
	vertexBuffer->Unmap(0, nullptr);

	//对索引做相同操作
	ThrowIfFailed(indexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pDataBegin)));
	memcpy(pDataBegin, triangleIndexs, sizeof(triangleIndexs));
	indexBuffer->Unmap(0, nullptr);

	//两种缓冲区视图
	vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
	vertexBufferView.StrideInBytes = sizeof(Vertex);
	vertexBufferView.SizeInBytes = vertexBufferSize;

	indexBufferView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
	indexBufferView.Format = DXGI_FORMAT_R32_UINT;
	indexBufferView.SizeInBytes = indexBufferSize;

	{
		//创建同步围栏点
		ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
		fenceValue = 1;

		fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (fenceEvent == nullptr)
		{
			ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
		}
	}
}

//添加命令
void PopulateCommandList()
{
	ThrowIfFailed(commandAllocator->Reset());
	ThrowIfFailed(commandList->Reset(commandAllocator.Get(), pipelineState.Get()));

	//设置根签名，视口，裁剪矩形
	commandList->SetGraphicsRootSignature(rootSignature.Get());
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissorRect);

	D3D12_RESOURCE_BARRIER resBarrier = CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	commandList->ResourceBarrier(1, &resBarrier);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart(), frameIndex, rtvDescriptorSize);

	commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	//以什么颜色填充后台缓冲区
	const float clearColor[] = { color[0], color[1], color[2], 1.0f };
	commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	//
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
	commandList->IASetIndexBuffer(&indexBufferView);
	//要给命令列表添加顶点和索引缓冲区
	//函数变了，要添加能绘制索引的函数
	commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);


	resBarrier = CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	commandList->ResourceBarrier(1, &resBarrier);

	ThrowIfFailed(commandList->Close());
}

//同步CPU和GPU
void WaitForPreviousFrame()
{
	const UINT64 tempFenceValue = fenceValue;
	ThrowIfFailed(commandQueue->Signal(fence.Get(), tempFenceValue));
	fenceValue++;

	if (fence->GetCompletedValue() < tempFenceValue)
	{
		ThrowIfFailed(fence->SetEventOnCompletion(tempFenceValue, fenceEvent));
		WaitForSingleObject(fenceEvent, INFINITE);
	}

	frameIndex = swapChain->GetCurrentBackBufferIndex();
}

//更新缓冲区中的颜色
void OnUpdate()
{
	//颜色值小于1了就开始添加（RGB）
	if (color[0] <= 1.0f && isRAdd)
	{
		color[0] += 0.001f;
		isRAdd = true;
	}
	else
	{
		color[0] -= 0.002f;
		color[0] <= 0 ? isRAdd = true : isRAdd = false;
	}
	if (color[1] <= 1.0f && isGAdd)
	{
		color[1] += 0.002f;
		isGAdd = true;
	}
	else
	{
		color[1] -= 0.001f;
		color[1] <= 0 ? isGAdd = true : isGAdd = false;

	}

	if (color[2] <= 1.0f && isBAdd)
	{
		color[2] += 0.001f;
		isBAdd = true;
	}
	else
	{
		color[2] -= 0.001f;
		color[2] <= 0 ? isBAdd = true : isBAdd = false;
	}
}

//渲染方法
void OnRender()
{
	//提交命令列表，按照输入的命令进行渲染
	PopulateCommandList();

	//转存到另一个数组中
	ID3D12CommandList* ppCommandLists[] = { commandList.Get() };
	commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	ThrowIfFailed(swapChain->Present(1, 0));

	//等待两个芯片同步
	WaitForPreviousFrame();
}

//清理
void OnDestroy()
{
	WaitForPreviousFrame();

	CloseHandle(fenceEvent);
}


LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_PAINT:
		//在调用绘制时
		//在渲染前更新参数，渲染时根据更新的函数里的数据渲染
		OnUpdate();
		OnRender();
		return 0;

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

	hwnd = CreateWindow(//比上一个版本的代码更改窗口句柄为全局变量，因此不能重复定义##，重复定义会导致ThrowIfFailed函数无法得到正确的返回值
		windowClass.lpszClassName,
		L"Render",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		width,
		height,
		nullptr,
		nullptr,
		hInstance,
		nullptr);

	//加入初始化DirectX的代码
	LoadPipeline();
	LoadAsset();

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
	//回收资源
	OnDestroy();

	return 0;
}
