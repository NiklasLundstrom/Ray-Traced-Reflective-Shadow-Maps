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

	// Set up viewPort
	mRasterViewPort.Width = (float)kShadowMapWidth;
	mRasterViewPort.Height = (float)kShadowMapHeight;
	mRasterViewPort.MinDepth = 0.0f;
	mRasterViewPort.MaxDepth = 1.0f;
	mRasterViewPort.TopLeftX = 0.0f;
	mRasterViewPort.TopLeftY = 0.0f;
	// Set up Scissor rectangle
	mRasterScissorRect.left = 0;
	mRasterScissorRect.top = 0;
	mRasterScissorRect.right = kShadowMapWidth;
	mRasterScissorRect.bottom = kShadowMapHeight;

	// Set up viewPort
	mPostProcessingViewPort.Width = (float)winWidth;
	mPostProcessingViewPort.Height = (float)winHeight;
	mPostProcessingViewPort.MinDepth = 0.0f;
	mPostProcessingViewPort.MaxDepth = 1.0f;
	mPostProcessingViewPort.TopLeftX = 0.0f;
	mPostProcessingViewPort.TopLeftY = 0.0f;
	// Set up Scissor rectangle
	mPostProcessingScissorRect.left = 0;
	mPostProcessingScissorRect.top = 0;
	mPostProcessingScissorRect.right = winWidth;
	mPostProcessingScissorRect.bottom = winHeight;

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

void PathTracer::createEnvironmentMapBuffer()
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
	textureDesc.Height = (UINT)image->height;
	textureDesc.Width = (UINT)image->width;
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
		IID_PPV_ARGS(&mpEnvironmentMapBuffer)
	));
	mpEnvironmentMapBuffer->SetName(L"Environment map");

	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(mpEnvironmentMapBuffer, 0, 1);

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
		mpEnvironmentMapBuffer,
		textureUploadHeap,
		0, 0, 1,
		&textureData
	);
	mpCmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mpEnvironmentMapBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

	// Close the command list and execute it to begin the initial GPU setup.
	mFenceValue = submitCommandList(mpCmdList, mpCmdQueue, mpFence, mFenceValue);
	mpFence->SetEventOnCompletion(mFenceValue, mFenceEvent);
	WaitForSingleObject(mFenceEvent, INFINITE);
	mpCmdList->Reset(mFrameObjects[0].pCmdAllocator, nullptr);

}

void PathTracer::buildTransforms(float rotation)
{
	mat4 rotationMat = eulerAngleY(rotation*0.5f);
	//// room
	//mModels["Floor"].setTransform(scale(1.0f*vec3(1.0f, 1.0f, 1.0f)));
	//mModels["Floor Extended"].setTransform(translate(mat4(), vec3(2.5, 0.0f, 7.5)) * eulerAngleY(-half_pi<float>()) * scale(1.0f*vec3(1.0f, 1.0f, 1.0f)));

	//mModels["Back wall"].setTransform(translate(mat4(), vec3(2.5, 0.0f, -2.475)) * eulerAngleY(-half_pi<float>()) * scale(1.0f*vec3(1.0f, 1.0f, 1.0f)));
	//mModels["Back wall extended"].setTransform(translate(mat4(), vec3(5.05, 0.0f, -0.04)) * eulerAngleY(-half_pi<float>()) * scale(1.0f*vec3(1.0f, 1.0f, 1.0f)));

	//mModels["Ceiling"].setTransform(translate(mat4(), vec3(-0.01f, 0, 0)) * scale(1.0f*vec3(1.002f, 1.0f, 1.0f)));
	//mModels["Ceiling extended"].setTransform(translate(mat4(), vec3(-4.325, 5.1f, 8.5f)) * eulerAngleY(half_pi<float>()) * scale(1.0f*vec3(1.00f, 1.0f, 1.0f)));

	//mModels["Window"].setTransform(translate(mat4(), vec3(0.0, -0.5f, 0.0)) * scale(0.2f*vec3(21.25f, 1.0f, 1.0f)));
	//mModels["Front wall extended"].setTransform(translate(mat4(), vec3(0.0075, 0, -0.05)) * eulerAngleY(-half_pi<float>()) * scale(1.0f*vec3(1.0f, 1.0f, 1.0f)));

	//mModels["Left wall outside"].setTransform(translate(mat4(), vec3(0.01f, -0.05f, 0.0)) * scale(1.0f*vec3(1.0f, 1.0f, 1.0f)));
	//mModels["Right wall outside"].setTransform(translate(mat4(), vec3(-5.1, 0, 5)) * scale(1.0f*vec3(1.0f, 1.0f, 1.0f)));

	//mModels["Right wall inside"].setTransform(translate(mat4(), vec3(-0.01f, 0, 0)) * scale(1.0f*vec3(1.002f, 1.0f, 1.0f)));
	//mModels["Left wall extended"].setTransform(translate(mat4(), vec3(0, 0, 13.5)) * scale(1.0f*vec3(1.00f, 1.0f, 1.0f)));

	//// robot
	//mModels["Robot"].setTransform(rotationMat * translate(mat4(), vec3(1.5f, 0.347499f, 0.0f)) * scale(0.75f*vec3(1.0f, 1.0f, 1.0f)));
	//mModels["Teapot"].setTransform(translate(mat4(), vec3(3.5f, 9.32*0.05f, 1.5f)) * eulerAngleY(0.0f) * scale(0.055f*vec3(1.0f, 1.0f, 1.0f)));

#ifdef OFFLINE
	// area light
	mModels["Area light"].setTransform(translate(mat4(), mLight.eye) * scale((0.00517905410f/0.255999625f)*vec3(1.0f, 1.0f, 1.0f)));
#endif
	// Sun temple
	mModels["Sun temple"].setTransform(translate(mat4(), vec3(0.0, 0.0, 0.0/*5.0*/)) * scale(/*0.005f*//*0.01f**/vec3(1.0f, 1.0f, 1.0f))*mat4());

}

AccelerationStructureBuffers createBottomLevelAS(ID3D12Device5Ptr pDevice, ID3D12GraphicsCommandList4Ptr pCmdList, ID3D12ResourcePtr pVB[], const uint32_t vertexCount[], ID3D12ResourcePtr pIB[], const uint32_t indexCount[], uint32_t geometryCount)
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
	inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;//D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
	inputs.NumDescs = geometryCount;
	inputs.pGeometryDescs = geomDesc.data();
	inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
	pDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

	// Create the buffers. They need to support UAV, and since we are going to immediately use them, we create them with an unordered-access state
	AccelerationStructureBuffers buffers;
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

void PathTracer::buildTopLevelAS(ID3D12Device5Ptr pDevice, ID3D12GraphicsCommandList4Ptr pCmdList, ID3D12ResourcePtr pBottomLevelAS[], uint64_t& tlasSize, bool update, std::map<std::string, Model> models, AccelerationStructureBuffers& buffers)
{
	int numInstances = mNumInstances;//14/*48*/; // keep in sync with mNumInstances

	// First, get the size of the TLAS buffers and create them
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
	inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
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
		buffers.pScratch->SetName(L"TLAS Scratch");
		buffers.pResult = createBuffer(pDevice, info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, kDefaultHeapProps);
		buffers.pResult->SetName(L"TLAS Results");
		buffers.pInstanceDesc = createBuffer(pDevice, sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * numInstances, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
		buffers.pInstanceDesc->SetName(L"TLAS InstanceDesc");
		tlasSize = info.ResultDataMaxSizeInBytes;
	}

	// Map the instance desc buffer
	D3D12_RAYTRACING_INSTANCE_DESC* instanceDescs;
	buffers.pInstanceDesc->Map(0, nullptr, (void**)&instanceDescs);
	ZeroMemory(instanceDescs, sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * numInstances);

	// The InstanceContributionToHitGroupIndex is set based on the shader-table layout specified in createShaderTable()
	int instanceIdx = 0;

	// Create the desc for the models
	for (auto it = models.begin(); it != models.end(); ++it)
	{
		for (uint i = 0; i < it->second.getNumMeshes(); i++)
		{
			instanceDescs[instanceIdx].InstanceID = i; // This value will be exposed to the shader via InstanceID()
			instanceDescs[instanceIdx].InstanceContributionToHitGroupIndex = mNbrHitGroups * instanceIdx;  // hard coded
			instanceDescs[instanceIdx].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE;
			mat4 m = transpose(it->second.getTransformMatrix()); // GLM is column major, the INSTANCE_DESC is row major
			memcpy(instanceDescs[instanceIdx].Transform, &m, sizeof(instanceDescs[instanceIdx].Transform));
			instanceDescs[instanceIdx].AccelerationStructure = pBottomLevelAS[it->second.getModelIndex() + i]->GetGPUVirtualAddress();
			instanceDescs[instanceIdx].InstanceMask = 0xFF;
#ifdef OFFLINE
			if (it->first == "Area light")
			{
				instanceDescs[instanceIdx].InstanceMask = 0x01;
			}
#endif // OFFLINE


			instanceIdx++;
		}
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
	if (update)
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
	vec3 white = 0.75f*vec3(1.0f, 1.0f, 1.0f);
	vec3 red = 0.75f*vec3(1.0f, 0.1f, 0.1f);
	vec3 green = 0.75f*vec3(0.1f, 1.0f, 0.1f);
	vec3 pureWhite = vec3(1.0f, 1.0f, 1.0f);
	uint8_t modelIndex = 0;

	// Sun temple
	// Load left wall extended
	Model sunTemple(L"Sun temple", modelIndex, white);
	mModels["Sun temple"] = sunTemple;
	std::vector<AccelerationStructureBuffers> sunTempleAS = mModels["Sun temple"].loadMultipleModelsFromFile(mpDevice, mpCmdList, "Data/Models/sponza2.fbx", &importer, false);
	for (int i = 0; i < sunTempleAS.size(); i++)
	{
		mpBottomLevelAS[modelIndex] = sunTempleAS.at(i).pResult;
		mpBottomLevelAS[modelIndex]->SetName((L"BLAS Sun temple sub" + std::to_wstring(i)).c_str());
		modelIndex++;
	}

	//// Load robot
	//Model robot(L"Robot", modelIndex, white);
	//mModels["Robot"] = robot;
	//AccelerationStructureBuffers robotAS = mModels["Robot"].loadModelFromFile(mpDevice, mpCmdList, "Data/Models/robot.fbx", &importer, false);
	//mpBottomLevelAS[modelIndex] = robotAS.pResult;
	//mpBottomLevelAS[modelIndex]->SetName(L"BLAS Robot");
	//modelIndex++;

	//// Load teapot
	//Model teapot(L"Teapot", modelIndex, white);
	//mModels["Teapot"] = teapot;
	//AccelerationStructureBuffers teapotAS = mModels["Teapot"].loadModelFromFile(mpDevice, mpCmdList, "Data/Models/teapot.fbx", &importer, false);
	//mpBottomLevelAS[modelIndex] = teapotAS.pResult;
	//mpBottomLevelAS[modelIndex]->SetName(L"BLAS Teapot");
	//modelIndex++;

	//// Load floor
	//Model floor(L"Floor", modelIndex, white);
	//mModels["Floor"] = floor;
	//AccelerationStructureBuffers floorAS = mModels["Floor"].loadModelFromFile(mpDevice, mpCmdList, "Data/Models/room/floor2.fbx", &importer, true);
	//mpBottomLevelAS[modelIndex] = floorAS.pResult;
	//mpBottomLevelAS[modelIndex]->SetName(L"BLAS Floor");
	//modelIndex++;

	//// Load extended floor
	//Model floorExt(L"Floor Extended", modelIndex, white);
	//mModels["Floor Extended"] = floorExt;
	//AccelerationStructureBuffers floorExtAS = mModels["Floor Extended"].loadModelFromFile(mpDevice, mpCmdList, "Data/Models/room/floor_extended2.fbx", &importer, true);
	//mpBottomLevelAS[modelIndex] = floorExtAS.pResult;
	//mpBottomLevelAS[modelIndex]->SetName(L"BLAS Floor Extended");
	//modelIndex++;

	//// Load back wall
	//Model backWall(L"Back wall", modelIndex, red);
	//mModels["Back wall"] = backWall;
	//AccelerationStructureBuffers backWallAS = mModels["Back wall"].loadModelFromFile(mpDevice, mpCmdList, "Data/Models/room/back_wall2.fbx", &importer, true);
	//mpBottomLevelAS[modelIndex] = backWallAS.pResult;
	//mpBottomLevelAS[modelIndex]->SetName(L"BLAS Back wall");
	//modelIndex++;

	//// Load extended back wall
	//Model backWallExt(L"Back wall extended", modelIndex, white);
	//mModels["Back wall extended"] = backWallExt;
	//AccelerationStructureBuffers backWallExtAS = mModels["Back wall extended"].loadModelFromFile(mpDevice, mpCmdList, "Data/Models/room/back_wall_extended2.fbx", &importer, true);
	//mpBottomLevelAS[modelIndex] = backWallExtAS.pResult;
	//mpBottomLevelAS[modelIndex]->SetName(L"BLAS Back wall extended");
	//modelIndex++;

	//// Load ceiling
	//Model ceiling(L"Ceiling", modelIndex, white);
	//mModels["Ceiling"] = ceiling;
	//AccelerationStructureBuffers ceilingAS = mModels["Ceiling"].loadModelFromFile(mpDevice, mpCmdList, "Data/Models/room/ceiling2.fbx", &importer, true);
	//mpBottomLevelAS[modelIndex] = ceilingAS.pResult;
	//mpBottomLevelAS[modelIndex]->SetName(L"BLAS Ceiling");
	//modelIndex++;

	//// Load extended ceiling
	//Model ceilingExt(L"Ceiling extended", modelIndex, white);
	//mModels["Ceiling extended"] = ceilingExt;
	//AccelerationStructureBuffers ceilingExtAS = mModels["Ceiling extended"].loadModelFromFile(mpDevice, mpCmdList, "Data/Models/room/ceiling_extended2.fbx", &importer, true);
	//mpBottomLevelAS[modelIndex] = ceilingExtAS.pResult;
	//mpBottomLevelAS[modelIndex]->SetName(L"BLAS Ceiling extended");
	//modelIndex++;

	//// Load window
	//Model window(L"Window", modelIndex, white);
	//mModels["Window"] = window;
	//AccelerationStructureBuffers windowAS = mModels["Window"].loadModelFromFile(mpDevice, mpCmdList, "Data/Models/room/wall_window2.fbx", &importer, true);
	//mpBottomLevelAS[modelIndex] = windowAS.pResult;
	//mpBottomLevelAS[modelIndex]->SetName(L"BLAS Window");
	//modelIndex++;

	//// Load extended front wall
	//Model frontWallExt(L"Front wall extended", modelIndex, white);
	//mModels["Front wall extended"] = frontWallExt;
	//AccelerationStructureBuffers frontWallExtAS = mModels["Front wall extended"].loadModelFromFile(mpDevice, mpCmdList, "Data/Models/room/front_wall_extended2.fbx", &importer, true);
	//mpBottomLevelAS[modelIndex] = frontWallExtAS.pResult;
	//mpBottomLevelAS[modelIndex]->SetName(L"BLAS Front wall extended");
	//modelIndex++;

	//// Load left wall outside
	//Model leftWallOutside(L"Left wall outside", modelIndex, green);
	//mModels["Left wall outside"] = leftWallOutside;
	//AccelerationStructureBuffers leftWallOutsideAS = mModels["Left wall outside"].loadModelFromFile(mpDevice, mpCmdList, "Data/Models/room/left_wall_outside2.fbx", &importer, true);
	//mpBottomLevelAS[modelIndex] = leftWallOutsideAS.pResult;
	//mpBottomLevelAS[modelIndex]->SetName(L"BLAS Left wall outside");
	//modelIndex++;

	//// Load right wall outside
	//Model rightWallOutside(L"Right wall outside", modelIndex, white);
	//mModels["Right wall outside"] = rightWallOutside;
	//AccelerationStructureBuffers rightWallOutsideAS = mModels["Right wall outside"].loadModelFromFile(mpDevice, mpCmdList, "Data/Models/room/right_wall_outside2.fbx", &importer, true);
	//mpBottomLevelAS[modelIndex] = rightWallOutsideAS.pResult;
	//mpBottomLevelAS[modelIndex]->SetName(L"BLAS Right wall outside");
	//modelIndex++;

	//// Load right wall inside
	//Model rightWallInside(L"Right wall inside", modelIndex, white);
	//mModels["Right wall inside"] = rightWallInside;
	//AccelerationStructureBuffers rightWallInsideAS = mModels["Right wall inside"].loadModelFromFile(mpDevice, mpCmdList, "Data/Models/room/right_wall_inside2.fbx", &importer, true);
	//mpBottomLevelAS[modelIndex] = rightWallInsideAS.pResult;
	//mpBottomLevelAS[modelIndex]->SetName(L"BLAS Right wall inside");
	//modelIndex++;

	//// Load left wall extended
	//Model leftWallExtended(L"Left wall extended", modelIndex, white);
	//mModels["Left wall extended"] = leftWallExtended;
	//AccelerationStructureBuffers leftWallExtendedAS = mModels["Left wall extended"].loadModelFromFile(mpDevice, mpCmdList, "Data/Models/room/left_wall_extended2.fbx", &importer, true);
	//mpBottomLevelAS[modelIndex] = leftWallExtendedAS.pResult;
	//mpBottomLevelAS[modelIndex]->SetName(L"BLAS Left wall extended");
	//modelIndex++;

#ifdef OFFLINE
	// Load sphere for area light
	Model areaLight(L"Area light", modelIndex, pureWhite);
	mModels["Area light"] = areaLight;
	AccelerationStructureBuffers areaLightAS = mModels["Area light"].loadModelFromFile(mpDevice, mpCmdList, "Data/Models/sphere.fbx", &importer, true);
	mpBottomLevelAS[modelIndex] = areaLightAS.pResult;
	mpBottomLevelAS[modelIndex]->SetName(L"Area light");
	modelIndex++;
#endif

	// Create the TLAS
	buildTopLevelAS(mpDevice, mpCmdList, mpBottomLevelAS, mTlasSize, false, mModels, mTopLevelBuffers);

	// The tutorial doesn't have any resource lifetime management, so we flush and sync here. 
	//This is not required by the DXR spec - you can submit the list whenever you like as long as you take care of the resources lifetime.
	mFenceValue = submitCommandList(mpCmdList, mpCmdQueue, mpFence, mFenceValue);
	mpFence->SetEventOnCompletion(mFenceValue, mFenceEvent);
	WaitForSingleObject(mFenceEvent, INFINITE);
	mpCmdList->Reset(mFrameObjects[0].pCmdAllocator, nullptr);
}

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
	desc.range.resize(4);
	// gIndirectOutput
	desc.range[0].BaseShaderRegister = 0;// u0
	desc.range[0].NumDescriptors = 1;
	desc.range[0].RegisterSpace = 0;
	desc.range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	desc.range[0].OffsetInDescriptorsFromTableStart = 0;

	// gDirectOutput
	desc.range[1].BaseShaderRegister = 1;// u1
	desc.range[1].NumDescriptors = 1;
	desc.range[1].RegisterSpace = 0;
	desc.range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	desc.range[1].OffsetInDescriptorsFromTableStart = 1;

	// gRtScene
	desc.range[2].BaseShaderRegister = 0; //t0
	desc.range[2].NumDescriptors = 1;
	desc.range[2].RegisterSpace = 0;
	desc.range[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	desc.range[2].OffsetInDescriptorsFromTableStart = 2;

	// Camera
	desc.range[3].BaseShaderRegister = 0; //b0
	desc.range[3].NumDescriptors = 1;
	desc.range[3].RegisterSpace = 0;
	desc.range[3].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	desc.range[3].OffsetInDescriptorsFromTableStart = 3;

	desc.rootParams.resize(1);
	// output UAV, TLAS and camera
	desc.rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	desc.rootParams[0].DescriptorTable.NumDescriptorRanges = 4;
	desc.rootParams[0].DescriptorTable.pDescriptorRanges = desc.range.data();


	// Create the desc
	desc.desc.NumParameters = 1;
	desc.desc.pParameters = desc.rootParams.data();
	desc.desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

	return desc;
}

RootSignatureDesc createModelHitRootDesc()
{
	RootSignatureDesc desc;
	desc.range.resize(8);

	// gRtScene
	desc.range[0].BaseShaderRegister = 0; //t0
	desc.range[0].NumDescriptors = 1;
	desc.range[0].RegisterSpace = 0;
	desc.range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	desc.range[0].OffsetInDescriptorsFromTableStart = 0;

	// Light
	desc.range[1].BaseShaderRegister = 0; //b0
	desc.range[1].NumDescriptors = 1;
	desc.range[1].RegisterSpace = 1;
	desc.range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	desc.range[1].OffsetInDescriptorsFromTableStart = 0;

	// Light Position
	desc.range[2].BaseShaderRegister = 1; //b1
	desc.range[2].NumDescriptors = 1;
	desc.range[2].RegisterSpace = 1;
	desc.range[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	desc.range[2].OffsetInDescriptorsFromTableStart = 1;

	// Shadow map Depth
	desc.range[3].BaseShaderRegister = 0; //t0
	desc.range[3].NumDescriptors = 1;
	desc.range[3].RegisterSpace = 1;
	desc.range[3].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	desc.range[3].OffsetInDescriptorsFromTableStart = 2;

	// Shadow map Position
	desc.range[4].BaseShaderRegister = 1; //t1
	desc.range[4].NumDescriptors = 1;
	desc.range[4].RegisterSpace = 1;
	desc.range[4].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	desc.range[4].OffsetInDescriptorsFromTableStart = 3;

	// Shadow map Normal
	desc.range[5].BaseShaderRegister = 2; //t2
	desc.range[5].NumDescriptors = 1;
	desc.range[5].RegisterSpace = 1;
	desc.range[5].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	desc.range[5].OffsetInDescriptorsFromTableStart = 4;

	// Shadow map Flux
	desc.range[6].BaseShaderRegister = 3; //t3
	desc.range[6].NumDescriptors = 1;
	desc.range[6].RegisterSpace = 1;
	desc.range[6].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	desc.range[6].OffsetInDescriptorsFromTableStart = 5;

	// motion vectors (for adaptive sampling)
	desc.range[7].BaseShaderRegister = 4; //t4
	desc.range[7].NumDescriptors = 1;
	desc.range[7].RegisterSpace = 1;
	desc.range[7].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	desc.range[7].OffsetInDescriptorsFromTableStart = 0;

	desc.rootParams.resize(5);
	// TLAS
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

	// Light and Shadow maps
	desc.rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	desc.rootParams[3].DescriptorTable.NumDescriptorRanges = 6;
	desc.rootParams[3].DescriptorTable.pDescriptorRanges = desc.range.data() + 1;

	// Motion vectors
	desc.rootParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	desc.rootParams[4].DescriptorTable.NumDescriptorRanges = 1;
	desc.rootParams[4].DescriptorTable.pDescriptorRanges = desc.range.data() + 7;

	desc.desc.NumParameters = 5;
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

RootSignatureDesc createOfflineRayGenRootDesc()
{
	// Create the root-signature
	RootSignatureDesc desc;
	desc.range.resize(3);

	// gDirectOutput
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
	// output UAV, TLAS and camera
	desc.rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	desc.rootParams[0].DescriptorTable.NumDescriptorRanges = 3;
	desc.rootParams[0].DescriptorTable.pDescriptorRanges = desc.range.data();


	// Create the desc
	desc.desc.NumParameters = 1;
	desc.desc.pParameters = desc.rootParams.data();
	desc.desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

	return desc;
}

RootSignatureDesc createOfflineModelHitRootDesc()
{
	RootSignatureDesc desc;
	desc.range.resize(3);

	// gRtScene
	desc.range[0].BaseShaderRegister = 0; //t0
	desc.range[0].NumDescriptors = 1;
	desc.range[0].RegisterSpace = 0;
	desc.range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	desc.range[0].OffsetInDescriptorsFromTableStart = 0;

	// Light
	desc.range[1].BaseShaderRegister = 0; //b0
	desc.range[1].NumDescriptors = 1;
	desc.range[1].RegisterSpace = 0;
	desc.range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	desc.range[1].OffsetInDescriptorsFromTableStart = 3;

	// Light Position
	desc.range[2].BaseShaderRegister = 1; //b1
	desc.range[2].NumDescriptors = 1;
	desc.range[2].RegisterSpace = 0;
	desc.range[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	desc.range[2].OffsetInDescriptorsFromTableStart = 4;

	desc.rootParams.resize(4);
	desc.rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	desc.rootParams[0].DescriptorTable.NumDescriptorRanges = 3;
	desc.rootParams[0].DescriptorTable.pDescriptorRanges = desc.range.data();

	// indices
	desc.rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	desc.rootParams[1].Descriptor.RegisterSpace = 0;
	desc.rootParams[1].Descriptor.ShaderRegister = 1;//t1

	// normals
	desc.rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	desc.rootParams[2].Descriptor.RegisterSpace = 0;
	desc.rootParams[2].Descriptor.ShaderRegister = 2;//t2

	// color
	desc.rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	desc.rootParams[3].Descriptor.RegisterSpace = 0;
	desc.rootParams[3].Descriptor.ShaderRegister = 3;//t3


	desc.desc.NumParameters = 4;
	desc.desc.pParameters = desc.rootParams.data();
	desc.desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

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
static const WCHAR* kAreaLightChs = L"areaLightChs";
static const WCHAR* kModelChs = L"modelChs";
static const WCHAR* kModelHitGroup = L"ModelHitGroup";
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
	//  4 for DXIL libraries    
	//  2 for the hit-groups: primary / shadow ray
	//  2 for RayGen root-signature (root-signature and the subobject association)
	//  2 for the model-hit root-signature (root-signature and the subobject association)
	//  2 for the miss root-signature (root-signature and the subobject association)
	//  2 for empty root-signature (root-signature and the subobject association)
	//  2 for shader config (shared between all programs. 1 for the config, 1 for association)
	//  1 for pipeline config
	//  1 for the global root signature
	const int numSubobjects = 18;

	std::array<D3D12_STATE_SUBOBJECT, numSubobjects> subobjects;
	uint32_t index = 0;

//----- Create the DXIL libraries -----//

#pragma region
	const WCHAR* entryPointsRayGen[] = { kRayGenShader };
	DxilLibrary rayGenLib = DxilLibrary(compileLibrary(L"Data/RayGeneration.hlsl", L"", L"lib_6_3"), entryPointsRayGen, arraysize(entryPointsRayGen));
	subobjects[index++] = rayGenLib.stateSubobject; // RayGen Library

	const WCHAR* entryPointsMiss[] = { kMissShader };
	DxilLibrary missLib = DxilLibrary(compileLibrary(L"Data/Miss.hlsl", L"", L"lib_6_3"), entryPointsMiss, arraysize(entryPointsMiss));
	subobjects[index++] = missLib.stateSubobject; // Miss Library

	const WCHAR* entryPointsHit[] = { kModelChs };
	DxilLibrary hitLib = DxilLibrary(compileLibrary(L"Data/Hit.hlsl", L"", L"lib_6_3"), entryPointsHit, arraysize(entryPointsHit));
	subobjects[index++] = hitLib.stateSubobject; // Hit Library

	const WCHAR* entryPointsShadowRay[] = { kShadowChs, kShadowMissShader };
	DxilLibrary shadowRayLib = DxilLibrary(compileLibrary(L"Data/ShadowRay.hlsl", L"", L"lib_6_3"), entryPointsShadowRay, arraysize(entryPointsShadowRay));
	subobjects[index++] = shadowRayLib.stateSubobject; // Shadow ray Library
#pragma endregion

//----- Create Hit Programs -----//

#pragma region
	// Create the model HitProgram
	HitProgram modelHitProgram(nullptr, kModelChs, kModelHitGroup);
	subobjects[index++] = modelHitProgram.subObject; // Model Hit Group

	// Create the Shadow-ray HitProgram
	HitProgram shadowHitProgram(nullptr, kShadowChs, kShadowHitGroup);
	subobjects[index++] = shadowHitProgram.subObject; // Shadow Hit Group
#pragma endregion

//---- Create root-signatures and associations ----//

#pragma region
	// Create the ray-gen root-signature and association
	LocalRootSignature rgsRootSignature(mpDevice, createRayGenRootDesc().desc);
	subobjects[index] = rgsRootSignature.subobject; // Ray Gen Root Sig

	uint32_t rgsRootIndex = index++;
	ExportAssociation rgsRootAssociation(&kRayGenShader, 1, &(subobjects[rgsRootIndex]));
	subobjects[index++] = rgsRootAssociation.subobject; // Associate Root Sig to RGS

	// Create the model hit root-signature and association
	LocalRootSignature modelHitRootSignature(mpDevice, createModelHitRootDesc().desc);
	subobjects[index] = modelHitRootSignature.subobject; // Robot Hit Root Sig

	uint32_t modelHitRootIndex = index++;
	ExportAssociation modelHitRootAssociation(&kModelHitGroup, 1, &(subobjects[modelHitRootIndex]));
	subobjects[index++] = modelHitRootAssociation.subobject; // Associate Robot Hit Root Sig to Robot Hit Group

	// Create the miss root-signature and association
	D3D12_STATIC_SAMPLER_DESC sampler = {};
	LocalRootSignature missRootSignature(mpDevice, createMissRootDesc(&sampler).desc);
	subobjects[index] = missRootSignature.subobject; // Miss Root Sig

	uint32_t missRootIndex = index++;
	ExportAssociation missRootAssociation(&kMissShader, 1, &(subobjects[missRootIndex]));
	subobjects[index++] = missRootAssociation.subobject; // Associate Miss Root Sig to Miss shader

	// Create the empty root-signature and associate it with the shadow rays
	D3D12_ROOT_SIGNATURE_DESC emptyDesc = {};
	emptyDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
	LocalRootSignature emptyRootSignature(mpDevice, emptyDesc);
	subobjects[index] = emptyRootSignature.subobject; // Empty Root Sig for Area light

	uint32_t emptyRootIndex = index++;
	const WCHAR* emptyRootExport[] = { kShadowChs, kShadowMissShader };
	ExportAssociation emptyRootAssociation(emptyRootExport, arraysize(emptyRootExport), &(subobjects[emptyRootIndex]));
	subobjects[index++] = emptyRootAssociation.subobject; // Associate empty root sig to Area light and Shadow ray

#pragma endregion

//---- Create Shader config, Pipeline config and Global Root-Signature----//

#pragma region
// Bind the payload size to all programs

	ShaderConfig primaryShaderConfig(sizeof(float) * 2, sizeof(float) *4 + sizeof(uint));
	subobjects[index] = primaryShaderConfig.subobject; // Payload size

	uint32_t primaryShaderConfigIndex = index++;
	const WCHAR* primaryShaderExports[] = { kRayGenShader, kMissShader, kModelChs, kShadowMissShader, kShadowChs };

	ExportAssociation primaryConfigAssociation(primaryShaderExports, arraysize(primaryShaderExports), &(subobjects[primaryShaderConfigIndex]));
	subobjects[index++] = primaryConfigAssociation.subobject; // Associate shader config to all programs

// Create the pipeline config

	PipelineConfig config(8); // maxRecursionDepth
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
	mpRtPipelineState->SetName(L"Rt PSO");
}

void PathTracer::createShaderTable()
{
	/** The shader-table layout is as follows:
		Entry 0 - Ray-gen program
		Entry 1 - Miss program for the primary ray
		  Entry 2 - Miss program for the shadow ray
		Entries 3,5... Hit program for the primary ray
			Entries 4,6... Hit programs for the shadow ray
		All entries in the shader-table must have the same size, so we will choose it base on the largest required entry.
		The primary-ray hit program requires the largest entry - sizeof(program identifier) + 3*8 bytes for the buffer root descriptors.
		The entry size must be aligned up to D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT
	*/

	const int numShaderTableEntries = 3 + 2 * mNumInstances;


	// Calculate the size and create the buffer
	mShaderTableEntrySize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
	mShaderTableEntrySize += 5 * sizeof(UINT64); // The hit shader constant-buffer descriptor
	mShaderTableEntrySize = align_to(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT, mShaderTableEntrySize);
	uint32_t shaderTableSize = mShaderTableEntrySize * numShaderTableEntries;
	if (mShaderTableEntrySize % D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT != 0)
	{
		shaderTableSize += 32; // padding between RayGen and first Miss. Works iff the number of miss entries are even.
	}

	// For simplicity, we create the shader-table on the upload heap. You can also create it on the default heap
	mpShaderTable = createBuffer(mpDevice, shaderTableSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
	mpShaderTable->SetName(L"Shader Table");

	// heap info
	D3D12_GPU_VIRTUAL_ADDRESS heapStart = mpCbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart().ptr;
	//uint64_t mHeapEntrySize = mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

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
		// Output UAVs + TLAS + Camera buffer
		*(D3D12_GPU_VIRTUAL_ADDRESS*)pEntry0 = heapStart;
		pEntry0 += sizeof(D3D12_GPU_VIRTUAL_ADDRESS*);
		entryIndex++;

	// pad to align to 64 bytes
	if (  ( (int64_t)pData + mShaderTableEntrySize * entryIndex) % D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT != 0)
	{
		pData += 32;
	}

	// Entry 1 - primary ray miss
		uint8_t* pEntry1 = pData + mShaderTableEntrySize * entryIndex;
		memcpy(pEntry1, pRtsoProps->GetShaderIdentifier(kMissShader), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
		pEntry1 += D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
		// Environment map
		*(D3D12_GPU_VIRTUAL_ADDRESS*)pEntry1 = heapStart + mEnvironmentMapHeapIndex * mHeapEntrySize;
		entryIndex++;

	// Entry 2 - shadow ray miss
		uint8_t* pEntryShadowMiss = pData + mShaderTableEntrySize * entryIndex;
		memcpy(pEntryShadowMiss, pRtsoProps->GetShaderIdentifier(kShadowMissShader), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
		entryIndex++;

	// pad to align to 64 bytes
	if (  ( (uint64_t)pData + mShaderTableEntrySize * entryIndex) % D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT != 0)
	{
		pData += 32;
	}

	// Entry 3 - Model, primary ray. ProgramID and index-buffer
	for (auto it = mModels.begin(); it != mModels.end(); ++it)
	{
		for (uint i = 0; i < it->second.getNumMeshes(); i++)
		{
			uint8_t* pEntry4 = pData + mShaderTableEntrySize * entryIndex;
			memcpy(pEntry4, pRtsoProps->GetShaderIdentifier(kModelHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			pEntry4 += D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
			// TLAS
			*(D3D12_GPU_VIRTUAL_ADDRESS*)pEntry4 = heapStart + 2 * mHeapEntrySize;
			pEntry4 += sizeof(D3D12_GPU_VIRTUAL_ADDRESS);
			// Index buffer
			*(D3D12_GPU_VIRTUAL_ADDRESS*)pEntry4 = it->second.getIndexBufferGPUAdress(i);
			pEntry4 += sizeof(D3D12_GPU_VIRTUAL_ADDRESS);
			// Normal buffer
			*(D3D12_GPU_VIRTUAL_ADDRESS*)pEntry4 = it->second.getNormalBufferGPUAdress(i);
			pEntry4 += sizeof(D3D12_GPU_VIRTUAL_ADDRESS*);
			// Light buffers and Shadow maps
			*(D3D12_GPU_VIRTUAL_ADDRESS*)pEntry4 = heapStart + mLightBufferHeapIndex * mHeapEntrySize;
			pEntry4 += sizeof(D3D12_GPU_VIRTUAL_ADDRESS*);
			// Motion vectors (for adaptive sampling)
			*(D3D12_GPU_VIRTUAL_ADDRESS*)pEntry4 = heapStart + mGeomteryBuffer_MotionVectors_SrvHeapIndex * mHeapEntrySize;
			entryIndex++;

	// Entry 4 - Model, shadow ray
			uint8_t* pEntryModelShadow = pData + mShaderTableEntrySize * entryIndex;
			memcpy(pEntryModelShadow, pRtsoProps->GetShaderIdentifier(kShadowHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			entryIndex++;
		}
	}

	// Unmap
	assert(entryIndex == numShaderTableEntries);
	mpShaderTable->Unmap(0, nullptr);
}

void PathTracer::createPathTracerPipilineState()
{
	const int numSubobjects = 15;

	std::array <D3D12_STATE_SUBOBJECT, numSubobjects> subobjects;
	uint32_t index = 0;

//----- Create the DXIL libraries -----//

	const WCHAR* entryPointsRayGen[] = { kRayGenShader };
	DxilLibrary rayGenLib = DxilLibrary(compileLibrary(L"Data/offline shaders/offline_RayGeneration.hlsl", L"", L"lib_6_3"), entryPointsRayGen, arraysize(entryPointsRayGen));
	subobjects[index++] = rayGenLib.stateSubobject; // RayGen Library

	const WCHAR* entryPointsMiss[] = { kMissShader };
	DxilLibrary missLib = DxilLibrary(compileLibrary(L"Data/offline shaders/offline_Miss.hlsl", L"", L"lib_6_3"), entryPointsMiss, arraysize(entryPointsMiss));
	subobjects[index++] = missLib.stateSubobject; // Miss Library

	const WCHAR* entryPointsHit[] = { kModelChs, kAreaLightChs };
	DxilLibrary hitLib = DxilLibrary(compileLibrary(L"Data/offline shaders/offline_Hit.hlsl", L"", L"lib_6_3"), entryPointsHit, arraysize(entryPointsHit));
	subobjects[index++] = hitLib.stateSubobject; // Hit Library

//----- Create Hit Programs -----//

	// Create the model HitProgram
	HitProgram modelHitProgram(nullptr, kModelChs, kModelHitGroup);
	subobjects[index++] = modelHitProgram.subObject; // Model Hit Group

	// Create the area light HitProgram
	HitProgram areaLightHitProgram(nullptr, kAreaLightChs, kAreaLightHitGroup);
	subobjects[index++] = areaLightHitProgram.subObject; // Model Hit Group

//---- Create root-signatures and associations ----//

	// Create the ray-gen root-signature and association
	LocalRootSignature rgsRootSignature(mpDevice, createOfflineRayGenRootDesc().desc);
	subobjects[index] = rgsRootSignature.subobject; // Ray Gen Root Sig

	uint32_t rgsRootIndex = index++;
	ExportAssociation rgsRootAssociation(&kRayGenShader, 1, &(subobjects[rgsRootIndex]));
	subobjects[index++] = rgsRootAssociation.subobject; // Associate Root Sig to RGS

	// Create the model hit root-signature and association
	LocalRootSignature modelHitRootSignature(mpDevice, createOfflineModelHitRootDesc().desc);
	subobjects[index] = modelHitRootSignature.subobject; // Model Hit Root Sig

	uint32_t modelHitRootIndex = index++;
	ExportAssociation modelHitRootAssociation(&kModelHitGroup, 1, &(subobjects[modelHitRootIndex]));
	subobjects[index++] = modelHitRootAssociation.subobject; // Associate Model Hit Root Sig to Model Hit Group

	// Create the empty root-signature and associate it with the area light and miss
	D3D12_ROOT_SIGNATURE_DESC emptyDesc = {};
	emptyDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
	LocalRootSignature emptyRootSignature(mpDevice, emptyDesc);
	subobjects[index] = emptyRootSignature.subobject; // Empty Root Sig for Area light and miss

	uint32_t emptyRootIndex = index++;
	const WCHAR* emptyRootExport[] = { kAreaLightChs, kMissShader };
	ExportAssociation emptyRootAssociation(emptyRootExport, arraysize(emptyRootExport), &(subobjects[emptyRootIndex]));
	subobjects[index++] = emptyRootAssociation.subobject; // Associate empty root sig to Area light and Miss

//---- Create Shader config, Pipeline config and Global Root-Signature----//

// Bind the payload size to all programs
	ShaderConfig primaryShaderConfig(sizeof(float) * 2, sizeof(float) * 3 + 2 * sizeof(int));
	subobjects[index] = primaryShaderConfig.subobject; // Payload size

	uint32_t primaryShaderConfigIndex = index++;
	const WCHAR* primaryShaderExports[] = { kRayGenShader, kMissShader, kModelChs, kAreaLightChs };

	ExportAssociation primaryConfigAssociation(primaryShaderExports, arraysize(primaryShaderExports), &(subobjects[primaryShaderConfigIndex]));
	subobjects[index++] = primaryConfigAssociation.subobject; // Associate shader config to all programs

// Create the pipeline config
	PipelineConfig config(10); // maxRecursionDepth
	subobjects[index++] = config.subobject; // Recursion depth

// Create the global root signature and store the empty signature
	GlobalRootSignature root(mpDevice, {});
	mpPathTracerEmptyRootSig = root.pRootSig;
	subobjects[index++] = root.subobject; // Global root signature

	assert(index == numSubobjects);

	// Create the state
	D3D12_STATE_OBJECT_DESC desc;
	desc.NumSubobjects = index;
	desc.pSubobjects = subobjects.data();
	desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;

	d3d_call(mpDevice->CreateStateObject(&desc, IID_PPV_ARGS(&mpPathTracerPipelineState)));

}

void PathTracer::createPathTracerShaderTable()
{
	const int numShaderTableEntries = 2 + mNumInstances;// raygen + miss + models

	// Calculate the size and create the buffer
	mPathTracerShaderTableEntrySize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
	mPathTracerShaderTableEntrySize += 4 * sizeof(UINT64); // The hit shader constant-buffer descriptor
	mPathTracerShaderTableEntrySize = align_to(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT, mPathTracerShaderTableEntrySize);
	uint32_t shaderTableSize = mPathTracerShaderTableEntrySize * numShaderTableEntries;
	if (mPathTracerShaderTableEntrySize % D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT != 0)
	{
		shaderTableSize += 32; // padding between RayGen and first Miss. Works iff the number of miss entries are even.
	}

	// For simplicity, we create the shader-table on the upload heap. You can also create it on the default heap
	mpPathTracerShaderTable = createBuffer(mpDevice, shaderTableSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
	mpPathTracerShaderTable->SetName(L"Offline Shader Table");

	// heap info
	D3D12_GPU_VIRTUAL_ADDRESS heapStart = mpCbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart().ptr;

	// Map the buffer
	uint8_t* pData;
	d3d_call(mpPathTracerShaderTable->Map(0, nullptr, (void**)&pData));

	MAKE_SMART_COM_PTR(ID3D12StateObjectProperties);
	ID3D12StateObjectPropertiesPtr pRtsoProps;
	mpPathTracerPipelineState->QueryInterface(IID_PPV_ARGS(&pRtsoProps));

	int entryIndex = 0;
	// Entry 0 - ray-gen program ID and descriptor data
		uint8_t* pEntry0 = pData;
		memcpy(pEntry0, pRtsoProps->GetShaderIdentifier(kRayGenShader), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
		pEntry0 += D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
		// output UAV, TLAS and Camera buffer
		*(uint64_t*)pEntry0 = heapStart + 1 * mHeapEntrySize;
		entryIndex++;

	// pad to align to 64 bytes
	if (((int64_t)pData + mPathTracerShaderTableEntrySize * entryIndex) % D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT != 0)
	{
		pData += 32;
	}

	// Entry 1 - miss
		memcpy(pData + mPathTracerShaderTableEntrySize * entryIndex, pRtsoProps->GetShaderIdentifier(kMissShader), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
		entryIndex++;

	// pad to align to 64 bytes
	if (((uint64_t)pData + mPathTracerShaderTableEntrySize * entryIndex) % D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT != 0)
	{
		pData += 32;
	}

	// Entry 2 - Model, primary ray. ProgramID and index-buffer
	for (auto it = mModels.begin(); it != mModels.end(); ++it)
	{
		if (it->first == "Area light")
		{
			uint8_t* pEntry2 = pData + mPathTracerShaderTableEntrySize * entryIndex;
			memcpy(pEntry2, pRtsoProps->GetShaderIdentifier(kAreaLightHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			entryIndex++;
		}
		else {
			for (uint i = 0; i < it->second.getNumMeshes(); i++)
			{
				uint8_t* pEntry2 = pData + mPathTracerShaderTableEntrySize * entryIndex;
				memcpy(pEntry2, pRtsoProps->GetShaderIdentifier(kModelHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
				pEntry2 += D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
				// TLAS
				*(D3D12_GPU_VIRTUAL_ADDRESS*)pEntry2 = heapStart + 2 * mHeapEntrySize;
				pEntry2 += sizeof(D3D12_GPU_VIRTUAL_ADDRESS);
				// Index buffer
				*(D3D12_GPU_VIRTUAL_ADDRESS*)pEntry2 = it->second.getIndexBufferGPUAdress(i);
				pEntry2 += sizeof(D3D12_GPU_VIRTUAL_ADDRESS);
				// Normal buffer
				*(D3D12_GPU_VIRTUAL_ADDRESS*)pEntry2 = it->second.getNormalBufferGPUAdress(i);
				pEntry2 += sizeof(D3D12_GPU_VIRTUAL_ADDRESS*);
				// Color
				*(D3D12_GPU_VIRTUAL_ADDRESS*)pEntry2 = it->second.getColorBufferGPUAdress();

				entryIndex++;
			}
		}
	}

	// Unmap
	assert(entryIndex == numShaderTableEntries);
	mpPathTracerShaderTable->Unmap(0, nullptr);
}

void PathTracer::createShaderResources()
{
	// Create the output resource. The dimensions and format should match the swap-chain
	D3D12_RESOURCE_DESC resDesc = {};
	resDesc.DepthOrArraySize = 1;
	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resDesc.Width = mSwapChainSize.x;
	resDesc.Height = mSwapChainSize.y;
	resDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; // The backbuffer is actually DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, but sRGB formats can't be used with UAVs. We will convert to sRGB ourselves in the shader
	resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resDesc.MipLevels = 1;
	resDesc.SampleDesc.Count = 1;
	d3d_call(mpDevice->CreateCommittedResource(&kDefaultHeapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&mpRtIndirectOutput))); // Starting as copy-source to simplify onFrameRender()
	mpRtIndirectOutput->SetName(L"RT Indirect Color Output");
	d3d_call(mpDevice->CreateCommittedResource(&kDefaultHeapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&mpRtDirectOutput))); // Starting as copy-source to simplify onFrameRender()
	mpRtDirectOutput->SetName(L"RT Direct Color Output");

	// Create an SRV/UAV/CBV descriptor heap. 
	// Need 27 entries 
	//	- 2 UAV for the ray tracing output
	//	- 1 SRV for the TLAS
	//	- 2 CBV for the camera
	//	- 2 CBV for the light
	//	- 4 SRV for the shadow map
	//	- 2 SRV for the RT output
	//	- 2 UAV for spatial filter
	//	- 4 SRV for spatial filter
	//  - 2 SRV for the temporal filter
	//  - 7 for the G-buffer and motion vectors

	uint32_t nbrEntries = 28;

	mpCbvSrvUavHeap = createDescriptorHeap(mpDevice, nbrEntries, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
	mHeapEntrySize = mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// Step size
	const UINT heapEntrySize = mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	D3D12_CPU_DESCRIPTOR_HANDLE handle = mpCbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart();
	uint8_t handleIndex = 0;

	////////////////////////////
	// Ray-tracing UAVs and TLAS
	////////////////////////////

	// Create the UAVs for the RT output. Based on the root signature we created it should be the first entry
		// indirect light
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	mpDevice->CreateUnorderedAccessView(mpRtIndirectOutput, nullptr, &uavDesc, handle);
	// direct light
	handle.ptr += heapEntrySize;
	handleIndex++;
	mpDevice->CreateUnorderedAccessView(mpRtDirectOutput, nullptr, &uavDesc, handle);

	// Create the TLAS SRV right after the UAV. Note that we are using a different SRV desc here
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.RaytracingAccelerationStructure.Location = mTopLevelBuffers.pResult->GetGPUVirtualAddress();

	handle.ptr += heapEntrySize;
	handleIndex++;

	mpDevice->CreateShaderResourceView(nullptr, &srvDesc, handle);

	//////////////////
	// Camera buffers
	//////////////////

	// Create the CBV for the camera buffer
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = mpCameraBuffer->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = (mCameraBufferSize + 255) & ~255; // align to 256

	handle.ptr += heapEntrySize;
	handleIndex++;

	mpDevice->CreateConstantBufferView(&cbvDesc, handle);

	// Create the CBV for the camera matrix buffer
	cbvDesc.BufferLocation = mpCameraMatrixBuffer->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = (mCameraMatrixBufferSize + 255) & ~255; // align to 256

	handle.ptr += heapEntrySize;
	handleIndex++;

	mpDevice->CreateConstantBufferView(&cbvDesc, handle);
	mCameraMatrixBufferHeapIndex = handleIndex;

	/////////////////
	// Light buffers
	/////////////////

	// Create the CBV for the light buffer
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvLightDesc = {};
	cbvLightDesc.BufferLocation = mpLightBuffer->GetGPUVirtualAddress();
	cbvLightDesc.SizeInBytes = (mLightBufferSize + 255) & ~255; // align to 256

	handle.ptr += heapEntrySize;
	handleIndex++;

	mpDevice->CreateConstantBufferView(&cbvLightDesc, handle);
	mLightBufferHeapIndex = handleIndex;

	// Create the CBV for the Light Position buffer
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvLightPositionDesc = {};
	cbvLightPositionDesc.BufferLocation = mpLightPositionBuffer->GetGPUVirtualAddress();
	cbvLightPositionDesc.SizeInBytes = (mLightPositionBufferSize + 255) & ~255; // align to 256

	handle.ptr += heapEntrySize;
	handleIndex++;

	mpDevice->CreateConstantBufferView(&cbvLightPositionDesc, handle);

	/////////////////
	// Shadow maps
	/////////////////

	// Create the SRV for the Shadow map Depth
	D3D12_SHADER_RESOURCE_VIEW_DESC shadowMapSrvDesc = {};
	shadowMapSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	shadowMapSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	shadowMapSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	shadowMapSrvDesc.Texture2D.MipLevels = 1;

	handle.ptr += heapEntrySize;
	handleIndex++;

	mpDevice->CreateShaderResourceView(mpShadowMapTexture_Depth, &shadowMapSrvDesc, handle);
	mShadowMapsHeapIndex = handleIndex;

	// Create the SRV for the Shadow map Position
	shadowMapSrvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;

	handle.ptr += heapEntrySize;
	handleIndex++;

	mpDevice->CreateShaderResourceView(mpShadowMapTexture_Position, &shadowMapSrvDesc, handle);

	// Create the SRV for the Shadow map Normal
	handle.ptr += heapEntrySize;
	handleIndex++;

	mpDevice->CreateShaderResourceView(mpShadowMapTexture_Normal, &shadowMapSrvDesc, handle);
	mShadowMaps_normal_HeapIndex = handleIndex;

	// Create the SRV for the Shadow map Flux
	handle.ptr += heapEntrySize;
	handleIndex++;

	mpDevice->CreateShaderResourceView(mpShadowMapTexture_Flux, &shadowMapSrvDesc, handle);

	///////////////////
	// Ray-tracing SRV
	///////////////////

	// Create the SRV for the RT Indirect Output
	D3D12_SHADER_RESOURCE_VIEW_DESC rtOutputSrvDesc = {};
	rtOutputSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	rtOutputSrvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	rtOutputSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	rtOutputSrvDesc.Texture2D.MipLevels = 1;

	handle.ptr += heapEntrySize;
	handleIndex++;

	mpDevice->CreateShaderResourceView(mpRtIndirectOutput, &rtOutputSrvDesc, handle);
	mRtIndirectOutputHeapIndex = handleIndex;

	// Create the SRV for the RT Direct Output
	handle.ptr += heapEntrySize;
	handleIndex++;

	mpDevice->CreateShaderResourceView(mpRtDirectOutput, &rtOutputSrvDesc, handle);
	mRtDirectOutputHeapIndex = handleIndex;

	//////////////////
	// Spatial filter
	//////////////////

	// Create the UAV for the Blur1 output
	d3d_call(mpDevice->CreateCommittedResource(&kDefaultHeapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mpBlurPass1Output))); // Starting as copy-source to simplify onFrameRender()
	mpBlurPass1Output->SetName(L"Blur1 Output resource");
	handle.ptr += heapEntrySize;
	handleIndex++;
	mpDevice->CreateUnorderedAccessView(mpBlurPass1Output, nullptr, &uavDesc, handle);
	mBlur1OutputUavHeapIndex = handleIndex;

	// Create the SRV for the Blur1 output
	handle.ptr += heapEntrySize;
	handleIndex++;

	mpDevice->CreateShaderResourceView(mpBlurPass1Output, &rtOutputSrvDesc, handle);
	mBlur1OutputSrvHeapIndex = handleIndex;

	// Create the UAV for the Blur2 output
	d3d_call(mpDevice->CreateCommittedResource(&kDefaultHeapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&mpBlurPass2Output))); // Starting as copy-source to simplify onFrameRender()
	mpBlurPass2Output->SetName(L"Blur2 Output resource");
	handle.ptr += heapEntrySize;
	handleIndex++;
	mpDevice->CreateUnorderedAccessView(mpBlurPass2Output, nullptr, &uavDesc, handle);
	mBlur2OutputUavHeapIndex = handleIndex;

	// Create the SRV for the Blur2 output
	handle.ptr += heapEntrySize;
	handleIndex++;

	mpDevice->CreateShaderResourceView(mpBlurPass2Output, &rtOutputSrvDesc, handle);
	mBlur2OutputSrvHeapIndex = handleIndex;

	// Create the SRV for the Filtered Indirect Color
	handle.ptr += heapEntrySize;
	handleIndex++;

	mpDevice->CreateShaderResourceView(mpFilteredIndirectColor, &rtOutputSrvDesc, handle);
	mFilteredIndirectColorHeapIndex = handleIndex;

	// Create the SRV for the Filtered Direct Color
	handle.ptr += heapEntrySize;
	handleIndex++;

	mpDevice->CreateShaderResourceView(mpFilteredDirectColor, &rtOutputSrvDesc, handle);
	mFilteredDirectColorHeapIndex = handleIndex;

	/////////////////
	// Color history
	/////////////////

	// Create the SRV for the indirect color history
	handle.ptr += heapEntrySize;
	handleIndex++;

	mpDevice->CreateShaderResourceView(mpIndirectColorHistory, &rtOutputSrvDesc, handle);
	mIndirectColorHistoryHeapIndex = handleIndex;

	// Create the SRV for the direct color history
	handle.ptr += heapEntrySize;
	handleIndex++;

	mpDevice->CreateShaderResourceView(mpDirectColorHistory, &rtOutputSrvDesc, handle);
	mDirectColorHistoryHeapIndex = handleIndex;

	/////////////////
	// G-buffer
	/////////////////

	// Create the SRV for the G-buffer Motion Vectors
	handle.ptr += heapEntrySize;
	handleIndex++;

	mpDevice->CreateShaderResourceView(mpGeometryBuffer_MotionVectors, &rtOutputSrvDesc, handle);
	mGeomteryBuffer_MotionVectors_SrvHeapIndex = handleIndex;

	// Create the SRV for the G-buffer Depth
	handle.ptr += heapEntrySize;
	handleIndex++;

	rtOutputSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	mpDevice->CreateShaderResourceView(mpGeometryBuffer_Depth, &rtOutputSrvDesc, handle);
	mGeomteryBuffer_Depth_SrvHeapIndex = handleIndex;

	// Create the SRV for the Previous G-buffer Depth
	handle.ptr += heapEntrySize;
	handleIndex++;

	mpDevice->CreateShaderResourceView(mpGeometryBuffer_Previous_Depth, &rtOutputSrvDesc, handle);
	mGeomteryBuffer_Previous_Depth_SrvHeapIndex = handleIndex;

	rtOutputSrvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;

	// Create the SRV for the G-buffer Normal
	handle.ptr += heapEntrySize;
	handleIndex++;

	mpDevice->CreateShaderResourceView(mpGeometryBuffer_Normal, &rtOutputSrvDesc, handle);
	mGeomteryBuffer_Normal_SrvHeapIndex = handleIndex;

	// Create the SRV for the Previous G-buffer Normal
	handle.ptr += heapEntrySize;
	handleIndex++;

	mpDevice->CreateShaderResourceView(mpGeometryBuffer_Previous_Normal, &rtOutputSrvDesc, handle);
	mGeomteryBuffer_Previous_Normal_SrvHeapIndex = handleIndex;

	// Create the SRV for the G-buffer Color
	handle.ptr += heapEntrySize;
	handleIndex++;

	mpDevice->CreateShaderResourceView(mpGeometryBuffer_Color, &rtOutputSrvDesc, handle);
	mGeomteryBuffer_Color_SrvHeapIndex = handleIndex;

	// Create the SRV for the G-buffer Position
	handle.ptr += heapEntrySize;
	handleIndex++;

	mpDevice->CreateShaderResourceView(mpGeometryBuffer_Position, &rtOutputSrvDesc, handle);
	mGeomteryBuffer_Position_SrvHeapIndex = handleIndex;
	

	////////////////// End of SRV/UAV/CBV descriptor heap //////////////////
	handleIndex++;
	assert(handleIndex == nbrEntries);


	// Create a DSV descriptor heap
	// needs 1 entry
	// - 1 DSV for the Shadow Map

	// create a DSV descriptor heap
	mpShadowMapDsvHeap = createDescriptorHeap(mpDevice, 3, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, false);

	// Description
	D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc = {};
	depthStencilViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
	depthStencilViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	depthStencilViewDesc.Texture2D.MipSlice = 0;

	// start handle and size
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = mpShadowMapDsvHeap->GetCPUDescriptorHandleForHeapStart();
	const UINT dsvDescriptorSize = mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	// RSM
	mpDevice->CreateDepthStencilView(mpShadowMapTexture_Depth, nullptr, dsvHandle);//null for desc?
	mShadowMapDsv_Depth = dsvHandle;

	// G-buffer
	dsvHandle.ptr += dsvDescriptorSize;
	mpDevice->CreateDepthStencilView(mpGeometryBuffer_Depth, nullptr, dsvHandle);//null for desc?
	mGeometryBufferDsv_Depth = dsvHandle;

	// Motion vectors
	dsvHandle.ptr += dsvDescriptorSize;
	mpDevice->CreateDepthStencilView(mpGeometryBuffer_MotionVectors_depth, nullptr, dsvHandle);//null for desc?
	mGeometryBufferDsv_MotionVectors = dsvHandle;



	// Create a RTV descriptor heap
	// needs 1 entry
	// - 1 RTV for Position
	// - 1 RTV for Normal
	// - 1 RTV for Flux

	// create a RTV descriptor heap
	mpShadowMapRtvHeap = createDescriptorHeap(mpDevice, 10, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, false);

	// Description
	D3D12_RENDER_TARGET_VIEW_DESC renderTargetViewDesc = {};
	renderTargetViewDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	renderTargetViewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	renderTargetViewDesc.Texture2D.MipSlice = 0;

	// start handle and size
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = mpShadowMapRtvHeap->GetCPUDescriptorHandleForHeapStart();
	const UINT rtvDescriptorSize = mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	// RSM Position
	mpDevice->CreateRenderTargetView(mpShadowMapTexture_Position, &renderTargetViewDesc, rtvHandle);
	mShadowMapRtv_Position = rtvHandle;
	mShadowMapRTVs[0] = mShadowMapRtv_Position;

	// RSM Normal
	rtvHandle.ptr += rtvDescriptorSize;
	mpDevice->CreateRenderTargetView(mpShadowMapTexture_Normal, &renderTargetViewDesc, rtvHandle);
	mShadowMapRtv_Normal = rtvHandle;
	mShadowMapRTVs[1] = mShadowMapRtv_Normal;

	// RSM Flux
	rtvHandle.ptr += rtvDescriptorSize;
	mpDevice->CreateRenderTargetView(mpShadowMapTexture_Flux, &renderTargetViewDesc, rtvHandle);
	mShadowMapRtv_Flux = rtvHandle;
	mShadowMapRTVs[2] = mShadowMapRtv_Flux;

	// Temporal filter indirect
	rtvHandle.ptr += rtvDescriptorSize;
	mpDevice->CreateRenderTargetView(mpTemporalFilterIndirectOutput, &renderTargetViewDesc, rtvHandle);
	mTemporalFilterIndirectRtv = rtvHandle;
	mTemporalFilterRtvs[0] = mTemporalFilterIndirectRtv;

	// Temporal filter direct
	rtvHandle.ptr += rtvDescriptorSize;
	mpDevice->CreateRenderTargetView(mpTemporalFilterDirectOutput, &renderTargetViewDesc, rtvHandle);
	mTemporalFilterDirectRtv = rtvHandle;
	mTemporalFilterRtvs[1] = mTemporalFilterDirectRtv;

	// G-buffer Motion Vectors
	rtvHandle.ptr += rtvDescriptorSize;
	mpDevice->CreateRenderTargetView(mpGeometryBuffer_MotionVectors, &renderTargetViewDesc, rtvHandle);
	mGeometryBufferRtv_MotionVectors = rtvHandle;

	// G-buffer Normal
	rtvHandle.ptr += rtvDescriptorSize;
	mpDevice->CreateRenderTargetView(mpGeometryBuffer_Normal, &renderTargetViewDesc, rtvHandle);
	mGeometryBufferRtv_Normal = rtvHandle;
	mGeometryBufferRTVs[0] = mGeometryBufferRtv_Normal;

	// G-buffer Color
	rtvHandle.ptr += rtvDescriptorSize;
	mpDevice->CreateRenderTargetView(mpGeometryBuffer_Color, &renderTargetViewDesc, rtvHandle);
	mGeometryBufferRtv_Color = rtvHandle;
	mGeometryBufferRTVs[1] = mGeometryBufferRtv_Color;

	// G-buffer Position
	rtvHandle.ptr += rtvDescriptorSize;
	mpDevice->CreateRenderTargetView(mpGeometryBuffer_Position, &renderTargetViewDesc, rtvHandle);
	mGeometryBufferRtv_Position = rtvHandle;
	mGeometryBufferRTVs[2] = mGeometryBufferRtv_Position;

	// Tone Mapping view
	renderTargetViewDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;// _sRGB?

	rtvHandle.ptr += rtvDescriptorSize;
	mpDevice->CreateRenderTargetView(mpToneMappingOutput, &renderTargetViewDesc, rtvHandle);
	mToneMappingRtv = rtvHandle;


}


//////////////////////////////////////////////////////////////////////////
// My own functions
//////////////////////////////////////////////////////////////////////////
void PathTracer::readKeyboardInput(bool *gKeys)
{
	mCameraSpeed = 0.1f * 60.0f*glm::clamp<float>(mDeltaTime, 0.0f, 30.0f)*0.001f;

	if (gKeys[VK_LEFT])
	{
		float angle = mCamera.cameraAngle + 0.25f * mCameraSpeed;
		mCamera.cameraDirection = vec3(eulerAngleY(-angle) * vec4(0, 0, 1, 1));
		mCamera.cameraAngle = angle;
	}
	else if (gKeys[VK_RIGHT])
	{
		float angle = mCamera.cameraAngle - 0.25f * mCameraSpeed;
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

	// Light
	if (gKeys['Y'])
	{
		mLight.radius = max(mLight.radius - mCameraSpeed, 0.0f);
	}
	else if (gKeys['H'])
	{
		mLight.radius += mCameraSpeed;
	}
	if (gKeys['G'])
	{
		mLight.theta += 0.1f * mCameraSpeed;
	}
	else if (gKeys['J'])
	{
		mLight.theta -= 0.1f * mCameraSpeed;
	}
	if (gKeys['U'])
	{
		mLight.phi = max(mLight.phi - 0.05f * mCameraSpeed, 0.0f);
	}
	else if (gKeys['T'])
	{
		mLight.phi = min(mLight.phi + 0.05f * mCameraSpeed, pi<float>());
	}

	// Drop history (reset)
	if (gKeys['R'])
	{
		mDropHistory = true;
	}
	else 
	{
		mDropHistory = false;
	}

}

void PathTracer::createCameraBuffers()
{
	// Create camera buffer
	uint32_t nbVec = 3; // Position and Direction
	mCameraBufferSize = nbVec * sizeof(vec4) + 2 * sizeof(mat4);
	mpCameraBuffer = createBuffer(mpDevice, mCameraBufferSize, D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
	mpCameraBuffer->SetName(L"Camera buffer");

	// Create camera matrix buffer
	mpCameraMatrixBuffer = createBuffer(mpDevice, mCameraMatrixBufferSize, D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
	mpCameraMatrixBuffer->SetName(L"Camera matrix buffer");
	// Set up camera properties
	float fovAngle = glm::half_pi<float>();

	// Left-hand system, depth from 0 to 1
	float fFar = 100.0f;
	mCamera.projMat = glm::perspectiveFovLH_ZO(fovAngle, (float)mSwapChainSize.x, (float)mSwapChainSize.y, 0.1f, fFar);

	mCamera.projMatInv = glm::inverse(mCamera.projMat);

}

void PathTracer::updateCameraBuffers()
{
	// Update camera matrix
	mCamera.eye = mCamera.cameraPosition;
	mCamera.at = mCamera.cameraDirection;
	// up vector constant

	mCamera.viewMatPrev = mCamera.viewMat;

	vec3 center = mCamera.eye + mCamera.at;
	mCamera.viewMat = lookAtLH(mCamera.eye, center, mCamera.up);
	// projMat constant

	mCamera.viewMatInv = glm::inverse(mCamera.viewMat);

	// camera buffer
	uint8_t* pData;
	d3d_call(mpCameraBuffer->Map(0, nullptr, (void**)&pData));
	memcpy(pData,
		&mCamera.viewMatInv, sizeof(mCamera.viewMatInv)
	);
	pData += sizeof(mCamera.viewMatInv);
	memcpy(pData,
		&mCamera.projMatInv, sizeof(mCamera.projMatInv)
	);
	pData += sizeof(mCamera.projMatInv);
	memcpy(pData,
		&mCamera.cameraPosition,
		sizeof(mCamera.cameraPosition)
	);
	pData += sizeof(mCamera.cameraPosition);
	memcpy(pData,
		&mCamera.cameraAngle,
		sizeof(mCamera.cameraAngle)
	);
	pData += sizeof(mCamera.cameraAngle);
	memcpy(pData,
		&frameCount, sizeof(frameCount)
	);
	mpCameraBuffer->Unmap(0, nullptr);


	// camera matrix buffer
	d3d_call(mpCameraMatrixBuffer->Map(0, nullptr, (void**)&pData));
	memcpy(pData,
		&mCamera.viewMat,
		sizeof(mCamera.viewMat));
	memcpy(pData + sizeof(mCamera.viewMat),
		&mCamera.projMat,
		sizeof(mCamera.projMat));
	memcpy(pData + sizeof(mCamera.viewMat) + sizeof(mCamera.projMat),
		&mCamera.viewMatPrev,
		sizeof(mCamera.viewMatPrev));
	mpCameraMatrixBuffer->Unmap(0, nullptr);
}

void PathTracer::createShadowMapPipelineState()
{
	D3D12_DESCRIPTOR_RANGE range[1];
	// Light buffer
	range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	range[0].NumDescriptors = 1;
	range[0].BaseShaderRegister = 0; //b0
	range[0].RegisterSpace = 0;
	range[0].OffsetInDescriptorsFromTableStart = 0;

	D3D12_ROOT_PARAMETER rootParameters[4];
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

	// color
	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	rootParameters[3].Constants.RegisterSpace = 0;
	rootParameters[3].Constants.ShaderRegister = 2;//b2
	rootParameters[3].Constants.Num32BitValues = 3;
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

	// Root signature
	RootSignatureDesc desc;
	desc.desc.NumParameters = 4;
	desc.desc.pParameters = rootParameters;
	desc.desc.NumStaticSamplers = 0;
	desc.desc.pStaticSamplers = nullptr;
	desc.desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	mpRasterRootSig = createRootSignature(mpDevice, desc.desc);

	// Compile shaders
	ID3DBlobPtr vertexShader = compileLibrary(L"Data/ShadowMap.hlsl", L"VSMain", L"vs_6_3");
	ID3DBlobPtr pixelShader = compileLibrary(L"Data/ShadowMap.hlsl", L"PSMain", L"ps_6_3");

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
	float fovAngle = /*1.5f*glm::half_pi<float>();*/glm::quarter_pi<float>()*1.5f;

	// Left-hand system, depth from 0 to 1
	float fFar = 100.0f;
	mLight.projMat = glm::perspectiveFovLH_ZO(fovAngle, (float)kShadowMapWidth, (float)kShadowMapHeight, 0.1f, fFar);



	// Light position buffer
	mpLightPositionBuffer = createBuffer(mpDevice, mLightPositionBufferSize, D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
	mpLightPositionBuffer->SetName(L"Light Position Buffer");
}

void PathTracer::updateLightBuffer()
{
	/*mLight.eye = mLight.position;
	mLight.at = mLight.direction; */
	mLight.eye.x = mLight.radius * cos(mLight.theta) * sin(mLight.phi);
	mLight.eye.y = mLight.radius * cos(mLight.phi);
	mLight.eye.z = mLight.radius * sin(mLight.theta) * sin(mLight.phi);

	// up vector constant

	//vec3 center = mLight.center;//mLight.eye + mLight.at;
	mLight.viewMat = lookAtLH(mLight.eye, mLight.center, mLight.up);
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


	// Light position buffer
	uint8_t* pDataPosition;
	d3d_call(mpLightPositionBuffer->Map(0, nullptr, (void**)&pDataPosition));
	memcpy(pDataPosition,
		&mLight.eye,
		sizeof(mLight.eye));
	mpLightPositionBuffer->Unmap(0, nullptr);

}

void PathTracer::updateTransformBuffers()
{
	for (auto it = mModels.begin(); it != mModels.end(); ++it)
	{
		it->second.updateTransformBuffer();
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
	shadowTexDesc.Format = DXGI_FORMAT_D32_FLOAT;
	shadowTexDesc.SampleDesc.Count = 1;
	shadowTexDesc.SampleDesc.Quality = 0;
	shadowTexDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	shadowTexDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE depthClearValue;
	depthClearValue.Format = DXGI_FORMAT_D32_FLOAT;
	depthClearValue.DepthStencil.Depth = 1.0f;
	depthClearValue.DepthStencil.Stencil = 0;

	d3d_call(mpDevice->CreateCommittedResource(
		&kDefaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&shadowTexDesc,
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
		&depthClearValue,
		IID_PPV_ARGS(&mpShadowMapTexture_Depth)
	));
	mpShadowMapTexture_Depth->SetName(L"RSM Depth");

	// Create resources for Position, Normal and Flux
	shadowTexDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	shadowTexDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE colorClearValue;
	colorClearValue.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	colorClearValue.Color[0] = 0.0f;
	colorClearValue.Color[1] = 0.0f;
	colorClearValue.Color[2] = 0.0f;
	colorClearValue.Color[3] = 0.0f;

	// position
	d3d_call(mpDevice->CreateCommittedResource(
		&kDefaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&shadowTexDesc,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		&colorClearValue,
		IID_PPV_ARGS(&mpShadowMapTexture_Position)
	));
	mpShadowMapTexture_Position->SetName(L"RSM Position");

	// normal
	d3d_call(mpDevice->CreateCommittedResource(
		&kDefaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&shadowTexDesc,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		&colorClearValue,
		IID_PPV_ARGS(&mpShadowMapTexture_Normal)
	));
	mpShadowMapTexture_Normal->SetName(L"RSM Normal");

	// flux
	d3d_call(mpDevice->CreateCommittedResource(
		&kDefaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&shadowTexDesc,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		&colorClearValue,
		IID_PPV_ARGS(&mpShadowMapTexture_Flux)
	));
	mpShadowMapTexture_Flux->SetName(L"RSM Flux");

}

std::vector<float> calcGaussWeights(float sigma)
{
	// taken from https://github.com/d3dcoder/d3d12book/blob/master/Chapter%2013%20The%20Compute%20Shader/Blur

	int maxBlurRadius = 5;

	float twoSigma2 = 2.0f*sigma*sigma;
	// Estimate the blur radius based on sigma since sigma controls the "width" of the bell curve.
	// For example, for sigma = 3, the width of the bell curve is 
	int blurRadius = (int)ceil(2.0f * sigma);
	assert(blurRadius <= maxBlurRadius);

	std::vector<float> weights;
	weights.resize(2 * blurRadius + 1);
	float weightSum = 0.0f;
	for (int i = -blurRadius; i <= blurRadius; ++i)
	{
		float x = (float)i;
		weights[i + blurRadius] = expf(-x * x / twoSigma2);
		weightSum += weights[i + blurRadius];
	}
	// Divide by the sum so all the weights add up to 1.0.
	for (int i = 0; i < weights.size(); ++i)
	{
		weights[i] /= weightSum;
	}
	return weights;
}

void PathTracer::createSpatialFilterPipeline()
{
	// Create compute root signature
	D3D12_DESCRIPTOR_RANGE ranges[4];

	// input texture (to blur)
	ranges[0].BaseShaderRegister = 0;//t0
	ranges[0].NumDescriptors = 1;
	ranges[0].RegisterSpace = 0;
	ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[0].OffsetInDescriptorsFromTableStart = 0;

	// output texture
	ranges[1].BaseShaderRegister = 0;//u0
	ranges[1].NumDescriptors = 1;
	ranges[1].RegisterSpace = 0;
	ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	ranges[1].OffsetInDescriptorsFromTableStart = 0;

	// depth
	ranges[2].BaseShaderRegister = 1;//t1
	ranges[2].NumDescriptors = 1;
	ranges[2].RegisterSpace = 0;
	ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[2].OffsetInDescriptorsFromTableStart = 0;

	// normal
	ranges[3].BaseShaderRegister = 2;//t2
	ranges[3].NumDescriptors = 1;
	ranges[3].RegisterSpace = 0;
	ranges[3].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[3].OffsetInDescriptorsFromTableStart = 0;

	D3D12_ROOT_PARAMETER parameters[5];

	parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	parameters[0].Constants.Num32BitValues = 1;
	parameters[0].Constants.RegisterSpace = 0;
	parameters[0].Constants.ShaderRegister = 0; // b0

	parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	parameters[1].DescriptorTable.NumDescriptorRanges = 1;
	parameters[1].DescriptorTable.pDescriptorRanges = ranges;

	parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	parameters[2].DescriptorTable.NumDescriptorRanges = 1;
	parameters[2].DescriptorTable.pDescriptorRanges = &ranges[1];

	parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	parameters[3].DescriptorTable.NumDescriptorRanges = 1;
	parameters[3].DescriptorTable.pDescriptorRanges = &ranges[2];

	parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	parameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	parameters[4].DescriptorTable.NumDescriptorRanges = 1;
	parameters[4].DescriptorTable.pDescriptorRanges = &ranges[3];

	RootSignatureDesc desc;
	desc.desc.NumParameters = 5;
	desc.desc.pParameters = parameters;
	desc.desc.NumStaticSamplers = 0;
	desc.desc.pStaticSamplers = nullptr;
	desc.desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

	mpSpatialFilterRootSig = createRootSignature(mpDevice, desc.desc);

	// Create compute pipeline state object (PSO)
	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = mpSpatialFilterRootSig.GetInterfacePtr();

	// horz
	ID3DBlobPtr computeShaderHorzBlob = nullptr;
	ID3DBlobPtr errorHorzBlob = nullptr;
	d3d_call(D3DCompileFromFile(L"Data/SpatialFilter.hlsl", NULL, NULL, "HorzBlurCS", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ALL_RESOURCES_BOUND, 0, &computeShaderHorzBlob, &errorHorzBlob));
	psoDesc.CS = CD3DX12_SHADER_BYTECODE(computeShaderHorzBlob.GetInterfacePtr());
	d3d_call(mpDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&mpSpatialFilterStateHorz)));

	// vert
	ID3DBlobPtr computeShaderVertBlob = nullptr;
	ID3DBlobPtr errorVertBlob = nullptr;
	d3d_call(D3DCompileFromFile(L"Data/SpatialFilter.hlsl", NULL, NULL, "VertBlurCS", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ALL_RESOURCES_BOUND, 0, &computeShaderVertBlob, &errorVertBlob));
	psoDesc.CS = CD3DX12_SHADER_BYTECODE(computeShaderVertBlob.GetInterfacePtr());
	d3d_call(mpDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&mpSpatialFilterStateVert)));


	// Calculate Gauss weights
	mGaussWeights = calcGaussWeights(2.5f);
	mBlurRadius = (int)mGaussWeights.size() / 2;

	// create output resource
	D3D12_RESOURCE_DESC texDesc;
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = mSwapChainSize.x;
	texDesc.Height = mSwapChainSize.y;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	// indirect output
	d3d_call(mpDevice->CreateCommittedResource(
		&kDefaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		nullptr,
		IID_PPV_ARGS(&mpFilteredIndirectColor)
	));
	mpFilteredIndirectColor->SetName(L"Filtered Indirect Color");

	// direct output
	d3d_call(mpDevice->CreateCommittedResource(
		&kDefaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		nullptr,
		IID_PPV_ARGS(&mpFilteredDirectColor)
	));
	mpFilteredDirectColor->SetName(L"Filtered Direct Color");
}

void PathTracer::createToneMappingPipeline()
{

	// root signature
	D3D12_DESCRIPTOR_RANGE ranges[5];

	// Filtered indirect Color
	ranges[0].BaseShaderRegister = 0;//t0
	ranges[0].NumDescriptors = 1;
	ranges[0].RegisterSpace = 0;
	ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[0].OffsetInDescriptorsFromTableStart = 0;

	// Filtered direct Color
	ranges[1].BaseShaderRegister = 1;//t1
	ranges[1].NumDescriptors = 1;
	ranges[1].RegisterSpace = 0;
	ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[1].OffsetInDescriptorsFromTableStart = 1;

	// Shadow map Normal
	ranges[2].BaseShaderRegister = 2; //t2
	ranges[2].NumDescriptors = 1;
	ranges[2].RegisterSpace = 0;
	ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[2].OffsetInDescriptorsFromTableStart = 0;

	// Motion Vector output
	ranges[3].BaseShaderRegister = 3; //t3
	ranges[3].NumDescriptors = 1;
	ranges[3].RegisterSpace = 0;
	ranges[3].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[3].OffsetInDescriptorsFromTableStart = 0;

	// G-buffer color
	ranges[4].BaseShaderRegister = 4; //t4
	ranges[4].NumDescriptors = 1;
	ranges[4].RegisterSpace = 0;
	ranges[4].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[4].OffsetInDescriptorsFromTableStart = 0;

	D3D12_ROOT_PARAMETER rootParameters[4];
	// filtered indirect and direct color
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[0].DescriptorTable.NumDescriptorRanges = 2;
	rootParameters[0].DescriptorTable.pDescriptorRanges = ranges;

	// RSM normal
	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
	rootParameters[1].DescriptorTable.pDescriptorRanges = &ranges[2];

	// motion vectors
	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;
	rootParameters[2].DescriptorTable.pDescriptorRanges = &ranges[3];

	// G-buffer color
	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[3].DescriptorTable.NumDescriptorRanges = 1;
	rootParameters[3].DescriptorTable.pDescriptorRanges = &ranges[4];

	D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS;

	RootSignatureDesc desc;
	desc.desc.NumParameters = 4;
	desc.desc.pParameters = rootParameters;
	desc.desc.NumStaticSamplers = 0;
	desc.desc.pStaticSamplers = nullptr;
	desc.desc.Flags = rootSignatureFlags;

	mpToneMappingRootSig = createRootSignature(mpDevice, desc.desc);

	// compile shaders
	ID3DBlobPtr vertexShader = compileLibrary(L"Data/ToneMapping.hlsl", L"VSMain", L"vs_6_3");
	ID3DBlobPtr pixelShader = compileLibrary(L"Data/ToneMapping.hlsl", L"PSMain", L"ps_6_3");

	// create the pipeline state object (PSO)
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { nullptr, 0 };
	psoDesc.pRootSignature = mpToneMappingRootSig.GetInterfacePtr();
	psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.GetInterfacePtr());
	psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.GetInterfacePtr());

	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

	psoDesc.DepthStencilState.DepthEnable = FALSE;
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
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
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;//_sRGB?
	psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;

	d3d_call(mpDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mpToneMappingState)));

	// create output resource
	D3D12_RESOURCE_DESC toneMappingTexDesc;
	toneMappingTexDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	toneMappingTexDesc.Alignment = 0;
	toneMappingTexDesc.Width = mSwapChainSize.x;
	toneMappingTexDesc.Height = mSwapChainSize.y;
	toneMappingTexDesc.DepthOrArraySize = 1;
	toneMappingTexDesc.MipLevels = 1;
	toneMappingTexDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	toneMappingTexDesc.SampleDesc.Count = 1;
	toneMappingTexDesc.SampleDesc.Quality = 0;
	toneMappingTexDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	toneMappingTexDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE colorClearValue;
	colorClearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	colorClearValue.Color[0] = 0.0f;
	colorClearValue.Color[1] = 0.0f;
	colorClearValue.Color[2] = 0.0f;
	colorClearValue.Color[3] = 0.0f;

	d3d_call(mpDevice->CreateCommittedResource(
		&kDefaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&toneMappingTexDesc,
		D3D12_RESOURCE_STATE_COPY_SOURCE,
		&colorClearValue,
		IID_PPV_ARGS(&mpToneMappingOutput)
	));
	mpToneMappingOutput->SetName(L"Tone Mapping RTV");
}

void PathTracer::createGeometryBufferPipeline()
{
	////////////////////////
	// G-buffer state
	////////////////////////

	D3D12_DESCRIPTOR_RANGE range[1];
	// Camera Matrix buffer
	range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	range[0].NumDescriptors = 1;
	range[0].BaseShaderRegister = 0; //b0
	range[0].RegisterSpace = 0;
	range[0].OffsetInDescriptorsFromTableStart = 0;

	D3D12_ROOT_PARAMETER rootParameters[4];
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

	// color and mesh id
	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	rootParameters[3].Constants.RegisterSpace = 0;
	rootParameters[3].Constants.ShaderRegister = 2;//b2
	rootParameters[3].Constants.Num32BitValues = 4;
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;


	D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

	// Root signature
	RootSignatureDesc desc;
	desc.desc.NumParameters = 4;
	desc.desc.pParameters = rootParameters;
	desc.desc.NumStaticSamplers = 0;
	desc.desc.pStaticSamplers = nullptr;
	desc.desc.Flags = rootSignatureFlags;

	mpGeometryBufferRootSig = createRootSignature(mpDevice, desc.desc);

	// Compile shaders
	ID3DBlobPtr vertexShader = compileLibrary(L"Data/GBuffer.hlsl", L"VSMain", L"vs_6_3");
	ID3DBlobPtr pixelShader = compileLibrary(L"Data/GBuffer.hlsl", L"PSMain", L"ps_6_3");

	// Define the vertex input layout.
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	// Create the graphics pipeline state object (PSO).
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	{
		psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		psoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
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
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleDesc.Count = 1;
	}
	psoDesc.pRootSignature = mpGeometryBufferRootSig.GetInterfacePtr();
	psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.GetInterfacePtr());
	psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.GetInterfacePtr());
	psoDesc.NumRenderTargets = 3;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
	psoDesc.RTVFormats[1] = DXGI_FORMAT_R32G32B32A32_FLOAT;
	psoDesc.RTVFormats[2] = DXGI_FORMAT_R32G32B32A32_FLOAT;

	d3d_call(mpDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mpGeometryBufferState)));

	////////////////////////
	// motion vectors state
	////////////////////////

	D3D12_DESCRIPTOR_RANGE ranges[5];
	// Camera Matrix buffer
	ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	ranges[0].NumDescriptors = 1;
	ranges[0].BaseShaderRegister = 0; //b0
	ranges[0].RegisterSpace = 0;
	ranges[0].OffsetInDescriptorsFromTableStart = 0;

	// Current Depth
	ranges[1].BaseShaderRegister = 0; //t0
	ranges[1].NumDescriptors = 1;
	ranges[1].RegisterSpace = 0;
	ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[1].OffsetInDescriptorsFromTableStart = 0;

	// Previous Depth
	ranges[2].BaseShaderRegister = 1; //t1
	ranges[2].NumDescriptors = 1;
	ranges[2].RegisterSpace = 0;
	ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[2].OffsetInDescriptorsFromTableStart = 1;

	// Current Normal+meshID
	ranges[3].BaseShaderRegister = 2; //t2
	ranges[3].NumDescriptors = 1;
	ranges[3].RegisterSpace = 0;
	ranges[3].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[3].OffsetInDescriptorsFromTableStart = 2;

	// Previous Normal+meshID
	ranges[4].BaseShaderRegister = 3; //t3
	ranges[4].NumDescriptors = 1;
	ranges[4].RegisterSpace = 0;
	ranges[4].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[4].OffsetInDescriptorsFromTableStart = 3;

	D3D12_ROOT_PARAMETER mvRootParameters[3];
	mvRootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	mvRootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
	mvRootParameters[0].DescriptorTable.pDescriptorRanges = ranges;
	mvRootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	// Model transform buffer
	mvRootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	mvRootParameters[1].Descriptor.RegisterSpace = 0;
	mvRootParameters[1].Descriptor.ShaderRegister = 1; // b1
	mvRootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	// depth, normal+meshID, current and previous
	mvRootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	mvRootParameters[2].DescriptorTable.NumDescriptorRanges = 4;
	mvRootParameters[2].DescriptorTable.pDescriptorRanges = &ranges[1];
	mvRootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;


	D3D12_ROOT_SIGNATURE_FLAGS mvRootSignatureFlags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

	D3D12_STATIC_SAMPLER_DESC sampler = {};
	{
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		sampler.MipLODBias = 0;
		sampler.MaxAnisotropy = 0;
		sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		sampler.MinLOD = 0.0f;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.ShaderRegister = 0; // s0
		sampler.RegisterSpace = 0;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	}

	RootSignatureDesc mvDesc;
	mvDesc.desc.NumParameters = 3;
	mvDesc.desc.pParameters = mvRootParameters;
	mvDesc.desc.NumStaticSamplers = 1;
	mvDesc.desc.pStaticSamplers = &sampler;
	mvDesc.desc.Flags = mvRootSignatureFlags;

	mpMotionVectorsRootSig = createRootSignature(mpDevice, mvDesc.desc);

	ID3DBlobPtr vertexShaderMotionVectors = compileLibrary(L"Data/MotionVectors.hlsl", L"VSMain", L"vs_6_3");
	ID3DBlobPtr pixelShaderMotionVectors = compileLibrary(L"Data/MotionVectors.hlsl", L"PSMain", L"ps_6_3");

	// Create the graphics pipeline state object (PSO).
	psoDesc.pRootSignature = mpMotionVectorsRootSig.GetInterfacePtr();
	psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShaderMotionVectors.GetInterfacePtr());
	psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShaderMotionVectors.GetInterfacePtr());
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[1] = DXGI_FORMAT_UNKNOWN;
	psoDesc.RTVFormats[2] = DXGI_FORMAT_UNKNOWN;
	d3d_call(mpDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mpMotionVectorsState)));

	////////////////////////////
	// create output resources
	////////////////////////////

	D3D12_RESOURCE_DESC texDesc;
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = mSwapChainSize.x;
	texDesc.Height = mSwapChainSize.y;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE colorClearValue;
	colorClearValue.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	colorClearValue.Color[0] = 0.0f;
	colorClearValue.Color[1] = 0.0f;
	colorClearValue.Color[2] = 0.0f;
	colorClearValue.Color[3] = 0.0f;

	// Motion Vectors
	d3d_call(mpDevice->CreateCommittedResource(
		&kDefaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&colorClearValue,
		IID_PPV_ARGS(&mpGeometryBuffer_MotionVectors)
	));
	mpGeometryBuffer_MotionVectors->SetName(L"G-buffer Motion Vectors");

	// Normal and Mesh ID
	d3d_call(mpDevice->CreateCommittedResource(
		&kDefaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&colorClearValue,
		IID_PPV_ARGS(&mpGeometryBuffer_Normal)
	));
	mpGeometryBuffer_Normal->SetName(L"G-buffer Normal");

	// Previous Normal and Mesh ID
	d3d_call(mpDevice->CreateCommittedResource(
		&kDefaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&colorClearValue,
		IID_PPV_ARGS(&mpGeometryBuffer_Previous_Normal)
	));
	mpGeometryBuffer_Previous_Normal->SetName(L"G-buffer Previous Normal");

	// Color
	d3d_call(mpDevice->CreateCommittedResource(
		&kDefaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&colorClearValue,
		IID_PPV_ARGS(&mpGeometryBuffer_Color)
	));
	mpGeometryBuffer_Color->SetName(L"G-buffer Color");

	// Position
	d3d_call(mpDevice->CreateCommittedResource(
		&kDefaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		&colorClearValue,
		IID_PPV_ARGS(&mpGeometryBuffer_Position)
	));
	mpGeometryBuffer_Position->SetName(L"G-buffer Position");

	// depth stencil
	texDesc.Format = DXGI_FORMAT_D32_FLOAT;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE depthClearValue;
	depthClearValue.Format = DXGI_FORMAT_D32_FLOAT;
	depthClearValue.DepthStencil.Depth = 1.0f;
	depthClearValue.DepthStencil.Stencil = 0;

	d3d_call(mpDevice->CreateCommittedResource(
		&kDefaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&depthClearValue,
		IID_PPV_ARGS(&mpGeometryBuffer_Depth)
	));
	mpGeometryBuffer_Depth->SetName(L"G-buffer Depth");

	// Previous depth stencil
	d3d_call(mpDevice->CreateCommittedResource(
		&kDefaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&depthClearValue,
		IID_PPV_ARGS(&mpGeometryBuffer_Previous_Depth)
	));
	mpGeometryBuffer_Previous_Depth->SetName(L"G-buffer Previous Depth");

	// Motion vectors depth stencil
	d3d_call(mpDevice->CreateCommittedResource(
		&kDefaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthClearValue,
		IID_PPV_ARGS(&mpGeometryBuffer_MotionVectors_depth)
	));
	mpGeometryBuffer_MotionVectors_depth->SetName(L"Motion vectors Depth");
}

void PathTracer::createTemporalFilterPipeline()
{
	// root signature
	D3D12_DESCRIPTOR_RANGE ranges[5];

	// Current RT indirect output
	ranges[0].BaseShaderRegister = 0;//t0
	ranges[0].NumDescriptors = 1;
	ranges[0].RegisterSpace = 0;
	ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[0].OffsetInDescriptorsFromTableStart = 0;

	// Current RT direct output
	ranges[1].BaseShaderRegister = 1;//t1
	ranges[1].NumDescriptors = 1;
	ranges[1].RegisterSpace = 0;
	ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[1].OffsetInDescriptorsFromTableStart = 1;

	// Indirect color history
	ranges[2].BaseShaderRegister = 2; //t2
	ranges[2].NumDescriptors = 1;
	ranges[2].RegisterSpace = 0;
	ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[2].OffsetInDescriptorsFromTableStart = 0;

	// Direct color history
	ranges[3].BaseShaderRegister = 3; //t3
	ranges[3].NumDescriptors = 1;
	ranges[3].RegisterSpace = 0;
	ranges[3].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[3].OffsetInDescriptorsFromTableStart = 1;

	// Motion vectors
	ranges[4].BaseShaderRegister = 4; //t4
	ranges[4].NumDescriptors = 1;
	ranges[4].RegisterSpace = 0;
	ranges[4].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[4].OffsetInDescriptorsFromTableStart = 2;


	D3D12_ROOT_PARAMETER rootParameters[3];
	// Current indirect and direct color
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[0].DescriptorTable.NumDescriptorRanges = 2;
	rootParameters[0].DescriptorTable.pDescriptorRanges = ranges;
	// color histories and motion vector
	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[1].DescriptorTable.NumDescriptorRanges = 3;
	rootParameters[1].DescriptorTable.pDescriptorRanges = &ranges[2];
	// constants
	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParameters[2].Constants.Num32BitValues = 2;
	rootParameters[2].Constants.RegisterSpace = 0;
	rootParameters[2].Constants.ShaderRegister = 0; // b0

	D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS;

	D3D12_STATIC_SAMPLER_DESC sampler = {};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler.MipLODBias = 0;
	sampler.MaxAnisotropy = 0;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	sampler.MinLOD = 0.0f;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderRegister = 0; // s0
	sampler.RegisterSpace = 0;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	RootSignatureDesc desc;
	desc.desc.NumParameters = 3;
	desc.desc.pParameters = rootParameters;
	desc.desc.NumStaticSamplers = 1;
	desc.desc.pStaticSamplers = &sampler;
	desc.desc.Flags = rootSignatureFlags;

	mpTemporalFilterRootSig = createRootSignature(mpDevice, desc.desc);

	// compile shaders
	ID3DBlobPtr vertexShader = compileLibrary(L"Data/TemporalFilter.hlsl", L"VSMain", L"vs_6_3");
	ID3DBlobPtr pixelShader = compileLibrary(L"Data/TemporalFilter.hlsl", L"PSMain", L"ps_6_3");

	// create the pipeline state object (PSO)
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { nullptr, 0 };
	psoDesc.pRootSignature = mpTemporalFilterRootSig.GetInterfacePtr();
	psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.GetInterfacePtr());
	psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.GetInterfacePtr());

	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	//psoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON;
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

	psoDesc.DepthStencilState.DepthEnable = FALSE;
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
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
	psoDesc.NumRenderTargets = 2;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
	psoDesc.RTVFormats[1] = DXGI_FORMAT_R32G32B32A32_FLOAT;
	psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;

	d3d_call(mpDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mpTemporalFilterState)));

	// create output resource
	D3D12_RESOURCE_DESC texDesc;
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = mSwapChainSize.x;
	texDesc.Height = mSwapChainSize.y;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE colorClearValue;
	colorClearValue.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	colorClearValue.Color[0] = 0.0f;
	colorClearValue.Color[1] = 0.0f;
	colorClearValue.Color[2] = 0.0f;
	colorClearValue.Color[3] = 0.0f;

	// indirect render target
	d3d_call(mpDevice->CreateCommittedResource(
		&kDefaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COPY_SOURCE,
		&colorClearValue,
		IID_PPV_ARGS(&mpTemporalFilterIndirectOutput)
	));
	mpTemporalFilterIndirectOutput->SetName(L"Temporal Filter Indirect RTV");

	// direct render target
	d3d_call(mpDevice->CreateCommittedResource(
		&kDefaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COPY_SOURCE,
		&colorClearValue,
		IID_PPV_ARGS(&mpTemporalFilterDirectOutput)
	));
	mpTemporalFilterDirectOutput->SetName(L"Temporal Filter Direct RTV");

	// indirect color history
	texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	d3d_call(mpDevice->CreateCommittedResource(
		&kDefaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
		nullptr,
		IID_PPV_ARGS(&mpIndirectColorHistory)
	));
	mpIndirectColorHistory->SetName(L"Indirect Color History");

	// direct color history
	texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	d3d_call(mpDevice->CreateCommittedResource(
		&kDefaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		nullptr,
		IID_PPV_ARGS(&mpDirectColorHistory)
	));
	mpDirectColorHistory->SetName(L"Direct Color History");

}

void PathTracer::renderShadowMap()
{
	PIXBeginEvent(mpCmdList.GetInterfacePtr(), 0, L"Rasterize shadow map");

	// Set pipeline state
	mpCmdList->SetPipelineState(mpRasterPipelineState);

	// Set Root signature
	mpCmdList->SetGraphicsRootSignature(mpRasterRootSig.GetInterfacePtr());

	// Set descriptor heaps
	ID3D12DescriptorHeap* ppHeaps[] = { mpCbvSrvUavHeap.GetInterfacePtr() };
	mpCmdList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	// set "Shader Table", i.e. resources for root signature
	D3D12_GPU_DESCRIPTOR_HANDLE lightBufferHandle = mpCbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart();
	lightBufferHandle.ptr += mLightBufferHeapIndex * mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
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
	mpCmdList->ClearRenderTargetView(mShadowMapRtv_Normal, clearColor, 0, nullptr);

	// set render target
	mpCmdList->OMSetRenderTargets(
		3,
		mShadowMapRTVs,
		false,
		&mShadowMapDsv_Depth
	);

	mpCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// render models
	for (auto it = mModels.begin(); it != mModels.end(); ++it)
	{
		if (it->first != "Area light")
		{
			for (uint i = 0; i < it->second.getNumMeshes(); i++)
			{
				// Model to World Transform
				mpCmdList->SetGraphicsRootConstantBufferView(1, it->second.getTransformBufferGPUAdress());
				// Normal buffer
				mpCmdList->SetGraphicsRootShaderResourceView(2, it->second.getNormalBufferGPUAdress(i));
				// Vertex and Index buffers
				mpCmdList->IASetVertexBuffers(0, 1, it->second.getVertexBufferView(i));
				mpCmdList->IASetIndexBuffer(it->second.getIndexBufferView(i));
				// Color
				mpCmdList->SetGraphicsRoot32BitConstants(3, 3, &it->second.getColor(i), 0);

				// Draw
				mpCmdList->DrawIndexedInstanced(it->second.getIndexBufferView(i)->SizeInBytes / sizeof(uint), 1, 0, 0, 0);
			}
		}
	}


	// submit command list and reset
	mpCmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mpShadowMapTexture_Depth, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
	//mFenceValue = submitCommandList(mpCmdList, mpCmdQueue, mpFence, mFenceValue);
	//mpFence->SetEventOnCompletion(mFenceValue, mFenceEvent);
	//WaitForSingleObject(mFenceEvent, INFINITE);
	//mFrameObjects[mpSwapChain->GetCurrentBackBufferIndex()].pCmdAllocator->Reset();
	//mpCmdList->Reset(mFrameObjects[mpSwapChain->GetCurrentBackBufferIndex()].pCmdAllocator, nullptr);

	PIXEndEvent(mpCmdList.GetInterfacePtr());
}

void PathTracer::rayTrace()
{
	PIXBeginEvent(mpCmdList.GetInterfacePtr(), 0, L"Build TLAS");

	// Refit the top-level acceleration structure
	buildTopLevelAS(mpDevice, mpCmdList, mpBottomLevelAS, mTlasSize, true, mModels, mTopLevelBuffers);

	PIXEndEvent(mpCmdList.GetInterfacePtr());

	PIXBeginEvent(mpCmdList.GetInterfacePtr(), 0, L"Raytrace");

	// Let's ray trace
	resourceBarrier(mpCmdList, mpRtIndirectOutput, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	resourceBarrier(mpCmdList, mpRtDirectOutput, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	D3D12_DISPATCH_RAYS_DESC raytraceDesc = {};
	raytraceDesc.Width = mSwapChainSize.x;
	raytraceDesc.Height = mSwapChainSize.y;
	raytraceDesc.Depth = 1;

	// RayGen is the first entry in the shader-table
	raytraceDesc.RayGenerationShaderRecord.StartAddress = mpShaderTable->GetGPUVirtualAddress() + 0 * mShaderTableEntrySize;
	raytraceDesc.RayGenerationShaderRecord.SizeInBytes = mShaderTableEntrySize;

	// Miss is the second entry in the shader-table
	size_t missOffset = align_to(D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, 1 * mShaderTableEntrySize);
	raytraceDesc.MissShaderTable.StartAddress = mpShaderTable->GetGPUVirtualAddress() + missOffset;
	raytraceDesc.MissShaderTable.StrideInBytes = mShaderTableEntrySize;
	raytraceDesc.MissShaderTable.SizeInBytes = mShaderTableEntrySize * 2;   // 2 miss-entries

	// Hit is the fourth entry in the shader-table
	size_t hitOffset = align_to(D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, missOffset + 2 * mShaderTableEntrySize);
	raytraceDesc.HitGroupTable.StartAddress = mpShaderTable->GetGPUVirtualAddress() + hitOffset;
	raytraceDesc.HitGroupTable.StrideInBytes = mShaderTableEntrySize;
	raytraceDesc.HitGroupTable.SizeInBytes = mShaderTableEntrySize * 2 * mNumInstances;    // 2 hit-entries per model

	// Bind the empty root signature
	mpCmdList->SetComputeRootSignature(mpEmptyRootSig);

	// Dispatch
	mpCmdList->SetPipelineState1(mpRtPipelineState.GetInterfacePtr());
	mpCmdList->DispatchRays(&raytraceDesc);
	PIXEndEvent(mpCmdList.GetInterfacePtr());
}

void PathTracer::offlinePathTrace()
{
	PIXBeginEvent(mpCmdList.GetInterfacePtr(), 0, L"Offline Path trace");

	// Let's path trace
	resourceBarrier(mpCmdList, mpRtDirectOutput, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	resourceBarrier(mpCmdList, mpRtIndirectOutput, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	buildTopLevelAS(mpDevice, mpCmdList, mpBottomLevelAS, mTlasSize, true, mModels, mTopLevelBuffers);

	D3D12_DISPATCH_RAYS_DESC raytraceDesc = {};
	raytraceDesc.Width = mSwapChainSize.x;
	raytraceDesc.Height = mSwapChainSize.y;
	raytraceDesc.Depth = 1;

	// RayGen is the first entry in the shader-table
	raytraceDesc.RayGenerationShaderRecord.StartAddress = mpPathTracerShaderTable->GetGPUVirtualAddress() + 0 * mPathTracerShaderTableEntrySize;
	raytraceDesc.RayGenerationShaderRecord.SizeInBytes = mPathTracerShaderTableEntrySize;

	// Miss is the second entry in the shader-table
	size_t missOffset = align_to(D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, 1 * mPathTracerShaderTableEntrySize);
	raytraceDesc.MissShaderTable.StartAddress = mpPathTracerShaderTable->GetGPUVirtualAddress() + missOffset;
	raytraceDesc.MissShaderTable.StrideInBytes = mPathTracerShaderTableEntrySize;
	raytraceDesc.MissShaderTable.SizeInBytes = mPathTracerShaderTableEntrySize;   // 1 miss-entry

	// Hit is the third entry in the shader-table
	size_t hitOffset = align_to(D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, missOffset + mPathTracerShaderTableEntrySize);
	raytraceDesc.HitGroupTable.StartAddress = mpPathTracerShaderTable->GetGPUVirtualAddress() + hitOffset;
	raytraceDesc.HitGroupTable.StrideInBytes = mPathTracerShaderTableEntrySize;
	raytraceDesc.HitGroupTable.SizeInBytes = mPathTracerShaderTableEntrySize * mNumInstances;    // 1 hit-entry per model

	// Bind the empty root signature
	mpCmdList->SetComputeRootSignature(mpPathTracerEmptyRootSig);

	// Dispatch
	mpCmdList->SetPipelineState1(mpPathTracerPipelineState.GetInterfacePtr());
	mpCmdList->DispatchRays(&raytraceDesc);

	PIXEndEvent(mpCmdList.GetInterfacePtr());
}

void PathTracer::renderGeometryBuffer()
{
	PIXBeginEvent(mpCmdList.GetInterfacePtr(), 0, L"Render G-buffer");

	resourceBarrier(mpCmdList, mpGeometryBuffer_Depth, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	resourceBarrier(mpCmdList, mpGeometryBuffer_Normal, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	resourceBarrier(mpCmdList, mpGeometryBuffer_Color, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);


	// Set pipeline state
	mpCmdList->SetPipelineState(mpGeometryBufferState);

	// Set Root signature
	mpCmdList->SetGraphicsRootSignature(mpGeometryBufferRootSig.GetInterfacePtr());

	// Set descriptor heaps
	/*ID3D12DescriptorHeap* ppHeaps[] = { mpCbvSrvUavHeap.GetInterfacePtr() };
	mpCmdList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);*/

	// set "Shader Table", i.e. resources for root signature
	D3D12_GPU_DESCRIPTOR_HANDLE cameraMatrixBufferHandle = mpCbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart();
	cameraMatrixBufferHandle.ptr += mCameraMatrixBufferHeapIndex * mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	mpCmdList->SetGraphicsRootDescriptorTable(0, cameraMatrixBufferHandle); // b0

	// viewport
	mpCmdList->RSSetViewports(1, &mPostProcessingViewPort);
	mpCmdList->RSSetScissorRects(1, &mPostProcessingScissorRect);

	mpCmdList->OMSetStencilRef(0);

	// clear render targets
	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	mpCmdList->ClearRenderTargetView(mGeometryBufferRtv_Normal, clearColor, 0, nullptr);
	mpCmdList->ClearRenderTargetView(mGeometryBufferRtv_Color, clearColor, 0, nullptr);
	mpCmdList->ClearRenderTargetView(mGeometryBufferRtv_Position, clearColor, 0, nullptr);
	mpCmdList->ClearDepthStencilView(mGeometryBufferDsv_Depth, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// set render target
	mpCmdList->OMSetRenderTargets(
		3,
		mGeometryBufferRTVs,
		false,
		&mGeometryBufferDsv_Depth
	);

	mpCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// render models
	int meshID = 1;
	for (auto it = mModels.begin(); it != mModels.end(); ++it)
	{
		for (uint i = 0; i < it->second.getNumMeshes(); i++)
		{
			// Model to World Transform
			mpCmdList->SetGraphicsRootConstantBufferView(1, it->second.getTransformBufferGPUAdress());
			// Normal buffer
			mpCmdList->SetGraphicsRootShaderResourceView(2, it->second.getNormalBufferGPUAdress(i));
			// Vertex and Index buffers
			mpCmdList->IASetVertexBuffers(0, 1, it->second.getVertexBufferView(i));
			mpCmdList->IASetIndexBuffer(it->second.getIndexBufferView(i));
			// Color
			mpCmdList->SetGraphicsRoot32BitConstants(3, 3, &it->second.getColor(i), 0);
			// mesh id
			mpCmdList->SetGraphicsRoot32BitConstants(3, 1, &meshID, 3);
			meshID++;

			// Draw
			mpCmdList->DrawIndexedInstanced(it->second.getIndexBufferView(i)->SizeInBytes / sizeof(uint), 1, 0, 0, 0);
		}
	}

	PIXEndEvent(mpCmdList.GetInterfacePtr());
}
void PathTracer::renderMotionVectors()
{
	PIXBeginEvent(mpCmdList.GetInterfacePtr(), 0, L"Render Motion Vectors");
	resourceBarrier(mpCmdList, mpGeometryBuffer_MotionVectors, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	resourceBarrier(mpCmdList, mpGeometryBuffer_Normal, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
	resourceBarrier(mpCmdList, mpGeometryBuffer_Depth, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	// Set pipeline state
	mpCmdList->SetPipelineState(mpMotionVectorsState);

	// Set Root signature
	mpCmdList->SetGraphicsRootSignature(mpMotionVectorsRootSig.GetInterfacePtr());

	// set "Shader Table", i.e. resources for root signature
	D3D12_GPU_DESCRIPTOR_HANDLE handle = mpCbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart();
	UINT stepSize = mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	// camera matrix
	handle.ptr += mCameraMatrixBufferHeapIndex * stepSize;
	mpCmdList->SetGraphicsRootDescriptorTable(0, handle); // b0
	// depth, current and previous
	handle = mpCbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart();
	handle.ptr += mGeomteryBuffer_Depth_SrvHeapIndex * stepSize;
	mpCmdList->SetGraphicsRootDescriptorTable(2, handle); // t0, t1

	// clear render targets
	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	mpCmdList->ClearRenderTargetView(mGeometryBufferRtv_MotionVectors, clearColor, 0, nullptr);
	mpCmdList->ClearDepthStencilView(mGeometryBufferDsv_MotionVectors, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);


	// set render target
	mpCmdList->OMSetRenderTargets(
		1,
		&mGeometryBufferRtv_MotionVectors,
		false,
		&mGeometryBufferDsv_MotionVectors
	);

	// render models
	for (auto it = mModels.begin(); it != mModels.end(); ++it)
	{
		for (uint i = 0; i < it->second.getNumMeshes(); i++)
		{
			// Model to World Transform
			mpCmdList->SetGraphicsRootConstantBufferView(1, it->second.getTransformBufferGPUAdress());
			// Vertex and Index buffers
			mpCmdList->IASetVertexBuffers(0, 1, it->second.getVertexBufferView(i));
			mpCmdList->IASetIndexBuffer(it->second.getIndexBufferView(i));

			// Draw
			mpCmdList->DrawIndexedInstanced(it->second.getIndexBufferView(i)->SizeInBytes / sizeof(uint), 1, 0, 0, 0);
		}
	}

	resourceBarrier(mpCmdList, mpGeometryBuffer_MotionVectors, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	PIXEndEvent(mpCmdList.GetInterfacePtr());

	// copy depth, and normal+meshID to history
	// depth
	resourceBarrier(mpCmdList, mpGeometryBuffer_Depth, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
	resourceBarrier(mpCmdList, mpGeometryBuffer_Previous_Depth, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);

	mpCmdList->CopyResource(mpGeometryBuffer_Previous_Depth, mpGeometryBuffer_Depth);

	resourceBarrier(mpCmdList, mpGeometryBuffer_Depth, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	resourceBarrier(mpCmdList, mpGeometryBuffer_Previous_Depth, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	// normal+meshID
	resourceBarrier(mpCmdList, mpGeometryBuffer_Normal, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
	resourceBarrier(mpCmdList, mpGeometryBuffer_Previous_Normal, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);

	mpCmdList->CopyResource(mpGeometryBuffer_Previous_Normal, mpGeometryBuffer_Normal);

	resourceBarrier(mpCmdList, mpGeometryBuffer_Normal, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	resourceBarrier(mpCmdList, mpGeometryBuffer_Previous_Normal, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

}

void PathTracer::applyTemporalFilter()
{
	PIXBeginEvent(mpCmdList.GetInterfacePtr(), 0, L"Temporal Filtering");

	// resource barriers
	resourceBarrier(mpCmdList, mpRtIndirectOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	resourceBarrier(mpCmdList, mpRtDirectOutput,   D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	resourceBarrier(mpCmdList, mpIndirectColorHistory, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	resourceBarrier(mpCmdList, mpDirectColorHistory, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	resourceBarrier(mpCmdList, mpTemporalFilterIndirectOutput, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	resourceBarrier(mpCmdList, mpTemporalFilterDirectOutput,   D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

	// Set pipeline state
	mpCmdList->SetPipelineState(mpTemporalFilterState);

	// Set Root signature
	mpCmdList->SetGraphicsRootSignature(mpTemporalFilterRootSig.GetInterfacePtr());

	// //Set descriptor heaps
	//ID3D12DescriptorHeap* ppHeaps[] = { mpCbvSrvUavHeap.GetInterfacePtr() };
	//mpCmdList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	// set "Shader Table", i.e. resources for root signature
	D3D12_GPU_DESCRIPTOR_HANDLE handle = mHeapStart;
	handle.ptr += mRtIndirectOutputHeapIndex * mHeapEntrySize;
	mpCmdList->SetGraphicsRootDescriptorTable(0, handle); // t0-1, current RT outputs

	handle = mHeapStart;
	handle.ptr += mIndirectColorHistoryHeapIndex * mHeapEntrySize;
	mpCmdList->SetGraphicsRootDescriptorTable(1, handle); // t2-4, color history and motion vector

	// set constants
	mpCmdList->SetGraphicsRoot32BitConstants(2, 1, &mDropHistory, 0);
	mpCmdList->SetGraphicsRoot32BitConstants(2, 1, &mOffline, 1);


	// viewport
	mpCmdList->RSSetViewports(1, &mPostProcessingViewPort);
	mpCmdList->RSSetScissorRects(1, &mPostProcessingScissorRect);

	// clear render target
	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	mpCmdList->ClearRenderTargetView(mTemporalFilterIndirectRtv, clearColor, 0, nullptr);
	mpCmdList->ClearRenderTargetView(mTemporalFilterDirectRtv, clearColor, 0, nullptr);


	// set render target
	mpCmdList->OMSetRenderTargets(
		2,
		mTemporalFilterRtvs,
		false,
		nullptr
	);

	mpCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// draw
	mpCmdList->IASetVertexBuffers(0, 0, nullptr);
	mpCmdList->IASetIndexBuffer(nullptr);
	mpCmdList->DrawInstanced(6, 1, 0, 0);

	// resource barriers

	// copy RTV to color history
	resourceBarrier(mpCmdList, mpTemporalFilterIndirectOutput, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
	resourceBarrier(mpCmdList, mpIndirectColorHistory, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
		mpCmdList->CopyResource(mpIndirectColorHistory, mpTemporalFilterIndirectOutput);

	resourceBarrier(mpCmdList, mpTemporalFilterDirectOutput, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
	resourceBarrier(mpCmdList, mpDirectColorHistory, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
		mpCmdList->CopyResource(mpDirectColorHistory, mpTemporalFilterDirectOutput);


	PIXEndEvent(mpCmdList.GetInterfacePtr());
}

void PathTracer::applySpatialFilter(bool indirect)
{
	uint8_t inputHeapIndex;
	uint8_t outputHeapIndex;
	ID3D12ResourcePtr inputResource;
	ID3D12ResourcePtr outputResource;
	if (indirect)
	{
		inputHeapIndex = mIndirectColorHistoryHeapIndex;
		inputResource = mpIndirectColorHistory;
		outputHeapIndex = mFilteredIndirectColorHeapIndex;
		outputResource = mpFilteredIndirectColor;
	}
	else
	{
		inputHeapIndex = mDirectColorHistoryHeapIndex;
		inputResource = mpDirectColorHistory;
		outputHeapIndex = mFilteredDirectColorHeapIndex;
		outputResource = mpFilteredDirectColor;
	}

	// Post processing
	PIXBeginEvent(mpCmdList.GetInterfacePtr(), 0, L"Spatial filter");

	// resource barriers
	resourceBarrier(mpCmdList, inputResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	// Set Root signature
	mpCmdList->SetComputeRootSignature(mpSpatialFilterRootSig.GetInterfacePtr());

	// Set descriptor heaps
	ID3D12DescriptorHeap* ppHeaps[] = { mpCbvSrvUavHeap.GetInterfacePtr() };
	mpCmdList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	auto heapStart = mpCbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart();
	auto heapEntrySize = mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	D3D12_GPU_DESCRIPTOR_HANDLE handle;

	// Set constants
	mpCmdList->SetComputeRoot32BitConstants(0, 1, &mBlurRadius, 0);

	// Set Depth input
	handle = heapStart;
	handle.ptr += mGeomteryBuffer_Depth_SrvHeapIndex * heapEntrySize;
	mpCmdList->SetComputeRootDescriptorTable(3, handle); // t1

	// Set Normal input
	handle = heapStart;
	handle.ptr += mGeomteryBuffer_Normal_SrvHeapIndex * heapEntrySize;
	mpCmdList->SetComputeRootDescriptorTable(4, handle); // t2

	for (int i = 0; i < 5; i++)
	{
		mSpatialItr = i + 1;
		mpCmdList->SetComputeRoot32BitConstants(0, 1, &mSpatialItr, 0);
		//////////////
		// Pass 1
		//////////////
		mpCmdList->SetPipelineState(mpSpatialFilterStateHorz);

		// set "Shader Table", i.e. resources for root signature
		handle = heapStart;
		if (i == 0) {
			handle.ptr += inputHeapIndex * heapEntrySize;
		}
		else
		{
			handle.ptr += mBlur2OutputSrvHeapIndex * heapEntrySize;
		}
		mpCmdList->SetComputeRootDescriptorTable(1, handle); // t0
		handle = heapStart;
		handle.ptr += mBlur1OutputUavHeapIndex * heapEntrySize;
		mpCmdList->SetComputeRootDescriptorTable(2, handle); // u0


		// dispatch
		UINT numGroupsX = (UINT)ceilf(mSwapChainSize[0] / 256.0f);
		mpCmdList->Dispatch(numGroupsX, mSwapChainSize[1], 1);

		if (i == 0)
		{
			resourceBarrier(mpCmdList, mpBlurPass2Output, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}
		else
		{
			resourceBarrier(mpCmdList, mpBlurPass2Output, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}
		resourceBarrier(mpCmdList, mpBlurPass1Output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);


		//////////////
		// Pass 2
		//////////////

		mpCmdList->SetPipelineState(mpSpatialFilterStateVert);

		// set "Shader Table", i.e. resources for root signature
		handle = heapStart;
		handle.ptr += mBlur1OutputSrvHeapIndex * heapEntrySize;
		mpCmdList->SetComputeRootDescriptorTable(1, handle); // t0
		handle = heapStart;
		handle.ptr += mBlur2OutputUavHeapIndex * heapEntrySize;
	
		mpCmdList->SetComputeRootDescriptorTable(2, handle); // u0

		// dispatch
		UINT numGroupsY = (UINT)ceilf(mSwapChainSize[1] / 256.0f);
		mpCmdList->Dispatch(mSwapChainSize[0], numGroupsY, 1);

		resourceBarrier(mpCmdList, mpBlurPass1Output, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		resourceBarrier(mpCmdList, mpBlurPass2Output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	}

	// copy final output
	resourceBarrier(mpCmdList, mpBlurPass2Output, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
	resourceBarrier(mpCmdList, outputResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
	mpCmdList->CopyResource(outputResource, mpBlurPass2Output);


	PIXEndEvent(mpCmdList.GetInterfacePtr());
}

void PathTracer::applyToneMapping()
{
	PIXBeginEvent(mpCmdList.GetInterfacePtr(), 0, L"Tone Mapping");

	resourceBarrier(mpCmdList, mpToneMappingOutput, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	resourceBarrier(mpCmdList, mpFilteredIndirectColor, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	//resourceBarrier(mpCmdList, mpFilteredDirectColor, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	resourceBarrier(mpCmdList, mpGeometryBuffer_Color, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );


	// Set pipeline state
	mpCmdList->SetPipelineState(mpToneMappingState);

	// Set Root signature
	mpCmdList->SetGraphicsRootSignature(mpToneMappingRootSig.GetInterfacePtr());

	// Set descriptor heaps
	ID3D12DescriptorHeap* ppHeaps[] = { mpCbvSrvUavHeap.GetInterfacePtr() };
	mpCmdList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	// set "Shader Table", i.e. resources for root signature
	D3D12_GPU_DESCRIPTOR_HANDLE heapStart = mpCbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart();
	auto heapEntrySize = mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	D3D12_GPU_DESCRIPTOR_HANDLE handle = heapStart;
	handle.ptr += mFilteredIndirectColorHeapIndex * heapEntrySize;
	mpCmdList->SetGraphicsRootDescriptorTable(0, handle); // t0-1, input textures SRV

	handle = heapStart;
	handle.ptr += mShadowMaps_normal_HeapIndex * heapEntrySize;
	mpCmdList->SetGraphicsRootDescriptorTable(1, handle); // t2, RSM normal

	handle = heapStart;
	handle.ptr += mGeomteryBuffer_MotionVectors_SrvHeapIndex * heapEntrySize;
	mpCmdList->SetGraphicsRootDescriptorTable(2, handle); // t3, Motion Vectors

	handle = heapStart;
	handle.ptr += mGeomteryBuffer_Color_SrvHeapIndex * heapEntrySize;
	mpCmdList->SetGraphicsRootDescriptorTable(3, handle); // t4, G-buffer Color



	// viewport
	mpCmdList->RSSetViewports(1, &mPostProcessingViewPort);
	mpCmdList->RSSetScissorRects(1, &mPostProcessingScissorRect);

	// clear render target
	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	mpCmdList->ClearRenderTargetView(mToneMappingRtv, clearColor, 0, nullptr);

	// set render target
	mpCmdList->OMSetRenderTargets(
		1,
		&mToneMappingRtv,
		false,
		nullptr
	);

	mpCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// draw
	mpCmdList->IASetVertexBuffers(0, 0, nullptr);
	mpCmdList->IASetIndexBuffer(nullptr);
	mpCmdList->DrawInstanced(6, 1, 0, 0);

	resourceBarrier(mpCmdList, mpToneMappingOutput, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);


	PIXEndEvent(mpCmdList.GetInterfacePtr());
}

//////////////////////////////////////////////////////////////////////////
// Callbacks
//////////////////////////////////////////////////////////////////////////
void PathTracer::onLoad(HWND winHandle, uint32_t winWidth, uint32_t winHeight)
{
	initDXR(winHandle, winWidth, winHeight);        
	createAccelerationStructures();                 // Load models and build AS
	buildTransforms(mRotation);
	createRtPipelineState();       
	createPathTracerPipilineState();
	createShadowMapPipelineState();
	createLightBuffer();
	createShadowMapTextures();
	createSpatialFilterPipeline();
	createToneMappingPipeline();
	createGeometryBufferPipeline();
	createTemporalFilterPipeline();
	createCameraBuffers();							
	createEnvironmentMapBuffer();
	createShaderResources();                        // Create heap
	createShaderTable();    
	createPathTracerShaderTable();

	// Submit command list and wait for completion
	mFenceValue = submitCommandList(mpCmdList, mpCmdQueue, mpFence, mFenceValue);
	mpFence->SetEventOnCompletion(mFenceValue, mFenceEvent);
	WaitForSingleObject(mFenceEvent, INFINITE);

	mFrameObjects[0].pCmdAllocator->Reset();
	mpCmdList->Reset(mFrameObjects[0].pCmdAllocator, nullptr);

	// get heap info from GPU
	mHeapStart = mpCbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart();
}

void PathTracer::onFrameRender(bool *gKeys)
{
//#ifndef OFFLINE
	readKeyboardInput(gKeys);
//#endif
	// Update light buffer
	updateLightBuffer();

	// Update object transforms
	buildTransforms(mRotation);
	//mRotation += 0.5f*mCameraSpeed;

	// Update camera
	updateCameraBuffers();

	// Update transform buffer
	updateTransformBuffers();

	uint32_t rtvIndex = beginFrame();

	//////////////////////
	// G-buffer
	//////////////////////
	renderGeometryBuffer();
	renderMotionVectors();

	//////////////////////
	// Shadow map
	//////////////////////
	renderShadowMap();

	//////////////////////
	// ray-trace
	//////////////////////
#ifdef OFFLINE
	offlinePathTrace();
#else
	rayTrace();
#endif // OFFLINE

	//////////////////////
	// Temporal filter
	//////////////////////
	applyTemporalFilter();

	//////////////////////
	// Spatial filter
	//////////////////////
	applySpatialFilter(true);//indirect
	//applySpatialFilter(false);//direct

	//////////////////////
	// Tone Map
	//////////////////////
	applyToneMapping();


	// Copy the results to the back-buffer
	//resourceBarrier(mpCmdList, mpOutputResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
	resourceBarrier(mpCmdList, mFrameObjects[rtvIndex].pSwapChainBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
	mpCmdList->CopyResource(mFrameObjects[rtvIndex].pSwapChainBuffer, mpToneMappingOutput);

	PIXEndEvent(mpCmdList.GetInterfacePtr());

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
