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

#define HYBRID

//using namespace System::Windows::Input;

class PathTracer : public Tutorial
{
public:
    struct AccelerationStructureBuffers
    {
        ID3D12ResourcePtr pScratch;
        ID3D12ResourcePtr pResult;
        ID3D12ResourcePtr pInstanceDesc;    // Used only for top-level AS
    };

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

    //////////////////////////////////////////////////////////////////////////
    // Tutorial 03, Tutorial 11
    //////////////////////////////////////////////////////////////////////////
    void createAccelerationStructures();
    ID3D12ResourcePtr			mpVertexBuffer[2];
	// 3 instances: plane, area light and robot
	// keep in sync with value hard coded in buildTopLevelAS()
	static const int			mNumInstances = 3; 
	mat4 mTransforms[mNumInstances];
	void buildTransforms(float rotation);

#ifdef HYBRID
	D3D12_VERTEX_BUFFER_VIEW	mVertexBufferView[2];
	D3D12_INDEX_BUFFER_VIEW		mIndexBufferView[2];
	ID3D12ResourcePtr			mpTransformBuffer[mNumInstances];
	void createTransformBuffers();
	void updateTransformBuffers();
#endif

	ID3D12ResourcePtr				mpIndexBuffer[2];
	ID3D12ResourcePtr				mpNormalBuffer[2];
    ID3D12ResourcePtr				mpBottomLevelAS[2];
    AccelerationStructureBuffers	mTopLevelBuffers;
    uint64_t						mTlasSize = 0;


    //////////////////////////////////////////////////////////////////////////
    // Tutorial 04
    //////////////////////////////////////////////////////////////////////////
    void createRtPipelineState();
    ID3D12StateObjectPtr	mpRtPipelineState;
    ID3D12RootSignaturePtr	mpEmptyRootSig;
    
    //////////////////////////////////////////////////////////////////////////
    // Tutorial 05
    //////////////////////////////////////////////////////////////////////////
    void createShaderTable();
    ID3D12ResourcePtr		mpShaderTable;
    uint32_t				mShaderTableEntrySize = 0;

    //////////////////////////////////////////////////////////////////////////
    // Tutorial 06
    //////////////////////////////////////////////////////////////////////////
    void createShaderResources();
    ID3D12ResourcePtr		mpOutputResource;
    ID3D12DescriptorHeapPtr mpCbvSrvUavHeap;
    static const uint32_t	kSrvUavHeapSize = 2;

    //////////////////////////////////////////////////////////////////////////
    // Tutorial 14
    //////////////////////////////////////////////////////////////////////////
    float mRotation = 0;

	//////////////////////////////////////////////////////////////////////////
	// My own stuff
	//////////////////////////////////////////////////////////////////////////
	void readKeyboardInput(bool *gKeys);

	struct {
		vec3	cameraPosition = vec3(0, 0, 0);
		vec3	cameraDirection = vec3(0, 0, 1);// direction is left_handed
		float	cameraAngle = 0.0f;// glm::pi<float>();// angle is right-handed
	}mCamera;
	
	void createCameraBuffer();
	void updateCameraBuffer();
	ID3D12ResourcePtr	mpCameraBuffer;
	uint32_t			mCameraBufferSize = 0;

	Assimp::Importer	importer;
	float				mCameraSpeed = 1.0f;

	void createHDRTextureBuffer();
	ID3D12ResourcePtr	mpHDRTextureBuffer;
	
	//////////////////////////////////////////////////////////////////////////
	// Hybrid stuff
	//////////////////////////////////////////////////////////////////////////
	#ifdef HYBRID
	
	const UINT kShadowMapWidth = 100;
	const UINT kShadowMapHeight = 100;

	D3D12_VIEWPORT			mRasterViewPort;
	D3D12_RECT				mRasterScissorRect;
	ID3D12RootSignaturePtr	mpRasterRootSig;
	ID3D12PipelineStatePtr	mpRasterPipelineState;

	ID3D12ResourcePtr			mpLightBuffer;
	uint32_t					mLightBufferSize = 2 * sizeof(mat4); // view and projection
	D3D12_CPU_DESCRIPTOR_HANDLE mLightBufferView;

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

	D3D12_CPU_DESCRIPTOR_HANDLE mShadowMapRTVs[3];

	void createRasterPipelineState();
	void renderDepthToTexture();
	void createLightBuffer();
	void updateLightBuffer();
	void createShadowMapTextures();

	struct
	{
		vec3 position = vec3(0.0, 0.0, 0.0);
		vec3 direction = vec3(0.0, 0.0, 1.0);

		vec3 eye;
		vec3 at;
		vec3 up = vec3(0, 1, 0);

		mat4 viewMat;
		mat4 projMat;
	} mLight;

	#endif // HYBRID
};