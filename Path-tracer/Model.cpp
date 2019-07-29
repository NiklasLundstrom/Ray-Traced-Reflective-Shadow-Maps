#include "Model.h"


///////////////////////////////////////////
// Create buffers
///////////////////////////////////////////

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

ID3D12ResourcePtr Model::createBuffer(ID3D12Device5Ptr pDevice, uint64_t size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState, const D3D12_HEAP_PROPERTIES& heapProps)
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

ID3D12ResourcePtr Model::createPlaneVB(ID3D12Device5Ptr pDevice)
{
	VertexType vertices[4];
	vertices[0].position = vec3(0, 0, 0);
	vertices[0].uv = vec2(0, 0);
	vertices[1].position = vec3(1, 1, 0);
	vertices[1].uv = vec2(1, 1);
	vertices[2].position = vec3(0, 1, 0);
	vertices[2].uv = vec2(0, 1);
	vertices[3].position = vec3(1, 0, 0);
	vertices[3].uv = vec2(1, 0);

	// For simplicity, we create the vertex buffer on the upload heap, but that's not required
	ID3D12ResourcePtr pBuffer = createBuffer(pDevice, sizeof(vertices), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
	uint8_t* pData;
	pBuffer->Map(0, nullptr, (void**)&pData);
	memcpy(pData, vertices, sizeof(vertices));
	pBuffer->Unmap(0, nullptr);
	return pBuffer;
}

ID3D12ResourcePtr Model::createPlaneIB(ID3D12Device5Ptr pDevice)
{
	const uint indices[] =
	{
		0,
		1,
		2,
		0,
		3,
		1
	};// left hand oriented!

	// For simplicity, we create the vertex buffer on the upload heap, but that's not required
	ID3D12ResourcePtr pBuffer = createBuffer(pDevice, sizeof(indices), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
	uint8_t* pData;
	pBuffer->Map(0, nullptr, (void**)&pData);
	memcpy(pData, indices, sizeof(indices));
	pBuffer->Unmap(0, nullptr);
	return pBuffer;
}

ID3D12ResourcePtr Model::createPlaneNB(ID3D12Device5Ptr pDevice)
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

ID3D12ResourcePtr Model::createVB(ID3D12Device5Ptr pDevice, aiVector3D* aiVertecies, int numVertices)
{
	ID3D12ResourcePtr pBuffer = createBuffer(pDevice, numVertices * sizeof(vec3), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
	uint8_t* pData;
	pBuffer->Map(0, nullptr, (void**)&pData);
	memcpy(pData, aiVertecies, numVertices * sizeof(vec3));
	pBuffer->Unmap(0, nullptr);
	return pBuffer;
}

ID3D12ResourcePtr Model::createIB(ID3D12Device5Ptr pDevice, aiFace* aiFaces, int numFaces)
{
	ID3D12ResourcePtr pBuffer = createBuffer(pDevice, numFaces * 3 * sizeof(uint), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
	uint8_t* pData;
	pBuffer->Map(0, nullptr, (void**)&pData);
	for (int i = 0; i < numFaces; i++)
	{
		memcpy(pData + i * 3 * sizeof(uint), aiFaces[i].mIndices, 3 * sizeof(uint));
	}
	pBuffer->Unmap(0, nullptr);
	return pBuffer;
}

ID3D12ResourcePtr Model::createNB(ID3D12Device5Ptr pDevice, aiVector3D* aiNormals, int numNormals)
{
	ID3D12ResourcePtr pBuffer = createBuffer(pDevice, numNormals * sizeof(vec3), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
	uint8_t* pData;
	pBuffer->Map(0, nullptr, (void**)&pData);
	memcpy(pData, aiNormals, numNormals * sizeof(vec3));
	pBuffer->Unmap(0, nullptr);
	return pBuffer;
}

ID3D12ResourcePtr Model::createCB(ID3D12Device5Ptr pDevice)
{
	ID3D12ResourcePtr pBuffer = createBuffer(pDevice, mNumMeshes * sizeof(vec3), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
	uint8_t* pData;
	pBuffer->Map(0, nullptr, (void**)&pData);
		memcpy(pData, multipleMeshes? mColors.data() : &mColor, mNumMeshes * sizeof(vec3));
	pBuffer->Unmap(0, nullptr);
	return pBuffer;
}

AccelerationStructureBuffers Model::createBottomLevelAS(ID3D12Device5Ptr pDevice, ID3D12GraphicsCommandList4Ptr pCmdList, ID3D12ResourcePtr pVB, const uint32_t vertexCount, ID3D12ResourcePtr pIB, const uint32_t indexCount)
{
	D3D12_RAYTRACING_GEOMETRY_DESC geomDesc;
	geomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	geomDesc.Triangles.VertexBuffer.StartAddress = pVB->GetGPUVirtualAddress();
	geomDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(vec3);
	geomDesc.Triangles.VertexCount = vertexCount;
	geomDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
	geomDesc.Triangles.IndexBuffer = pIB->GetGPUVirtualAddress();
	geomDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
	geomDesc.Triangles.IndexCount = indexCount;
	geomDesc.Triangles.Transform3x4 = NULL;
	geomDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE | D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION;
	

	// Get the size requirements for the scratch and AS buffers
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
	inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
	inputs.NumDescs = 1;
	inputs.pGeometryDescs = &geomDesc;
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

inline glm::mat4 aiMatrix4x4ToGlm(const aiMatrix4x4* from)
{
	// from: https://stackoverflow.com/questions/29184311/how-to-rotate-a-skinned-models-bones-in-c-using-assimp
	glm::mat4 to;

	to[0][0] = (f32)from->a1; to[0][1] = (f32)from->b1;  to[0][2] = (f32)from->c1; to[0][3] = (f32)from->d1;
	to[1][0] = (f32)from->a2; to[1][1] = (f32)from->b2;  to[1][2] = (f32)from->c2; to[1][3] = (f32)from->d2;
	to[2][0] = (f32)from->a3; to[2][1] = (f32)from->b3;  to[2][2] = (f32)from->c3; to[2][3] = (f32)from->d3;
	to[3][0] = (f32)from->a4; to[3][1] = (f32)from->b4;  to[3][2] = (f32)from->c4; to[3][3] = (f32)from->d4;

	return to;
}

///////////////////////////////////////////
// Callbacks
///////////////////////////////////////////

void Model::loadModelHardCodedPlane(ID3D12Device5Ptr pDevice, ID3D12GraphicsCommandList4Ptr pCmdList)
{
	// create and set up VB, IB and NB
	mpVertexBuffer = createPlaneVB(pDevice);
	mpVertexBuffer->SetName((std::wstring(mName) + L" Vertex Buffer").c_str());
	mpIndexBuffer = createPlaneIB(pDevice);
	mpIndexBuffer->SetName((std::wstring(mName) + L" Index Buffer").c_str());
	//mpNormalBuffer = createPlaneNB(pDevice);
	//mpNormalBuffer->SetName((std::wstring(mName) + L" Normal Buffer").c_str());

	// VB view
	mVertexBufferView.BufferLocation = mpVertexBuffer->GetGPUVirtualAddress();
	mVertexBufferView.StrideInBytes = sizeof(VertexType);
	mVertexBufferView.SizeInBytes = 4 * sizeof(VertexType);

	// IB view
	mIndexBufferView.BufferLocation = mpIndexBuffer->GetGPUVirtualAddress();
	mIndexBufferView.Format = DXGI_FORMAT_R32_UINT;
	mIndexBufferView.SizeInBytes = 6 * sizeof(uint);

	//// create transform buffer
	//mpTransformBuffer = createBuffer(pDevice, sizeof(mat4), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
	//mpTransformBuffer->SetName((std::wstring(mName) + L" Transform").c_str());


	//// BLAS
	//AccelerationStructureBuffers bottomLevelBuffer = createBottomLevelAS(
	//															pDevice,
	//															pCmdList,
	//															mpVertexBuffer,
	//															4,
	//															mpIndexBuffer,
	//															6
	//														);

	return;// bottomLevelBuffer;

}
/*
	Load a single mesh from a file using assimp and create all the buffers and BLAS
*/
AccelerationStructureBuffers Model::loadModelFromFile(ID3D12Device5Ptr pDevice, ID3D12GraphicsCommandList4Ptr pCmdList, const char* pFileName, Assimp::Importer* pImporter, bool loadTransform)
{
	const aiScene* scene = pImporter->ReadFile(pFileName,
		aiProcess_CalcTangentSpace |
		aiProcess_JoinIdenticalVertices |
		aiProcess_Triangulate |
		aiProcess_GenNormals |
		aiProcess_FixInfacingNormals |
		aiProcess_GenUVCoords |
		aiProcess_TransformUVCoords |
		aiProcess_MakeLeftHanded |
		aiProcess_FindInvalidData);
	aiMesh* mesh = scene->mMeshes[0];

	// create and set up VB, IB and NB
	mpVertexBuffer = createVB(pDevice, mesh->mVertices, mesh->mNumVertices);
	mpVertexBuffer->SetName((std::wstring(mName) + L" Vertex Buffer").c_str());
	mpIndexBuffer = createIB(pDevice, mesh->mFaces, mesh->mNumFaces);
	mpIndexBuffer->SetName((std::wstring(mName) + L" Index Buffer").c_str());
	mpNormalBuffer = createNB(pDevice, mesh->mNormals, mesh->mNumVertices);
	mpNormalBuffer->SetName((std::wstring(mName) + L" Normal Buffer").c_str());

	// VB view
	mVertexBufferView.BufferLocation = mpVertexBuffer->GetGPUVirtualAddress();
	mVertexBufferView.StrideInBytes = sizeof(vec3);
	mVertexBufferView.SizeInBytes = mesh->mNumVertices * sizeof(vec3);

	// IB view
	mIndexBufferView.BufferLocation = mpIndexBuffer->GetGPUVirtualAddress();
	mIndexBufferView.Format = DXGI_FORMAT_R32_UINT;
	mIndexBufferView.SizeInBytes = mesh->mNumFaces * 3 * sizeof(uint);

	// Color buffer
	mpColorBuffer = createCB(pDevice);
	mpColorBuffer->SetName((std::wstring(mName) + L" Color").c_str());

	// create transform buffer
	mpTransformBuffer = createBuffer(pDevice, 2*sizeof(mat4), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
	mpTransformBuffer->SetName((std::wstring(mName) + L" Transform").c_str());
	if (loadTransform)
	{
		aiMatrix4x4 transform = scene->mRootNode->mChildren[0]->mTransformation;
		mVertexToModel = aiMatrix4x4ToGlm(&transform);
	}
	// BLAS
	AccelerationStructureBuffers bottomLevelBuffer = createBottomLevelAS(
																pDevice,
																pCmdList,
																mpVertexBuffer,
																mesh->mNumVertices,
																mpIndexBuffer,
																mesh->mNumFaces * 3
															);

	return bottomLevelBuffer;
}

/*
	Load multiple meshes from a file using assimp, and create all the buffers and BLAS
*/
std::vector<AccelerationStructureBuffers> Model::loadMultipleModelsFromFile(ID3D12Device5Ptr pDevice, ID3D12GraphicsCommandList4Ptr pCmdList, const char* pFileName, Assimp::Importer* pImporter, bool loadTransform)
{
	const aiScene* scene = pImporter->ReadFile(pFileName,
		aiProcess_CalcTangentSpace |
		aiProcess_JoinIdenticalVertices |
		aiProcess_Triangulate |
		aiProcess_GenNormals |
		aiProcess_FixInfacingNormals |
		aiProcess_GenUVCoords |
		aiProcess_TransformUVCoords |
		aiProcess_MakeLeftHanded |
		aiProcess_FindInvalidData);

	mNumMeshes = scene->mNumMeshes;
	multipleMeshes = true;
	std::vector<AccelerationStructureBuffers> bottomLevelBuffers;
	for (uint i = 0; i < scene->mNumMeshes; i++)
	{
		aiMesh* mesh = scene->mMeshes[i];

		// create and set up VB, IB and NB
		mpVertexBuffers.push_back(createVB(pDevice, mesh->mVertices, mesh->mNumVertices));
		mpVertexBuffers[i]->SetName((std::wstring(mName) + L" sub" + std::to_wstring(i) + L" Vertex Buffer").c_str());
		mpIndexBuffers.push_back(createIB(pDevice, mesh->mFaces, mesh->mNumFaces));
		mpIndexBuffers[i]->SetName((std::wstring(mName) + L" sub" + std::to_wstring(i) + L" Index Buffer").c_str());
		mpNormalBuffers.push_back(createNB(pDevice, mesh->mNormals, mesh->mNumVertices));
		mpNormalBuffers[i]->SetName((std::wstring(mName) + L" sub" + std::to_wstring(i) + L" Normal Buffer").c_str());

		// VB view
		D3D12_VERTEX_BUFFER_VIEW vbView;
		vbView.BufferLocation = mpVertexBuffers[i]->GetGPUVirtualAddress();
		vbView.StrideInBytes = sizeof(vec3);
		vbView.SizeInBytes = mesh->mNumVertices * sizeof(vec3);
		mVertexBufferViews.push_back(vbView);

		// IB view
		D3D12_INDEX_BUFFER_VIEW ibView;
		ibView.BufferLocation = mpIndexBuffers[i]->GetGPUVirtualAddress();
		ibView.Format = DXGI_FORMAT_R32_UINT;
		ibView.SizeInBytes = mesh->mNumFaces * 3 * sizeof(uint);
		mIndexBufferViews.push_back(ibView);

		// BLAS
		bottomLevelBuffers.push_back( createBottomLevelAS(
			pDevice,
			pCmdList,
			mpVertexBuffers[i],
			mesh->mNumVertices,
			mpIndexBuffers[i],
			mesh->mNumFaces * 3
		));

		// colour
		mColors.push_back(mColor);
	}

	// create colour buffer
	mpColorBuffer = createCB(pDevice);
	mpColorBuffer->SetName((std::wstring(mName) + L" Colors").c_str());

	// create transform buffer
	mpTransformBuffer = createBuffer(pDevice, 2 * sizeof(mat4), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
	mpTransformBuffer->SetName((std::wstring(mName) + L" Transform").c_str());
	if (loadTransform)
	{
		aiMatrix4x4 transform = scene->mRootNode->mChildren[0]->mTransformation;
		mVertexToModel = aiMatrix4x4ToGlm(&transform);
	}

	return bottomLevelBuffers;
}

void Model::updateTransformBuffer()
{
	uint8_t* pData;
	d3d_call(mpTransformBuffer->Map(0, nullptr, (void**)&pData));
		memcpy(pData, &mModelToWorld, sizeof(mat4));
		memcpy(pData + sizeof(mModelToWorld), &mModelToWorldPrev, sizeof(mat4));
	mpTransformBuffer->Unmap(0, nullptr);
}

Model::Model(LPCWSTR name, uint8_t idx, vec3 color)
{
	mName = name;
	mModelIndex = idx;
	mColor = color;
}
