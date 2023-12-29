// // 包含 SDKDDKVer.h 可定义可用的最高版本的 Windows 平台。
// 如果希望为之前的 Windows 平台构建应用程序，在包含 SDKDDKVer.h 之前请先包含 WinSDKVer.h 并
// 将 _WIN32_WINNT 宏设置为想要支持的平台。
#include <SDKDDKVer.h>

// 从 Windows 头文件中排除极少使用的内容
#define WIN32_LEAN_AND_MEAN             

// Windows 头文件
#include <windows.h>
#include <strsafe.h>
#include <comdef.h>
#include <wrl.h>
using namespace Microsoft;
using namespace Microsoft::WRL;

#include <dxgi1_6.h>
#include <DirectXMath.h>
using namespace DirectX;

#include <d3d12.h>
#include <d3d12shader.h>
#include <d3dcompiler.h>

#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")

#if defined(_DEBUG)
#include <dxgidebug.h>
#endif

// C 运行时头文件
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>
#include <string>
#include <exception>
#include <vector>

class HRException : public std::exception
{
public:
    HRException(HRESULT hr)
        : std::exception(), m_error(hr)
    {
        const wchar_t* wstr = m_error.ErrorMessage();
        size_t len = wcslen(wstr);
        std::string str(len, '\0');
        size_t convertedChars = 0;
        wcstombs_s(&convertedChars, &str[0], len + 1, wstr, _TRUNCATE);
        m_what = str;
    }

    const char* what() const noexcept override
    {
        return m_what.c_str();
    }

private:
    _com_error m_error;
    std::string m_what;
};


#define _ThrowIfFailed(hr) { HRESULT ret = (hr); if(FAILED(ret)) throw HRException(ret); }

// 全局变量:
HINSTANCE hInst;
const char* windowTitle = "0002-Triangle";
const char* windowClass = "0002-Triangle";
const int windowWidth = 800;
const int windowHeight = 600;


struct Vertex {
    XMFLOAT3 pos;
    XMFLOAT4 color;
};

class Graphics {

public:
    void init(HWND windowHandle, UINT frameBufferCount) {

        // Create DXGI Factory
        UINT dxgiFactoryFlags = 0U;
        _ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&mDXGIFactory)));
        _ThrowIfFailed(mDXGIFactory->MakeWindowAssociation(windowHandle, DXGI_MWA_NO_ALT_ENTER));


        // Enum Adapter and Create Device
        for(UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != mDXGIFactory->EnumAdapters1(adapterIndex, &mDXGIAdapter); adapterIndex++) {
            DXGI_ADAPTER_DESC1 desc = {};
            mDXGIAdapter->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                continue;
            }

            D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_12_1;
            _ThrowIfFailed(D3D12CreateDevice(mDXGIAdapter.Get(), featureLevel, IID_PPV_ARGS(&mDevice)));
            break;
        }
        if (mDevice == nullptr) {
            return;
        }


        // Create Command Queue
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        _ThrowIfFailed(mDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mCommandQueue)));


        // Create Swap Chain
        DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
        swapchainDesc.BufferCount = 2;
        swapchainDesc.Width = windowWidth;
        swapchainDesc.Height = windowHeight;
        swapchainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapchainDesc.SampleDesc.Count = 1;
        _ThrowIfFailed(mDXGIFactory->CreateSwapChainForHwnd(mCommandQueue.Get(), windowHandle, &swapchainDesc, nullptr, nullptr, &mSwapChain1));
        _ThrowIfFailed(mSwapChain1.As(&mSwapChain3));
        UINT currentBackBufferIndex = mSwapChain3->GetCurrentBackBufferIndex();


        // Create RTV Heap
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.NumDescriptors = frameBufferCount;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        _ThrowIfFailed(mDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&mRTVHeap)));
        UINT rtvSize = mDevice->GetDescriptorHandleIncrementSize(rtvHeapDesc.Type);


        // Get RenderTarget and Create RTV
        mRenderTargets.resize(frameBufferCount);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle(mRTVHeap->GetCPUDescriptorHandleForHeapStart());
        for (UINT i = 0; i < frameBufferCount; i++) {
            _ThrowIfFailed(mSwapChain3->GetBuffer(i, IID_PPV_ARGS(&mRenderTargets[i])));
            mDevice->CreateRenderTargetView(mRenderTargets[i].Get(), nullptr, rtvHandle);
            rtvHandle.ptr += rtvSize;
        }


        // Create Root Signature
        D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
        rootSignatureDesc.NumParameters = 0;
        rootSignatureDesc.pParameters = nullptr;
        rootSignatureDesc.NumStaticSamplers = 0;
        rootSignatureDesc.pStaticSamplers = nullptr;
        rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        
        ComPtr<ID3DBlob> rootSignatureBlob;
        ComPtr<ID3DBlob> errorBlob;
        _ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rootSignatureBlob, &errorBlob));
        _ThrowIfFailed(mDevice->CreateRootSignature(0, rootSignatureBlob->GetBufferPointer(), rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&mRootSignature)));


        // Compile Shader
        {
            D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
            {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
            };

            ComPtr<ID3DBlob> vsCode;
            ComPtr<ID3DBlob> psCode;
            this->compileShader("../shaders/001-triangle.hlsl", "vs_5_0", "VSMain", &vsCode);
            this->compileShader("../shaders/001-triangle.hlsl", "ps_5_0", "PSMain", &psCode);

            // Create Pipeline State
            {
                D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
                psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                psoDesc.InputLayout.pInputElementDescs = inputElementDescs;
                psoDesc.InputLayout.NumElements = _countof(inputElementDescs);

                psoDesc.pRootSignature = mRootSignature.Get();
                psoDesc.VS = { vsCode->GetBufferPointer(), vsCode->GetBufferSize() };
                psoDesc.PS = { psCode->GetBufferPointer(), psCode->GetBufferSize() };

                psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
                psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;

                psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
                psoDesc.BlendState.IndependentBlendEnable = FALSE;
                psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

                psoDesc.DepthStencilState.DepthEnable = FALSE;
                psoDesc.DepthStencilState.StencilEnable = FALSE;

                psoDesc.NumRenderTargets = 1;
                psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
                psoDesc.SampleMask = UINT_MAX;
                psoDesc.SampleDesc.Count = 1;

                _ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPipelineState)));
            }

            // Create Vertex Buffer
            {
                float aspect = windowWidth / (windowHeight + 0.0f);
                Vertex triangleVertices[] = {
                    { { 0.0f, 0.25f * aspect, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
                    { { 0.25f, -0.25f * aspect, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
                    { { -0.25f, -0.25f * aspect, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } }
                };
                const UINT vertexBufferSize = sizeof(triangleVertices);

                D3D12_HEAP_PROPERTIES heapProperties = {};
                heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

                D3D12_RESOURCE_DESC vbDesc = {};
                vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                vbDesc.Alignment = 0;
                vbDesc.Width = sizeof(triangleVertices);
                vbDesc.Height = 1;
                vbDesc.DepthOrArraySize = 1;
                vbDesc.MipLevels = 1;
                vbDesc.Format = DXGI_FORMAT_UNKNOWN;
                vbDesc.SampleDesc.Count = 1;
                vbDesc.SampleDesc.Quality = 0;
                vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                vbDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

                _ThrowIfFailed(mDevice->CreateCommittedResource(
                    &heapProperties,
                    D3D12_HEAP_FLAG_NONE,
                    &vbDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ,
                    nullptr,
                    IID_PPV_ARGS(&mVertexBuffer)));

                
                void* vertexDataPtr = NULL;
                D3D12_RANGE readRange = { 0, 0 };
                _ThrowIfFailed(mVertexBuffer->Map(0, &readRange, &vertexDataPtr));
                memcpy(vertexDataPtr, triangleVertices, vertexBufferSize);
                mVertexBuffer->Unmap(0, nullptr);
            }
        }

        // Create Command Allocator and Graphics Command List
        _ThrowIfFailed(mDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&mCommandAllocator)));
        _ThrowIfFailed(mDevice->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, 
            mCommandAllocator.Get(), 
            mPipelineState.Get(), 
            IID_PPV_ARGS(&mCommandList)));

        
        _ThrowIfFailed(mDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence)));

    }

    void quit() {
    
    }

    void tick(float delta) {
    
    }

    void compileShader(const std::string& file, const char* target, const char* entry, ID3DBlob** code) {
        const size_t maxShaderSize = 1024;

        char buffer[maxShaderSize] = { 0 };
        memset(buffer, 0, maxShaderSize);

        FILE* fd = NULL;
        fopen_s(&fd, file.c_str(), "rb");
        if(fd == NULL) {
            throw std::exception("Open shader file failed.");
        }

        fseek(fd, 0, SEEK_END);
        size_t size = ftell(fd);
        if (size > maxShaderSize) {
            throw std::exception("The shader file is too large.");
        }

        fseek(fd, 0, SEEK_SET);
        fread(buffer, size, 1, fd);
        fclose(fd);

        this->compileShader(file.c_str(), buffer, target, entry, code);
    }

    void compileShader(const std::string& name, const std::string& source, const char* target, const char* entry, ID3DBlob** code) {
        UINT compileFlags = 0;
#if defined(_DEBUG)
        compileFlags |= D3DCOMPILE_DEBUG;
        compileFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

        ComPtr<ID3DBlob> error;
        _ThrowIfFailed(D3DCompile(source.c_str(), source.size(), name.c_str(), nullptr, nullptr, entry, target, compileFlags, 0, code, &error));
        if (error != nullptr) {
            std::string str((const char*)error->GetBufferPointer(), error->GetBufferSize());
            throw std::exception(str.c_str());
        }
    }

private:
    ComPtr<IDXGIFactory7> mDXGIFactory;
    ComPtr<IDXGIAdapter1> mDXGIAdapter;
    ComPtr<ID3D12Device4> mDevice;
    ComPtr<ID3D12CommandQueue> mCommandQueue;
    ComPtr<IDXGISwapChain1> mSwapChain1;
    ComPtr<IDXGISwapChain3> mSwapChain3;
    ComPtr<ID3D12DescriptorHeap> mRTVHeap;
    std::vector<ComPtr<ID3D12Resource>> mRenderTargets;

    ComPtr<ID3D12RootSignature> mRootSignature;
    ComPtr<ID3D12PipelineState> mPipelineState;

    ComPtr<ID3D12CommandAllocator> mCommandAllocator;
    ComPtr<ID3D12CommandList> mCommandList;
    ComPtr<ID3D12Resource> mVertexBuffer;
    ComPtr<ID3D12Fence> mFence;
};


LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CLOSE:
    {
        PostQuitMessage(0);
        return 0;
    }
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

int APIENTRY WinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    hInst = hInstance; // 将实例句柄存储在全局变量中

    WNDCLASSEXA wcex;
    wcex.cbSize = sizeof(WNDCLASSEXA);
    wcex.style = CS_GLOBALCLASS;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = NULL;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = windowClass;
    wcex.hIconSm = NULL;
    RegisterClassExA(&wcex);

    HWND hWnd = CreateWindowA(windowClass, windowTitle, WS_OVERLAPPEDWINDOW,
        0, 0, windowWidth, windowHeight, nullptr, nullptr, hInstance, nullptr);
    if (!hWnd)
    {
        return FALSE;
    }
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    Graphics graphics;
    graphics.init(hWnd, 2);

    MSG msg;
    msg.message = static_cast<UINT>(~WM_QUIT);
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {

        }
    }

    return (int) msg.wParam;
}

