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
#pragma once
#include "Framework.h"
#include "Model.h"

#define HYBRID

 //using namespace System::Windows::Input;

class PathTracer : public Tutorial
{
public:
	//struct AccelerationStructureBuffers
	//{
	//    ID3D12ResourcePtr pScratch;
	//    ID3D12ResourcePtr pResult;
	//    ID3D12ResourcePtr pInstanceDesc;    // Used only for top-level AS
	//};

	void onLoad(HWND winHandle, uint32_t winWidth, uint32_t winHeight) override;
	void onFrameRender(bool *gKeys) override;
	void onShutdown() override;
private:
	//////////////////////////////////////////////////////////////////////////
	// Tutorial 02 code
	//////////////////////////////////////////////////////////////////////////
	void initDXR(HWND winHandle, uint32_t winWidth, uint32_t winHeight);
	uint32_t beginFrame();
	void endFrame(uint32_t rtvIndex);
	HWND							mHwnd = nullptr;
	ID3D12Device5Ptr				mpDevice;
	ID3D12CommandQueuePtr			mpCmdQueue;
	IDXGISwapChain3Ptr				mpSwapChain;
	uvec2							mSwapChainSize;
	ID3D12GraphicsCommandList4Ptr	mpCmdList;
	ID3D12FencePtr					mpFence;
	HANDLE							mFenceEvent;
	uint64_t						mFenceValue = 0;

	struct
	{
		ID3D12CommandAllocatorPtr	pCmdAllocator;
		ID3D12ResourcePtr			pSwapChainBuffer;
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
	} mFrameObjects[kDefaultSwapChainBuffers];


	// Heap data
	struct HeapData
	{
		ID3D12DescriptorHeapPtr pHeap;
		uint32_t				usedEntries = 0;
	};
	HeapData mRtvHeap;
	static const uint32_t		kRtvHeapSize = 3;
	void createShaderResources();
	ID3D12DescriptorHeapPtr mpCbvSrvUavHeap;

	D3D12_GPU_DESCRIPTOR_HANDLE mHeapStart;
	UINT64						mHeapEntrySize;

	//////////////////////////////////////////////////////////////////////////
	// Scene
	//////////////////////////////////////////////////////////////////////////

	static const int mNumInstances = 14 /*+ 48*/; // keep in sync with value hard coded in buildTopLevelAS()

	// models
	std::map<std::string, Model> mModels;

	void buildTransforms(float rotation);
	float mRotation = 0;

#ifdef HYBRID
	void updateTransformBuffers();
#endif

	void createAccelerationStructures();
	ID3D12ResourcePtr				mpBottomLevelAS[mNumInstances];
	AccelerationStructureBuffers	mTopLevelBuffers;
	uint64_t						mTlasSize = 0;

	//////////////////////////////////////////////////////////////////////////
	// Ray-tracing
	//////////////////////////////////////////////////////////////////////////
	void createRtPipelineState();
	void createShaderTable();
	void rayTrace();
	ID3D12StateObjectPtr	mpRtPipelineState;
	ID3D12RootSignaturePtr	mpEmptyRootSig;
	ID3D12ResourcePtr		mpShaderTable;
	uint32_t				mShaderTableEntrySize = 0;

	ID3D12ResourcePtr		mpRtIndirectOutput;
	ID3D12ResourcePtr		mpRtDirectOutput;
	uint8_t					mRtIndirectOutputHeapIndex;
	uint8_t					mRtDirectOutputHeapIndex;

	//////////////////////////////////////////////////////////////////////////
	// Miscellaneous
	//////////////////////////////////////////////////////////////////////////
	void readKeyboardInput(bool *gKeys);
	Assimp::Importer	importer;

	void createEnvironmentMapBuffer();
	ID3D12ResourcePtr	mpEnvironmentMapBuffer;
	uint8_t				mEnvironmentMapHeapIndex;


	//////////////////////////////////////////////////////////////////////////
	// Camera
	//////////////////////////////////////////////////////////////////////////
	struct {
		vec3	cameraPosition = vec3(2.95, 2.12, 7.04);
		float	cameraAngle = -3.15f;
		vec3	cameraDirection = vec3(sin(-cameraAngle), 0, cos(-cameraAngle));

		vec3 eye;
		vec3 at;
		vec3 up = vec3(0, 1, 0);

		mat4 viewMat;
		mat4 viewMatPrev;
		mat4 projMat;

		mat4 viewMatInv;
		mat4 projMatInv;
	}mCamera;

	void createCameraBuffers();
	void updateCameraBuffers();
	ID3D12ResourcePtr	mpCameraBuffer;
	uint32_t			mCameraBufferSize = 0;
	ID3D12ResourcePtr	mpCameraMatrixBuffer;
	uint32_t			mCameraMatrixBufferSize = 3 * sizeof(mat4); // view, viewPrev and projection
	uint8_t				mCameraMatrixBufferHeapIndex;


	float				mCameraSpeed = 1.0f;

	//////////////////////////////////////////////////////////////////////////
	// Shadow map
	//////////////////////////////////////////////////////////////////////////
#ifdef HYBRID

	const UINT kShadowMapWidth = 512;
	const UINT kShadowMapHeight = 512;

	D3D12_VIEWPORT			mRasterViewPort;
	D3D12_RECT				mRasterScissorRect;
	ID3D12RootSignaturePtr	mpRasterRootSig;
	ID3D12PipelineStatePtr	mpRasterPipelineState;

	ID3D12ResourcePtr			mpLightBuffer;
	uint32_t					mLightBufferSize = 2 * sizeof(mat4); // view and projection
	uint8_t						mLightBufferHeapIndex;
	ID3D12ResourcePtr			mpLightPositionBuffer;
	uint32_t					mLightPositionBufferSize = sizeof(float3);

	ID3D12DescriptorHeapPtr		mpShadowMapDsvHeap;
	ID3D12DescriptorHeapPtr		mpShadowMapRtvHeap;
	ID3D12ResourcePtr			mpShadowMapTexture_Depth;
	ID3D12ResourcePtr			mpShadowMapTexture_Position;
	ID3D12ResourcePtr			mpShadowMapTexture_Normal;
	ID3D12ResourcePtr			mpShadowMapTexture_Flux;
	D3D12_CPU_DESCRIPTOR_HANDLE mShadowMapDsv_Depth;
	D3D12_CPU_DESCRIPTOR_HANDLE	mShadowMapRtv_Position;
	D3D12_CPU_DESCRIPTOR_HANDLE	mShadowMapRtv_Normal;
	D3D12_CPU_DESCRIPTOR_HANDLE	mShadowMapRtv_Flux;
	uint8_t						mShadowMapsHeapIndex;
	uint8_t						mShadowMaps_normal_HeapIndex;

	D3D12_CPU_DESCRIPTOR_HANDLE mShadowMapRTVs[3];

	void createRasterPipelineState();
	void renderShadowMap();
	void createLightBuffer();
	void updateLightBuffer();
	void createShadowMapTextures();

	struct
	{
		vec3 position = vec3(-11.9, 9.73, -5.8);
		vec3 direction = vec3(sin(0.893), 0.0, cos(0.893));
		vec3 center = vec3(0.8, 2.5, 4.0);
		float radius = 28.0f;
		float phi = quarter_pi<float>();
		float theta = pi<float>();// 0.0f;

		vec3 eye;
		vec3 at;
		vec3 up = vec3(0, 1, 0);

		mat4 viewMat;
		mat4 projMat;
	} mLight;

#endif // HYBRID

	//////////////////////////////////////////////////////////////////////////
	// Post processing stuff
	//////////////////////////////////////////////////////////////////////////

	// Spatial filter
	ID3D12RootSignaturePtr	mpSpatialFilterRootSig;
	ID3D12PipelineStatePtr	mpSpatialFilterStateHorz;
	ID3D12PipelineStatePtr	mpSpatialFilterStateVert;

	uint8_t					mBlur1OutputUavHeapIndex;
	uint8_t					mBlur1OutputSrvHeapIndex;
	uint8_t					mBlur2OutputUavHeapIndex;
	uint8_t					mBlur2OutputSrvHeapIndex;

	ID3D12ResourcePtr		mpBlurPass1Output;
	ID3D12ResourcePtr		mpBlurPass2Output;

	ID3D12ResourcePtr		mpFilteredIndirectColor;
	ID3D12ResourcePtr		mpFilteredDirectColor;
	uint8_t					mFilteredIndirectColorHeapIndex;
	uint8_t					mFilteredDirectColorHeapIndex;

	void createSpatialFilterPipeline();
	void applySpatialFilter(bool indirect);
	std::vector<float>		mGaussWeights;
	int						mBlurRadius;
	int						mSpatialItr;

	// tone mapping
	void createToneMappingPipeline();
	void applyToneMapping();

	ID3D12RootSignaturePtr	mpToneMappingRootSig;
	ID3D12PipelineStatePtr	mpToneMappingState;

	D3D12_VIEWPORT			mPostProcessingViewPort;
	D3D12_RECT				mPostProcessingScissorRect;

	ID3D12ResourcePtr		mpToneMappingOutput;
	D3D12_CPU_DESCRIPTOR_HANDLE mToneMappingRtv;

	// G-buffer
	void createGeometryBufferPipeline();
	void renderGeometryBuffer();
	void renderMotionVectors();
	ID3D12RootSignaturePtr	mpGeometryBufferRootSig;
	ID3D12RootSignaturePtr	mpMotionVectorsRootSig;
	ID3D12PipelineStatePtr	mpGeometryBufferState;
	ID3D12PipelineStatePtr	mpMotionVectorsState;

	ID3D12ResourcePtr		mpGeometryBuffer_MotionVectors;
	ID3D12ResourcePtr		mpGeometryBuffer_MotionVectors_depth;
	ID3D12ResourcePtr		mpGeometryBuffer_Depth;
	ID3D12ResourcePtr		mpGeometryBuffer_Previous_Depth;
	ID3D12ResourcePtr		mpGeometryBuffer_Normal;
	ID3D12ResourcePtr		mpGeometryBuffer_Color;
	ID3D12ResourcePtr		mpGeometryBuffer_Position;
	D3D12_CPU_DESCRIPTOR_HANDLE mGeometryBufferRtv_MotionVectors;
	D3D12_CPU_DESCRIPTOR_HANDLE mGeometryBufferDsv_MotionVectors;
	D3D12_CPU_DESCRIPTOR_HANDLE mGeometryBufferDsv_Depth;
	D3D12_CPU_DESCRIPTOR_HANDLE mGeometryBufferRtv_Normal;
	D3D12_CPU_DESCRIPTOR_HANDLE mGeometryBufferRtv_Color;
	D3D12_CPU_DESCRIPTOR_HANDLE mGeometryBufferRtv_Position;
	uint8_t					mGeomteryBuffer_MotionVectors_SrvHeapIndex;
	uint8_t					mGeomteryBuffer_Depth_SrvHeapIndex;
	uint8_t					mGeomteryBuffer_Previous_Depth_SrvHeapIndex;
	uint8_t					mGeomteryBuffer_Normal_SrvHeapIndex;
	uint8_t					mGeomteryBuffer_Color_SrvHeapIndex;
	uint8_t					mGeomteryBuffer_Position_SrvHeapIndex;

	D3D12_CPU_DESCRIPTOR_HANDLE mGeometryBufferRTVs[3];

	// temporal filter
	void createTemporalFilterPipeline();
	void applyTemporalFilter();
	ID3D12RootSignaturePtr	mpTemporalFilterRootSig;
	ID3D12PipelineStatePtr	mpTemporalFilterState;

	ID3D12ResourcePtr		mpIndirectColorHistory;
	ID3D12ResourcePtr		mpDirectColorHistory;
	ID3D12ResourcePtr		mpTemporalFilterIndirectOutput;
	ID3D12ResourcePtr		mpTemporalFilterDirectOutput;
	D3D12_CPU_DESCRIPTOR_HANDLE mTemporalFilterIndirectRtv;
	D3D12_CPU_DESCRIPTOR_HANDLE mTemporalFilterDirectRtv;
	uint8_t					mIndirectColorHistoryHeapIndex;
	uint8_t					mDirectColorHistoryHeapIndex;
	uint8_t					mTemporalFilterOutputHeapIndex;

	D3D12_CPU_DESCRIPTOR_HANDLE mTemporalFilterRtvs[2];




};