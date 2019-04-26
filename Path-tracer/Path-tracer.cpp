/***************************************************************************
# Copyright (c) 2018, NVIDIA CORPORATION. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#  * Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#  * Neither the name of NVIDIA CORPORATION nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
# OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***************************************************************************/
#include "Path-tracer.h"
#include <sstream>

static dxc::DxcDllSupport gDxcDllHelper;
MAKE_SMART_COM_PTR(IDxcCompiler);
MAKE_SMART_COM_PTR(IDxcLibrary);
MAKE_SMART_COM_PTR(IDxcBlobEncoding);
MAKE_SMART_COM_PTR(IDxcOperationResult);


//////////////////////////////////////////////////////////////////////////
// Tutorial 02 code
//////////////////////////////////////////////////////////////////////////

IDXGISwapChain3Ptr createDxgiSwapChain(IDXGIFactory4Ptr pFactory, HWND hwnd, uint32_t width, uint32_t height, DXGI_FORMAT format, ID3D12CommandQueuePtr pCommandQueue)
{
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = kDefaultSwapChainBuffers;
    swapChainDesc.Width = width;
    swapChainDesc.Height = height;
    swapChainDesc.Format = format;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    // CreateSwapChainForHwnd() doesn't accept IDXGISwapChain3 (Why MS? Why?)
    MAKE_SMART_COM_PTR(IDXGISwapChain1);
    IDXGISwapChain1Ptr pSwapChain;

    HRESULT hr = pFactory->CreateSwapChainForHwnd(pCommandQueue, hwnd, &swapChainDesc, nullptr, nullptr, &pSwapChain);
    if (FAILED(hr))
    {
        d3dTraceHR("Failed to create the swap-chain", hr);
        return false;
    }

    IDXGISwapChain3Ptr pSwapChain3;
    d3d_call(pSwapChain->QueryInterface(IID_PPV_ARGS(&pSwapChain3)));
    return pSwapChain3;
}

ID3D12Device5Ptr createDevice(IDXGIFactory4Ptr pDxgiFactory)
{
    // Find the HW adapter
    IDXGIAdapter1Ptr pAdapter;

    for (uint32_t i = 0; DXGI_ERROR_NOT_FOUND != pDxgiFactory->EnumAdapters1(i, &pAdapter); i++)
    {
        DXGI_ADAPTER_DESC1 desc;
        pAdapter->GetDesc1(&desc);

        // Skip SW adapters
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
#ifdef _DEBUG
        ID3D12DebugPtr pDx12Debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pDx12Debug))))
        {
            pDx12Debug->EnableDebugLayer();
        }
#endif
        // Create the device
        ID3D12Device5Ptr pDevice;
        d3d_call(D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&pDevice)));

        D3D12_FEATURE_DATA_D3D12_OPTIONS5 features5;
        HRESULT hr = pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &features5, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS5));
        if (FAILED(hr) || features5.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
        {
            msgBox("Raytracing is not supported on this device. Make sure your GPU supports DXR (such as Nvidia's Volta or Turing RTX) and you're on the latest drivers. The DXR fallback layer is not supported.");
            exit(1);
        }
        return pDevice;
    }
    return nullptr;
}

ID3D12CommandQueuePtr createCommandQueue(ID3D12Device5Ptr pDevice)
{
    ID3D12CommandQueuePtr pQueue;
    D3D12_COMMAND_QUEUE_DESC cqDesc = {};
    cqDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    cqDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    d3d_call(pDevice->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&pQueue)));
    return pQueue;
}

ID3D12DescriptorHeapPtr createDescriptorHeap(ID3D12Device5Ptr pDevice, uint32_t count, D3D12_DESCRIPTOR_HEAP_TYPE type, bool shaderVisible)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors = count;
    desc.Type = type;
    desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    ID3D12DescriptorHeapPtr pHeap;
    d3d_call(pDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&pHeap)));
    return pHeap;
}

D3D12_CPU_DESCRIPTOR_HANDLE createRTV(ID3D12Device5Ptr pDevice, ID3D12ResourcePtr pResource, ID3D12DescriptorHeapPtr pHeap, uint32_t& usedHeapEntries, DXGI_FORMAT format)
{
    D3D12_RENDER_TARGET_VIEW_DESC desc = {};
    desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    desc.Format = format;
    desc.Texture2D.MipSlice = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = pHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += usedHeapEntries * pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    usedHeapEntries++;
    pDevice->CreateRenderTargetView(pResource, &desc, rtvHandle);
    return rtvHandle;
}

void resourceBarrier(ID3D12GraphicsCommandList4Ptr pCmdList, ID3D12ResourcePtr pResource, D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = pResource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = stateBefore;
    barrier.Transition.StateAfter = stateAfter;
    pCmdList->ResourceBarrier(1, &barrier);
}

uint64_t submitCommandList(ID3D12GraphicsCommandList4Ptr pCmdList, ID3D12CommandQueuePtr pCmdQueue, ID3D12FencePtr pFence, uint64_t fenceValue)
{
    pCmdList->Close();
    ID3D12CommandList* pGraphicsList = pCmdList.GetInterfacePtr();
    pCmdQueue->ExecuteCommandLists(1, &pGraphicsList);
    fenceValue++;
    pCmdQueue->Signal(pFence, fenceValue);
    return fenceValue;
}

void PathTracer::initDXR(HWND winHandle, uint32_t winWidth, uint32_t winHeight)
{
    mHwnd = winHandle;
    mSwapChainSize = uvec2(winWidth, winHeight);

    // Initialize the debug layer for debug builds
#ifdef _DEBUG
    ID3D12DebugPtr pDebug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pDebug))))
    {
        pDebug->EnableDebugLayer();
    }
#endif
    // Create the DXGI factory
    IDXGIFactory4Ptr pDxgiFactory;
    d3d_call(CreateDXGIFactory1(IID_PPV_ARGS(&pDxgiFactory)));
    mpDevice = createDevice(pDxgiFactory);
    mpCmdQueue = createCommandQueue(mpDevice);
    mpSwapChain = createDxgiSwapChain(pDxgiFactory, mHwnd, winWidth, winHeight, DXGI_FORMAT_R8G8B8A8_UNORM, mpCmdQueue);

    // Create a RTV descriptor heap
    mRtvHeap.pHeap = createDescriptorHeap(mpDevice, kRtvHeapSize, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, false);

#ifdef  HYBRID


	// Set up viewPort
		mRasterViewPort.Width = (float) kShadowMapWidth;
		mRasterViewPort.Height = (float) kShadowMapHeight;
		mRasterViewPort.MinDepth = 0.0f;
		mRasterViewPort.MaxDepth = 1.0f;
		mRasterViewPort.TopLeftX = 0.0f;
		mRasterViewPort.TopLeftY = 0.0f;
	// Set up Scissor rectangle
		mRasterScissorRect.left = 0;
		mRasterScissorRect.top = 0;
		mRasterScissorRect.right = kShadowMapWidth;
		mRasterScissorRect.bottom = kShadowMapHeight;

#endif

    // Create the per-frame objects
    for (uint32_t i = 0; i < arraysize(mFrameObjects); i++)
    {
        d3d_call(mpDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&mFrameObjects[i].pCmdAllocator)));
        d3d_call(mpSwapChain->GetBuffer(i, IID_PPV_ARGS(&mFrameObjects[i].pSwapChainBuffer)));
        mFrameObjects[i].rtvHandle = createRTV(mpDevice, mFrameObjects[i].pSwapChainBuffer, mRtvHeap.pHeap, mRtvHeap.usedEntries, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    }

    // Create the command-list
    d3d_call(mpDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, mFrameObjects[0].pCmdAllocator, nullptr, IID_PPV_ARGS(&mpCmdList)));

    // Create a fence and the event
    d3d_call(mpDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mpFence)));
    mFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

uint32_t PathTracer::beginFrame()
{
    // Bind the descriptor heaps
    ID3D12DescriptorHeap* heaps[] = { mpCbvSrvUavHeap };
    mpCmdList->SetDescriptorHeaps(arraysize(heaps), heaps);
    return mpSwapChain->GetCurrentBackBufferIndex();
}

void PathTracer::endFrame(uint32_t rtvIndex)
{
    resourceBarrier(mpCmdList, mFrameObjects[rtvIndex].pSwapChainBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
    mFenceValue = submitCommandList(mpCmdList, mpCmdQueue, mpFence, mFenceValue);
    mpSwapChain->Present(0, 0);

    // Prepare the command list for the next frame
    uint32_t bufferIndex = mpSwapChain->GetCurrentBackBufferIndex();

    // Sync. We need to do this because the TLAS resources are not double-buffered and we are going to update them
    mpFence->SetEventOnCompletion(mFenceValue, mFenceEvent);
    WaitForSingleObject(mFenceEvent, INFINITE);

    mFrameObjects[bufferIndex].pCmdAllocator->Reset();
    mpCmdList->Reset(mFrameObjects[bufferIndex].pCmdAllocator, nullptr);
}

//////////////////////////////////////////////////////////////////////////
// Tutorial 03 code
//////////////////////////////////////////////////////////////////////////
static const D3D12_HEAP_PROPERTIES kUploadHeapProps =
{
    D3D12_HEAP_TYPE_UPLOAD,
    D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
    D3D12_MEMORY_POOL_UNKNOWN,
    0,
    0,
};

static const D3D12_HEAP_PROPERTIES kDefaultHeapProps =
{
    D3D12_HEAP_TYPE_DEFAULT,
    D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
    D3D12_MEMORY_POOL_UNKNOWN,
    0,
    0
};

ID3D12ResourcePtr createBuffer(ID3D12Device5Ptr pDevice, uint64_t size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState, const D3D12_HEAP_PROPERTIES& heapProps)
{
    D3D12_RESOURCE_DESC bufDesc = {};
    bufDesc.Alignment = 0;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Flags = flags;
    bufDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufDesc.Height = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufDesc.MipLevels = 1;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.SampleDesc.Quality = 0;
    bufDesc.Width = size;

    ID3D12ResourcePtr pBuffer;
    d3d_call(pDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc, initState, nullptr, IID_PPV_ARGS(&pBuffer)));
    return pBuffer;
}


ID3D12ResourcePtr createPlaneVB(ID3D12Device5Ptr pDevice)
{
    const vec3 vertices[] =
    {
        vec3(-1, 0,  -1),
        vec3( 1, 0,  1),
        vec3(-1, 0,  1),
        vec3( 1, 0,  -1),
    };			   

    // For simplicity, we create the vertex buffer on the upload heap, but that's not required
    ID3D12ResourcePtr pBuffer = createBuffer(pDevice, sizeof(vertices), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
    uint8_t* pData;
    pBuffer->Map(0, nullptr, (void**)&pData);
    memcpy(pData, vertices, sizeof(vertices));
    pBuffer->Unmap(0, nullptr);
    return pBuffer;
}

ID3D12ResourcePtr createPlaneIB(ID3D12Device5Ptr pDevice)
{
	const uint indices[] =
	{
		0,
		2,
		3,
		1,
		3,
		2
	};// left hand oriented!

	// For simplicity, we create the vertex buffer on the upload heap, but that's not required
	ID3D12ResourcePtr pBuffer = createBuffer(pDevice, sizeof(indices), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
	uint8_t* pData;
	pBuffer->Map(0, nullptr, (void**)&pData);
	memcpy(pData, indices, sizeof(indices));
	pBuffer->Unmap(0, nullptr);
	return pBuffer;
}

ID3D12ResourcePtr createPlaneNB(ID3D12Device5Ptr pDevice)
{
	const vec3 normals[] =
	{
		vec3(0, 1, 0),
		vec3(0, 1, 0),
		vec3(0, 1, 0),
		vec3(0, 1, 0)
	};

	// For simplicity, we create the vertex buffer on the upload heap, but that's not required
	ID3D12ResourcePtr pBuffer = createBuffer(pDevice, sizeof(normals), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
	uint8_t* pData;
	pBuffer->Map(0, nullptr, (void**)&pData);
	memcpy(pData, normals, sizeof(normals));
	pBuffer->Unmap(0, nullptr);
	return pBuffer;
}

ID3D12ResourcePtr createModelVB(ID3D12Device5Ptr pDevice, aiVector3D* aiVertecies, int numVertices)
{
	ID3D12ResourcePtr pBuffer = createBuffer(pDevice, numVertices*sizeof(vec3), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
	uint8_t* pData;
	pBuffer->Map(0, nullptr, (void**)&pData);
		memcpy(pData, aiVertecies, numVertices*sizeof(vec3));
	pBuffer->Unmap(0, nullptr);
	return pBuffer;
}

ID3D12ResourcePtr createModelIB(ID3D12Device5Ptr pDevice, aiFace* aiFaces, int numFaces)
{
	ID3D12ResourcePtr pBuffer = createBuffer(pDevice, numFaces * 3 * sizeof(uint), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
	uint8_t* pData;
	pBuffer->Map(0, nullptr, (void**)&pData);
		for (int i = 0; i < numFaces; i++)
		{
			memcpy(pData + i*3*sizeof(uint), aiFaces[i].mIndices, 3 * sizeof(uint));
		}
	pBuffer->Unmap(0, nullptr);
	return pBuffer;
}

ID3D12ResourcePtr createModelNB(ID3D12Device5Ptr pDevice, aiVector3D* aiNormals, int numNormals)
{
	ID3D12ResourcePtr pBuffer = createBuffer(pDevice, numNormals * sizeof(vec3), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
	uint8_t* pData;
	pBuffer->Map(0, nullptr, (void**)&pData);
		memcpy(pData, aiNormals, numNormals * sizeof(vec3));
	pBuffer->Unmap(0, nullptr);
	return pBuffer;
}

void PathTracer::createHDRTextureBuffer()
{
	ID3D12ResourcePtr textureUploadHeap;

	// Load texture file
	auto scratchImage = std::make_unique<ScratchImage>();
	HRESULT hr = LoadFromHDRFile(L"Data/HDR_maps/grace_probe.hdr", nullptr, *scratchImage);
	if (FAILED(hr))
	{
		msgBox("Failed to import HDR texture.");
		return;
	}
	auto image = scratchImage->GetImage(0, 0, 0);
	

	// Describe and create a Texture2D
	D3D12_RESOURCE_DESC textureDesc = {};
	textureDesc.Alignment = 0;
	textureDesc.DepthOrArraySize = 1;
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	textureDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	textureDesc.Height = (UINT) image->height;
	textureDesc.Width = (UINT) image->width;
	//textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	textureDesc.MipLevels = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;

	d3d_call(mpDevice->CreateCommittedResource(
		&kDefaultHeapProps, 
		D3D12_HEAP_FLAG_NONE,
		&textureDesc, 
		D3D12_RESOURCE_STATE_COPY_DEST, 
		nullptr, 
		IID_PPV_ARGS(&mpHDRTextureBuffer)
	));
	mpHDRTextureBuffer->SetName(L"Environment map");

	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(mpHDRTextureBuffer, 0, 1);
	
	// Create the GPU upload buffer.
	d3d_call(mpDevice->CreateCommittedResource(
		&kUploadHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&textureUploadHeap)
	));

	// Copy data to the intermediate upload heap and then schedule a copy 
	// from the upload heap to the Texture2D.
	D3D12_SUBRESOURCE_DATA textureData = {};
	textureData.pData = image->pixels;
	textureData.RowPitch = image->rowPitch;
	textureData.SlicePitch = image->slicePitch;// TODO: check if these nummerical values are correct

	UpdateSubresources(
		mpCmdList,
		mpHDRTextureBuffer,
		textureUploadHeap,
		0, 0, 1,
		&textureData
	);
	mpCmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mpHDRTextureBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

	// Close the command list and execute it to begin the initial GPU setup.
	mFenceValue = submitCommandList(mpCmdList, mpCmdQueue, mpFence, mFenceValue);
	mpFence->SetEventOnCompletion(mFenceValue, mFenceEvent);
	WaitForSingleObject(mFenceEvent, INFINITE);
	mpCmdList->Reset(mFrameObjects[0].pCmdAllocator, nullptr);

}

void PathTracer::buildTransforms(float rotation)
{
	mat4 rotationMat = eulerAngleY(rotation*0.5f);
	// plane
	mTransforms[0] = scale(10.0f*vec3(1.0f, 1.0f, 1.0f));
	// area light
	mTransforms[1] = translate(mat4(), vec3(0.0, 15, 0.0)) * eulerAngleX(pi<float>()) * scale(5.0f*vec3(0.5f, 0.5f, 0.5f));
	// robot
	mTransforms[2] = rotationMat * translate(mat4(), vec3(2, 1.39, 2 * sin(rotation*0.7f))) * scale(3.0f*vec3(1.0f, 1.0f, 1.0f));
	// room
	//transformation[3] = scale(100.0f*vec3(1.0f, 1.0f, 1.0f));

}

#ifdef HYBRID
void PathTracer::createTransformBuffers()
{
	for (int i = 0; i < mNumInstances; i++)
	{
		mpTransformBuffer[i] = createBuffer(mpDevice, sizeof(mat4), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
	}
	mpTransformBuffer[0]->SetName(L"Plane Transform");
	mpTransformBuffer[1]->SetName(L"Area Light Transform");
	mpTransformBuffer[2]->SetName(L"Robot Transform");
}
#endif

PathTracer::AccelerationStructureBuffers createBottomLevelAS(ID3D12Device5Ptr pDevice, ID3D12GraphicsCommandList4Ptr pCmdList, ID3D12ResourcePtr pVB[], const uint32_t vertexCount[], ID3D12ResourcePtr pIB[], const uint32_t indexCount[], uint32_t geometryCount)
{
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geomDesc;
    geomDesc.resize(geometryCount);

    for (uint32_t i = 0; i < geometryCount; i++)
    {
        geomDesc[i].Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geomDesc[i].Triangles.VertexBuffer.StartAddress = pVB[i]->GetGPUVirtualAddress();
        geomDesc[i].Triangles.VertexBuffer.StrideInBytes = sizeof(vec3);
        geomDesc[i].Triangles.VertexCount = vertexCount[i];
        geomDesc[i].Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
		geomDesc[i].Triangles.IndexBuffer = pIB[i]->GetGPUVirtualAddress();
		geomDesc[i].Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
		geomDesc[i].Triangles.IndexCount = indexCount[i];
        geomDesc[i].Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    }

    // Get the size requirements for the scratch and AS buffers
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
    inputs.NumDescs = geometryCount;
    inputs.pGeometryDescs = geomDesc.data();
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
    pDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

    // Create the buffers. They need to support UAV, and since we are going to immediately use them, we create them with an unordered-access state
    PathTracer::AccelerationStructureBuffers buffers;
    buffers.pScratch = createBuffer(pDevice, info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, kDefaultHeapProps);
    buffers.pResult = createBuffer(pDevice, info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, kDefaultHeapProps);

    // Create the bottom-level AS
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
    asDesc.Inputs = inputs;
    asDesc.DestAccelerationStructureData = buffers.pResult->GetGPUVirtualAddress();
    asDesc.ScratchAccelerationStructureData = buffers.pScratch->GetGPUVirtualAddress();

    pCmdList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);

    // We need to insert a UAV barrier before using the acceleration structures in a raytracing operation
    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = buffers.pResult;
    pCmdList->ResourceBarrier(1, &uavBarrier);

    return buffers;
}

void buildTopLevelAS(ID3D12Device5Ptr pDevice, ID3D12GraphicsCommandList4Ptr pCmdList, ID3D12ResourcePtr pBottomLevelAS[], uint64_t& tlasSize, bool update, mat4 transforms[], PathTracer::AccelerationStructureBuffers& buffers)
{
	int numInstances = 3; // keep in sync with mNumInstances

	// First, get the size of the TLAS buffers and create them
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
	inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
	inputs.NumDescs = numInstances;
	inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
	pDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

	if (update)
	{
		// If this a request for an update, then the TLAS was already used in a DispatchRay() call. We need a UAV barrier to make sure the read operation ends before updating the buffer
		D3D12_RESOURCE_BARRIER uavBarrier = {};
		uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		uavBarrier.UAV.pResource = buffers.pResult;
		pCmdList->ResourceBarrier(1, &uavBarrier);
	}
	else
	{
		// If this is not an update operation then we need to create the buffers, otherwise we will refit in-place
		buffers.pScratch = createBuffer(pDevice, info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, kDefaultHeapProps);
		buffers.pResult = createBuffer(pDevice, info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, kDefaultHeapProps);
		buffers.pInstanceDesc = createBuffer(pDevice, sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * numInstances, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
		tlasSize = info.ResultDataMaxSizeInBytes;
	}

	// Map the instance desc buffer
	D3D12_RAYTRACING_INSTANCE_DESC* instanceDescs;
	buffers.pInstanceDesc->Map(0, nullptr, (void**)&instanceDescs);
	ZeroMemory(instanceDescs, sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * numInstances);

	// The InstanceContributionToHitGroupIndex is set based on the shader-table layout specified in createShaderTable()
	int instanceIdx = 0;
	// Create the desc for the planes
	for (uint32_t i = 0; i < 1; i++)
	{
		instanceDescs[instanceIdx].InstanceID = i;// This value will be exposed to the shader via InstanceID()
		instanceDescs[instanceIdx].InstanceContributionToHitGroupIndex = 0; // hard coded
		instanceDescs[instanceIdx].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		// GLM is column major, the INSTANCE_DESC is row major
		memcpy(instanceDescs[instanceIdx].Transform, &transpose(transforms[instanceIdx]), sizeof(instanceDescs[instanceIdx].Transform));
		instanceDescs[instanceIdx].AccelerationStructure = pBottomLevelAS[0]->GetGPUVirtualAddress();
		instanceDescs[instanceIdx].InstanceMask = 0xFF;

		instanceIdx++;
	}

	// Create the desc for the Area Light
	instanceDescs[instanceIdx].InstanceID = 0;// This value will be exposed to the shader via InstanceID()
	instanceDescs[instanceIdx].InstanceContributionToHitGroupIndex = 2; // hard coded
	instanceDescs[instanceIdx].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
	// GLM is column major, the INSTANCE_DESC is row major
	memcpy(instanceDescs[instanceIdx].Transform, &transpose(transforms[instanceIdx]), sizeof(instanceDescs[instanceIdx].Transform));
	instanceDescs[instanceIdx].AccelerationStructure = pBottomLevelAS[0]->GetGPUVirtualAddress();
	instanceDescs[instanceIdx].InstanceMask = 0xFF;

	instanceIdx++;

	// Create the desc for the robots
	for (uint32_t i = 0; i < 1; i++)
	{
		instanceDescs[instanceIdx].InstanceID = i; // This value will be exposed to the shader via InstanceID()
		instanceDescs[instanceIdx].InstanceContributionToHitGroupIndex = 4;  // hard coded
		instanceDescs[instanceIdx].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		mat4 m = transpose(transforms[instanceIdx]); // GLM is column major, the INSTANCE_DESC is row major
		memcpy(instanceDescs[instanceIdx].Transform, &m, sizeof(instanceDescs[instanceIdx].Transform));
		instanceDescs[instanceIdx].AccelerationStructure = pBottomLevelAS[1]->GetGPUVirtualAddress();
		instanceDescs[instanceIdx].InstanceMask = 0xFF;

		instanceIdx++;
	}

	//// Create the desc for the room
	//for (uint32_t i = 0; i < 8; i++)
	//{
	//	instanceDescs[instanceIdx].InstanceID = i; // This value will be exposed to the shader via InstanceID()
	//	instanceDescs[instanceIdx].InstanceContributionToHitGroupIndex = 0;  // hard coded
	//	instanceDescs[instanceIdx].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
	//	mat4 m = transpose(transformation[3]); // GLM is column major, the INSTANCE_DESC is row major
	//	memcpy(instanceDescs[instanceIdx].Transform, &m, sizeof(instanceDescs[instanceIdx].Transform));
	//	instanceDescs[instanceIdx].AccelerationStructure = pBottomLevelAS[2+i]->GetGPUVirtualAddress();
	//	instanceDescs[instanceIdx].InstanceMask = 0xFF;

	//	instanceIdx++;
	//}

	assert(instanceIdx == numInstances);

    // Unmap
    buffers.pInstanceDesc->Unmap(0, nullptr);

    // Create the TLAS
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
    asDesc.Inputs = inputs;
    asDesc.Inputs.InstanceDescs = buffers.pInstanceDesc->GetGPUVirtualAddress();
    asDesc.DestAccelerationStructureData = buffers.pResult->GetGPUVirtualAddress();
    asDesc.ScratchAccelerationStructureData = buffers.pScratch->GetGPUVirtualAddress();

    // If this is an update operation, set the source buffer and the perform_update flag
    if(update)
    {
        asDesc.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
        asDesc.SourceAccelerationStructureData = buffers.pResult->GetGPUVirtualAddress();
    }

    pCmdList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);

    // We need to insert a UAV barrier before using the acceleration structures in a raytracing operation
    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = buffers.pResult;
    pCmdList->ResourceBarrier(1, &uavBarrier);
}

void PathTracer::createAccelerationStructures()
{

    mpVertexBuffer[0] = createPlaneVB(mpDevice);
		mpVertexBuffer[0]->SetName(L"Plane Vertex Buffer");
	mpIndexBuffer[0] = createPlaneIB(mpDevice);
		mpIndexBuffer[0]->SetName(L"Plane Index Buffer");
	mpNormalBuffer[0] = createPlaneNB(mpDevice);
		mpNormalBuffer[0]->SetName(L"Plane Normal Buffer");
#ifdef HYBRID
	mVertexBufferView[0].BufferLocation = mpVertexBuffer[0]->GetGPUVirtualAddress();
	mVertexBufferView[0].StrideInBytes = sizeof(vec3);
	mVertexBufferView[0].SizeInBytes = 4 * sizeof(vec3);//hard coded

	mIndexBufferView[0].BufferLocation = mpIndexBuffer[0]->GetGPUVirtualAddress();
	mIndexBufferView[0].Format = DXGI_FORMAT_R32_UINT;
	mIndexBufferView[0].SizeInBytes = 6 * sizeof(uint);
#endif


	// Load robot
	const aiScene* robotScene = importer.ReadFile("Data/Models/robot.fbx",
		aiProcess_CalcTangentSpace		|
		aiProcess_JoinIdenticalVertices	|
		aiProcess_Triangulate			|
		aiProcess_GenNormals			|
		aiProcess_FixInfacingNormals	|
		aiProcess_GenUVCoords			|
		aiProcess_TransformUVCoords		|
		aiProcess_MakeLeftHanded		|
		aiProcess_FindInvalidData);
	aiMesh* robot = robotScene->mMeshes[0];
	
	mpVertexBuffer[1] = createModelVB(mpDevice, robot->mVertices, robot->mNumVertices);
		mpVertexBuffer[1]->SetName(L"Robot Vertex Buffer");
	mpIndexBuffer[1] = createModelIB(mpDevice, robot->mFaces, robot->mNumFaces);
		mpIndexBuffer[1]->SetName(L"Robot Index Buffer");
	mpNormalBuffer[1] = createModelNB(mpDevice, robot->mNormals, robot->mNumVertices);
		mpNormalBuffer[1]->SetName(L"Robot Normal Buffer");
#ifdef HYBRID
	mVertexBufferView[1].BufferLocation = mpVertexBuffer[1]->GetGPUVirtualAddress();
	mVertexBufferView[1].StrideInBytes = sizeof(vec3);
	mVertexBufferView[1].SizeInBytes = robot->mNumVertices * sizeof(vec3);

	mIndexBufferView[1].BufferLocation = mpIndexBuffer[1]->GetGPUVirtualAddress();
	mIndexBufferView[1].Format = DXGI_FORMAT_R32_UINT;
	mIndexBufferView[1].SizeInBytes = robot->mNumFaces * 3 * sizeof(uint);
#endif

	
	//// Load room
	//const aiScene* roomScene = importer.ReadFile("Data/Models/room_with_window.fbx",
	//	aiProcess_CalcTangentSpace |
	//	aiProcess_JoinIdenticalVertices |
	//	aiProcess_Triangulate |
	//	aiProcess_GenNormals |
	//	aiProcess_FixInfacingNormals |
	//	aiProcess_GenUVCoords |
	//	aiProcess_TransformUVCoords |
	//	aiProcess_MakeLeftHanded |
	//	aiProcess_FindInvalidData);
	//aiNode** rootNodeChildren = roomScene->mRootNode->mChildren;
	//aiMesh* room[8];
	//for (int i = 0; i < 8; i++)
	//{
	//	aiNode* child = rootNodeChildren[i];

	//	room[i] = roomScene->mMeshes[0];
	//	mpVertexBuffer[2+i] = createRoomVB(mpDevice, room[i]->mVertices, room[i]->mNumVertices);
	//	mpIndexBuffer[2+i] = createRoomIB(mpDevice, room[i]->mFaces, room[i]->mNumFaces);
	//	mpNormalBuffer[2+i] = createRoomNB(mpDevice, room[i]->mNormals, room[i]->mNumVertices);
	//}
	
	
    AccelerationStructureBuffers bottomLevelBuffers[2];

    // The first bottom-level buffer is for the plane
		ID3D12ResourcePtr vertexBufferPlane[] = { mpVertexBuffer[0] };
		const uint32_t vertexCountPlane[] = { 4 };// Plane has 4
		ID3D12ResourcePtr indexBufferPlane[] = { mpIndexBuffer[0] };
		const uint32_t indexCountPlane[] = { 6 };
		bottomLevelBuffers[0] = createBottomLevelAS(
													mpDevice, 
													mpCmdList, 
													vertexBufferPlane, 
													vertexCountPlane, 
													indexBufferPlane, 
													indexCountPlane, 
													1
												   );
		mpBottomLevelAS[0] = bottomLevelBuffers[0].pResult;
			mpBottomLevelAS[0]->SetName(L"BLAS Plane");

	// The second bottom-level buffer is for the robot
		ID3D12ResourcePtr vertexBufferRobot[] = { mpVertexBuffer[1] };
		const uint32_t vertexCountRobot[] = { robot->mNumVertices };
		ID3D12ResourcePtr indexBufferRobot[] = { mpIndexBuffer[1] };
		const uint32_t indexCountRobot[] = { robot->mNumFaces * 3};
		bottomLevelBuffers[1] = createBottomLevelAS(
													mpDevice, 
													mpCmdList, 
													vertexBufferRobot, 
													vertexCountRobot, 
													indexBufferRobot, 
													indexCountRobot, 
													1
												   );
		mpBottomLevelAS[1] = bottomLevelBuffers[1].pResult;
			mpBottomLevelAS[1]->SetName(L"BLAS Robot");

	//// Then set up the Room buffers
	//	for (int i = 0; i < 8; i++)
	//	{
	//		ID3D12ResourcePtr vertexBufferRoom[] = { mpVertexBuffer[2+i] };
	//		const uint32_t vertexCountRoom[] = { room[i]->mNumVertices };
	//		ID3D12ResourcePtr indexBufferRoom[] = { mpIndexBuffer[2+i] };
	//		const uint32_t indexCountRoom[] = { room[i]->mNumFaces * 3 };
	//		bottomLevelBuffers[2+i] = createBottomLevelAS(
	//			mpDevice,
	//			mpCmdList,
	//			vertexBufferRoom,
	//			vertexCountRoom,
	//			indexBufferRoom,
	//			indexCountRoom,
	//			1
	//		);
	//		mpBottomLevelAS[2+i] = bottomLevelBuffers[2+i].pResult;
	//	}

    // Create the TLAS
    buildTopLevelAS(mpDevice, mpCmdList, mpBottomLevelAS, mTlasSize, false, mTransforms, mTopLevelBuffers);

    // The tutorial doesn't have any resource lifetime management, so we flush and sync here. 
	//This is not required by the DXR spec - you can submit the list whenever you like as long as you take care of the resources lifetime.
    mFenceValue = submitCommandList(mpCmdList, mpCmdQueue, mpFence, mFenceValue);
    mpFence->SetEventOnCompletion(mFenceValue, mFenceEvent);
    WaitForSingleObject(mFenceEvent, INFINITE);
    mpCmdList->Reset(mFrameObjects[0].pCmdAllocator, nullptr);
}

//////////////////////////////////////////////////////////////////////////
// Tutorial 04 code
//////////////////////////////////////////////////////////////////////////
ID3DBlobPtr compileLibrary(const WCHAR* filename, const WCHAR* entryPoint, const WCHAR* targetString)
{
    // Initialize the helper
    d3d_call(gDxcDllHelper.Initialize());
    IDxcCompilerPtr pCompiler;
    IDxcLibraryPtr pLibrary;
	IDxcIncludeHandler* dxcIncludeHandler;//-----
    d3d_call(gDxcDllHelper.CreateInstance(CLSID_DxcCompiler, &pCompiler));
    d3d_call(gDxcDllHelper.CreateInstance(CLSID_DxcLibrary, &pLibrary));
	pLibrary->CreateIncludeHandler(&dxcIncludeHandler);//-----

    // Open and read the file
    std::ifstream shaderFile(filename);
    if (shaderFile.good() == false)
    {
        msgBox("Can't open file " + wstring_2_string(std::wstring(filename)));
        return nullptr;
    }
    std::stringstream strStream;
    strStream << shaderFile.rdbuf();
    std::string shader = strStream.str();

    // Create blob from the string
    IDxcBlobEncodingPtr pTextBlob;
    d3d_call(pLibrary->CreateBlobWithEncodingFromPinned((LPBYTE)shader.c_str(), (uint32_t)shader.size(), 0, &pTextBlob));

    // Compile
    IDxcOperationResultPtr pResult;
    d3d_call(pCompiler->Compile(pTextBlob, filename, entryPoint, targetString, nullptr, 0, nullptr, 0, dxcIncludeHandler, &pResult));

    // Verify the result
    HRESULT resultCode;
    d3d_call(pResult->GetStatus(&resultCode));
    if (FAILED(resultCode))
    {
        IDxcBlobEncodingPtr pError;
        d3d_call(pResult->GetErrorBuffer(&pError));
        std::string log = convertBlobToString(pError.GetInterfacePtr());
        msgBox("Compiler error:\n" + log);
        return nullptr;
    }

    MAKE_SMART_COM_PTR(IDxcBlob);
    IDxcBlobPtr pBlob;
    d3d_call(pResult->GetResult(&pBlob));
    return pBlob;
}

ID3D12RootSignaturePtr createRootSignature(ID3D12Device5Ptr pDevice, const D3D12_ROOT_SIGNATURE_DESC& desc)
{
    ID3DBlobPtr pSigBlob;
    ID3DBlobPtr pErrorBlob;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &pSigBlob, &pErrorBlob);
    if (FAILED(hr))
    {
        std::string msg = convertBlobToString(pErrorBlob.GetInterfacePtr());
        msgBox(msg);
        return nullptr;
    }
    ID3D12RootSignaturePtr pRootSig;
    d3d_call(pDevice->CreateRootSignature(0, pSigBlob->GetBufferPointer(), pSigBlob->GetBufferSize(), IID_PPV_ARGS(&pRootSig)));
    return pRootSig;
}

struct RootSignatureDesc
{
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    std::vector<D3D12_DESCRIPTOR_RANGE> range;
    std::vector<D3D12_ROOT_PARAMETER> rootParams;
};

RootSignatureDesc createRayGenRootDesc()
{
	// Create the root-signature
	RootSignatureDesc desc;
	desc.range.resize(7);
	// gOutput
	desc.range[0].BaseShaderRegister = 0;// u0
	desc.range[0].NumDescriptors = 1;
	desc.range[0].RegisterSpace = 0;
	desc.range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	desc.range[0].OffsetInDescriptorsFromTableStart = 0;

	// gRtScene
	desc.range[1].BaseShaderRegister = 0; //t0
	desc.range[1].NumDescriptors = 1;
	desc.range[1].RegisterSpace = 0;
	desc.range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	desc.range[1].OffsetInDescriptorsFromTableStart = 1;

	// Camera
	desc.range[2].BaseShaderRegister = 0; //b0
	desc.range[2].NumDescriptors = 1;
	desc.range[2].RegisterSpace = 0;
	desc.range[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	desc.range[2].OffsetInDescriptorsFromTableStart = 2;

	// Shadow map Depth
	desc.range[3].BaseShaderRegister = 1; //t1
	desc.range[3].NumDescriptors = 1;
	desc.range[3].RegisterSpace = 0;
	desc.range[3].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	desc.range[3].OffsetInDescriptorsFromTableStart = 0;

	// Shadow map Position
	desc.range[4].BaseShaderRegister = 2; //t2
	desc.range[4].NumDescriptors = 1;
	desc.range[4].RegisterSpace = 0;
	desc.range[4].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	desc.range[4].OffsetInDescriptorsFromTableStart = 1;

	// Shadow map Normal
	desc.range[5].BaseShaderRegister = 3; //t3
	desc.range[5].NumDescriptors = 1;
	desc.range[5].RegisterSpace = 0;
	desc.range[5].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	desc.range[5].OffsetInDescriptorsFromTableStart = 2;

	// Shadow map Flux
	desc.range[6].BaseShaderRegister = 4; //t4
	desc.range[6].NumDescriptors = 1;
	desc.range[6].RegisterSpace = 0;
	desc.range[6].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	desc.range[6].OffsetInDescriptorsFromTableStart = 3;

	desc.rootParams.resize(2);
	desc.rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	desc.rootParams[0].DescriptorTable.NumDescriptorRanges = 3;
	desc.rootParams[0].DescriptorTable.pDescriptorRanges = desc.range.data();

	desc.rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	desc.rootParams[1].DescriptorTable.NumDescriptorRanges = 4;
	desc.rootParams[1].DescriptorTable.pDescriptorRanges = desc.range.data() + 3;//&desc.range[3];

    // Create the desc
    desc.desc.NumParameters = 2;
    desc.desc.pParameters = desc.rootParams.data();
    desc.desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

    return desc;
}


RootSignatureDesc createPlaneHitRootDesc()
{
    RootSignatureDesc desc;
    desc.range.resize(1);

	// gRtScene
    desc.range[0].BaseShaderRegister = 0; //t0
    desc.range[0].NumDescriptors = 1;
    desc.range[0].RegisterSpace = 0;
    desc.range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    desc.range[0].OffsetInDescriptorsFromTableStart = 0;

    desc.rootParams.resize(2);
    desc.rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    desc.rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    desc.rootParams[0].DescriptorTable.pDescriptorRanges = desc.range.data();
	
	// normals
	desc.rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	desc.rootParams[1].Descriptor.RegisterSpace = 0;
	desc.rootParams[1].Descriptor.ShaderRegister = 2;//t2

    desc.desc.NumParameters = 2;
    desc.desc.pParameters = desc.rootParams.data();
    desc.desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

    return desc;
}

RootSignatureDesc createRobotHitRootDesc()
{
	RootSignatureDesc desc;
	desc.range.resize(1);

	// gRtScene
	desc.range[0].BaseShaderRegister = 0; //t0
	desc.range[0].NumDescriptors = 1;
	desc.range[0].RegisterSpace = 0;
	desc.range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	desc.range[0].OffsetInDescriptorsFromTableStart = 0;

	desc.rootParams.resize(3);
	desc.rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	desc.rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
	desc.rootParams[0].DescriptorTable.pDescriptorRanges = desc.range.data();

	// indices
	desc.rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	desc.rootParams[1].Descriptor.RegisterSpace = 0;
	desc.rootParams[1].Descriptor.ShaderRegister = 1;//t1

	// normals
	desc.rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	desc.rootParams[2].Descriptor.RegisterSpace = 0;
	desc.rootParams[2].Descriptor.ShaderRegister = 2;//t2

	desc.desc.NumParameters = 3;
	desc.desc.pParameters = desc.rootParams.data();
	desc.desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

	return desc;
}

RootSignatureDesc createMissRootDesc(D3D12_STATIC_SAMPLER_DESC* sampler)
{
	RootSignatureDesc desc;
	desc.range.resize(1);

	// gHDRTexture
	desc.range[0].BaseShaderRegister = 0; //t0
	desc.range[0].NumDescriptors = 1;
	desc.range[0].RegisterSpace = 0;
	desc.range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	desc.range[0].OffsetInDescriptorsFromTableStart = 0;
	//desc.range[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;

	desc.rootParams.resize(1);
	desc.rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	desc.rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
	desc.rootParams[0].DescriptorTable.pDescriptorRanges = desc.range.data();
	desc.rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	// Sampler
	//D3D12_STATIC_SAMPLER_DESC sampler = {};
	sampler->Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler->AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler->AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler->AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler->MipLODBias = 0;
	sampler->MaxAnisotropy = 0;
	sampler->ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	sampler->BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	sampler->MinLOD = 0.0f;
	sampler->MaxLOD = D3D12_FLOAT32_MAX;
	sampler->ShaderRegister = 0; // s0
	sampler->RegisterSpace = 0;
	sampler->ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	desc.desc.NumParameters = 1;
	desc.desc.pParameters = desc.rootParams.data();
	desc.desc.NumStaticSamplers = 1;
	desc.desc.pStaticSamplers = sampler;
	desc.desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;//D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT

	return desc;
}

struct DxilLibrary
{
    DxilLibrary(ID3DBlobPtr pBlob, const WCHAR* entryPoint[], uint32_t entryPointCount) : pShaderBlob(pBlob)
    {
        stateSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
        stateSubobject.pDesc = &dxilLibDesc;

        dxilLibDesc = {};
        exportDesc.resize(entryPointCount);
        exportName.resize(entryPointCount);
        if (pBlob)
        {
            dxilLibDesc.DXILLibrary.pShaderBytecode = pBlob->GetBufferPointer();
            dxilLibDesc.DXILLibrary.BytecodeLength = pBlob->GetBufferSize();
            dxilLibDesc.NumExports = entryPointCount;
            dxilLibDesc.pExports = exportDesc.data();

            for (uint32_t i = 0; i < entryPointCount; i++)
            {
                exportName[i] = entryPoint[i];
                exportDesc[i].Name = exportName[i].c_str();
                exportDesc[i].Flags = D3D12_EXPORT_FLAG_NONE;
                exportDesc[i].ExportToRename = nullptr;
            }
        }
    };

    DxilLibrary() : DxilLibrary(0, nullptr, 0) {} //nullptr instead of 0??

    D3D12_DXIL_LIBRARY_DESC dxilLibDesc = {};
    D3D12_STATE_SUBOBJECT stateSubobject{};
    ID3DBlobPtr pShaderBlob;
    std::vector<D3D12_EXPORT_DESC> exportDesc;
    std::vector<std::wstring> exportName;
};

static const WCHAR* kRayGenShader = L"rayGen";
static const WCHAR* kMissShader = L"miss";
static const WCHAR* kPlaneChs = L"planeChs";
static const WCHAR* kAreaLightChs = L"areaLightChs";
static const WCHAR* kRobotChs = L"robotChs";
static const WCHAR* kPlaneHitGroup = L"PlaneHitGroup";
static const WCHAR* kRobotHitGroup = L"RobotHitGroup";
static const WCHAR* kAreaLightHitGroup = L"AreaLightHitGroup";
static const WCHAR* kShadowMissShader = L"shadowMiss";
static const WCHAR* kShadowChs = L"shadowChs";
static const WCHAR* kShadowHitGroup = L"shadowHitGroup";

struct HitProgram
{
    HitProgram(LPCWSTR ahsExport, LPCWSTR chsExport, const std::wstring& name) : exportName(name)
    {
        desc = {};
        desc.AnyHitShaderImport = ahsExport;
        desc.ClosestHitShaderImport = chsExport;
        desc.HitGroupExport = exportName.c_str();

        subObject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
        subObject.pDesc = &desc;
    }

    std::wstring exportName;
    D3D12_HIT_GROUP_DESC desc;
    D3D12_STATE_SUBOBJECT subObject;
};

struct ExportAssociation
{
    ExportAssociation(const WCHAR* exportNames[], uint32_t exportCount, const D3D12_STATE_SUBOBJECT* pSubobjectToAssociate)
    {
        association.NumExports = exportCount;
        association.pExports = exportNames;
        association.pSubobjectToAssociate = pSubobjectToAssociate;

        subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
        subobject.pDesc = &association;
    }

    D3D12_STATE_SUBOBJECT subobject = {};
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION association = {};
};

struct LocalRootSignature
{
    LocalRootSignature(ID3D12Device5Ptr pDevice, const D3D12_ROOT_SIGNATURE_DESC& desc)
    {
        pRootSig = createRootSignature(pDevice, desc);
        pInterface = pRootSig.GetInterfacePtr();
        subobject.pDesc = &pInterface;
        subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
    }
    ID3D12RootSignaturePtr pRootSig;
    ID3D12RootSignature* pInterface = nullptr;
    D3D12_STATE_SUBOBJECT subobject = {};
};

struct GlobalRootSignature
{
    GlobalRootSignature(ID3D12Device5Ptr pDevice, const D3D12_ROOT_SIGNATURE_DESC& desc)
    {
        pRootSig = createRootSignature(pDevice, desc);
        pInterface = pRootSig.GetInterfacePtr();
        subobject.pDesc = &pInterface;
        subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
    }
    ID3D12RootSignaturePtr pRootSig;
    ID3D12RootSignature* pInterface = nullptr;
    D3D12_STATE_SUBOBJECT subobject = {};
};

struct ShaderConfig
{
    ShaderConfig(uint32_t maxAttributeSizeInBytes, uint32_t maxPayloadSizeInBytes)
    {
        shaderConfig.MaxAttributeSizeInBytes = maxAttributeSizeInBytes;
        shaderConfig.MaxPayloadSizeInBytes = maxPayloadSizeInBytes;

        subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
        subobject.pDesc = &shaderConfig;
    }

    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
    D3D12_STATE_SUBOBJECT subobject = {};
};

struct PipelineConfig
{
    PipelineConfig(uint32_t maxTraceRecursionDepth)
    {
        config.MaxTraceRecursionDepth = maxTraceRecursionDepth;

        subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
        subobject.pDesc = &config;
    }

    D3D12_RAYTRACING_PIPELINE_CONFIG config = {};
    D3D12_STATE_SUBOBJECT subobject = {};
};

void PathTracer::createRtPipelineState()
{
    // Need 20 subobjects:
    //  3/4 for DXIL libraries    
    //  3/4 for the hit-groups (plane hit-group, area light hit-group, robot hit-group) / shadow ray
    //  2 for RayGen root-signature (root-signature and the subobject association)
    //  2 for the plane-hit root-signature (root-signature and the subobject association)
	//  2 for the robot-hit root-signature (root-signature and the subobject association)
	//  2 for the miss root-signature (root-signature and the subobject association)
	//  2 for empty root-signature (root-signature and the subobject association)
    //  2 for shader config (shared between all programs. 1 for the config, 1 for association)
    //  1 for pipeline config
    //  1 for the global root signature
#ifdef HYBRID
	const int numSubobjects = 22;
#else
	const int numSubobjects = 20;
#endif
    std::array<D3D12_STATE_SUBOBJECT, numSubobjects> subobjects;
    uint32_t index = 0;

    // Create the DXIL libraries
	#pragma region
		const WCHAR* entryPointsRayGen[] = { kRayGenShader };
		DxilLibrary rayGenLib = DxilLibrary(compileLibrary(L"Data/RayGeneration.hlsl", L"", L"lib_6_3"), entryPointsRayGen, arraysize(entryPointsRayGen));
		subobjects[index++] = rayGenLib.stateSubobject; // RayGen Library
		
		const WCHAR* entryPointsMiss[] = { kMissShader };
		DxilLibrary missLib = DxilLibrary(compileLibrary(L"Data/Miss.hlsl", L"", L"lib_6_3"), entryPointsMiss, arraysize(entryPointsMiss));
		subobjects[index++] = missLib.stateSubobject; // Miss Library

		const WCHAR* entryPointsHit[] = { kRobotChs, kPlaneChs, kAreaLightChs };
		DxilLibrary hitLib = DxilLibrary(compileLibrary(L"Data/Hit.hlsl", L"", L"lib_6_3"), entryPointsHit, arraysize(entryPointsHit));
		subobjects[index++] = hitLib.stateSubobject; // Hit Library

#ifdef HYBRID
		const WCHAR* entryPointsShadowRay[] = { kShadowChs, kShadowMissShader };
		DxilLibrary shadowRayLib = DxilLibrary(compileLibrary(L"Data/ShadowRay.hlsl", L"", L"lib_6_3"), entryPointsShadowRay, arraysize(entryPointsShadowRay));
		subobjects[index++] = shadowRayLib.stateSubobject; // Shadow ray Library
#endif


	#pragma endregion
	
	//----- Create Hit Programs -----//
	#pragma region
		// Create the plane HitProgram
			HitProgram planeHitProgram(nullptr, kPlaneChs, kPlaneHitGroup);
			subobjects[index++] = planeHitProgram.subObject; // Plane Hit Group

		// Create the area light HitProgram
			HitProgram areaLightHitProgram(nullptr, kAreaLightChs, kAreaLightHitGroup);
			subobjects[index++] = areaLightHitProgram.subObject; // Area Light Hit Group
		
		// Create the robot HitProgram
			HitProgram robotHitProgram(nullptr, kRobotChs, kRobotHitGroup);
			subobjects[index++] = robotHitProgram.subObject; // Robot Hit Group

#ifdef HYBRID
		// Create the Shadow-ray HitProgram
			HitProgram shadowHitProgram(nullptr, kShadowChs, kShadowHitGroup);
			subobjects[index++] = shadowHitProgram.subObject; // Shadow Hit Group
#endif


	#pragma endregion

	//---- Create root-signatures and associations ----//
	#pragma region
		// Create the ray-gen root-signature and association
			LocalRootSignature rgsRootSignature(mpDevice, createRayGenRootDesc().desc);
			subobjects[index] = rgsRootSignature.subobject; // Ray Gen Root Sig

			uint32_t rgsRootIndex = index++;
			ExportAssociation rgsRootAssociation(&kRayGenShader, 1, &(subobjects[rgsRootIndex]));
			subobjects[index++] = rgsRootAssociation.subobject; // Associate Root Sig to RGS

		// Create the plane hit root-signature and association
			LocalRootSignature planeHitRootSignature(mpDevice, createPlaneHitRootDesc().desc);
			subobjects[index] = planeHitRootSignature.subobject; // Plane Hit Root Sig

			uint32_t planeHitRootIndex = index++;
			ExportAssociation planeHitRootAssociation(&kPlaneHitGroup, 1, &(subobjects[planeHitRootIndex]));
			subobjects[index++] = planeHitRootAssociation.subobject; // Associate Plane Hit Root Sig to Plane Hit Group

		// Create the robot hit root-signature and association
			LocalRootSignature robotHitRootSignature(mpDevice, createRobotHitRootDesc().desc);
			subobjects[index] = robotHitRootSignature.subobject; // Robot Hit Root Sig

			uint32_t robotHitRootIndex = index++;
			ExportAssociation robotHitRootAssociation(&kRobotHitGroup, 1, &(subobjects[robotHitRootIndex]));
			subobjects[index++] = robotHitRootAssociation.subobject; // Associate Robot Hit Root Sig to Robot Hit Group

		// Create the miss root-signature and association
			D3D12_STATIC_SAMPLER_DESC sampler = {};
			LocalRootSignature missRootSignature(mpDevice, createMissRootDesc(&sampler).desc);
			subobjects[index] = missRootSignature.subobject; // Miss Root Sig

			uint32_t missRootIndex = index++;
			ExportAssociation missRootAssociation(&kMissShader, 1, &(subobjects[missRootIndex]));
			subobjects[index++] = missRootAssociation.subobject; // Associate Miss Root Sig to Miss shader

		// Create the empty root-signature and associate it with the area light
			D3D12_ROOT_SIGNATURE_DESC emptyDesc = {};
			emptyDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
			LocalRootSignature emptyRootSignature(mpDevice, emptyDesc);
			subobjects[index] = emptyRootSignature.subobject; // Empty Root Sig for Area light

			uint32_t emptyRootIndex = index++;
#ifdef HYBRID
			const WCHAR* emptyRootExport[] = { kAreaLightChs, kShadowChs, kShadowMissShader };
#else
			const WCHAR* emptyRootExport[] = { kAreaLightChs};
#endif
			ExportAssociation emptyRootAssociation(emptyRootExport, arraysize(emptyRootExport), &(subobjects[emptyRootIndex]));
			subobjects[index++] = emptyRootAssociation.subobject; // Associate empty root sig to Area light and Shadow ray
	#pragma endregion

	//---- Create Shader config, Pipeline config and Global Root-Signature----//
	#pragma region
    // Bind the payload size to all programs
    
		ShaderConfig primaryShaderConfig(sizeof(float) * 2, sizeof(float) * 7);
		subobjects[index] = primaryShaderConfig.subobject; // Payload size

		uint32_t primaryShaderConfigIndex = index++;
#ifdef HYBRID
		const WCHAR* primaryShaderExports[] = { kRayGenShader, kMissShader, kPlaneChs, kAreaLightChs, kRobotChs, kShadowMissShader, kShadowChs};
#else
		const WCHAR* primaryShaderExports[] = { kRayGenShader, kMissShader, kPlaneChs, kAreaLightChs, kRobotChs};
#endif
		ExportAssociation primaryConfigAssociation(primaryShaderExports, arraysize(primaryShaderExports), &(subobjects[primaryShaderConfigIndex]));
		subobjects[index++] = primaryConfigAssociation.subobject; // Associate shader config to all programs

    // Create the pipeline config
    
		PipelineConfig config(10); // maxRecursionDepth
		subobjects[index++] = config.subobject; // Recursion depth

    // Create the global root signature and store the empty signature
	
		GlobalRootSignature root(mpDevice, {});
		mpEmptyRootSig = root.pRootSig;
		subobjects[index++] = root.subobject; // Global root signature
	#pragma endregion

	assert(index == numSubobjects);

    // Create the state
    D3D12_STATE_OBJECT_DESC desc;
    desc.NumSubobjects = index;
    desc.pSubobjects = subobjects.data();
    desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;

    d3d_call(mpDevice->CreateStateObject(&desc, IID_PPV_ARGS(&mpRtPipelineState)));
}

//////////////////////////////////////////////////////////////////////////
// Tutorial 05
//////////////////////////////////////////////////////////////////////////
void PathTracer::createShaderTable()
{
    /** The shader-table layout is as follows:
        Entry 0 - Ray-gen program
        Entry 1 - Miss program for the primary ray
		  /Entry 2 - Miss program for the shadow ray
        Entries 2/3,4 - Hit programs for the planes /primary and shadow
		Entries 3/5,6 - Hit program for the area light /primary and shadow
		Entries 4/7,8 - Hit programs for robot /primary and shadow
        All entries in the shader-table must have the same size, so we will choose it base on the largest required entry.
        The primary-ray hit program requires the largest entry - sizeof(program identifier) + 3*8 bytes for the buffer root descriptors.
        The entry size must be aligned up to D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT
    */
#ifdef HYBRID
	const int numShaderTableEntries = 9;
#else
	const int numShaderTableEntries = 5;
#endif

    // Calculate the size and create the buffer
    mShaderTableEntrySize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    mShaderTableEntrySize += 3 * sizeof(UINT64); // The hit shader constant-buffer descriptor
    mShaderTableEntrySize = align_to(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT, mShaderTableEntrySize);
    uint32_t shaderTableSize = mShaderTableEntrySize * numShaderTableEntries;

    // For simplicity, we create the shader-table on the upload heap. You can also create it on the default heap
    mpShaderTable = createBuffer(mpDevice, shaderTableSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
		mpShaderTable->SetName(L"Shader Table");

	// heap info
	D3D12_GPU_VIRTUAL_ADDRESS heapStart = mpCbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart().ptr;
	uint64_t heapEntrySize = mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Map the buffer
	uint8_t* pData;
	d3d_call(mpShaderTable->Map(0, nullptr, (void**)&pData));

		MAKE_SMART_COM_PTR(ID3D12StateObjectProperties);
		ID3D12StateObjectPropertiesPtr pRtsoProps;
		mpRtPipelineState->QueryInterface(IID_PPV_ARGS(&pRtsoProps));

		int entryIndex = 0;
    // Entry 0 - ray-gen program ID and descriptor data
		uint8_t* pEntry0 = pData;
		memcpy(pEntry0, pRtsoProps->GetShaderIdentifier(kRayGenShader), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			pEntry0 += D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
		// Output UAV + TLAS + Camera buffer
			*(D3D12_GPU_VIRTUAL_ADDRESS*) pEntry0 = heapStart;
			pEntry0 += sizeof(D3D12_GPU_VIRTUAL_ADDRESS*);
		// Shadow maps
			*(D3D12_GPU_VIRTUAL_ADDRESS*) pEntry0 = heapStart + 5 * heapEntrySize;
		entryIndex++;

    // Entry 1 - primary ray miss
		uint8_t* pEntry1 = pData + mShaderTableEntrySize * entryIndex;
		memcpy(pEntry1, pRtsoProps->GetShaderIdentifier(kMissShader), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			pEntry1 += D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
		// HDR Texture
			*(D3D12_GPU_VIRTUAL_ADDRESS*) pEntry1 = heapStart + 3 * heapEntrySize;
		entryIndex++;

#ifdef HYBRID
	// Entry /2 - shadow ray miss
		uint8_t* pEntryShadowMiss = pData + mShaderTableEntrySize * entryIndex;
		memcpy(pEntryShadowMiss, pRtsoProps->GetShaderIdentifier(kShadowMissShader), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
		entryIndex++;
#endif

    // Entry 2/3 - Plane, primary ray. ProgramID, TLAS SRV and Normal buffer
		uint8_t* pEntry2 = pData + mShaderTableEntrySize * entryIndex;
		memcpy(pEntry2, pRtsoProps->GetShaderIdentifier(kPlaneHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			pEntry2 += D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
		// TLAS
			*(D3D12_GPU_VIRTUAL_ADDRESS*) pEntry2 = heapStart + heapEntrySize;
			pEntry2 += sizeof(D3D12_GPU_VIRTUAL_ADDRESS*);
		// Normal buffer
			*(D3D12_GPU_VIRTUAL_ADDRESS*) pEntry2 = mpNormalBuffer[0]->GetGPUVirtualAddress();
		entryIndex++;

#ifdef HYBRID
		// Entry /4 - Plane, shadow ray
		uint8_t* pEntryPlaneShadow = pData + mShaderTableEntrySize * entryIndex;
		memcpy(pEntryPlaneShadow, pRtsoProps->GetShaderIdentifier(kShadowHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
		entryIndex++;
#endif

	// Entry 3/5 - Area Light, primary ray.
		uint8_t* pEntry3 = pData + mShaderTableEntrySize * entryIndex;
		memcpy(pEntry3, pRtsoProps->GetShaderIdentifier(kAreaLightHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
		entryIndex++;

#ifdef HYBRID
		// Entry /6 - Area Light, shadow ray
		uint8_t* pEntryAreaLightShadow = pData + mShaderTableEntrySize * entryIndex;
		memcpy(pEntryAreaLightShadow, pRtsoProps->GetShaderIdentifier(kShadowHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
		entryIndex++;
#endif

	// Entry 4/7 - Robot, primary ray. ProgramID and index-buffer
		uint8_t* pEntry4 = pData + mShaderTableEntrySize * entryIndex;
		memcpy(pEntry4, pRtsoProps->GetShaderIdentifier(kRobotHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			pEntry4 += D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
		// TLAS
			*(D3D12_GPU_VIRTUAL_ADDRESS*) pEntry4 = heapStart + heapEntrySize;
			pEntry4 += sizeof(D3D12_GPU_VIRTUAL_ADDRESS);
		// Index buffer
			*(D3D12_GPU_VIRTUAL_ADDRESS*) pEntry4 = mpIndexBuffer[1]->GetGPUVirtualAddress();
			pEntry4 += sizeof(D3D12_GPU_VIRTUAL_ADDRESS);
		// Normal buffer
			*(D3D12_GPU_VIRTUAL_ADDRESS*) pEntry4 = mpNormalBuffer[1]->GetGPUVirtualAddress();
		entryIndex++;
		
#ifdef HYBRID
		// Entry /8 - Robot, shadow ray
		uint8_t* pEntryRobotShadow = pData + mShaderTableEntrySize * entryIndex;
		memcpy(pEntryRobotShadow, pRtsoProps->GetShaderIdentifier(kShadowHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
		entryIndex++;
#endif

    // Unmap
	assert(entryIndex == numShaderTableEntries);
    mpShaderTable->Unmap(0, nullptr);
}

//////////////////////////////////////////////////////////////////////////
// Tutorial 06
//////////////////////////////////////////////////////////////////////////
void PathTracer::createShaderResources()
{
    // Create the output resource. The dimensions and format should match the swap-chain
    D3D12_RESOURCE_DESC resDesc = {};
    resDesc.DepthOrArraySize = 1;
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // The backbuffer is actually DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, but sRGB formats can't be used with UAVs. We will convert to sRGB ourselves in the shader
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    resDesc.Height = mSwapChainSize.y;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resDesc.MipLevels = 1;
    resDesc.SampleDesc.Count = 1;
    resDesc.Width = mSwapChainSize.x;
    d3d_call(mpDevice->CreateCommittedResource(&kDefaultHeapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&mpOutputResource))); // Starting as copy-source to simplify onFrameRender()
		mpOutputResource->SetName(L"RT Output resource");
	
    // Create an SRV/UAV/CBV descriptor heap. 
	// Need 4 entries 
	//	- 1 UAV for the output
	//	- 1 SRV for the scene
	//	- 1 for the camera
	//	- 1 for the texture
#ifdef HYBRID
	//  - 1 for the light buffer
	//  - 4 for the Shadow map (depth, position, normal, flux)

	mpCbvSrvUavHeap = createDescriptorHeap(mpDevice, 9, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
#else
    mpCbvSrvUavHeap = createDescriptorHeap(mpDevice, 4, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
#endif

	// Step size
	const UINT cbvSrvDescriptorSize = mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	const D3D12_CPU_DESCRIPTOR_HANDLE cbvSrvHeapStart = mpCbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart();

    // Create the UAV. Based on the root signature we created it should be the first entry
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		mpDevice->CreateUnorderedAccessView(mpOutputResource, nullptr, &uavDesc, cbvSrvHeapStart);

    // Create the TLAS SRV right after the UAV. Note that we are using a different SRV desc here
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.RaytracingAccelerationStructure.Location = mTopLevelBuffers.pResult->GetGPUVirtualAddress();
		
		D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = cbvSrvHeapStart;
		srvHandle.ptr += cbvSrvDescriptorSize;
		
		mpDevice->CreateShaderResourceView(nullptr, &srvDesc, srvHandle);

	// Create the CBV for the camera buffer
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
		cbvDesc.BufferLocation = mpCameraBuffer->GetGPUVirtualAddress();
		cbvDesc.SizeInBytes = (mCameraBufferSize + 255) & ~255; // align to 256
	
		D3D12_CPU_DESCRIPTOR_HANDLE cbvHandle = cbvSrvHeapStart;
		cbvHandle.ptr += 2 * cbvSrvDescriptorSize;

		mpDevice->CreateConstantBufferView(&cbvDesc, cbvHandle);

	// Create the SRV for the Environment map.
		D3D12_SHADER_RESOURCE_VIEW_DESC srvTextureDesc = {};
		srvTextureDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvTextureDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		srvTextureDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvTextureDesc.Texture2D.MipLevels = 1;
		srvTextureDesc.Texture2D.MostDetailedMip = 0;
		srvTextureDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		D3D12_CPU_DESCRIPTOR_HANDLE srvTextureHandle = cbvSrvHeapStart;
		srvTextureHandle.ptr += 3 * cbvSrvDescriptorSize;
	
		mpDevice->CreateShaderResourceView(mpHDRTextureBuffer, &srvTextureDesc, srvTextureHandle);

#ifdef HYBRID
	// Create the CBV for the light buffer
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvLightDesc = {};
		cbvLightDesc.BufferLocation = mpLightBuffer->GetGPUVirtualAddress();
		cbvLightDesc.SizeInBytes = (mLightBufferSize + 255) & ~255; // align to 256

		D3D12_CPU_DESCRIPTOR_HANDLE cbvLightHandle = cbvSrvHeapStart;
		cbvLightHandle.ptr += 4 * cbvSrvDescriptorSize;

		mpDevice->CreateConstantBufferView(&cbvLightDesc, cbvLightHandle);
		mLightBufferView = cbvLightHandle;

	// Create the SRV for the Shadow map Depth
		D3D12_SHADER_RESOURCE_VIEW_DESC shadowMapSrvDesc = {};
		shadowMapSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		shadowMapSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
		shadowMapSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		shadowMapSrvDesc.Texture2D.MipLevels = 1;

		D3D12_CPU_DESCRIPTOR_HANDLE srvShadowMapDepthHandle = cbvSrvHeapStart;
		srvShadowMapDepthHandle.ptr += 5 * cbvSrvDescriptorSize;

		mpDevice->CreateShaderResourceView(mpShadowMapTexture_Depth, &shadowMapSrvDesc, srvShadowMapDepthHandle);

	// Create the SRV for the Shadow map Position
		shadowMapSrvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;

		D3D12_CPU_DESCRIPTOR_HANDLE srvShadowMapPositionHandle = cbvSrvHeapStart;
		srvShadowMapPositionHandle.ptr += 6 * cbvSrvDescriptorSize;

		mpDevice->CreateShaderResourceView(mpShadowMapTexture_Position, &shadowMapSrvDesc, srvShadowMapPositionHandle);

	// Create the SRV for the Shadow map Normal
		D3D12_CPU_DESCRIPTOR_HANDLE srvShadowMapNormalHandle = cbvSrvHeapStart;
		srvShadowMapNormalHandle.ptr += 7 * cbvSrvDescriptorSize;

		mpDevice->CreateShaderResourceView(mpShadowMapTexture_Normal, &shadowMapSrvDesc, srvShadowMapNormalHandle);

	// Create the SRV for the Shadow map Normal
		D3D12_CPU_DESCRIPTOR_HANDLE srvShadowMapFluxHandle = cbvSrvHeapStart;
		srvShadowMapFluxHandle.ptr += 8 * cbvSrvDescriptorSize;

		mpDevice->CreateShaderResourceView(mpShadowMapTexture_Flux, &shadowMapSrvDesc, srvShadowMapFluxHandle);

	////////////////// End of SRV/UAV/CBV descriptor heap //////////////////


	// Create a DSV descriptor heap
	// needs 1 entry
	// - 1 DSV for the Shadow Map

	// create a DSV descriptor heap
		mpShadowMapDsvHeap = createDescriptorHeap(mpDevice, 1, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, false);

	// create the depth view
		D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc = {};
		depthStencilViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
		depthStencilViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		depthStencilViewDesc.Texture2D.MipSlice = 0;

		D3D12_CPU_DESCRIPTOR_HANDLE dsvHeapStart = mpShadowMapDsvHeap->GetCPUDescriptorHandleForHeapStart();

		mpDevice->CreateDepthStencilView(mpShadowMapTexture_Depth, nullptr, dsvHeapStart);//null for desc?
		mShadowMapDsv_Depth = mpShadowMapDsvHeap->GetCPUDescriptorHandleForHeapStart();

	// Create a RTV descriptor heap
	// needs 1 entry
	// - 1 RTV for Position
	// - 1 RTV for Normal
	// - 1 RTV for Flux

	// create a RTV descriptor heap
		mpShadowMapRtvHeap = createDescriptorHeap(mpDevice, 3, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, false);

		// Description
			D3D12_RENDER_TARGET_VIEW_DESC renderTargetViewDesc = {};
			renderTargetViewDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			renderTargetViewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
			renderTargetViewDesc.Texture2D.MipSlice = 0;

		// start handle and size
			D3D12_CPU_DESCRIPTOR_HANDLE rtvHeapStart = mpShadowMapRtvHeap->GetCPUDescriptorHandleForHeapStart();
			const UINT rtvDescriptorSize = mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		// Position view
			mpDevice->CreateRenderTargetView(mpShadowMapTexture_Position, &renderTargetViewDesc, rtvHeapStart);
			mShadowMapRtv_Position = rtvHeapStart;
			mShadowMapRTVs[0] = mShadowMapRtv_Position;

		// Normal view
			D3D12_CPU_DESCRIPTOR_HANDLE rtvNormalHandle = rtvHeapStart;
			rtvNormalHandle.ptr += rtvDescriptorSize;

			mpDevice->CreateRenderTargetView(mpShadowMapTexture_Normal, &renderTargetViewDesc, rtvNormalHandle);
			mShadowMapRtv_Normal = rtvNormalHandle;
			mShadowMapRTVs[1] = mShadowMapRtv_Normal;

		// Flux view
			D3D12_CPU_DESCRIPTOR_HANDLE rtvFluxHandle = rtvNormalHandle;
			rtvFluxHandle.ptr += rtvDescriptorSize;

			mpDevice->CreateRenderTargetView(mpShadowMapTexture_Flux, &renderTargetViewDesc, rtvFluxHandle);
			mShadowMapRtv_Flux = rtvFluxHandle;
			mShadowMapRTVs[2] = mShadowMapRtv_Flux;

#endif
}


//////////////////////////////////////////////////////////////////////////
// My own functions
//////////////////////////////////////////////////////////////////////////
void PathTracer::readKeyboardInput(bool *gKeys)
{
	mCameraSpeed = 0.05f * 60.0f*mDeltaTime*0.001f;

	if (gKeys[VK_LEFT])
	{
		float angle = mCamera.cameraAngle + 0.5f * mCameraSpeed;
		mCamera.cameraDirection = vec3(eulerAngleY(-angle) * vec4(0,0,1,1)); 
		mCamera.cameraAngle = angle;
	}
	else if (gKeys[VK_RIGHT])
	{
		float angle = mCamera.cameraAngle - 0.5f * mCameraSpeed;
		mCamera.cameraDirection = vec3(eulerAngleY(-angle) * vec4(0, 0, 1, 1));
		mCamera.cameraAngle = angle;
	}
	if (gKeys['W'])
	{
		mCamera.cameraPosition += mCamera.cameraDirection * mCameraSpeed;
	}
	else if (gKeys['S'])
	{
		mCamera.cameraPosition -= mCamera.cameraDirection * mCameraSpeed;
	}
	if (gKeys['A'])
	{
		mCamera.cameraPosition += mCameraSpeed * vec3(-mCamera.cameraDirection.z, 0, mCamera.cameraDirection.x);
	}
	else if (gKeys['D'])
	{
		mCamera.cameraPosition -= mCameraSpeed * vec3(-mCamera.cameraDirection.z, 0, mCamera.cameraDirection.x);
	}
	if (gKeys['E'])
	{
		mCamera.cameraPosition.y += mCameraSpeed;
	}
	else if (gKeys['Q'])
	{
		mCamera.cameraPosition.y -= mCameraSpeed;
	}

#ifdef HYBRID
	// Light
	if (gKeys['F'])
	{
		float angle = 0.5f * mCameraSpeed;
		mLight.direction = vec3(eulerAngleY(-angle) * vec4(mLight.direction, 1));	
	}
	else if (gKeys['K'])
	{
		float angle = -0.5f * mCameraSpeed;
		mLight.direction = vec3(eulerAngleY(-angle) * vec4(mLight.direction, 1));
	}
	if (gKeys['Y'])
	{
		mLight.position += mLight.direction * mCameraSpeed;
	}
	else if (gKeys['H'])
	{
		mLight.position -= mLight.direction * mCameraSpeed;
	}
	if (gKeys['G'])
	{
		mLight.position += mCameraSpeed * vec3(-mLight.direction.z, 0, mLight.direction.x);
	}
	else if (gKeys['J'])
	{
		mLight.position -= mCameraSpeed * vec3(-mLight.direction.z, 0, mLight.direction.x);
	}
	if (gKeys['U'])
	{
		mLight.position.y += mCameraSpeed;
	}
	else if (gKeys['T'])
	{
		mLight.position.y -= mCameraSpeed;
	}
#endif


}

void PathTracer::createCameraBuffer()
{
	// Create camera buffer
	uint32_t nbVec = 3; // Position and Direction
	mCameraBufferSize = nbVec * sizeof(vec4);
	mpCameraBuffer = createBuffer(mpDevice, mCameraBufferSize, D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
	mpCameraBuffer->SetName(L"Camera buffer");
}

void PathTracer::updateCameraBuffer()
{

	HRESULT deviceError = mpDevice->GetDeviceRemovedReason();
	if (deviceError != S_OK)
	{
		int bp = 1;
	}

	uint8_t* pData;
	d3d_call(mpCameraBuffer->Map(0, nullptr, (void**)&pData));
	memcpy(	pData, 
			&mCamera.cameraPosition, 
			sizeof(mCamera.cameraPosition)
			);
	memcpy(	pData + sizeof(mCamera.cameraPosition), 
			&mCamera.cameraAngle, 
			sizeof(mCamera.cameraAngle)
			);
	memcpy(	pData + sizeof(mCamera.cameraPosition) + sizeof(mCamera.cameraAngle),
			&frameCount, sizeof(frameCount)
			);
	mpCameraBuffer->Unmap(0, nullptr);
}

#ifdef HYBRID
void PathTracer::createRasterPipelineState()
{
	D3D12_DESCRIPTOR_RANGE range[1];
	// Light buffer
	range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	range[0].NumDescriptors = 1;
	range[0].BaseShaderRegister = 0; //b0
	range[0].RegisterSpace = 0;
	range[0].OffsetInDescriptorsFromTableStart = 0;

	D3D12_ROOT_PARAMETER rootParameters[3];
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
	rootParameters[0].DescriptorTable.pDescriptorRanges = range;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	// Model transform buffer
	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[1].Descriptor.RegisterSpace = 0;
	rootParameters[1].Descriptor.ShaderRegister = 1; // b1
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	// normals
	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	rootParameters[2].Descriptor.RegisterSpace = 0;
	rootParameters[2].Descriptor.ShaderRegister = 0;//t0
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

	// Root signature
	RootSignatureDesc desc;
	desc.desc.NumParameters = 3;
	desc.desc.pParameters = rootParameters;
	desc.desc.NumStaticSamplers = 0;
	desc.desc.pStaticSamplers = nullptr;
	desc.desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	mpRasterRootSig = createRootSignature(mpDevice, desc.desc);

	// Compile shaders
	ID3DBlobPtr vertexShader = compileLibrary(L"Data/RasterShaders.hlsl", L"VSMain", L"vs_6_3");
	ID3DBlobPtr pixelShader = compileLibrary(L"Data/RasterShaders.hlsl", L"PSMain", L"ps_6_3");

	// Define the vertex input layout.
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	// Create the graphics pipeline state object (PSO).
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
	psoDesc.pRootSignature = mpRasterRootSig.GetInterfacePtr();
	psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.GetInterfacePtr());
	psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.GetInterfacePtr());
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState.DepthEnable = TRUE;
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	psoDesc.DepthStencilState.StencilEnable = FALSE;
	psoDesc.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
	psoDesc.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
	const D3D12_DEPTH_STENCILOP_DESC defaultStencilOp =
	{ D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS };
	psoDesc.DepthStencilState.FrontFace = defaultStencilOp;
	psoDesc.DepthStencilState.BackFace = defaultStencilOp;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 3;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
	psoDesc.RTVFormats[1] = DXGI_FORMAT_R32G32B32A32_FLOAT;
	psoDesc.RTVFormats[2] = DXGI_FORMAT_R32G32B32A32_FLOAT;
	psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	psoDesc.SampleDesc.Count = 1;

	d3d_call(mpDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mpRasterPipelineState)));
}

void PathTracer::createLightBuffer()
{
	// Create light buffer
	mpLightBuffer = createBuffer(mpDevice, mLightBufferSize, D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
	mpLightBuffer->SetName(L"Light Buffer");

	// Set up Light values
	float fovAngle = glm::quarter_pi<float>();
	
	// Left-hand system, depth from 0 to 1
	float fFar = 40.0f;
	mLight.projMat = glm::perspectiveFovLH_ZO(fovAngle, (float) kShadowMapWidth, (float) kShadowMapHeight, 0.1f, fFar);
	// Make depth linear
	/*mLight.projMat[2][2] /= fFar;
	mLight.projMat[3][2] /= fFar;*/
}

void PathTracer::updateLightBuffer()
{
	mLight.eye = mLight.position;//mCamera.cameraPosition; 
	mLight.at = mLight.direction;//mCamera.cameraDirection; 
	// up vector constant

	vec3 center = mLight.eye + mLight.at;
	mLight.viewMat = lookAtLH(mLight.eye, center, mLight.up);
	// projMat constant

	uint8_t* pData;
	d3d_call(mpLightBuffer->Map(0, nullptr, (void**)&pData));
	memcpy(pData,
		&mLight.viewMat,
		sizeof(mLight.viewMat));
	memcpy(pData + sizeof(mLight.viewMat),
		&mLight.projMat,
		sizeof(mLight.projMat));
	mpLightBuffer->Unmap(0, nullptr);
}

void PathTracer::updateTransformBuffers()
{
	for (int i = 0; i < mNumInstances; i++)
	{
		uint8_t* pData;
		d3d_call(mpTransformBuffer[i]->Map(0, nullptr, (void**)&pData));
			memcpy(pData, &mTransforms[i], sizeof(mat4));
		mpTransformBuffer[i]->Unmap(0, nullptr);
	}
}

void PathTracer::createShadowMapTextures()
{
	D3D12_RESOURCE_DESC shadowTexDesc;
	shadowTexDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	shadowTexDesc.Alignment = 0;
	shadowTexDesc.Width = kShadowMapWidth;
	shadowTexDesc.Height = kShadowMapHeight;
	shadowTexDesc.DepthOrArraySize = 1;
	shadowTexDesc.MipLevels = 1;
	shadowTexDesc.Format = DXGI_FORMAT_D32_FLOAT;//D32_float? R32_typeless??
	shadowTexDesc.SampleDesc.Count = 1;
	shadowTexDesc.SampleDesc.Quality = 0;
	shadowTexDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	shadowTexDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE clearValue;
	clearValue.Format = DXGI_FORMAT_D32_FLOAT;
	clearValue.DepthStencil.Depth = 1.0f;
	clearValue.DepthStencil.Stencil = 0;

	d3d_call(mpDevice->CreateCommittedResource(
		&kDefaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&shadowTexDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&clearValue,
		IID_PPV_ARGS(&mpShadowMapTexture_Depth)
	));
	mpShadowMapTexture_Depth->SetName(L"RSM Depth");

	// Create resources for Position, Normal and Flux
		shadowTexDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		shadowTexDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

		/*D3D12_CLEAR_VALUE rgbClearValue;
		rgbClearValue.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		rgbClearValue.Color = 1.0f;*/
		
		// position

			d3d_call(mpDevice->CreateCommittedResource(
				&kDefaultHeapProps,
				D3D12_HEAP_FLAG_NONE,
				&shadowTexDesc,
				D3D12_RESOURCE_STATE_RENDER_TARGET,
				nullptr,
				IID_PPV_ARGS(&mpShadowMapTexture_Position)
			));
			mpShadowMapTexture_Position->SetName(L"RSM Position");

		// normal
			d3d_call(mpDevice->CreateCommittedResource(
				&kDefaultHeapProps,
				D3D12_HEAP_FLAG_NONE,
				&shadowTexDesc,
				D3D12_RESOURCE_STATE_RENDER_TARGET,
				nullptr,
				IID_PPV_ARGS(&mpShadowMapTexture_Normal)
			));
			mpShadowMapTexture_Normal->SetName(L"RSM Normal");

		// flux
			d3d_call(mpDevice->CreateCommittedResource(
				&kDefaultHeapProps,
				D3D12_HEAP_FLAG_NONE,
				&shadowTexDesc,
				D3D12_RESOURCE_STATE_RENDER_TARGET,
				nullptr,
				IID_PPV_ARGS(&mpShadowMapTexture_Flux)
			));
			mpShadowMapTexture_Flux->SetName(L"RSM Flux");

}

void PathTracer::renderDepthToTexture()
{
	// Set pipeline state
	mpCmdList->SetPipelineState(mpRasterPipelineState);

	// Set Root signature
	mpCmdList->SetGraphicsRootSignature(mpRasterRootSig.GetInterfacePtr());

	// Set descriptor heaps
	ID3D12DescriptorHeap* ppHeaps[] = { mpCbvSrvUavHeap.GetInterfacePtr() };// , mpDsvHeap};
	mpCmdList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	// set "Shader Table", i.e. resources for root signature
	D3D12_GPU_DESCRIPTOR_HANDLE lightBufferHandle = mpCbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart();
	lightBufferHandle.ptr += 4 * mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	mpCmdList->SetGraphicsRootDescriptorTable(0, lightBufferHandle); // b0

	// viewport
	mpCmdList->RSSetViewports(1, &mRasterViewPort);
	mpCmdList->RSSetScissorRects(1, &mRasterScissorRect);

	mpCmdList->OMSetStencilRef(0);

	mpCmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mpShadowMapTexture_Depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE));

	// clear shadow map
	mpCmdList->ClearDepthStencilView(mShadowMapDsv_Depth, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	mpCmdList->ClearRenderTargetView(mShadowMapRtv_Position, clearColor, 0, nullptr);
	mpCmdList->ClearRenderTargetView(mShadowMapRtv_Flux, clearColor, 0, nullptr);
	float clearNormal[4] = { 0.5f, 0.5f, 1.0f, 0.0f };
	mpCmdList->ClearRenderTargetView(mShadowMapRtv_Normal, clearNormal, 0, nullptr);

	// set render target
	// TODO: fix
	mpCmdList->OMSetRenderTargets(
								3,
								mShadowMapRTVs,
								false,
								&mShadowMapDsv_Depth
								);

	mpCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	
	// Render Plane

		// Model to World Transform
		mpCmdList->SetGraphicsRootConstantBufferView(1, mpTransformBuffer[0]->GetGPUVirtualAddress());
		// Normal buffer
		mpCmdList->SetGraphicsRootShaderResourceView(2, mpNormalBuffer[0]->GetGPUVirtualAddress());
		// Vertex and Index buffers
		mpCmdList->IASetVertexBuffers(0, 1, &mVertexBufferView[0]);
		mpCmdList->IASetIndexBuffer(&mIndexBufferView[0]);

		// Draw
		mpCmdList->DrawIndexedInstanced(mIndexBufferView[0].SizeInBytes / sizeof(uint), 1, 0, 0, 0);

	// render robot

		// Model to World Transform
		mpCmdList->SetGraphicsRootConstantBufferView(1, mpTransformBuffer[2]->GetGPUVirtualAddress());
		// Normal buffer
		mpCmdList->SetGraphicsRootShaderResourceView(2, mpNormalBuffer[1]->GetGPUVirtualAddress());
		// Vertex and Index buffers
		mpCmdList->IASetVertexBuffers(0, 1, &mVertexBufferView[1]);
		mpCmdList->IASetIndexBuffer(&mIndexBufferView[1]);

		// Draw
		mpCmdList->DrawIndexedInstanced(mIndexBufferView[1].SizeInBytes / sizeof(uint), 1, 0, 0, 0);

	// Render area light

		// Model to World Transform
		mpCmdList->SetGraphicsRootConstantBufferView(1, mpTransformBuffer[1]->GetGPUVirtualAddress());
		// Normal buffer
		mpCmdList->SetGraphicsRootShaderResourceView(2, mpNormalBuffer[0]->GetGPUVirtualAddress());
		// Vertex and Index buffers
		mpCmdList->IASetVertexBuffers(0, 1, &mVertexBufferView[0]);
		mpCmdList->IASetIndexBuffer(&mIndexBufferView[0]);

		// Draw
		mpCmdList->DrawIndexedInstanced(mIndexBufferView[0].SizeInBytes / sizeof(uint), 1, 0, 0, 0);


	// submit command list and reset
	mpCmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mpShadowMapTexture_Depth, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
	mFenceValue = submitCommandList(mpCmdList, mpCmdQueue, mpFence, mFenceValue);
	mpFence->SetEventOnCompletion(mFenceValue, mFenceEvent);
	WaitForSingleObject(mFenceEvent, INFINITE);
	mFrameObjects[mpSwapChain->GetCurrentBackBufferIndex()].pCmdAllocator->Reset();
	mpCmdList->Reset(mFrameObjects[mpSwapChain->GetCurrentBackBufferIndex()].pCmdAllocator, nullptr);


}

#endif

//////////////////////////////////////////////////////////////////////////
// Callbacks
//////////////////////////////////////////////////////////////////////////
void PathTracer::onLoad(HWND winHandle, uint32_t winWidth, uint32_t winHeight)
{
    initDXR(winHandle, winWidth, winHeight);        // Tutorial 02
	buildTransforms(mRotation);
    createAccelerationStructures();                 // Tutorial 03
    createRtPipelineState();                        // Tutorial 04
#ifdef HYBRID
	createRasterPipelineState();
	createLightBuffer();
	createShadowMapTextures();
	createTransformBuffers();
#endif
	createCameraBuffer();							// My own
	createHDRTextureBuffer();
    createShaderResources();                        // Tutorial 06
	createShaderTable();                            // Tutorial 05

	// Submit command list and wait for completion
		mFenceValue = submitCommandList(mpCmdList, mpCmdQueue, mpFence, mFenceValue);
		mpFence->SetEventOnCompletion(mFenceValue, mFenceEvent);
		WaitForSingleObject(mFenceEvent, INFINITE);

		mFrameObjects[0].pCmdAllocator->Reset();
		mpCmdList->Reset(mFrameObjects[0].pCmdAllocator, nullptr);
}

void PathTracer::onFrameRender(bool *gKeys)
{	
	readKeyboardInput(gKeys);
	
	// Update object transforms
	buildTransforms(mRotation);
	mRotation += 0.5f*mCameraSpeed;

#ifdef HYBRID

	// Update transform buffer
	updateTransformBuffers();

	// Update light buffer
	updateLightBuffer();

	// Rasterize
	renderDepthToTexture();

#endif

    uint32_t rtvIndex = beginFrame();

	// Update camera
	updateCameraBuffer();

    // Refit the top-level acceleration structure
    buildTopLevelAS(mpDevice, mpCmdList, mpBottomLevelAS, mTlasSize, true, mTransforms, mTopLevelBuffers);
    

    // Let's raytrace
    resourceBarrier(mpCmdList, mpOutputResource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    D3D12_DISPATCH_RAYS_DESC raytraceDesc = {};
    raytraceDesc.Width = mSwapChainSize.x;
    raytraceDesc.Height = mSwapChainSize.y;
    raytraceDesc.Depth = 1;

    // RayGen is the first entry in the shader-table
    raytraceDesc.RayGenerationShaderRecord.StartAddress = mpShaderTable->GetGPUVirtualAddress() + 0 * mShaderTableEntrySize;
    raytraceDesc.RayGenerationShaderRecord.SizeInBytes = mShaderTableEntrySize;

    // Miss is the second entry in the shader-table
    size_t missOffset = 1 * mShaderTableEntrySize;
#ifdef HYBRID
	raytraceDesc.MissShaderTable.StartAddress = mpShaderTable->GetGPUVirtualAddress() + missOffset;
	raytraceDesc.MissShaderTable.StrideInBytes = mShaderTableEntrySize;
	raytraceDesc.MissShaderTable.SizeInBytes = mShaderTableEntrySize * 2;   // 2 miss-entries
#else
    raytraceDesc.MissShaderTable.StartAddress = mpShaderTable->GetGPUVirtualAddress() + missOffset;
    raytraceDesc.MissShaderTable.StrideInBytes = mShaderTableEntrySize;
    raytraceDesc.MissShaderTable.SizeInBytes = mShaderTableEntrySize * 1;   // 1 miss-entries
#endif

#ifdef HYBRID
	// Hit is the fourth entry in the shader-table
	size_t hitOffset = 3 * mShaderTableEntrySize;
	raytraceDesc.HitGroupTable.StartAddress = mpShaderTable->GetGPUVirtualAddress() + hitOffset;
	raytraceDesc.HitGroupTable.StrideInBytes = mShaderTableEntrySize;
	raytraceDesc.HitGroupTable.SizeInBytes = mShaderTableEntrySize * 6;    // 6 hit-entries
#else
    // Hit is the third entry in the shader-table
    size_t hitOffset = 2 * mShaderTableEntrySize;
    raytraceDesc.HitGroupTable.StartAddress = mpShaderTable->GetGPUVirtualAddress() + hitOffset;
    raytraceDesc.HitGroupTable.StrideInBytes = mShaderTableEntrySize;
    raytraceDesc.HitGroupTable.SizeInBytes = mShaderTableEntrySize * 3;    // 3 hit-entries
#endif


    // Bind the empty root signature
    mpCmdList->SetComputeRootSignature(mpEmptyRootSig);

    // Dispatch
    mpCmdList->SetPipelineState1(mpRtPipelineState.GetInterfacePtr());
    mpCmdList->DispatchRays(&raytraceDesc);

    // Copy the results to the back-buffer
    resourceBarrier(mpCmdList, mpOutputResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    resourceBarrier(mpCmdList, mFrameObjects[rtvIndex].pSwapChainBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
	mpCmdList->CopyResource(mFrameObjects[rtvIndex].pSwapChainBuffer, mpOutputResource);//mpShadowMapTexture); 

    endFrame(rtvIndex);
}

void PathTracer::onShutdown()
{
    // Wait for the command queue to finish execution
    mFenceValue++;
    mpCmdQueue->Signal(mpFence, mFenceValue);
    mpFence->SetEventOnCompletion(mFenceValue, mFenceEvent);
    WaitForSingleObject(mFenceEvent, INFINITE);
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
	Framework::run(PathTracer(), "Path-tracer");
}
