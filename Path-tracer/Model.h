#pragma once
#include "Framework.h"

class Model
{

public:
	Model(LPCWSTR name);
	Model() { mName = L"unnamed model"; };

	D3D12_VERTEX_BUFFER_VIEW* getVertexBufferView() { return &mVertexBufferView; }
	D3D12_INDEX_BUFFER_VIEW* getIndexBufferView() { return &mIndexBufferView; }
	mat4 getTransformMatrix() { return mModelToWorld; }
	D3D12_GPU_VIRTUAL_ADDRESS getIndexBufferGPUAdress() { return mpIndexBuffer->GetGPUVirtualAddress(); }
	D3D12_GPU_VIRTUAL_ADDRESS getNormalBufferGPUAdress() { return mpNormalBuffer->GetGPUVirtualAddress(); }
	D3D12_GPU_VIRTUAL_ADDRESS getTransformBufferGPUAdress() { return mpTransformBuffer->GetGPUVirtualAddress(); }

	AccelerationStructureBuffers loadModelFromFile(ID3D12Device5Ptr pDevice, ID3D12GraphicsCommandList4Ptr pCmdList, const char* pFileName, Assimp::Importer* pImporter);
	AccelerationStructureBuffers loadModelHardCodedPlane(ID3D12Device5Ptr pDevice, ID3D12GraphicsCommandList4Ptr pCmdList);

	void setTransform(mat4 transform) { mModelToWorld = transform; }
	void updateTransformBuffer();

protected:
	

	LPCWSTR mName;

	// Mesh
	ID3D12ResourcePtr mpVertexBuffer;
	ID3D12ResourcePtr mpIndexBuffer;
	ID3D12ResourcePtr mpNormalBuffer;

	ID3D12ResourcePtr createBuffer(ID3D12Device5Ptr pDevice, uint64_t size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState, const D3D12_HEAP_PROPERTIES& heapProps);
	ID3D12ResourcePtr createVB(ID3D12Device5Ptr pDevice, aiVector3D* aiVertecies, int numVertices);
	ID3D12ResourcePtr createIB(ID3D12Device5Ptr pDevice, aiFace* aiFaces, int numFaces);
	ID3D12ResourcePtr createNB(ID3D12Device5Ptr pDevice, aiVector3D* aiNormals, int numNormals);

	ID3D12ResourcePtr createPlaneVB(ID3D12Device5Ptr pDevice);
	ID3D12ResourcePtr createPlaneIB(ID3D12Device5Ptr pDevice);
	ID3D12ResourcePtr createPlaneNB(ID3D12Device5Ptr pDevice);

	D3D12_VERTEX_BUFFER_VIEW	mVertexBufferView;
	D3D12_INDEX_BUFFER_VIEW		mIndexBufferView;

	AccelerationStructureBuffers createBottomLevelAS(ID3D12Device5Ptr pDevice, ID3D12GraphicsCommandList4Ptr pCmdList, ID3D12ResourcePtr pVB, const uint32_t vertexCount, ID3D12ResourcePtr pIB, const uint32_t indexCount);


	// transform
	mat4 mModelToWorld;
	ID3D12ResourcePtr mpTransformBuffer;

	// material
	LPCWSTR mHitGroup;
	vec3 mColor;








};