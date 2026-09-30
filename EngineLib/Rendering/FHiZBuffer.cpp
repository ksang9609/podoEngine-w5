#include "FHiZBuffer.h"
#include <d3dcompiler.h>

void FHiZBuffer::Initialize(ID3D11Device* device)
{
	if (!mbIsInitialized)
	{
		createResources(device);
		createShader(device);
		createCullShader(device);

		mbIsInitialized = true;
	}	
}

void FHiZBuffer::Release()
{
	for (UINT i = 0; i < MipLevels; ++i)
	{
		mHzbMipUAV[i].Reset();
		mHzbMipSRV[i].Reset();
	}
	mHzbTexture.Reset();
	mHzbFullSRV.Reset();
	mHzBuildCS.Reset();
	mHzCullCS.Reset();
	mConstantBuffer.Reset();
	mCullConstantBuffer.Reset();
	mPointClampSampler.Reset();
	mAABBBuffer.Reset();
	mAABBSRV.Reset();
	mVisibilityBuffer.Reset();
	mVisibilityUAV.Reset();
	for (uint32 i = 0; i < STAGING_BUFFER_COUNT; ++i)
	{
		mStagingBuffers[i].Reset();
	}
	mVisibilityCache.Reset(0);
	mHasValidStagingData = false;
	mbIsInitialized = false;
}

void FHiZBuffer::BuildHiZ(ID3D11DeviceContext* context, ID3D11ShaderResourceView* mainDepthSRV, float viewportX, float viewportY,
	float viewportWidth, float viewportHeight, float totalBufferWidth, float totalBufferHeight)
{
	if (!mainDepthSRV || !mHzBuildCS)
	{
		return;
	}

	// Binding shader and sampler
	context->CSSetShader(mHzBuildCS.Get(), nullptr, 0);
	context->CSSetSamplers(0, 1, mPointClampSampler.GetAddressOf());

	uint32 currentDstWidth = HZBWidth;
	uint32 currentDstHeight = HZBHeight;	

	uint32 prevWidth = currentDstWidth;
	uint32 prevHeight = currentDstHeight;

	for (uint32 mip = 0; mip < MipLevels; ++mip)
	{
		// Update constant buffer
		FHiZBufferConstants cbData = {};
		if (mip == 0)
		{
			cbData.inputOffsetX = (uint32)viewportX;
			cbData.inputOffsetY = (uint32)viewportY;
			cbData.inputWidth = (uint32)viewportWidth;
			cbData.inputHeight = (uint32)viewportHeight;
		}
		else
		{
			cbData.inputOffsetX = 0;
			cbData.inputOffsetY = 0;
			cbData.inputWidth = prevWidth;
			cbData.inputHeight = prevHeight;
		}
		cbData.outputWidth = currentDstWidth;
		cbData.outputHeight = currentDstHeight;

		context->UpdateSubresource(mConstantBuffer.Get(), 0, nullptr, &cbData, 0, 0);
		context->CSSetConstantBuffers(0, 1, mConstantBuffer.GetAddressOf());

		// Binding Write SRV and Read UAV
		ID3D11ShaderResourceView* inputSRV = (mip == 0) ? mainDepthSRV : mHzbMipSRV[mip - 1].Get();
		context->CSSetShaderResources(0, 1, &inputSRV);
		context->CSSetUnorderedAccessViews(0, 1, mHzbMipUAV[mip].GetAddressOf(), nullptr);

		// Dispatch (16 x 16 threads) (16, 16, 1)
		uint32 threadGroupsX = (currentDstWidth + 15) / 16;
		uint32 threadGroupsY = (currentDstHeight + 15) / 16;
		context->Dispatch(threadGroupsX, threadGroupsY, 1);

		// Unbinding slot
		ID3D11ShaderResourceView* nullSRV = nullptr;
		ID3D11UnorderedAccessView* nullUAV = nullptr;
		context->CSSetShaderResources(0, 1, &nullSRV);
		context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

		// Update next Width, Height

		prevWidth = currentDstWidth;
		prevHeight = currentDstHeight;
		currentDstWidth = (currentDstWidth > 1) ? (currentDstWidth / 2) : 1;
		currentDstHeight = (currentDstHeight > 1) ? (currentDstHeight / 2) : 1;
	}

	// Release Shader
	context->CSSetShader(nullptr, nullptr, 0);
}

void FHiZBuffer::createResources(ID3D11Device* device)
{
	// Create R32_Float texture
	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = HZBWidth;
	texDesc.Height = HZBHeight;
	texDesc.MipLevels = MipLevels;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R32_FLOAT;
	texDesc.SampleDesc.Count = 1;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	HRESULT hr = device->CreateTexture2D(&texDesc, nullptr, &mHzbTexture);

	// Create full Mipmap SRV
	D3D11_SHADER_RESOURCE_VIEW_DESC fullSRVDesc = {};
	fullSRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
	fullSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	fullSRVDesc.Texture2D.MostDetailedMip = 0;
	fullSRVDesc.Texture2D.MipLevels = MipLevels;
	hr = device->CreateShaderResourceView(mHzbTexture.Get(), &fullSRVDesc, &mHzbFullSRV);

	// Create UAV, SRV for Mipmap
	for (UINT mip = 0; mip < MipLevels; ++mip)
	{
		// UAV
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDecs = {};
		uavDecs.Format = DXGI_FORMAT_R32_FLOAT;
		uavDecs.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uavDecs.Texture2D.MipSlice = mip;
		hr = device->CreateUnorderedAccessView(mHzbTexture.Get(), &uavDecs, &mHzbMipUAV[mip]);

		// SRV
		D3D11_SHADER_RESOURCE_VIEW_DESC mipSRVDecs = {};
		mipSRVDecs.Format = DXGI_FORMAT_R32_FLOAT;
		mipSRVDecs.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		mipSRVDecs.Texture2D.MostDetailedMip = mip;
		mipSRVDecs.Texture2D.MipLevels = 1;
		hr = device->CreateShaderResourceView(mHzbTexture.Get(), &mipSRVDecs, &mHzbMipSRV[mip]);
	}

	// Create constatns buffer
	D3D11_BUFFER_DESC cbDesc = {};
	cbDesc.ByteWidth = sizeof(FHiZBufferConstants);
	cbDesc.Usage = D3D11_USAGE_DEFAULT;
	cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	hr = device->CreateBuffer(&cbDesc, nullptr, &mConstantBuffer);

	// Crate point clamp sampler
	D3D11_SAMPLER_DESC sampDesc = {};
	sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	hr = device->CreateSamplerState(&sampDesc, &mPointClampSampler);
}

void FHiZBuffer::createShader(ID3D11Device* device)
{
	ComPtr<ID3DBlob> csBlob;
	ComPtr<ID3DBlob> errorBlob;

	HRESULT hr = D3DCompileFromFile(
		L"Shaders/HzBuildCS.hlsl",
		nullptr, nullptr,
		"mainCS", "cs_5_0",
		0, 0,
		&csBlob, &errorBlob
	);

	hr = device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &mHzBuildCS);
}

void FHiZBuffer::ExecuteOcclusionCull(ID3D11DeviceContext* context, ID3D11Device* device, const TArray<const FRenderInfo*>& renderInfos, const FMatrix& viewProjectionMatrix, float screenWidth, float screenHeight, float nearPlane)
{
	uint32 numObjects = (uint32)renderInfos.Num();
	if (numObjects == 0 || !mHzCullCS || !mHzbFullSRV)
	{
		return;
	}

	uint32 maxObjectId = (uint32)UObject::GetGObjectArray().Size();

	ensureBufferCapacity(device, numObjects, maxObjectId + 1);

	// Copy WorldAABB from CPU to GPU
	D3D11_MAPPED_SUBRESOURCE mapped = {};
	if (SUCCEEDED(context->Map(mAABBBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
	{
		FGpuAABB* dstAABB = static_cast<FGpuAABB*>(mapped.pData);
		const FRenderInfo* const* rawInfos = renderInfos.GetData();

		for (uint32 i = 0; i < numObjects; ++i)
		{
			const FRenderInfo* info = rawInfos[i];
			dstAABB[i].Min = info->WorldBounds.min;
			dstAABB[i].InternalID = info->ObejctID.InternalIndex;
			dstAABB[i].Max = info->WorldBounds.max;
			dstAABB[i].Pad2 = 0.0f;
		}
		context->Unmap(mAABBBuffer.Get(), 0);
	}

	// Update constant buffer
	FCullConstants cbData = {};
	cbData.ViewProjection = viewProjectionMatrix;
	cbData.ScreenWidth = (float)HZBWidth;
	cbData.ScreenHeight = (float)HZBHeight;
	cbData.NearPlane = nearPlane;
	cbData.NumObject = numObjects;
	context->UpdateSubresource(mCullConstantBuffer.Get(), 0, nullptr, &cbData, 0, 0);

	// Binding pipeline
	context->CSSetShader(mHzCullCS.Get(), nullptr, 0);
	context->CSSetConstantBuffers(0, 1, mCullConstantBuffer.GetAddressOf());
	context->CSSetSamplers(0, 1, mPointClampSampler.GetAddressOf());

	ID3D11ShaderResourceView* srvs[2] = { mHzbFullSRV.Get(), mAABBSRV.Get() };
	context->CSSetShaderResources(0, 2, srvs);
	context->CSSetUnorderedAccessViews(0, 1, mVisibilityUAV.GetAddressOf(), nullptr);

	// Dispatch (64 threads)
	uint32 threadGroups = (numObjects + 63) / 64;
	context->Dispatch(threadGroups, 1, 1);

	// Unbinding
	ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
	ID3D11UnorderedAccessView* nullUAV = nullptr;
	context->CSSetShaderResources(0, 2, nullSRVs);
	context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

	// Copy result to staging buffer (Triple buffering)
	context->CopyResource(mStagingBuffers[mCurrentStagingIndex].Get(), mVisibilityBuffer.Get());

	mCurrentStagingIndex = (mCurrentStagingIndex + 1) % STAGING_BUFFER_COUNT;
	mStagedFrameCount++;
	if (mStagedFrameCount >= 2)
	{
		mHasValidStagingData = true;
	}
}

const uint32* FHiZBuffer::ReadbackVisibility(ID3D11DeviceContext* contex, uint32& outCount)
{
	if (!mHasValidStagingData)
	{
		outCount = 0;
		return nullptr;
	}

	// Read buffer from 2 frames ago (avoids GPU stall)
	uint32 readIndex = (mCurrentStagingIndex + 1) % STAGING_BUFFER_COUNT;

	D3D11_MAPPED_SUBRESOURCE mapped = {};
	HRESULT hr = contex->Map(mStagingBuffers[readIndex].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
	if (SUCCEEDED(hr))
	{
		outCount = mAllocatedVisibilityCount;
		if (mVisibilityCache.Num() < static_cast<int32>(mAllocatedVisibilityCount))
		{
			mVisibilityCache.SetNum(mAllocatedVisibilityCount);
		}
		memcpy(mVisibilityCache.GetData(), mapped.pData, mAllocatedVisibilityCount * sizeof(uint32));
		contex->Unmap(mStagingBuffers[readIndex].Get(), 0);
		mIsMapped = false;
		return mVisibilityCache.GetData();
	}
	else if (hr == DXGI_ERROR_WAS_STILL_DRAWING)
	{
		// GPU is still writing: return previous cached frame immediately (0ms stall)
		if (mVisibilityCache.Num() > 0)
		{
			outCount = static_cast<uint32>(mVisibilityCache.Num());
			return mVisibilityCache.GetData();
		}
	}

	outCount = 0;
	return nullptr;
}

void FHiZBuffer::UnmapVisibility(ID3D11DeviceContext* context)
{
	if (mIsMapped)
	{
		context->Unmap(mStagingBuffers[mLastMappedIndex].Get(), 0);
		mIsMapped = false;
	}
}

void FHiZBuffer::createCullShader(ID3D11Device* device)
{
	ComPtr<ID3DBlob> csBlob;
	ComPtr<ID3DBlob> errorBlob;

	HRESULT hr = D3DCompileFromFile(
		L"Shaders/HzCullCS.hlsl",
		nullptr, nullptr,
		"mainCS", "cs_5_0",
		0, 0,
		&csBlob, &errorBlob
	);

	hr = device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &mHzCullCS);

	// Create cull constants buffer
	D3D11_BUFFER_DESC cbDesc = {};
	cbDesc.ByteWidth = sizeof(FCullConstants);
	cbDesc.Usage = D3D11_USAGE_DEFAULT;
	cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	device->CreateBuffer(&cbDesc, nullptr, &mCullConstantBuffer);
}

void FHiZBuffer::ensureBufferCapacity(ID3D11Device* device, uint32 requiredAABBCount, uint32 requiredVisibilityCount)
{
	// not need to realloc
	if ((requiredAABBCount <= mAllocatedAABBCount && mAABBBuffer) &&
		(requiredVisibilityCount <= mAllocatedVisibilityCount && mVisibilityBuffer))
	{
		return;
	}

	// Increase capacity
	uint32 newCapacity = 2 << 11;
	while (newCapacity < requiredAABBCount)
	{
		newCapacity *= 2;
	}
	
	mAllocatedAABBCount = newCapacity;

	// Reset buffers
	mAABBBuffer.Reset();
	mAABBSRV.Reset();
	mVisibilityBuffer.Reset();
	mVisibilityUAV.Reset();
	for (uint32 i = 0; i < STAGING_BUFFER_COUNT; ++i)
	{
		mStagingBuffers[i].Reset();
	}

	// Create AABB StructuredBuffer
	D3D11_BUFFER_DESC aabbDesc = {};
	aabbDesc.ByteWidth = sizeof(FGpuAABB) * mAllocatedAABBCount;
	aabbDesc.Usage = D3D11_USAGE_DYNAMIC;
	aabbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	aabbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	aabbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	aabbDesc.StructureByteStride = sizeof(FGpuAABB);
	device->CreateBuffer(&aabbDesc, nullptr, &mAABBBuffer);

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.NumElements = mAllocatedAABBCount;
	device->CreateShaderResourceView(mAABBBuffer.Get(), &srvDesc, &mAABBSRV);

	// Increase Capacity
	uint32 newVisibilityCap = 2 << 11;
	while (newVisibilityCap < requiredVisibilityCount)
	{
		newVisibilityCap *= 2;
	}

	mAllocatedVisibilityCount = newVisibilityCap;

	// Create visibility buffer
	D3D11_BUFFER_DESC visDesc = {};
	visDesc.ByteWidth = sizeof(uint32) * mAllocatedVisibilityCount;
	visDesc.Usage = D3D11_USAGE_DEFAULT;
	visDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	visDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	visDesc.StructureByteStride = sizeof(uint32);
	device->CreateBuffer(&visDesc, nullptr, &mVisibilityBuffer);

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.NumElements = mAllocatedVisibilityCount;
	device->CreateUnorderedAccessView(mVisibilityBuffer.Get(), &uavDesc, &mVisibilityUAV);

	// Create Staging buffer (Triple Buffering)
	D3D11_BUFFER_DESC stagingDesc = {};
	stagingDesc.ByteWidth = sizeof(uint32) * mAllocatedVisibilityCount;
	stagingDesc.Usage = D3D11_USAGE_STAGING;
	stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	for (uint32 i = 0; i < STAGING_BUFFER_COUNT; ++i)
	{
		device->CreateBuffer(&stagingDesc, nullptr, &mStagingBuffers[i]);
	}

	mCurrentStagingIndex = 0;
	mStagedFrameCount = 0;
	mHasValidStagingData = false;
	mVisibilityCache.Reset(0);
}
