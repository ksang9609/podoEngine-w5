// EngineLib/Rendering/GpuResourceManager.cpp

#include "GpuResourceManager.h"

#include <directxtk/DDSTextureLoader.h>
#include <directxtk/WICTextureLoader.h>
#include <Windows.h>

#include "Core/BuiltinAssets.h"
#include "Rendering/Primitives/Cube.h"
#include "Rendering/Primitives/Sphere.h"
#include "Rendering/Primitives/Triangle.h"
#include "Rendering/Primitives/GizmoArrow.h"
#include "Rendering/Primitives/Circle.h"
#include "Rendering/Primitives/Primitives.h"
#include "Rendering/ShaderConstants.h"

#include "Editor/Console.h"

using Microsoft::WRL::ComPtr;

FBoundingBox calculateBoundingBox(const TArray<FVertexSimple>& vertices)
{
	FVector3 LocalMin = FVector3(vertices[0].x, vertices[0].y, vertices[0].z);
	FVector3 LocalMax = FVector3(vertices[0].x, vertices[0].y, vertices[0].z);
	for (int i = 0;i < vertices.Num();i++)
	{
		LocalMin.x = min(LocalMin.x, vertices[i].x);
		LocalMin.y = min(LocalMin.y, vertices[i].y);
		LocalMin.z = min(LocalMin.z, vertices[i].z);
		LocalMax.x = max(LocalMax.x, vertices[i].x);
		LocalMax.y = max(LocalMax.y, vertices[i].y);
		LocalMax.z = max(LocalMax.z, vertices[i].z);
	} // AABB 렌더링에 필요한 LocalMin,Max 저장
	FBoundingBox LocalBound;
	LocalBound.min = LocalMin;
	LocalBound.max = LocalMax;
	return LocalBound;
}

FBoundingBox calculateBoundingBox(const TArray<FVertexTextured>& vertices)
{
	FVector3 LocalMin = FVector3(vertices[0].x, vertices[0].y, vertices[0].z);
	FVector3 LocalMax = FVector3(vertices[0].x, vertices[0].y, vertices[0].z);
	for (int i = 0;i < vertices.Num();i++)
	{
		LocalMin.x = min(LocalMin.x, vertices[i].x);
		LocalMin.y = min(LocalMin.y, vertices[i].y);
		LocalMin.z = min(LocalMin.z, vertices[i].z);
		LocalMax.x = max(LocalMax.x, vertices[i].x);
		LocalMax.y = max(LocalMax.y, vertices[i].y);
		LocalMax.z = max(LocalMax.z, vertices[i].z);
	} // AABB 렌더링에 필요한 LocalMin,Max 저장
	FBoundingBox LocalBound;
	LocalBound.min = LocalMin;
	LocalBound.max = LocalMax;
	return LocalBound;
}

FBoundingBox calculateBoundingBox(const TArray<FNormalVertex>& vertices)
{
	FVector3 LocalMin = FVector3(vertices[0].pos.x, vertices[0].pos.y, vertices[0].pos.z);
	FVector3 LocalMax = FVector3(vertices[0].pos.x, vertices[0].pos.y, vertices[0].pos.z);
	for (int i = 0;i < vertices.Num();i++)
	{
		LocalMin.x = min(LocalMin.x, vertices[i].pos.x);
		LocalMin.y = min(LocalMin.y, vertices[i].pos.y);
		LocalMin.z = min(LocalMin.z, vertices[i].pos.z);
		LocalMax.x = max(LocalMax.x, vertices[i].pos.x);
		LocalMax.y = max(LocalMax.y, vertices[i].pos.y);
		LocalMax.z = max(LocalMax.z, vertices[i].pos.z);
	} // AABB 렌더링에 필요한 LocalMin,Max 저장
	FBoundingBox LocalBound;
	LocalBound.min = LocalMin;
	LocalBound.max = LocalMax;
	return LocalBound;
}

FGpuResourceManager::FGpuResourceManager()
{
}

void FGpuResourceManager::Initialize(FAssetManager& assetManagerRef,
	ID3D11Device& deviceRef)
{
	mAssetManagerRef = &assetManagerRef;
	mDeviceRef = &deviceRef;

	create(0);
}

void FGpuResourceManager::SetDistanceRange(float distanceRange)
{
	// Recreate unicode font constant buffer
	createConstantBuffers(distanceRange);
}

const FBuffer* FGpuResourceManager::FindImmutableBufferOrAdd(FName bufferName)
{
	const FBuffer* buffer = mImmutableBufferMap.Find(bufferName);
	if (buffer)
	{
		return buffer;
	}

	const UStaticMesh* staticMeshAsset = mAssetManagerRef->FindStaticMeshAssetOrNull(bufferName);
	if (!staticMeshAsset)
	{
		assert(false && "Static mesh asset not found in AssetManager.");
		return nullptr;
	}

	const FStaticMesh* staticMeshData = staticMeshAsset->GetStaticMeshAsset();
	if (staticMeshData)
	{
		CreateBuffer(bufferName, staticMeshData->Vertices, staticMeshData->Indices);
		return mImmutableBufferMap.Find(bufferName);
	}
	else
	{
		assert(false && "Static mesh data not found in AssetManager.");
		return nullptr;
	}
}

ID3D11ShaderResourceView* FGpuResourceManager::FindTextureOrAdd(FName texturePath)
{

	// 비어 있는 FName("None")은 텍스처 경로가 아니다.
	if (texturePath.IsNone())
	{
		return nullptr;
	}

	ComPtr<ID3D11ShaderResourceView>* textureSRV = mTextureMap.Find(texturePath);
	if (textureSRV)
	{
		return textureSRV->Get();
	}

	FString lowerPath = texturePath.ToString().ToLower();

	if(lowerPath.EndsWith(FString(".dds")))
	{
		CreateTextureFromDDS(texturePath);
	}
	else if (lowerPath.EndsWith(FString(".png")) || lowerPath.EndsWith(FString(".jpg")) || lowerPath.EndsWith(FString(".jpeg")))
	{
		CreateTextureFromWIC(texturePath);
	}
	else
	{
		assert(false && "Unsupported texture format. Only .dds, .png, .jpg, and .jpeg are supported.");
		return nullptr;
	}
	textureSRV = mTextureMap.Find(texturePath);

	if (textureSRV)
	{
		return textureSRV->Get();
	}
	else
	{
		assert(false && "Failed to create or find texture.");
		return nullptr;
	}
}

ID3D11Buffer& FGpuResourceManager::GetConstantBuffer(EContantBufferType bufferType) const
{
	assert(bufferType >= 0 && bufferType < CBT_Count && "Invalid constant buffer type.");
	assert(mConstantBuffer[bufferType] && "Constant buffer not created.");
	return *mConstantBuffer[bufferType].Get();
}

ID3D11BlendState& FGpuResourceManager::GetBlendState(EBlendStateType stateType) const
{
	assert(stateType >= 0 && stateType < BST_Count && "Invalid blend state type.");
	assert(mBlendState[stateType] && "Blend state not created.");
	return *mBlendState[stateType].Get();
}

ID3D11InputLayout& FGpuResourceManager::GetInputLayout(EInputLayoutType layoutType) const
{
	assert(layoutType >= 0 && layoutType < ILT_Count && "Invalid input layout type.");
	assert(mInputLayout[layoutType] && "Input layout not created.");
	return *mInputLayout[layoutType].Get();
}

ID3D11SamplerState& FGpuResourceManager::GetSamplerState(ESamplerStateType stateType) const
{
	assert(stateType >= 0 && stateType < SST_Count && "Invalid sampler state type.");
	assert(mSamplerState[stateType] && "Sampler state not created.");
	return *mSamplerState[stateType].Get();
}

ID3D11RasterizerState& FGpuResourceManager::GetRasterizerState(ERasterizerStateType stateType) const
{
	assert(stateType >= 0 && stateType < RST_Count && "Invalid rasterizer state type.");
	assert(mRasterizerState[stateType] && "Rasterizer state not created.");
	return *mRasterizerState[stateType].Get();
}

ID3D11DepthStencilState& FGpuResourceManager::GetDepthStencilState(EDepthStencilStateType stateType) const
{
	assert(stateType >= 0 && stateType < DSS_Count && "Invalid depth stencil state type.");
	assert(mDepthStencilState[stateType] && "Depth stencil state not created.");
	return *mDepthStencilState[stateType].Get();
}

const FBuffer& FGpuResourceManager::GetFontBuffer() const
{
	assert(mFontBuffer.Buffer && "Font buffer not created.");
	return mFontBuffer;
}

const FBuffer& FGpuResourceManager::GetUnicodeFontBuffer() const
{
	assert(mUnicodeFontBuffer.Buffer && "Unicode font buffer not created.");
	return mUnicodeFontBuffer;
}

const FBuffer& FGpuResourceManager::GetLineBuffer() const
{
	assert(mLineBuffer.Buffer && "Line buffer not created.");
	return mLineBuffer;
}

const FBuffer& FGpuResourceManager::GetInstanceBuffer() const
{
	assert(mInstanceBuffer.Buffer && "Instance buffer not created.");
	return mInstanceBuffer;
}

ID3D11VertexShader& FGpuResourceManager::GetVertexShader(EVertexShaderType shaderType) const
{
	assert(shaderType >= 0 && shaderType < VST_Count && "Invalid vertex shader type.");
	assert(mVertexShader[shaderType] && "Vertex shader not created.");
	return *mVertexShader[shaderType].Get();
}

ID3D11PixelShader& FGpuResourceManager::GetPixelShader(EPixelShaderType shaderType) const
{
	assert(shaderType >= 0 && shaderType < PST_Count && "Invalid pixel shader type.");
	assert(mPixelShader[shaderType] && "Pixel shader not created.");
	return *mPixelShader[shaderType].Get();
}

void FGpuResourceManager::create(float distanceRange)
{
	createBuiltinBuffers();
	createBuiltinTextures();

	createBlendStates();
	createSamplerStates();
	createRasterizerStates();
	createDepthStencilStates();
	createShadersAndInputLayout();

	createConstantBuffers(distanceRange);
}

void FGpuResourceManager::CreateBuffer(FName bufferName, const TArray<FVertexSimple>& vertices, const TArray<uint32>& indices)
{
	assert(mDeviceRef != nullptr && "Device reference is not set. Call SetDevice() before creating resources.");
	FBuffer buffer;

	// Create vertex buffer
	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.ByteWidth = static_cast<UINT>(vertices.Num() * sizeof(FVertexSimple));
	bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
	bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

	D3D11_SUBRESOURCE_DATA vertexBufferData = {};
	vertexBufferData.pSysMem = vertices.GetData();

	mDeviceRef->CreateBuffer(&bufferDesc, &vertexBufferData, &buffer.Buffer);
	buffer.SourceNum = static_cast<uint32>(vertices.Num());

	// Create index buffer if needed
	if (indices.Num() > 0)
	{
		D3D11_BUFFER_DESC indexBufferDesc = {};
		indexBufferDesc.ByteWidth = static_cast<UINT>(indices.Num() * sizeof(uint32));
		indexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
		indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

		D3D11_SUBRESOURCE_DATA indexBufferData = {};
		indexBufferData.pSysMem = indices.GetData();

		mDeviceRef->CreateBuffer(&indexBufferDesc, &indexBufferData, &buffer.IndexBuffer);
		buffer.IndexCount = static_cast<uint32>(indices.Num());
	}

	buffer.LocalBounds = calculateBoundingBox(vertices);

	mImmutableBufferMap.Add(bufferName, std::move(buffer));
}

void FGpuResourceManager::CreateBuffer(FName bufferName, const TArray<FVertexTextured>& vertices, const TArray<uint32>& indices)
{
	assert(mDeviceRef != nullptr && "Device reference is not set. Call SetDevice() before creating resources.");
	FBuffer buffer;

	// Create vertex buffer
	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.ByteWidth = static_cast<UINT>(vertices.Num() * sizeof(FVertexTextured));
	bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
	bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	D3D11_SUBRESOURCE_DATA vertexBufferData = {};
	vertexBufferData.pSysMem = vertices.GetData();
	mDeviceRef->CreateBuffer(&bufferDesc, &vertexBufferData, &buffer.Buffer);
	buffer.SourceNum = static_cast<uint32>(vertices.Num());

	// Create index buffer if needed
	if (indices.Num() > 0)
	{
		D3D11_BUFFER_DESC indexBufferDesc = {};
		indexBufferDesc.ByteWidth = static_cast<UINT>(indices.Num() * sizeof(uint32));
		indexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
		indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		D3D11_SUBRESOURCE_DATA indexBufferData = {};
		indexBufferData.pSysMem = indices.GetData();
		mDeviceRef->CreateBuffer(&indexBufferDesc, &indexBufferData, &buffer.IndexBuffer);
		buffer.IndexCount = static_cast<uint32>(indices.Num());
	}

	buffer.LocalBounds = calculateBoundingBox(vertices);
	mImmutableBufferMap.Add(bufferName, std::move(buffer));
}

void FGpuResourceManager::CreateBuffer(FName bufferName, const TArray<FNormalVertex>& vertices, const TArray<uint32>& indices)
{
	assert(mDeviceRef != nullptr && "Device reference is not set. Call SetDevice() before creating resources.");
	FBuffer buffer;

	// Create vertex buffer
	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.ByteWidth = static_cast<UINT>(vertices.Num() * sizeof(FNormalVertex));
	bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
	bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

	D3D11_SUBRESOURCE_DATA vertexBufferData = {};
	vertexBufferData.pSysMem = vertices.GetData();

	mDeviceRef->CreateBuffer(&bufferDesc, &vertexBufferData, &buffer.Buffer);
	buffer.SourceNum = static_cast<uint32>(vertices.Num());

	// Create index buffer if needed
	if (indices.Num() > 0)
	{
		D3D11_BUFFER_DESC indexBufferDesc = {};
		indexBufferDesc.ByteWidth = static_cast<UINT>(indices.Num() * sizeof(uint32));
		indexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
		indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

		D3D11_SUBRESOURCE_DATA indexBufferData = {};
		indexBufferData.pSysMem = indices.GetData();

		mDeviceRef->CreateBuffer(&indexBufferDesc, &indexBufferData, &buffer.IndexBuffer);
		buffer.IndexCount = static_cast<uint32>(indices.Num());
	}

	buffer.LocalBounds = calculateBoundingBox(vertices);

	mImmutableBufferMap.Add(bufferName, std::move(buffer));
}

void FGpuResourceManager::createBuiltinBuffers()
{
	assert(mDeviceRef != nullptr && "Device reference is not set. Call SetDevice() before creating resources.");

	/* Immutable buffers */
	// Static mesh
	CreateBuffer(BuiltinAssets::CubeMesh, CubeNormal_vertices, Cube_indices);
	CreateBuffer(BuiltinAssets::SphereMesh, SphereNormal_vertices, Sphere_indices);
	// Simple
	CreateBuffer(BuiltinAssets::CubeSimple, Cube_vertices);
	CreateBuffer(BuiltinAssets::Triangle, Triangle_vertices);
	CreateBuffer(BuiltinAssets::GizmoArrow, GizmoArrow_vertices);
	CreateBuffer(BuiltinAssets::Circle, Circle_vertices);
	CreateBuffer(BuiltinAssets::BillboardQuad, Quad_vertices);
	CreateBuffer(BuiltinAssets::BillboardQuadTextured, Quad_textured_indexed_vertices, Quad_indices);

	// Create loading screen vertex buffer
	TArray<FVertexTextured> loadingScreenVertices = {
		{ -1.0f,  1.0f, 0.0f,	0.0f, 0.0f }, // Top-left
		{  1.0f,  1.0f, 0.0f,	1.0f, 0.0f }, // Top-right
		{  1.0f, -1.0f, 0.0f,	1.0f, 1.0f }, // Bottom-right
		{ -1.0f, -1.0f, 0.0f,	0.0f, 1.0f }  // Bottom-left
	};
	TArray<uint32> loadingScreenIndices = { 0, 1, 2,   // First triangle
										   0, 2, 3 }; // Second triangle
	CreateBuffer(BuiltinAssets::LoadingScreenQuad,
		loadingScreenVertices,
		loadingScreenIndices);

	/* Mutable buffers */
	createFontBuffer(InitialMaxFontCount);
	createUnicodeFontBuffer(InitialMaxUnicodeFontCount);
	createInstanceBuffer(InitialMaxInstanceCount);
	createLineVertexBuffer(InitialMaxLineVertexCapacity);
	createLineIndexBuffer(InitialMaxLineIndexCapacity);
}

void FGpuResourceManager::createBuiltinTextures()
{
	assert(mDeviceRef != nullptr && "Device reference is not set. Call SetDevice() before creating resources.");

	// Create Font Texture
	CreateTextureFromDDS(BuiltinAssets::EnglishFontAtlas);
	CreateUnicodeFontTexture(BuiltinAssets::KoreanFontAtlas);

	// Create Loading Screen Texture
	CreateTextureFromDDS(BuiltinAssets::LoadingScreenTexture);

	// Create Default White Texture
	createDefaultWhiteTexture();
}

void FGpuResourceManager::CreateTextureFromDDS(FName texturePath)
{
	assert(mDeviceRef != nullptr && "Device reference is not set. Call SetDevice() before creating resources.");

	ComPtr<ID3D11ShaderResourceView> textureSRV;

	FString texturePathStr = texturePath.ToString();
	std::wstring texturePathW = std::wstring(texturePathStr.begin(), texturePathStr.end());
	DirectX::CreateDDSTextureFromFile(
		mDeviceRef,
		texturePathW.c_str(),
		nullptr,
		textureSRV.GetAddressOf());

	mTextureMap.Add(texturePath, std::move(textureSRV));
}

//png, jpg, bmp, gif, tiff, ico 등 WIC 지원 포맷
void FGpuResourceManager::CreateTextureFromWIC(FName texturePath)
{
	assert(mDeviceRef != nullptr && "Device reference is not set. Call SetDevice() before creating resources.");
	ComPtr<ID3D11ShaderResourceView> textureSRV;
	FString  texturePathStr = texturePath.ToString();
	std::wstring texturePathW = std::wstring(texturePathStr.begin(), texturePathStr.end());
	DirectX::CreateWICTextureFromFileEx(
		mDeviceRef,
		texturePathW.c_str(),				// Path to the texture file
		0,									// Default maximum size (0 means no limit)
		D3D11_USAGE_DEFAULT,
		D3D11_BIND_SHADER_RESOURCE,
		0,									// No CPU access flags
		0,									// No additional resource options
		DirectX::WIC_LOADER_IGNORE_SRGB,
		nullptr,							// No resource pointer needed
		textureSRV.GetAddressOf());
	mTextureMap.Add(texturePath, std::move(textureSRV));
}

void FGpuResourceManager::CreateUnicodeFontTexture(FName texturePath)
{
	assert(mDeviceRef != nullptr && "Device reference is not set. Call SetDevice() before creating resources.");

	ComPtr<ID3D11ShaderResourceView> textureSRV;

	FString  texturePathStr = texturePath.ToString();
	std::wstring texturePathW = std::wstring(texturePathStr.begin(), texturePathStr.end());

	DirectX::CreateWICTextureFromFileEx(
		mDeviceRef,
		texturePathW.c_str(),				// Path to the texture file
		0,									// Default maximum size (0 means no limit)
		D3D11_USAGE_DEFAULT,
		D3D11_BIND_SHADER_RESOURCE,
		0,									// No CPU access flags
		0,									// No additional resource options
		DirectX::WIC_LOADER_IGNORE_SRGB,
		nullptr,							// No resource pointer needed
		textureSRV.GetAddressOf());

	mTextureMap.Add(texturePath, std::move(textureSRV));
}

void FGpuResourceManager::createDefaultWhiteTexture()
{
	assert(mDeviceRef != nullptr && "Device reference is not set. Call SetDevice() before creating resources.");

	// 1x1 흰색 텍스처 생성
	constexpr uint32 whitePixel = 0xFFFFFFFF; // ARGB: 흰색
	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.Width = 1;
	textureDesc.Height = 1;
	textureDesc.MipLevels = 1;
	textureDesc.ArraySize = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.Usage = D3D11_USAGE_IMMUTABLE;
	textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA initialData = {};
	initialData.pSysMem = &whitePixel;
	initialData.SysMemPitch = sizeof(whitePixel);

	ComPtr<ID3D11Texture2D> whiteTexture;
	HRESULT hr = mDeviceRef->CreateTexture2D(&textureDesc, &initialData, &whiteTexture);
	if (FAILED(hr))
	{
		UE_LOG_F(Error, Render, "Failed to create default white texture. HRESULT: {}", hr);
		return;
	}

	ComPtr<ID3D11ShaderResourceView> whiteSRV;
	hr = mDeviceRef->CreateShaderResourceView(whiteTexture.Get(), nullptr, &whiteSRV);

	if (FAILED(hr))
	{
		UE_LOG_F(Error, Render, "Failed to create shader resource view for default white texture. HRESULT: {}", hr);
		return;
	}

	mTextureMap.Add(BuiltinAssets::DefaultWhiteTexture, std::move(whiteSRV));
}

void FGpuResourceManager::createSamplerStates()
{
	// Default
	{
		D3D11_SAMPLER_DESC samplerDesc{};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

		mDeviceRef->CreateSamplerState(&samplerDesc,
			&mSamplerState[SST_Default]);
	}

	// Clamp
	{
		D3D11_SAMPLER_DESC samplerDesc{};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

		mDeviceRef->CreateSamplerState(&samplerDesc,
			&mSamplerState[SST_Clamp]);
	}

	// Wrap
	{
		D3D11_SAMPLER_DESC samplerDesc{};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

		mDeviceRef->CreateSamplerState(&samplerDesc,
			&mSamplerState[SST_Wrap]);
	}
}

void FGpuResourceManager::createRasterizerStates()
{
	// Default
	{
		D3D11_RASTERIZER_DESC rasterizerDesc{};
		rasterizerDesc.FillMode = D3D11_FILL_SOLID;
		rasterizerDesc.CullMode = D3D11_CULL_BACK;
		rasterizerDesc.FrontCounterClockwise = FALSE;
		rasterizerDesc.DepthBias = 0;
		rasterizerDesc.DepthBiasClamp = 0.0f;
		rasterizerDesc.SlopeScaledDepthBias = 0.0f;
		rasterizerDesc.DepthClipEnable = TRUE;
		rasterizerDesc.ScissorEnable = TRUE;
		rasterizerDesc.MultisampleEnable = FALSE;
		rasterizerDesc.AntialiasedLineEnable = FALSE;
		mDeviceRef->CreateRasterizerState(&rasterizerDesc,
			&mRasterizerState[RST_Default]);
	}
	// Wireframe
	{
		D3D11_RASTERIZER_DESC rasterizerDesc{};
		rasterizerDesc.FillMode = D3D11_FILL_WIREFRAME;
		rasterizerDesc.CullMode = D3D11_CULL_NONE;
		rasterizerDesc.FrontCounterClockwise = FALSE;
		rasterizerDesc.DepthBias = 0;
		rasterizerDesc.DepthBiasClamp = 0.0f;
		rasterizerDesc.SlopeScaledDepthBias = 0.0f;
		rasterizerDesc.DepthClipEnable = TRUE;
		rasterizerDesc.ScissorEnable = TRUE;
		rasterizerDesc.MultisampleEnable = FALSE;
		rasterizerDesc.AntialiasedLineEnable = FALSE;
		mDeviceRef->CreateRasterizerState(&rasterizerDesc,
			&mRasterizerState[RST_Wireframe]);
	}
}

void FGpuResourceManager::createDepthStencilStates()
{
	// Default
	{
		D3D11_DEPTH_STENCIL_DESC depthStencilDesc{};
		depthStencilDesc.DepthEnable = TRUE;
		depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS;
		depthStencilDesc.StencilEnable = FALSE;
		mDeviceRef->CreateDepthStencilState(&depthStencilDesc,
			&mDepthStencilState[DSS_Default]);
	}
	// No Write
	{
		D3D11_DEPTH_STENCIL_DESC depthStencilDesc{};
		depthStencilDesc.DepthEnable = TRUE;
		depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS;
		depthStencilDesc.StencilEnable = FALSE;
		mDeviceRef->CreateDepthStencilState(&depthStencilDesc,
			&mDepthStencilState[DSS_NoWrite]);
	}
	// Stencil Mark
	{
		D3D11_DEPTH_STENCIL_DESC depthStencilDesc{};
		depthStencilDesc.DepthEnable = TRUE;
		depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		depthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		depthStencilDesc.StencilEnable = TRUE;

		depthStencilDesc.StencilReadMask = 0xFF;
		depthStencilDesc.StencilWriteMask = 0xFF;

		// 스텐실 마킹: 모든 픽셀에 StencilRef를 기록
		depthStencilDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
		depthStencilDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
		depthStencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_REPLACE;
		depthStencilDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		depthStencilDesc.BackFace = depthStencilDesc.FrontFace;

		mDeviceRef->CreateDepthStencilState(&depthStencilDesc,
			&mDepthStencilState[DSS_StencilMark]);
	}
	// Stencil Outline
	{
		D3D11_DEPTH_STENCIL_DESC depthStencilDesc{};
		depthStencilDesc.DepthEnable = TRUE;
		depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS;
		depthStencilDesc.StencilEnable = TRUE;

		depthStencilDesc.StencilReadMask = 0xFF;
		depthStencilDesc.StencilWriteMask = 0x00;

		// 스텐실 아웃라인: 마킹된 곳(=원본 실루엣)은 통과 못 함 -> 바깥 테두리만 남는다
		depthStencilDesc.FrontFace.StencilFunc = D3D11_COMPARISON_NOT_EQUAL;
		depthStencilDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		depthStencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
		depthStencilDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		depthStencilDesc.BackFace = depthStencilDesc.FrontFace;

		mDeviceRef->CreateDepthStencilState(&depthStencilDesc,
			&mDepthStencilState[DSS_StencilOutline]);
	}
}

void FGpuResourceManager::createBlendStates()
{
	// Default Blend State (No Blending)
	{
		D3D11_BLEND_DESC blendDesc{};
		auto& rt = blendDesc.RenderTarget[0];

		rt.BlendEnable = FALSE;
		rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		mDeviceRef->CreateBlendState(&blendDesc,
			&mBlendState[BST_Default]);
	}

	// Standard Alpha Blending
	{
		D3D11_BLEND_DESC blendDesc{};
		auto& rt = blendDesc.RenderTarget[0];

		rt.BlendEnable = TRUE;

		// RGB = Src.rgb * src.a + Dest.rgb * (1 - src.a)
		rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
		rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		rt.BlendOp = D3D11_BLEND_OP_ADD;

		// Alpha = Src.a + Dest.a * (1 - src.a)
		rt.SrcBlendAlpha = D3D11_BLEND_ONE;
		rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
		rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		mDeviceRef->CreateBlendState(&blendDesc,
			&mBlendState[BST_AlphaBlend]);
	}

	// Additive Blending
	{
		D3D11_BLEND_DESC blendDesc{};
		auto& rt = blendDesc.RenderTarget[0];
		rt.BlendEnable = TRUE;
		// RGB = Src.rgb * src.a + Dest.rgb * 1
		rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
		rt.DestBlend = D3D11_BLEND_ONE;
		rt.BlendOp = D3D11_BLEND_OP_ADD;

		// Alpha = Src.a + Dest.a * (1 - src.a)
		rt.SrcBlendAlpha = D3D11_BLEND_ONE;
		rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
		rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		mDeviceRef->CreateBlendState(&blendDesc,
			&mBlendState[BST_Additive]);
	}

	// No Color Write (Stencil Marking)
	{
		D3D11_BLEND_DESC blendDesc{};
		auto& rt = blendDesc.RenderTarget[0];
		rt.BlendEnable = FALSE;
		rt.RenderTargetWriteMask = 0;
		mDeviceRef->CreateBlendState(&blendDesc,
			&mBlendState[BST_NoColorWrite]);
	}
}

void FGpuResourceManager::createShadersAndInputLayout()
{
	/* Compile and create shaders */
	// Simple shader
	auto simpleCSO = createVertexShaderFromFile(L"Shaders/ShaderW0.hlsl", "mainVS", "vs_5_0", VST_Simple);
	createPixelShaderFromFile(L"Shaders/ShaderW0.hlsl", "mainPS", "ps_5_0", PST_Simple);

	createVertexShaderFromFile(L"Shaders/ShaderLine.hlsl", "mainVS", "vs_5_0", VST_Line);
	createPixelShaderFromFile(L"Shaders/ShaderLine.hlsl", "mainPS", "ps_5_0", PST_Line);

	// Texture shader
	auto textureCSO = createVertexShaderFromFile(L"Shaders/ShaderTexture.hlsl", "mainVS", "vs_5_0", VST_Texture);
	createPixelShaderFromFile(L"Shaders/ShaderTexture.hlsl", "mainPS", "ps_5_0", PST_Texture);

	createVertexShaderFromFile(L"Shaders/ShaderTexture.hlsl", "mainVS", "vs_5_0", VST_Billboard);
	createPixelShaderFromFile(L"Shaders/ShaderTexture.hlsl", "billboardPS", "ps_5_0", PST_Billboard);

	// Instanced shader
	auto instancedCSO = createVertexShaderFromFile(L"Shaders/ShaderW0.hlsl", "mainVSInstanced", "vs_5_0", VST_Instanced);

	// Font shader
	createVertexShaderFromFile(L"Shaders/ShaderFont.hlsl", "mainVS", "vs_5_0", VST_Font);
	createPixelShaderFromFile(L"Shaders/ShaderFont.hlsl", "mainPS", "ps_5_0", PST_Font);
	createPixelShaderFromFile(L"Shaders/ShaderFontMSDF.hlsl", "mainPS", "ps_5_0", PST_UnicodeFont);

	// Particle shader
	createVertexShaderFromFile(L"Shaders/ShaderParticle.hlsl", "mainVS", "vs_5_0", VST_Particle);
	createPixelShaderFromFile(L"Shaders/ShaderParticle.hlsl", "mainPS", "ps_5_0", PST_Particle);

	auto staticMeshCSO = createVertexShaderFromFile(L"Shaders/StaticMeshShader.hlsl", "mainVS", "vs_5_0", VST_StaticMesh);
	createPixelShaderFromFile(L"Shaders/StaticMeshShader.hlsl", "mainPS", "ps_5_0", PST_StaticMesh);

	// Loading screen shader
	createVertexShaderFromFile(L"Shaders/ShaderLoadingScreen.hlsl", "mainVS", "vs_5_0", VST_LoadingScreen);
	createPixelShaderFromFile(L"Shaders/ShaderLoadingScreen.hlsl", "mainPS", "ps_5_0", PST_LoadingScreen);

	// Hightlight shader
	auto maskCSO = createVertexShaderFromFile(L"Shaders/ShaderHighlight.hlsl", "mainVS", "vs_5_0", VST_HighlightMask);
	createPixelShaderFromFile(L"Shaders/ShaderHighlight.hlsl", "mainPS", "ps_5_0", PST_HighlightMask);
	createVertexShaderFromFile(L"Shaders/ShaderHighlight.hlsl", "outlineVS", "vs_5_0", VST_HighlightOutline);
	createPixelShaderFromFile(L"Shaders/ShaderHighlight.hlsl", "outlinePS", "ps_5_0", PST_HighlightOutline);

	/* Create input layouts */
	const D3D11_INPUT_ELEMENT_DESC position[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	const D3D11_INPUT_ELEMENT_DESC positionColor[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	const D3D11_INPUT_ELEMENT_DESC positionTexture[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,	0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }// float u, v;    // 12바이트 위치부터 시작
	};

	const D3D11_INPUT_ELEMENT_DESC positionColorMatrixTint[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,  0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,  0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },

		// 슬롯 1: 인스턴스의 World 행렬(행렬을 한꺼번에 넣는 건 불가능, 한줄 씩 넣는다)
	   { "INSTANCE_WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	   { "INSTANCE_WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 }, // 16: 내부 오프셋
	   { "INSTANCE_WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1 }, // 32: 내부 오프셋
	   { "INSTANCE_WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1 }, // 48: 내부 오프셋

	   //// 슬롯 1: 인스턴스의 Tint
	   { "INSTANCE_TINT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 64, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	};

	const D3D11_INPUT_ELEMENT_DESC positionNormalColorTexture[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};

	mDeviceRef->CreateInputLayout(position, ARRAYSIZE(position),
		maskCSO->GetBufferPointer(), maskCSO->GetBufferSize(),
		&mInputLayout[ILT_Position]);

	mDeviceRef->CreateInputLayout(positionColor, ARRAYSIZE(positionColor),
		simpleCSO->GetBufferPointer(), simpleCSO->GetBufferSize(),
		&mInputLayout[ILT_PositionColor]);

	mDeviceRef->CreateInputLayout(positionTexture, ARRAYSIZE(positionTexture),
		textureCSO->GetBufferPointer(), textureCSO->GetBufferSize(),
		&mInputLayout[ILT_PositionTexture]);

	mDeviceRef->CreateInputLayout(positionColorMatrixTint, ARRAYSIZE(positionColorMatrixTint),
		instancedCSO->GetBufferPointer(), instancedCSO->GetBufferSize(),
		&mInputLayout[ILT_PositionColorMatrixTint]);

	mDeviceRef->CreateInputLayout(positionNormalColorTexture, ARRAYSIZE(positionNormalColorTexture),
		staticMeshCSO->GetBufferPointer(), staticMeshCSO->GetBufferSize(),
		&mInputLayout[ILT_PositionNormalColorTexture]);
}

ComPtr<ID3DBlob> FGpuResourceManager::createVertexShaderFromFile(
	const wchar_t* filename, const char* entryPoint, const char* shaderModel,
	EVertexShaderType vertexShaderType)
{
	ComPtr<ID3DBlob> errorBlob;
	ComPtr<ID3DBlob> shaderBlob;

	HRESULT hr = D3DCompileFromFile(filename, nullptr, nullptr, entryPoint, shaderModel, 0, 0, &shaderBlob, &errorBlob);
	if (FAILED(hr))
	{
		throw std::runtime_error("Failed to compile shader");
	}

	hr = mDeviceRef->CreateVertexShader(
		shaderBlob->GetBufferPointer(),
		shaderBlob->GetBufferSize(), nullptr,
		&mVertexShader[vertexShaderType]);

	if (FAILED(hr))
	{
		throw std::runtime_error("Failed to create vertex shader");
	}
	return shaderBlob;
}

void FGpuResourceManager::createPixelShaderFromFile(
	const wchar_t* filename, const char* entryPoint, const char* shaderModel,
	EPixelShaderType pixelShaderType)
{
	ComPtr<ID3DBlob> errorBlob;
	ComPtr<ID3DBlob> shaderBlob;

	HRESULT hr = D3DCompileFromFile(filename, nullptr, nullptr, entryPoint, shaderModel, 0, 0, &shaderBlob, &errorBlob);
	if (FAILED(hr))
	{
		throw std::runtime_error("Failed to compile shader");
	}

	hr = mDeviceRef->CreatePixelShader(
		shaderBlob->GetBufferPointer(),
		shaderBlob->GetBufferSize(), nullptr,
		&mPixelShader[pixelShaderType]);

	if (FAILED(hr))
	{
		throw std::runtime_error("Failed to create pixel shader");
	}
}

void FGpuResourceManager::createLineVertexBuffer(uint32 maxVertices)
{
	// Create a dynamic vertex buffer for lines
	/* Vertex Buffer */
	{
		D3D11_BUFFER_DESC bufferDesc{};
		bufferDesc.ByteWidth = maxVertices * sizeof(FVertexSimple);
		bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		mDeviceRef->CreateBuffer(&bufferDesc, nullptr, &mLineBuffer.Buffer);
	}
}
void FGpuResourceManager::createLineIndexBuffer(uint32 maxIndices)
{
	/* Index Buffer */
	{
		D3D11_BUFFER_DESC bufferDesc{};
		bufferDesc.ByteWidth = maxIndices * sizeof(uint32);
		bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		mDeviceRef->CreateBuffer(&bufferDesc, nullptr, &mLineBuffer.IndexBuffer);
	}
}

TArray<uint32> getFontIndices(uint32 numQuad)
{
	assert(numQuad >= 0 && "Number of quads must be non-negative");

	TArray<uint32> indices;
	indices.Reserve(numQuad * 6); // Each quad has 6 indices (2 triangles)
	for (uint32 i = 0; i < numQuad; ++i)
	{
		uint32 baseIndex = i * 4; // Each quad has 4 vertices

		indices.Add(baseIndex + Quad_indices[0]);
		indices.Add(baseIndex + Quad_indices[1]);
		indices.Add(baseIndex + Quad_indices[2]);
		indices.Add(baseIndex + Quad_indices[3]);
		indices.Add(baseIndex + Quad_indices[4]);
		indices.Add(baseIndex + Quad_indices[5]);
	}
	return indices;
}

void FGpuResourceManager::createFontBuffer(uint32 numQuad)
{
	/* Vertex Buffer */
	{
		D3D11_BUFFER_DESC bufferDesc{};
		bufferDesc.ByteWidth = numQuad * 4 * sizeof(FVertexTextured);
		bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		mDeviceRef->CreateBuffer(&bufferDesc, nullptr, &mFontBuffer.Buffer);
	}

	/* Index Buffer */
	{
		TArray<uint32> fontIndices = getFontIndices(numQuad); // Each quad has 6 indices
		D3D11_SUBRESOURCE_DATA indexData{};
		indexData.pSysMem = fontIndices.GetData();

		D3D11_BUFFER_DESC bufferDesc{};
		bufferDesc.ByteWidth = fontIndices.Num() * sizeof(uint32);
		bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
		bufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

		mDeviceRef->CreateBuffer(&bufferDesc, &indexData, &mFontBuffer.IndexBuffer);
	}
}

void FGpuResourceManager::createUnicodeFontBuffer(uint32 numQuad)
{
	/* Vertex Buffer */
	{
		D3D11_BUFFER_DESC bufferDesc{};
		bufferDesc.ByteWidth = numQuad * 4 * sizeof(FVertexTextured);
		bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		mDeviceRef->CreateBuffer(&bufferDesc, nullptr, &mUnicodeFontBuffer.Buffer);
	}

	/* Index Buffer */
	{
		TArray<uint32> fontIndices = getFontIndices(numQuad); // Each quad has 6 indices
		D3D11_SUBRESOURCE_DATA indexData{};
		indexData.pSysMem = fontIndices.GetData();

		D3D11_BUFFER_DESC bufferDesc{};
		bufferDesc.ByteWidth = numQuad * 6 * sizeof(uint32);
		bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		mDeviceRef->CreateBuffer(&bufferDesc, &indexData, &mUnicodeFontBuffer.IndexBuffer);
	}
}

void FGpuResourceManager::createInstanceBuffer(uint32 maxInstances)
{
	/* Instance Buffer */
	{
		D3D11_BUFFER_DESC bufferDesc{};
		bufferDesc.ByteWidth = maxInstances * sizeof(FInstanceData);
		bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		mDeviceRef->CreateBuffer(&bufferDesc, nullptr, &mInstanceBuffer.Buffer);
	}
}

void FGpuResourceManager::createConstantBuffers(float distanceRange)
{
	/* Simple */
	{
		D3D11_BUFFER_DESC bufferDesc{};
		bufferDesc.ByteWidth = (sizeof(FConstants) + 0xF) & 0xFFFFFFF0;
		bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

		mDeviceRef->CreateBuffer(&bufferDesc, nullptr, &mConstantBuffer[CBT_Simple]);
	}

	/* Texture */
	{
		D3D11_BUFFER_DESC bufferDesc{};
		bufferDesc.ByteWidth = (sizeof(FTextureConstants) + 0xF) & 0xFFFFFFF0;
		bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

		mDeviceRef->CreateBuffer(&bufferDesc, nullptr, &mConstantBuffer[CBT_Texture]);
	}

	/* Billboard Texture */
	{
		D3D11_BUFFER_DESC bufferDesc{};
		bufferDesc.ByteWidth = (sizeof(FBillboardConstants) + 0xF) & 0xFFFFFFF0;
		bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

		mDeviceRef->CreateBuffer(&bufferDesc, nullptr, &mConstantBuffer[CBT_BillboardTexture]);
	}

	/* Font */
	{
		D3D11_BUFFER_DESC bufferDesc{};
		bufferDesc.ByteWidth = (sizeof(FFontConstants) + 0xF) & 0xFFFFFFF0;
		bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

		mDeviceRef->CreateBuffer(&bufferDesc, nullptr, &mConstantBuffer[CBT_Font]);
	}

	/* Unicode Font */
	{
		FUnicodeFontConstants unicodeFontConstants{};
		unicodeFontConstants.DistanceRange = distanceRange;

		D3D11_SUBRESOURCE_DATA subresourceData{};
		subresourceData.pSysMem = &unicodeFontConstants;

		D3D11_BUFFER_DESC bufferDesc{};
		bufferDesc.ByteWidth = (sizeof(FUnicodeFontConstants) + 0xF) & 0xFFFFFFF0;
		bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

		mDeviceRef->CreateBuffer(&bufferDesc, &subresourceData, &mConstantBuffer[CBT_UnicodeFont]);
	}

	/* Particle */
	{
		D3D11_BUFFER_DESC bufferDesc{};
		bufferDesc.ByteWidth = (sizeof(FParticleConstants) + 0xF) & 0xFFFFFFF0;
		bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

		mDeviceRef->CreateBuffer(&bufferDesc, nullptr, &mConstantBuffer[CBT_Particle]);
	}

	/* Highlight */
	// Mask
	{
		D3D11_BUFFER_DESC bufferDesc{
			.ByteWidth = (sizeof(FMaskConstants) + 0xF) & 0xFFFFFFF0,
			.Usage = D3D11_USAGE_DYNAMIC,
			.BindFlags = D3D11_BIND_CONSTANT_BUFFER,
			.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
		};
		mDeviceRef->CreateBuffer(&bufferDesc, nullptr, &mConstantBuffer[CBT_HighlightMask]);
	}
	// Outline
	{
		D3D11_BUFFER_DESC bufferDesc{
			.ByteWidth = (sizeof(FOutlineConstants) + 0xF) & 0xFFFFFFF0,
			.Usage = D3D11_USAGE_DYNAMIC,
			.BindFlags = D3D11_BIND_CONSTANT_BUFFER,
			.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
		};
		mDeviceRef->CreateBuffer(&bufferDesc, nullptr, &mConstantBuffer[CBT_HighlightOutline]);
	}
}

bool FGpuResourceManager::EnsureFontBuffer(uint32 fontCount)
{
	if (fontCount == 0)
	{
		return true;
	}

	if (fontCount > mMaxFontCount)
	{
		// Recreate the font buffer with the new size
		while (mMaxFontCount < fontCount)
		{
			mMaxFontCount *= 2; // Double the size until it can accommodate the new font count
		}

		createFontBuffer(mMaxFontCount);

		UE_LOG_F(Log, Render,
			"Font buffer resized to accommodate {} fonts. New max font count: {}",
			fontCount, mMaxFontCount);

		return true;
	}

	return true;
}

bool FGpuResourceManager::EnsureUnicodeFontBuffer(uint32 fontCount)
{
	if (fontCount == 0)
	{
		return true;
	}

	if (fontCount > mMaxUnicodeFontCount)
	{
		// Recreate the unicode font buffer with the new size
		while (mMaxUnicodeFontCount < fontCount)
		{
			mMaxUnicodeFontCount *= 2; // Double the size until it can accommodate the new font count
		}

		createUnicodeFontBuffer(mMaxUnicodeFontCount);

		UE_LOG_F(Log, Render,
			"Unicode font buffer resized to accommodate {} fonts. New max unicode font count: {}",
			fontCount, mMaxUnicodeFontCount);

		return true;
	}

	return true;
}

bool FGpuResourceManager::EnsureLineBuffer(uint32 vertexCount, uint32 indexCount)
{
	if (vertexCount == 0 && indexCount == 0)
	{
		return true;
	}

	if (vertexCount > mMaxLineVertexCapacity)
	{
		while (mMaxLineVertexCapacity < vertexCount)
		{
			mMaxLineVertexCapacity *= 2; // Double the size until it can accommodate the new vertex count
		}

		createLineVertexBuffer(mMaxLineVertexCapacity);
		UE_LOG_F(Log, Render,
			"Line vertex buffer resized to accommodate {} vertices. New max line vertex capacity: {}",
			vertexCount, mMaxLineVertexCapacity);
	}

	if (indexCount > mMaxLineIndexCapacity)
	{
		while (mMaxLineIndexCapacity < indexCount)
		{
			mMaxLineIndexCapacity *= 2; // Double the size until it can accommodate the new index count
		}

		createLineIndexBuffer(mMaxLineIndexCapacity);
		UE_LOG_F(Log, Render,
			"Line index buffer resized to accommodate {} indices. New max line index capacity: {}",
			indexCount, mMaxLineIndexCapacity);
	}

	return true;
}

bool FGpuResourceManager::EnsureInstanceCapacity(uint32 instanceCount)
{
	if (instanceCount == 0)
	{
		return true;
	}

	constexpr uint32 maxCount = 100000; // arbitary limit to prevent excessive memory allocation
	constexpr uint32 instanceSize = sizeof(FInstanceData);


	if (instanceCount > mMaxInstanceCount)
	{
		// Recreate the instance buffer with the new size
		while (mMaxInstanceCount < instanceCount)
		{
			mMaxInstanceCount *= 2; // Double the size until it can accommodate the new instance count
		}

		createInstanceBuffer(mMaxInstanceCount);

		UE_LOG_F(Log, Render,
			"Instance buffer resized to accommodate {} instances. New max instance count: {}",
			instanceCount, mMaxInstanceCount);

		return true;
	}
	return true;
}
