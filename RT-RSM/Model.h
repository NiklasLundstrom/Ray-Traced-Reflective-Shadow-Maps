#pragma once
#include "Framework.h"

class Model
{

public:
	Model(LPCWSTR name, uint8_t idx, vec3 color);
	Model() { mName = L"unnamed model"; mModelIndex = 0; mColor = vec3(1.0f, 1.0f, 1.0f); };

	LPCWSTR getName() { return mName; }
	uint8_t getModelIndex() { return mModelIndex; }
	vec3	getColor(int idx) { return multipleMeshes? mColors[idx] : mColor; }
	bool	hasMultipleMeshes() { return multipleMeshes; }

	D3D12_VERTEX_BUFFER_VIEW* getVertexBufferView(int idx) { return multipleMeshes? &mVertexBufferViews[idx] : &mVertexBufferView; }
	D3D12_INDEX_BUFFER_VIEW* getIndexBufferView(int idx) { return multipleMeshes? &mIndexBufferViews[idx] : &mIndexBufferView; }
	mat4 getTransformMatrix() { return mModelToWorld; }
	D3D12_GPU_VIRTUAL_ADDRESS getIndexBufferGPUAdress(int idx) { return multipleMeshes? mpIndexBuffers[idx]->GetGPUVirtualAddress() : mpIndexBuffer->GetGPUVirtualAddress(); }
	D3D12_GPU_VIRTUAL_ADDRESS getNormalBufferGPUAdress(int idx) { return multipleMeshes? mpNormalBuffers[idx]->GetGPUVirtualAddress() : mpNormalBuffer->GetGPUVirtualAddress(); }
	D3D12_GPU_VIRTUAL_ADDRESS getColorBufferGPUAdress() { return mpColorBuffer->GetGPUVirtualAddress(); }
	D3D12_GPU_VIRTUAL_ADDRESS getTransformBufferGPUAdress() { return mpTransformBuffer->GetGPUVirtualAddress(); }
	uint getNumMeshes() { return mNumMeshes; }

	AccelerationStructureBuffers loadModelFromFile(ID3D12Device5Ptr pDevice, ID3D12GraphicsCommandList4Ptr pCmdList, const char* pFileName, Assimp::Importer* pImporter, bool loadTransform);
	std::vector<AccelerationStructureBuffers> loadMultipleModelsFromFile(ID3D12Device5Ptr pDevice, ID3D12GraphicsCommandList4Ptr pCmdList, const char* pFileName, Assimp::Importer* pImporter, bool loadTransform);
	void loadModelHardCodedPlane(ID3D12Device5Ptr pDevice, ID3D12GraphicsCommandList4Ptr pCmdList);

	void setTransform(mat4 transform) { mModelToWorldPrev = mModelToWorld; 
											mModelToWorld = transform * mVertexToModel; }
	void updateTransformBuffer();

protected:
	
	LPCWSTR mName;
	uint8_t mModelIndex;

	// Single mesh
	ID3D12ResourcePtr mpVertexBuffer;
	ID3D12ResourcePtr mpIndexBuffer;
	ID3D12ResourcePtr mpNormalBuffer;

	D3D12_VERTEX_BUFFER_VIEW	mVertexBufferView;
	D3D12_INDEX_BUFFER_VIEW		mIndexBufferView;
	
	vec3 mColor;
	ID3D12ResourcePtr mpColorBuffer;

	// Multiple meshes
	uint mNumMeshes = 1;
	bool multipleMeshes = false;
	std::vector<ID3D12ResourcePtr> mpVertexBuffers;
	std::vector<ID3D12ResourcePtr> mpIndexBuffers;
	std::vector<ID3D12ResourcePtr> mpNormalBuffers;

	std::vector < D3D12_VERTEX_BUFFER_VIEW>	mVertexBufferViews;
	std::vector < D3D12_INDEX_BUFFER_VIEW>		mIndexBufferViews;

	std::vector < vec3 > mColors;

	// help functions to create the buffers
	ID3D12ResourcePtr createBuffer(ID3D12Device5Ptr pDevice, uint64_t size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState, const D3D12_HEAP_PROPERTIES& heapProps);
	ID3D12ResourcePtr createVB(ID3D12Device5Ptr pDevice, aiVector3D* aiVertecies, int numVertices);
	ID3D12ResourcePtr createIB(ID3D12Device5Ptr pDevice, aiFace* aiFaces, int numFaces);
	ID3D12ResourcePtr createNB(ID3D12Device5Ptr pDevice, aiVector3D* aiNormals, int numNormals);
	ID3D12ResourcePtr createCB(ID3D12Device5Ptr pDevice);


	ID3D12ResourcePtr createPlaneVB(ID3D12Device5Ptr pDevice);
	ID3D12ResourcePtr createPlaneIB(ID3D12Device5Ptr pDevice);
	ID3D12ResourcePtr createPlaneNB(ID3D12Device5Ptr pDevice);

	AccelerationStructureBuffers createBottomLevelAS(ID3D12Device5Ptr pDevice, ID3D12GraphicsCommandList4Ptr pCmdList, ID3D12ResourcePtr pVB, const uint32_t vertexCount, ID3D12ResourcePtr pIB, const uint32_t indexCount);


	// transform
	mat4 mVertexToModel;
	mat4 mModelToWorld;
	mat4 mModelToWorldPrev;
	ID3D12ResourcePtr mpTransformBuffer;


	// for plane only
	struct VertexType
	{
		vec3 position;
		vec2 uv;
	};
};