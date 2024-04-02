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

#include "include/d3dx12/d3dx12.h"

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
#include <map>
#include <codecvt>

class HRException : public std::exception
{
public:
    HRException(HRESULT hr)
        : std::exception(), m_error(hr)
    {
        const wchar_t* wcs = m_error.ErrorMessage();
        std::wstring_convert<std::codecvt_utf8<wchar_t>> convertor;
        std::string mbs = convertor.to_bytes(wcs);
        m_what = mbs;
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
const char* windowTitle = "0003-Texture";
const char* windowClass = "0003-Texture";
const int windowWidth = 800;
const int windowHeight = 600;

const UINT frameBufferCount = 2;

struct Vertex {
    XMFLOAT3 pos;
    XMFLOAT4 color;
    XMFLOAT2 uv;
};

class Graphics {

public:
    void init(HWND windowHandle) {
        UINT dxgiFactoryFlags = 0U;

#if defined(_DEBUG)
        // Enable the debug layer (requires the Graphics Tools "optional feature").
        // NOTE: Enabling the debug layer after device creation will invalidate the active device.
        {
            ComPtr<ID3D12Debug> dc;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dc)))) {
                if (SUCCEEDED(dc->QueryInterface(IID_PPV_ARGS(&mDebugController)))) {
                    mDebugController->EnableDebugLayer();
                    mDebugController->SetEnableGPUBasedValidation(true);
                }

                // Enable additional debug layers.
                dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
            }
        }
#endif

        // Create DXGI Factory
        _ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&mDXGIFactory)));
        _ThrowIfFailed(mDXGIFactory->MakeWindowAssociation(windowHandle, DXGI_MWA_NO_ALT_ENTER));


        // Enum Adapter and Create Device
        for(UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != mDXGIFactory->EnumAdapters1(adapterIndex, &mDXGIAdapter); adapterIndex++) {
            DXGI_ADAPTER_DESC1 desc = {};
            mDXGIAdapter->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                continue;
            }

            _ThrowIfFailed(D3D12CreateDevice(mDXGIAdapter.Get(), mFeatureLevel, IID_PPV_ARGS(&mDevice)));
#if defined(_DEBUG)
            _ThrowIfFailed(mDevice->QueryInterface(IID_PPV_ARGS(&mDebugDevice)));
#endif
            break;
        }
        if (mDevice == nullptr) {
            return;
        }


        // Create Command Queue
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        _ThrowIfFailed(mDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mCommandQueue)));


        // Create Swap Chain
        ComPtr<IDXGISwapChain> swapchain;
        DXGI_SWAP_CHAIN_DESC swapchainDesc = {};
        swapchainDesc.BufferCount = frameBufferCount;
        swapchainDesc.BufferDesc.Width = windowWidth;
        swapchainDesc.BufferDesc.Height = windowHeight;
        swapchainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapchainDesc.SampleDesc.Count = 1;
        swapchainDesc.OutputWindow = windowHandle;
        swapchainDesc.Windowed = TRUE;
        _ThrowIfFailed(mDXGIFactory->CreateSwapChain(mCommandQueue.Get(), &swapchainDesc, &swapchain));
        _ThrowIfFailed(swapchain.As(&mSwapChain));
        mFrameBufferIndex = mSwapChain->GetCurrentBackBufferIndex();


        // Create RTV Heap
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        rtvHeapDesc.NumDescriptors = frameBufferCount;
        _ThrowIfFailed(mDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&mRTVHeap)));
        mRTVHeapStride = mDevice->GetDescriptorHandleIncrementSize(rtvHeapDesc.Type);


        // Get RenderTarget and Create RTV
        mRenderTargets.resize(frameBufferCount);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle(mRTVHeap->GetCPUDescriptorHandleForHeapStart());
        for (UINT i = 0; i < frameBufferCount; i++) {
            _ThrowIfFailed(mSwapChain->GetBuffer(i, IID_PPV_ARGS(&mRenderTargets[i])));
            mDevice->CreateRenderTargetView(mRenderTargets[i].Get(), nullptr, rtvHandle);
            rtvHandle.ptr += mRTVHeapStride;
        }

        // Create Command Allocators
        for (UINT i = 0; i < frameBufferCount; i++) {
            _ThrowIfFailed(mDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&mCommandAllocators[i])));
        }
    
        // Create SRV Heap
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        srvHeapDesc.NumDescriptors = 1;
        _ThrowIfFailed(mDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSRVHeap)));
        mSRVHeapStride = mDevice->GetDescriptorHandleIncrementSize(srvHeapDesc.Type);


        // Create Viewport and Scissor-Rect
        {
            mViewport.TopLeftX = 0;
            mViewport.TopLeftY = 0;
            mViewport.Width = windowWidth;
            mViewport.Height = windowHeight;
            mViewport.MinDepth = 0.1f;
            mViewport.MaxDepth = 100.0f;

            mScissorRect.left = 0;
            mScissorRect.top = 0;
            mScissorRect.right = windowWidth;
            mScissorRect.bottom = windowHeight;
        }

        // Create Fence
        memset(mFenceValues, 0, sizeof(UINT64) * frameBufferCount);
        _ThrowIfFailed(mDevice->CreateFence(mFenceValues[mFrameBufferIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence)));
        mFenceValues[mFrameBufferIndex]++;
        mFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (mFenceEvent == nullptr) {
            _ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }

        // Create Assets
        this->createAssets();

        this->waitForGPU();
    }

    void quit() {
        this->waitForGPU();
        CloseHandle(mFenceEvent);
        mFenceEvent = nullptr;
    }

    void tick(float delta) {
        this->fillCommandList();

        ID3D12CommandList* commandLists[] = { mCommandList.Get() };
        mCommandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

        _ThrowIfFailed(mSwapChain->Present(1, 0));

        this->waitForNextFrame();
    }

    void createAssets() {
        // Create Root Signature
        {
            D3D12_STATIC_SAMPLER_DESC samplers[1] = { {} };
            samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
            samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            samplers[0].MipLODBias = 0;
            samplers[0].MinLOD = 0.0f;
            samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
            samplers[0].MaxAnisotropy = 0;
            samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
            samplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
            samplers[0].ShaderRegister = 0;
            samplers[0].RegisterSpace = 0;
            samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
            if (FAILED(mDevice->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
            {
                featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
            }

            D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
            rootSignatureDesc.Version = featureData.HighestVersion;
            if(rootSignatureDesc.Version == D3D_ROOT_SIGNATURE_VERSION_1_1)
            {
                D3D12_DESCRIPTOR_RANGE1 descriptorRanges[1] = {};
                descriptorRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                descriptorRanges[0].NumDescriptors = 1;
                descriptorRanges[0].BaseShaderRegister = 0;
                descriptorRanges[0].RegisterSpace = 0;
                descriptorRanges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;

                D3D12_ROOT_PARAMETER1 parameters[1] = {};
                parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                parameters[0].DescriptorTable.NumDescriptorRanges = 1;
                parameters[0].DescriptorTable.pDescriptorRanges = (const D3D12_DESCRIPTOR_RANGE1*)descriptorRanges;

                rootSignatureDesc.Desc_1_1.NumParameters = 1;
                rootSignatureDesc.Desc_1_1.pParameters = parameters;
                rootSignatureDesc.Desc_1_1.NumStaticSamplers = 1;
                rootSignatureDesc.Desc_1_1.pStaticSamplers = samplers;
                rootSignatureDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
            }
            else
            {
                D3D12_DESCRIPTOR_RANGE descriptorRanges[1] = {};
                descriptorRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                descriptorRanges[0].NumDescriptors = 1;
                descriptorRanges[0].BaseShaderRegister = 0;
                descriptorRanges[0].RegisterSpace = 0;

                D3D12_ROOT_PARAMETER parameters[1] = {};
                parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                parameters[0].DescriptorTable.NumDescriptorRanges = 1;
                parameters[0].DescriptorTable.pDescriptorRanges = (const D3D12_DESCRIPTOR_RANGE*)descriptorRanges;

                rootSignatureDesc.Desc_1_0.NumParameters = 1;
                rootSignatureDesc.Desc_1_0.pParameters = parameters;
                rootSignatureDesc.Desc_1_0.NumStaticSamplers = 1;
                rootSignatureDesc.Desc_1_0.pStaticSamplers = samplers;
                rootSignatureDesc.Desc_1_0.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
            }

            ComPtr<ID3DBlob> rootSignatureBlob;
            ComPtr<ID3DBlob> errorBlob;
            _ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &rootSignatureBlob, &errorBlob));
            _ThrowIfFailed(mDevice->CreateRootSignature(0, rootSignatureBlob->GetBufferPointer(), rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&mRootSignature)));
        }

        // Compile Shader
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        ComPtr<ID3DBlob> vsCode;
        ComPtr<ID3DBlob> psCode;
        this->compileShader("../shaders/002-texture.hlsl", "vs_5_0", "VSMain", &vsCode);
        this->compileShader("../shaders/002-texture.hlsl", "ps_5_0", "PSMain", &psCode);

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


        // Create Command Allocator and Graphics Command List
        _ThrowIfFailed(mDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            mCommandAllocators[mFrameBufferIndex].Get(),
            mPipelineState.Get(),
            IID_PPV_ARGS(&mCommandList)));
        

        // Create Vertex Buffer
        {
            float aspect = windowWidth / (windowHeight + 0.0f);
            Vertex triangleVertices[] = {
                { { -0.25f, 0.25f * aspect, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, {0.0f, 1.0f} },
                { { 0.25f, 0.25f * aspect, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f }, {1.0f, 1.0f} },
                { { 0.25f, -0.25f * aspect, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f }, {1.0f, 0.0f} },
                { { -0.25f, -0.25f * aspect, 0.0f }, { 1.0f, 1.0f, 0.0f, 1.0f }, {0.0f, 0.0f} }
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

            // Initialize the vertex buffer view.
            mVertexBufferView.BufferLocation = mVertexBuffer->GetGPUVirtualAddress();
            mVertexBufferView.StrideInBytes = sizeof(Vertex);
            mVertexBufferView.SizeInBytes = vertexBufferSize;
        }

        // Create Index Buffer
        {
            const DWORD indices[] = { 0, 1, 2, 2, 3, 0 };

            D3D12_HEAP_PROPERTIES heapProperties = {};
            heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

            D3D12_RESOURCE_DESC ibDesc = {};
            ibDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            ibDesc.Alignment = 0;
            ibDesc.Width = sizeof(indices);
            ibDesc.Height = 1;
            ibDesc.DepthOrArraySize = 1;
            ibDesc.MipLevels = 1;
            ibDesc.Format = DXGI_FORMAT_UNKNOWN;
            ibDesc.SampleDesc.Count = 1;
            ibDesc.SampleDesc.Quality = 0;
            ibDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            ibDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

            _ThrowIfFailed(mDevice->CreateCommittedResource(
                &heapProperties,
                D3D12_HEAP_FLAG_NONE,
                &ibDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&mIndexBuffer)));


            void* dataPtr = NULL;
            D3D12_RANGE readRange = { 0, 0 };
            _ThrowIfFailed(mIndexBuffer->Map(0, &readRange, &dataPtr));
            memcpy(dataPtr, indices, ibDesc.Width);
            mIndexBuffer->Unmap(0, nullptr);

            // Initialize the index buffer view.
            mIndexBufferView.BufferLocation = mIndexBuffer->GetGPUVirtualAddress();
            mIndexBufferView.Format = DXGI_FORMAT_R32_UINT;
            mIndexBufferView.SizeInBytes = ibDesc.Width;
        }

        // Texture
        {
            auto makeTextureData = [](std::vector<UINT8>& image, UINT textureWidth, UINT textureHeight)->void
            {
                const UINT rowPitch = textureWidth * 4;
                const UINT cellPitch = rowPitch >> 3; // The width of a cell in the checkboard texture.
                const UINT cellHeight = textureWidth >> 3; // The height of a cell in the checkerboard texture.
                const UINT textureSize = rowPitch * textureHeight;

                image.resize(textureSize);
    
                UINT8* pData = (UINT8*)image.data();
                for (UINT n = 0; n < textureSize; n += 4)
                {
                    UINT x = n % rowPitch;
                    UINT y = n / rowPitch;
                    UINT i = x / cellPitch;
                    UINT j = y / cellHeight;

                    if (i % 2 == j % 2)
                    {
                        pData[n + 0] = 0x00;    // R
                        pData[n + 1] = 0x00;    // G
                        pData[n + 2] = 0x00;    // B
                        pData[n + 3] = 0xff;    // A
                    }
                    else
                    {
                        pData[n + 0] = 0xff;    // R
                        pData[n + 1] = 0xff;    // G
                        pData[n + 2] = 0xff;    // B
                        pData[n + 3] = 0xff;    // A
                    }
                }
            };
            
            UINT width = 256;
            UINT height = 256;
            DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;

            std::vector<UINT8> image;
            makeTextureData(image, width, height);

            this->createTextureFromData(
                mDevice.Get(), mCommandList.Get(), 
                image, width, height, format, 
                mTextureResource, mTextureBuffer);
        }


        _ThrowIfFailed(mCommandList->Close());
        ID3D12CommandList* commandLists[] = { mCommandList.Get() };
        mCommandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);
    }

    void fillCommandList() {
        _ThrowIfFailed(mCommandAllocators[mFrameBufferIndex]->Reset());
        _ThrowIfFailed(mCommandList->Reset(mCommandAllocators[mFrameBufferIndex].Get(), mPipelineState.Get()));

        mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

        ID3D12DescriptorHeap* srvHeapList[] = { mSRVHeap.Get() };
        mCommandList->SetDescriptorHeaps(_countof(srvHeapList), srvHeapList);
        mCommandList->SetGraphicsRootDescriptorTable(0, mSRVHeap->GetGPUDescriptorHandleForHeapStart());

        mCommandList->RSSetViewports(1, &mViewport);
        mCommandList->RSSetScissorRects(1, &mScissorRect);

        D3D12_RESOURCE_BARRIER onBegin = {};
        onBegin.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        onBegin.Transition.pResource = mRenderTargets[mFrameBufferIndex].Get();
        onBegin.Transition.Subresource = 0;
        onBegin.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        onBegin.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        mCommandList->ResourceBarrier(1, &onBegin);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle{ mRTVHeap->GetCPUDescriptorHandleForHeapStart() };
        rtvHandle.ptr += mFrameBufferIndex * mRTVHeapStride;
        mCommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

        const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
        mCommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
        mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        mCommandList->IASetVertexBuffers(0, 1, &mVertexBufferView);
        mCommandList->IASetIndexBuffer(&mIndexBufferView);
        mCommandList->DrawIndexedInstanced(6, 1, 0, 0, 0);

        D3D12_RESOURCE_BARRIER onEnd = {};
        onEnd.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        onEnd.Transition.pResource = mRenderTargets[mFrameBufferIndex].Get();
        onEnd.Transition.Subresource = 0;
        onEnd.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        onEnd.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        mCommandList->ResourceBarrier(1, &onEnd);

        _ThrowIfFailed(mCommandList->Close());
    }

    void waitForGPU() {
        const UINT64 currentFenceValue = mFenceValues[mFrameBufferIndex];

        _ThrowIfFailed(mCommandQueue->Signal(mFence.Get(), currentFenceValue));
        
        _ThrowIfFailed(mFence->SetEventOnCompletion(currentFenceValue, mFenceEvent));
        WaitForSingleObject(mFenceEvent, INFINITE);
        
        mFenceValues[mFrameBufferIndex] = currentFenceValue + 1;
    }

    void waitForNextFrame() {
        const UINT64 currentFenceValue = mFenceValues[mFrameBufferIndex];

        _ThrowIfFailed(mCommandQueue->Signal(mFence.Get(), currentFenceValue));
        
        mFrameBufferIndex = mSwapChain->GetCurrentBackBufferIndex();

        const UINT completed = mFence->GetCompletedValue();
        if (completed < mFenceValues[mFrameBufferIndex])
        {
            _ThrowIfFailed(mFence->SetEventOnCompletion(mFenceValues[mFrameBufferIndex], mFenceEvent));
            WaitForSingleObject(mFenceEvent, INFINITE);
        }
        
        mFenceValues[mFrameBufferIndex] = currentFenceValue + 1;
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

    void createTextureFromData(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* commandList,
        const std::vector<UINT8>& imageData,
        UINT width,
        UINT height,
        DXGI_FORMAT format,
        ComPtr<ID3D12Resource>& textureResource,
        ComPtr<ID3D12Resource>& uploadBuffer)
    {
        // 1. 准备纹理数据和描述符
        D3D12_RESOURCE_DESC textureDesc = {};
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        //textureDesc.Alignment = 0;
        textureDesc.Width = width;
        textureDesc.Height = height;
        textureDesc.DepthOrArraySize = 1;
        textureDesc.MipLevels = 1;
        textureDesc.Format = format;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.SampleDesc.Quality = 0;
        textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        // 2. 创建纹理资源
        D3D12_HEAP_PROPERTIES textureHeapProps = {};
        textureHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
        _ThrowIfFailed(device->CreateCommittedResource(
            &textureHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &textureDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&textureResource)));

        // 3. 创建上传堆
        UINT subresourceIndex = 0;
        UINT subresourceCount = 1;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT uploadFootprint;
        UINT64 uploadBufferSize;
        UINT numRows;
        UINT64 rowSizeInBytes;
        device->GetCopyableFootprints(
            &textureDesc, subresourceIndex, subresourceCount, 0, 
            &uploadFootprint, &numRows, &rowSizeInBytes, &uploadBufferSize);

        D3D12_RESOURCE_DESC bufferDesc = {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Alignment = 0;
        bufferDesc.Width = uploadBufferSize;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.SampleDesc.Quality = 0;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        D3D12_HEAP_PROPERTIES uploadHeapProps = {};
        uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        _ThrowIfFailed(device->CreateCommittedResource(
            &uploadHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&uploadBuffer)));

        // 4. 将数据从 std::vector<UINT8> 复制到上传堆中
        const UINT8* srcPtr = imageData.data();
        UINT8* dstPtr;
        _ThrowIfFailed(uploadBuffer->Map(0, nullptr, (void**)&dstPtr));
        for (UINT row = 0; row < numRows; ++row)
        {
            memcpy(dstPtr, srcPtr, rowSizeInBytes);
            dstPtr += uploadFootprint.Footprint.RowPitch;
            srcPtr += rowSizeInBytes;
        }
        uploadBuffer->Unmap(0, nullptr);

        // 5. 将数据从上传堆复制到纹理资源中
        D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
        srcLocation.pResource = uploadBuffer.Get();
        srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLocation.PlacedFootprint = uploadFootprint;

        D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
        dstLocation.pResource = textureResource.Get();
        dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLocation.SubresourceIndex = 0;

        commandList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);

        D3D12_RESOURCE_BARRIER onFinish = {};
        onFinish.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        onFinish.Transition.pResource = textureResource.Get();
        onFinish.Transition.Subresource = 0;
        onFinish.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        onFinish.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        mCommandList->ResourceBarrier(1, &onFinish);

        // 6. 创建 SRV 描述符
        // 在此处创建 SRV 描述符并将其绑定到纹理资源。请注意，您需要确定 SRV 描述符堆的偏移量。
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(textureResource.Get(), &srvDesc, mSRVHeap->GetCPUDescriptorHandleForHeapStart());
    }

private:
    ComPtr<ID3D12Debug1> mDebugController;
    D3D_FEATURE_LEVEL mFeatureLevel = D3D_FEATURE_LEVEL_12_0;

    ComPtr<IDXGIFactory7> mDXGIFactory;
    ComPtr<IDXGIAdapter1> mDXGIAdapter;
    ComPtr<ID3D12Device4> mDevice;
    ComPtr<ID3D12DebugDevice> mDebugDevice;
    ComPtr<ID3D12CommandQueue> mCommandQueue;
    ComPtr<IDXGISwapChain3> mSwapChain;
    UINT mFrameBufferIndex = 0;
    
    ComPtr<ID3D12DescriptorHeap> mRTVHeap;
    UINT mRTVHeapStride = 0;
    std::vector<ComPtr<ID3D12Resource>> mRenderTargets;
    D3D12_VIEWPORT mViewport;
    D3D12_RECT mScissorRect;

    ComPtr<ID3D12DescriptorHeap> mSRVHeap;
    UINT mSRVHeapStride = 0;

    ComPtr<ID3D12Fence> mFence;
    UINT64 mFenceValues[frameBufferCount];
    HANDLE mFenceEvent;
    
    ComPtr<ID3D12RootSignature> mRootSignature;
    ComPtr<ID3D12PipelineState> mPipelineState;
    ComPtr<ID3D12CommandAllocator> mCommandAllocators[frameBufferCount];
    ComPtr<ID3D12GraphicsCommandList> mCommandList;
    ComPtr<ID3D12Resource> mVertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW mVertexBufferView;
    ComPtr<ID3D12Resource> mIndexBuffer;
    D3D12_INDEX_BUFFER_VIEW mIndexBufferView;
    ComPtr<ID3D12Resource> mTextureResource;
    ComPtr<ID3D12Resource> mTextureBuffer;
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
    try {
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
        graphics.init(hWnd);

        MSG msg;
        msg.message = static_cast<UINT>(~WM_QUIT);
        while (msg.message != WM_QUIT)
        {
            if (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
            else
            {
                graphics.tick(0);
            }
        }

        graphics.quit();

        return (int)msg.wParam;
    }
    catch (std::exception& e) {
        MessageBoxA(NULL, e.what(), NULL, 0);
        return 1;
    }
}

