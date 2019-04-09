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
        vec3(-5, 0,  -5),
        vec3( 5, 0,  5),
        vec3(-5, 0,  5),
        vec3( 5, 0,  -5),
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
		1,
		2,
		0,
		3,
		1
	};

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

ID3D12ResourcePtr createTeapotVB(ID3D12Device5Ptr pDevice, aiVector3D* aiVertecies)
{
	vec3 vertices[1388];
	for (uint i = 0; i < 1388; i++)
	{
		vertices[i] = vec3(aiVertecies[i].x, aiVertecies[i].y, aiVertecies[i].z);
	}

	ID3D12ResourcePtr pBuffer = createBuffer(pDevice, sizeof(vertices), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
	uint8_t* pData;
	pBuffer->Map(0, nullptr, (void**)&pData);
	memcpy(pData, vertices, sizeof(vertices));
	pBuffer->Unmap(0, nullptr);
	return pBuffer;
}

ID3D12ResourcePtr createTeapotIB(ID3D12Device5Ptr pDevice, aiFace* aiIndices)
{

	uint indices[1226 * 3];
	for (uint i = 0; i < 1226; i++)
	{
		indices[3*i] = aiIndices[i].mIndices[0];
		indices[3*i + 1] = aiIndices[i].mIndices[1];
		indices[3*i + 2] = aiIndices[i].mIndices[2];
	}

	ID3D12ResourcePtr pBuffer = createBuffer(pDevice, sizeof(indices), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
	uint8_t* pData;
	pBuffer->Map(0, nullptr, (void**)&pData);
	memcpy(pData, indices, sizeof(indices));
	pBuffer->Unmap(0, nullptr);
	return pBuffer;
}

ID3D12ResourcePtr createTeapotNB(ID3D12Device5Ptr pDevice, aiVector3D* aiVertecies)
{
	vec3 normals[1388];
	for (uint i = 0; i < 1388; i++)
	{
		normals[i] = vec3(aiVertecies[i].x, aiVertecies[i].y, aiVertecies[i].z);
	}

	ID3D12ResourcePtr pBuffer = createBuffer(pDevice, sizeof(normals), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
	uint8_t* pData;
	pBuffer->Map(0, nullptr, (void**)&pData);
	memcpy(pData, normals, sizeof(normals));
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

void buildTopLevelAS(ID3D12Device5Ptr pDevice, ID3D12GraphicsCommandList4Ptr pCmdList, ID3D12ResourcePtr pBottomLevelAS[3], uint64_t& tlasSize, float rotation, bool update, PathTracer::AccelerationStructureBuffers& buffers)
{
	int numInstances = 5;

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

	// The transformation matrices for the instances
	mat4 transformation[5]; // hard coded number, doesn't have to be equal to numInstances
	mat4 rotationMat = eulerAngleY(rotation*0.5f);
	mat4 rotationMat2 = eulerAngleY(rotation*0.25f);
	// planes
	transformation[0] = scale(10.0f*vec3(1.0f, 1.0f, 1.0f));
	transformation[1] = translate(mat4(), vec3(-5.0, 5.0, 0.0)) * eulerAngleZ(-0.5f*pi<float>());
	// area light
	transformation[2] = translate(mat4(), vec3(0.0, 15, 0.0)) * eulerAngleX(pi<float>()) * scale(vec3(0.5f, 0.5f, 0.5f));
	// robots
	transformation[3] = translate(mat4(), vec3(0, 1.39, 0)) * scale(3.0f*vec3(1.0f, 1.0f, 1.0f));
	transformation[4] = translate(mat4(), vec3(3.0, 1.39, -4.2)) * eulerAngleY(-0.55f*pi<float>()) * scale(3.0f*vec3(1.0f, 1.0f, 1.0f));


	// The InstanceContributionToHitGroupIndex is set based on the shader-table layout specified in createShaderTable()
	int instanceIdx = 0;
	// Create the desc for the planes
	for (uint32_t i = 0; i < 2; i++)
	{
		instanceDescs[instanceIdx].InstanceID = i;// This value will be exposed to the shader via InstanceID()
		instanceDescs[instanceIdx].InstanceContributionToHitGroupIndex = 0; // hard coded
		instanceDescs[instanceIdx].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		// GLM is column major, the INSTANCE_DESC is row major
		memcpy(instanceDescs[instanceIdx].Transform, &transpose(transformation[instanceIdx]), sizeof(instanceDescs[instanceIdx].Transform));
		instanceDescs[instanceIdx].AccelerationStructure = pBottomLevelAS[0]->GetGPUVirtualAddress();
		instanceDescs[instanceIdx].InstanceMask = 0xFF;

		instanceIdx++;
	}

	// Create the desc for the Area Light
	instanceDescs[instanceIdx].InstanceID = 0;// This value will be exposed to the shader via InstanceID()
	instanceDescs[instanceIdx].InstanceContributionToHitGroupIndex = 1; // hard coded
	instanceDescs[instanceIdx].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
	// GLM is column major, the INSTANCE_DESC is row major
	memcpy(instanceDescs[instanceIdx].Transform, &transpose(transformation[instanceIdx]), sizeof(instanceDescs[instanceIdx].Transform));
	instanceDescs[instanceIdx].AccelerationStructure = pBottomLevelAS[0]->GetGPUVirtualAddress();
	instanceDescs[instanceIdx].InstanceMask = 0xFF;

	instanceIdx++;

	// Create the desc for the robots
	for (uint32_t i = 0; i < 2; i++)
	{
		instanceDescs[instanceIdx].InstanceID = i; // This value will be exposed to the shader via InstanceID()
		instanceDescs[instanceIdx].InstanceContributionToHitGroupIndex = 2;  // hard coded
		instanceDescs[instanceIdx].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		mat4 m = transpose(transformation[instanceIdx]); // GLM is column major, the INSTANCE_DESC is row major
		memcpy(instanceDescs[instanceIdx].Transform, &m, sizeof(instanceDescs[instanceIdx].Transform));
		instanceDescs[instanceIdx].AccelerationStructure = pBottomLevelAS[1]->GetGPUVirtualAddress();
		instanceDescs[instanceIdx].InstanceMask = 0xFF;

		instanceIdx++;
	}

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
	mpIndexBuffer[0] = createPlaneIB(mpDevice);
	mpNormalBuffer[0] = createPlaneNB(mpDevice);


	// Load teapot
	const aiScene* teapotScene = importer.ReadFile("Data/Models/robot.fbx",
		aiProcess_CalcTangentSpace		|
		aiProcess_JoinIdenticalVertices	|
		aiProcess_Triangulate			|
		aiProcess_GenNormals			|
		aiProcess_FixInfacingNormals	|
		aiProcess_GenUVCoords			|
		aiProcess_TransformUVCoords		|
		aiProcess_MakeLeftHanded		|
		aiProcess_FindInvalidData);
	aiMesh* teapot = teapotScene->mMeshes[0];
	
	mpVertexBuffer[1] = createTeapotVB(mpDevice, teapot->mVertices);
	mpIndexBuffer[1] = createTeapotIB(mpDevice, teapot->mFaces);
	mpNormalBuffer[1] = createTeapotNB(mpDevice, teapot->mNormals);
	


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

	// The second bottom-level buffer is for the teapot
		ID3D12ResourcePtr vertexBufferTeapot[] = { mpVertexBuffer[1] };
		const uint32_t vertexCountTeapot[] = { teapot->mNumVertices };
		ID3D12ResourcePtr indexBufferTeapot[] = { mpIndexBuffer[1] };
		const uint32_t indexCountTeapot[] = { teapot->mNumFaces * 3};
		bottomLevelBuffers[1] = createBottomLevelAS(
													mpDevice, 
													mpCmdList, 
													vertexBufferTeapot, 
													vertexCountTeapot, 
													indexBufferTeapot, 
													indexCountTeapot, 
													1
												   );
		mpBottomLevelAS[1] = bottomLevelBuffers[1].pResult;

    // Create the TLAS
    buildTopLevelAS(mpDevice, mpCmdList, mpBottomLevelAS, mTlasSize, false, 0, mTopLevelBuffers);

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
ID3DBlobPtr compileLibrary(const WCHAR* filename, const WCHAR* targetString)
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
    d3d_call(pCompiler->Compile(pTextBlob, filename, L"", targetString, nullptr, 0, nullptr, 0, dxcIncludeHandler, &pResult));

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
    desc.range.resize(3);
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

    desc.rootParams.resize(1);
    desc.rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    desc.rootParams[0].DescriptorTable.NumDescriptorRanges = 3;
    desc.rootParams[0].DescriptorTable.pDescriptorRanges = desc.range.data();

    // Create the desc
    desc.desc.NumParameters = 1;
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

RootSignatureDesc createTeapotHitRootDesc()
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
static const WCHAR* kTeapotChs = L"teapotChs";
static const WCHAR* kPlaneHitGroup = L"PlaneHitGroup";
static const WCHAR* kTeapotHitGroup = L"TeapotHitGroup";
static const WCHAR* kAreaLightHitGroup = L"AreaLightHitGroup";


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
    //  3 for DXIL libraries    
    //  3 for the hit-groups (plane hit-group, area light hit-group, teapot hit-group)
    //  2 for RayGen root-signature (root-signature and the subobject association)
    //  2 for the plane-hit root-signature (root-signature and the subobject association)
	//  2 for the teapot-hit root-signature (root-signature and the subobject association)
	//  2 for the miss root-signature (root-signature and the subobject association)
	//  2 for empty root-signature (root-signature and the subobject association)
    //  2 for shader config (shared between all programs. 1 for the config, 1 for association)
    //  1 for pipeline config
    //  1 for the global root signature
    std::array<D3D12_STATE_SUBOBJECT,20> subobjects;
    uint32_t index = 0;

    // Create the DXIL libraries
	#pragma region
		const WCHAR* entryPointsRayGen[] = { kRayGenShader };
		DxilLibrary rayGenLib = DxilLibrary(compileLibrary(L"Data/RayGeneration.hlsl", L"lib_6_3"), entryPointsRayGen, arraysize(entryPointsRayGen));
		subobjects[index++] = rayGenLib.stateSubobject; // 0 RayGen Library
		
		const WCHAR* entryPointsMiss[] = { kMissShader };
		DxilLibrary missLib = DxilLibrary(compileLibrary(L"Data/Miss.hlsl", L"lib_6_3"), entryPointsMiss, arraysize(entryPointsMiss));
		subobjects[index++] = missLib.stateSubobject; // 1 Miss Library

		const WCHAR* entryPointsHit[] = { kTeapotChs, kPlaneChs, kAreaLightChs };
		DxilLibrary hitLib = DxilLibrary(compileLibrary(L"Data/Hit.hlsl", L"lib_6_3"), entryPointsHit, arraysize(entryPointsHit));
		subobjects[index++] = hitLib.stateSubobject; // 2 Hit Library

	#pragma endregion
	
	//----- Create Hit Programs -----//
	#pragma region
		// Create the plane HitProgram
			HitProgram planeHitProgram(nullptr, kPlaneChs, kPlaneHitGroup);
			subobjects[index++] = planeHitProgram.subObject; // 3 Plane Hit Group

		// Create the area light HitProgram
			HitProgram areaLightHitProgram(nullptr, kAreaLightChs, kAreaLightHitGroup);
			subobjects[index++] = areaLightHitProgram.subObject; //4 Area Light Hit Group
		
		// Create the teapot HitProgram
			HitProgram teapotHitProgram(nullptr, kTeapotChs, kTeapotHitGroup);
			subobjects[index++] = teapotHitProgram.subObject; // 5 Teapot Hit Group

	#pragma endregion

	//---- Create root-signatures and associations ----//
	#pragma region
		// Create the ray-gen root-signature and association
			LocalRootSignature rgsRootSignature(mpDevice, createRayGenRootDesc().desc);
			subobjects[index] = rgsRootSignature.subobject; // 6 Ray Gen Root Sig

			uint32_t rgsRootIndex = index++;
			ExportAssociation rgsRootAssociation(&kRayGenShader, 1, &(subobjects[rgsRootIndex]));
			subobjects[index++] = rgsRootAssociation.subobject; // 7 Associate Root Sig to RGS

		// Create the plane hit root-signature and association
			LocalRootSignature planeHitRootSignature(mpDevice, createPlaneHitRootDesc().desc);
			subobjects[index] = planeHitRootSignature.subobject; // 8 Plane Hit Root Sig

			uint32_t planeHitRootIndex = index++;
			ExportAssociation planeHitRootAssociation(&kPlaneHitGroup, 1, &(subobjects[planeHitRootIndex]));
			subobjects[index++] = planeHitRootAssociation.subobject; // 9 Associate Plane Hit Root Sig to Plane Hit Group

		// Create the teapot hit root-signature and association
			LocalRootSignature teapotHitRootSignature(mpDevice, createTeapotHitRootDesc().desc);
			subobjects[index] = teapotHitRootSignature.subobject; // 10 Teapot Hit Root Sig

			uint32_t teapotHitRootIndex = index++;
			ExportAssociation teapotHitRootAssociation(&kTeapotHitGroup, 1, &(subobjects[teapotHitRootIndex]));
			subobjects[index++] = teapotHitRootAssociation.subobject; // 11 Associate Teapot Hit Root Sig to Teapot Hit Group

		// Create the miss root-signature and association
			D3D12_STATIC_SAMPLER_DESC sampler = {};
			LocalRootSignature missRootSignature(mpDevice, createMissRootDesc(&sampler).desc);
			subobjects[index] = missRootSignature.subobject; // 12 Miss Root Sig

			uint32_t missRootIndex = index++;
			ExportAssociation missRootAssociation(&kMissShader, 1, &(subobjects[missRootIndex]));
			subobjects[index++] = missRootAssociation.subobject; // 13 Associate Miss Root Sig to Miss shader

		// Create the empty root-signature and associate it with the area light
			D3D12_ROOT_SIGNATURE_DESC emptyDesc = {};
			emptyDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
			LocalRootSignature emptyRootSignature(mpDevice, emptyDesc);
			subobjects[index] = emptyRootSignature.subobject; // 14 Empty Root Sig for Area light

			uint32_t emptyRootIndex = index++;
			const WCHAR* emptyRootExport[] = { kAreaLightChs };
			ExportAssociation emptyRootAssociation(emptyRootExport, arraysize(emptyRootExport), &(subobjects[emptyRootIndex]));
			subobjects[index++] = emptyRootAssociation.subobject; // 15 Associate empty root sig to Area light
	#pragma endregion

	//---- Create Shader config, Pipeline config and Global Root-Signature----//
	#pragma region
    // Bind the payload size to all programs
    
		ShaderConfig primaryShaderConfig(sizeof(float) * 2, sizeof(float) * 7);
		subobjects[index] = primaryShaderConfig.subobject; // 16

		uint32_t primaryShaderConfigIndex = index++;
		const WCHAR* primaryShaderExports[] = { kRayGenShader, kMissShader, kPlaneChs, kAreaLightChs, kTeapotChs};
		ExportAssociation primaryConfigAssociation(primaryShaderExports, arraysize(primaryShaderExports), &(subobjects[primaryShaderConfigIndex]));
		subobjects[index++] = primaryConfigAssociation.subobject; // 17 Associate shader config to all programs

    // Create the pipeline config
    
		PipelineConfig config(10); // maxRecursionDepth
		subobjects[index++] = config.subobject; // 18

    // Create the global root signature and store the empty signature
	
		GlobalRootSignature root(mpDevice, {});
		mpEmptyRootSig = root.pRootSig;
		subobjects[index++] = root.subobject; // 19
	#pragma endregion

    // Create the state
    D3D12_STATE_OBJECT_DESC desc;
    desc.NumSubobjects = index; // 20
    desc.pSubobjects = subobjects.data();
    desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;

    d3d_call(mpDevice->CreateStateObject(&desc, IID_PPV_ARGS(&mpPipelineState)));
}

//////////////////////////////////////////////////////////////////////////
// Tutorial 05
//////////////////////////////////////////////////////////////////////////
void PathTracer::createShaderTable()
{
    /** The shader-table layout is as follows:
        Entry 0 - Ray-gen program
        Entry 1 - Miss program for the primary ray
        Entries 2 - Hit programs for the planes
		Entries 3 - Hit program for the area light
		Entries 4 - Hit programs for teapot
        All entries in the shader-table must have the same size, so we will choose it base on the largest required entry.
        The triangle primary-ray hit program requires the largest entry - sizeof(program identifier) + 8 bytes for the constant-buffer root descriptor.
        The entry size must be aligned up to D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT
    */

    // Calculate the size and create the buffer
    mShaderTableEntrySize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    mShaderTableEntrySize += 3 * sizeof(UINT64); // The hit shader constant-buffer descriptor
    mShaderTableEntrySize = align_to(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT, mShaderTableEntrySize);
    uint32_t shaderTableSize = mShaderTableEntrySize * 5;

    // For simplicity, we create the shader-table on the upload heap. You can also create it on the default heap
    mpShaderTable = createBuffer(mpDevice, shaderTableSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);

    // Map the buffer
		uint8_t* pData;
		d3d_call(mpShaderTable->Map(0, nullptr, (void**)&pData));

		MAKE_SMART_COM_PTR(ID3D12StateObjectProperties);
		ID3D12StateObjectPropertiesPtr pRtsoProps;
		mpPipelineState->QueryInterface(IID_PPV_ARGS(&pRtsoProps));

    // Entry 0 - ray-gen program ID and descriptor data
		memcpy(pData, pRtsoProps->GetShaderIdentifier(kRayGenShader), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
		uint64_t heapStart = mpCbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart().ptr;
		*(uint64_t*)(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES) = heapStart;

    // Entry 1 - primary ray miss
		uint8_t* pEntry1 = pData + mShaderTableEntrySize * 1;
		memcpy(pEntry1, pRtsoProps->GetShaderIdentifier(kMissShader), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
		*(uint64_t*)(pEntry1 + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES) = heapStart + 3 * mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Entry 2 - Planes, primary ray. ProgramID, TLAS SRV and Normal buffer
		uint8_t* pEntry2 = pData + mShaderTableEntrySize * 2;
		memcpy(pEntry2, pRtsoProps->GetShaderIdentifier(kPlaneHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
		// TLAS
		*(uint64_t*)(pEntry2 + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES) = heapStart + mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // The SRV comes directly after the program id
		// Normal buffer
		assert(((uint64_t)(pEntry2 + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + sizeof(UINT64)) % 8) == 0);
		*(D3D12_GPU_VIRTUAL_ADDRESS*)(pEntry2 + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + sizeof(UINT64)) = mpNormalBuffer[0]->GetGPUVirtualAddress();

	// Entry 3 - Area Light, primary ray.
		uint8_t* pEntry3 = pData + mShaderTableEntrySize * 3;
		memcpy(pEntry3, pRtsoProps->GetShaderIdentifier(kAreaLightHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

	// Entry 4 - Teapot, primary ray. ProgramID and index-buffer
		uint8_t* pEntry4 = pData + mShaderTableEntrySize * 4;
		memcpy(pEntry4, pRtsoProps->GetShaderIdentifier(kTeapotHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
		// TLAS
		assert(((uint64_t)(pEntry4 + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES) % 8) == 0);
		*(uint64_t*)(pEntry4 + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES) = heapStart + mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		// Index buffer
		assert(((uint64_t)(pEntry4 + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + sizeof(UINT64)) % 8) == 0);
		*(D3D12_GPU_VIRTUAL_ADDRESS*)(pEntry4 + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + sizeof(UINT64)) = mpIndexBuffer[1]->GetGPUVirtualAddress();
		// Normal buffer
		assert(((uint64_t)(pEntry4 + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + 2*sizeof(UINT64)) % 8) == 0);
		*(D3D12_GPU_VIRTUAL_ADDRESS*)(pEntry4 + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + 2*sizeof(UINT64)) = mpNormalBuffer[1]->GetGPUVirtualAddress();

    // Unmap
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

	
    // Create an SRV/UAV/CBV descriptor heap. 
	// Need 4 entries 
	//	- 1 SRV for the scene
	//	- 1 UAV for the output
	//	- 1 for the camera
	//	- 1 for the texture
    mpCbvSrvUavHeap = createDescriptorHeap(mpDevice, 4, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);


    // Create the UAV. Based on the root signature we created it should be the first entry
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		mpDevice->CreateUnorderedAccessView(mpOutputResource, nullptr, &uavDesc, mpCbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart());

    // Create the TLAS SRV right after the UAV. Note that we are using a different SRV desc here
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.RaytracingAccelerationStructure.Location = mTopLevelBuffers.pResult->GetGPUVirtualAddress();
		
		D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = mpCbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart();
		srvHandle.ptr += mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		
		mpDevice->CreateShaderResourceView(nullptr, &srvDesc, srvHandle);

	// Create the CBV for the camera buffer
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
		cbvDesc.BufferLocation = mpCameraBuffer->GetGPUVirtualAddress();
		uint32_t allignmentConst = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - mCameraBufferSize % D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
		cbvDesc.SizeInBytes = mCameraBufferSize + allignmentConst;
	
		D3D12_CPU_DESCRIPTOR_HANDLE cbvHandle = mpCbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart();
		cbvHandle.ptr += 2 * mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		mpDevice->CreateConstantBufferView(&cbvDesc, cbvHandle);

	// Describe and create a SRV for the texture.
		D3D12_SHADER_RESOURCE_VIEW_DESC srvTextureDesc = {};
		srvTextureDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvTextureDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		srvTextureDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvTextureDesc.Texture2D.MipLevels = 1;
		srvTextureDesc.Texture2D.MostDetailedMip = 0;
		srvTextureDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		D3D12_CPU_DESCRIPTOR_HANDLE srvTextureHandle = mpCbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart();
		srvTextureHandle.ptr += 3 * mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	
		mpDevice->CreateShaderResourceView(mpHDRTextureBuffer, &srvTextureDesc, srvTextureHandle);

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


}

void PathTracer::createCameraBuffer()
{
	// Create camera buffer
	uint32_t nbVec = 3; // Position and Direction
	mCameraBufferSize = nbVec * sizeof(vec4);
	mpCameraBuffer = createBuffer(mpDevice, mCameraBufferSize, D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);

}

void PathTracer::updateCameraBuffer()
{

	HRESULT deviceError = mpDevice->GetDeviceRemovedReason();
	if (deviceError != S_OK)
	{
		int bp = 1;
	}


	/*std::vector<vec4> vectors(3);
	vectors[0] = mCamera.cameraPosition;
	vectors[1] = vec4(mCamera.cameraAngle);
	vectors[2] = vec4((float)frameCount);

	uint8_t* pData;
	d3d_call(mpCameraBuffer->Map(0, nullptr, (void**)&pData));
	memcpy(pData, vectors.data(), mCameraBufferSize);
	mpCameraBuffer->Unmap(0, nullptr);*/

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


//////////////////////////////////////////////////////////////////////////
// Callbacks
//////////////////////////////////////////////////////////////////////////
void PathTracer::onLoad(HWND winHandle, uint32_t winWidth, uint32_t winHeight)
{
    initDXR(winHandle, winWidth, winHeight);        // Tutorial 02
    createAccelerationStructures();                 // Tutorial 03
    createRtPipelineState();                        // Tutorial 04
	createCameraBuffer();							// My own
	createHDRTextureBuffer();
    createShaderResources();                        // Tutorial 06
	createShaderTable();                            // Tutorial 05
}

void PathTracer::onFrameRender(bool *gKeys)
{	
	readKeyboardInput(gKeys);

    uint32_t rtvIndex = beginFrame();

	// Update camera
	updateCameraBuffer();

    // Refit the top-level acceleration structure
    buildTopLevelAS(mpDevice, mpCmdList, mpBottomLevelAS, mTlasSize, mRotation, true, mTopLevelBuffers);
    mRotation += 0.5f*mCameraSpeed;

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
    raytraceDesc.MissShaderTable.StartAddress = mpShaderTable->GetGPUVirtualAddress() + missOffset;
    raytraceDesc.MissShaderTable.StrideInBytes = mShaderTableEntrySize;
    raytraceDesc.MissShaderTable.SizeInBytes = mShaderTableEntrySize * 1;   // 1 miss-entries

    // Hit is the third entry in the shader-table
    size_t hitOffset = 2 * mShaderTableEntrySize;
    raytraceDesc.HitGroupTable.StartAddress = mpShaderTable->GetGPUVirtualAddress() + hitOffset;
    raytraceDesc.HitGroupTable.StrideInBytes = mShaderTableEntrySize;
    raytraceDesc.HitGroupTable.SizeInBytes = mShaderTableEntrySize * 3;    // 3 hit-entries

    // Bind the empty root signature
    mpCmdList->SetComputeRootSignature(mpEmptyRootSig);

    // Dispatch
    mpCmdList->SetPipelineState1(mpPipelineState.GetInterfacePtr());
    mpCmdList->DispatchRays(&raytraceDesc);

    // Copy the results to the back-buffer
    resourceBarrier(mpCmdList, mpOutputResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    resourceBarrier(mpCmdList, mFrameObjects[rtvIndex].pSwapChainBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
    mpCmdList->CopyResource(mFrameObjects[rtvIndex].pSwapChainBuffer, mpOutputResource);

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
