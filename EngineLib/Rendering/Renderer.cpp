#include "Renderer.h"

#include <fstream>
#include <filesystem>
#include <vector>
#include <directxtk/DDSTextureLoader.h>
#include <directxtk/WICTextureLoader.h>
#include <cmath>
#include <string>
#include <wrl/client.h>

#include "Core/BuiltinAssets.h"
#include "Core/Math/MathUtility.h"
#include "Editor/Console.h"
#include "Rendering/Primitives/TexturedPrimitives.h"
#include "Rendering/TextMesh.h"
#include "Rendering/GpuResourceManager.h"
#include "SceneView.h"

#pragma comment(lib, "DirectXTK.lib")
#pragma comment(lib, "dxguid.lib")

using Microsoft::WRL::ComPtr;

void URenderer::Initialize(HWND hWindow, FGpuResourceManager& gpuResourceManagerRef)
{
	mGpuResourceManagerRef = &gpuResourceManagerRef;
	createDeviceAndSwapChain(hWindow);
	createFrameBuffer();
	createOffsetConstantBuffers();

	// 실제 Back Buffer 크기를 가져온다.
	// SwapChain 생성 시 Width/Height를 0으로 전달했기 때문에
	// 처음 전달한 SwapChainDesc 값에 의존하면 안 된다.
	D3D11_TEXTURE2D_DESC backBufferDesc = {};

	if (mFrameBuffer != nullptr)
	{
		mFrameBuffer->GetDesc(&backBufferDesc);

		mViewportInfo = {
			0.0f,
			0.0f,
			static_cast<float>(backBufferDesc.Width),
			static_cast<float>(backBufferDesc.Height),
			0.0f,
			1.0f
		};

		createDepthStencilBuffer(
			backBufferDesc.Width,
			backBufferDesc.Height);

		createSelectionMaskResources(
			backBufferDesc.Width,
			backBufferDesc.Height);
	}

	HRESULT hr = mGpuTimer.Initialize(mDevice.Get());
	if (FAILED(hr))
	{
		UE_LOG(Error, Render, "Failed to initialize GPU timer. HRESULT: 0x%08X", hr);
	}
}

void URenderer::createDeviceAndSwapChain(HWND hWindow)
{
	D3D_FEATURE_LEVEL featurelevels[] = { D3D_FEATURE_LEVEL_11_0 };

	DXGI_SWAP_CHAIN_DESC swapchaindesc = {};
	swapchaindesc.BufferDesc.Width = 0;
	swapchaindesc.BufferDesc.Height = 0;
	swapchaindesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	swapchaindesc.SampleDesc.Count = 1;
	swapchaindesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapchaindesc.BufferCount = 2;
	swapchaindesc.OutputWindow = hWindow;
	swapchaindesc.Windowed = TRUE;
	swapchaindesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	UINT createDeviceFlags = 0;

#if defined(_DEBUG) && defined(ENABLE_D3D11_DEBUG_LAYER)
	// 50,000개 드로우콜 환경에서 D3D11 디버그 레이어는 매 드로우콜마다 API 밸리데이션 락을 발생시켜
	// 극심한 FPS 폭락을 유발하므로 성능 테스트 시에는 기본 비활성화합니다.
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
		D3D11_CREATE_DEVICE_BGRA_SUPPORT | createDeviceFlags,
		featurelevels, ARRAYSIZE(featurelevels), D3D11_SDK_VERSION,
		&swapchaindesc, &mSwapChain, &mDevice, nullptr, &mDeviceContext);

	if (mDeviceContext)
	{
		mDeviceContext.As(&mDeviceContext1);
	}

	mSwapChain->GetDesc(&swapchaindesc);

	mViewportInfo = { 0.0f, 0.0f, (float)swapchaindesc.BufferDesc.Width, (float)swapchaindesc.BufferDesc.Height, 0.0f, 1.0f };
}

void URenderer::releaseDeviceAndSwapChain()
{
	if (mDeviceContext)
	{
		mDeviceContext->Flush();
	}

	mFrameCB.Reset();
	mPerObjectCB.Reset();
	mDeviceContext1.Reset();

	mSwapChain.Reset();
	mDevice.Reset();
	mDeviceContext.Reset();
}

void URenderer::createFrameBuffer()
{
	mSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&mFrameBuffer);

	D3D11_RENDER_TARGET_VIEW_DESC framebufferRTVdesc = {};
	framebufferRTVdesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
	framebufferRTVdesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

	mDevice->CreateRenderTargetView(mFrameBuffer.Get(), &framebufferRTVdesc, &mFrameBufferRTV);
}

void URenderer::createDepthStencilBuffer(UINT width, UINT height)
{
	D3D11_TEXTURE2D_DESC desc = {};

	desc.Width = width;   // 백버퍼와 크기가 정확히 같아야 함
	desc.Height = height;

	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	desc.SampleDesc.Count = 1;                    // 스왑체인의 SampleDesc와 반드시 동일
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;    // 이 플래그가 없으면 DSV 생성 실패

	mDevice->CreateTexture2D(&desc, nullptr, &mDepthStencilBuffer);

	D3D11_DEPTH_STENCIL_VIEW_DESC dsvdesc = {};
	dsvdesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvdesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;

	mDevice->CreateDepthStencilView(mDepthStencilBuffer.Get(), &dsvdesc, &mDepthStencilView);

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.MostDetailedMip = 0;

	mDevice->CreateShaderResourceView(mDepthStencilBuffer.Get(), &srvDesc, &mDepthBufferSRV);
}

void URenderer::createSelectionMaskResources(UINT width, UINT height)
{
	assert(width != 0 && height != 0);

	D3D11_TEXTURE2D_DESC desc = {
		.Width = width,
		.Height = height,
		.MipLevels = 1,
		.ArraySize = 1,
		.Format = DXGI_FORMAT_R8_UNORM,
		.SampleDesc = {
			.Count = 1,
		},
		.Usage = D3D11_USAGE_DEFAULT,

		.BindFlags = // Used for highlighting shader
			D3D11_BIND_RENDER_TARGET |
			D3D11_BIND_SHADER_RESOURCE,
	};

	mDevice->CreateTexture2D(&desc, nullptr, &mSelectionMaskTexture);

	mDevice->CreateRenderTargetView(
		mSelectionMaskTexture.Get(),
		nullptr,
		&mSelectionMaskRTV);

	mDevice->CreateShaderResourceView(
		mSelectionMaskTexture.Get(),
		nullptr,
		&mSelectionMaskSRV);
}

void URenderer::createOffsetConstantBuffers()
{
	// Slot 0: FrameConstants (ViewProjection)
	D3D11_BUFFER_DESC frameDesc = {};
	frameDesc.ByteWidth = sizeof(FFrameConstants);
	frameDesc.Usage = D3D11_USAGE_DYNAMIC;
	frameDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	frameDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	mDevice->CreateBuffer(&frameDesc, nullptr, &mFrameCB);

	// Slot 1: PerObjectConstants (64KB = 256 entries x 256 bytes)
	D3D11_BUFFER_DESC objDesc = {};
	objDesc.ByteWidth = static_cast<UINT>(sizeof(FPerObjectConstants) * 256);
	objDesc.Usage = D3D11_USAGE_DYNAMIC;
	objDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	objDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	mDevice->CreateBuffer(&objDesc, nullptr, &mPerObjectCB);
}

void URenderer::releaseFrameBuffer()
{
	mDepthBufferSRV.Reset();
	mFrameBuffer.Reset();
	mFrameBufferRTV.Reset();
}

// 인스턴스 사용하여 렌더링(텍스쳐 X)
bool URenderer::RenderSimpleInstanced(
	ID3D11Buffer* vertexBuffer,
	ID3D11Buffer* indexBuffer,
	UINT indexCount,
	const FInstanceData* instances,
	UINT instanceCount)
{
	if (instanceCount == 0)
		return true;

	assert(
		mGpuResourceManagerRef &&
		mDeviceContext &&
		vertexBuffer &&
		instances &&
		indexBuffer &&
		indexCount > 0
	);

	if (!mGpuResourceManagerRef->EnsureInstanceCapacity(instanceCount))
		return false;

	// CPU의 인스턴스 배열을 GPU 버퍼에 복사
	// Map / Unmap은 “CPU가 쓸 수 있게 잠깐 문 열어주는 것”
	ID3D11Buffer* instanceBuffer = mGpuResourceManagerRef->GetInstanceBuffer().Buffer.Get();

	D3D11_MAPPED_SUBRESOURCE mapped{};

	HRESULT hr = mDeviceContext->Map(instanceBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);

	if (FAILED(hr))
	{
		return false;
	}

	std::memcpy(mapped.pData, instances, static_cast<size_t>(instanceCount) * sizeof(FInstanceData));
	mDeviceContext->Unmap(instanceBuffer, 0);

	const UINT vertexStride = sizeof(FVertexSimple);
	const UINT instanceStride = sizeof(FInstanceData);
	UINT offset = 0;

	ID3D11Buffer* vbs[2] = { vertexBuffer, instanceBuffer }; // VertexBuffer와 InstanceBuffer 각각의 슬롯 0,1에 삽입
	UINT strides[2] = { vertexStride, instanceStride };
	UINT offsets[2] = { 0, 0 };
	mDeviceContext->IASetVertexBuffers(0, 2, vbs, strides, offsets);


	// 인덱스 버퍼는 정점 버퍼 슬롯과 별도로 연결
	mDeviceContext->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);


	// indexCount: 인스턴스 하나를 그리는 데 사용할 인덱스 개수
	mDeviceContext->DrawIndexedInstanced(indexCount, instanceCount, 0, 0, 0);

	return true;

}

void URenderer::Release()
{
	mGpuTimer.Shutdown();

	mDeviceContext->OMSetRenderTargets(0, nullptr, nullptr);

	releaseDepthStencilBuffer();
	releaseFrameBuffer();

	//ReleaseTestTexture();
	releaseDeviceAndSwapChain();
}

void URenderer::SwapBuffer()
{
	mGpuTimer.End(mDeviceContext.Get());

	mSwapChain->Present(0, 0);
}

// Prepare global rendering state for a new frame
void URenderer::BeginFrame()
{
	HRESULT hr = mGpuTimer.Poll(mDeviceContext.Get());
	if (FAILED(hr))
	{
		UE_LOG(Error, Render, "Failed to poll GPU timer. HRESULT: 0x%08X", hr);
		mGpuTimer.Shutdown();
	}

	mGpuTimer.Begin(mDeviceContext.Get());


	mDeviceContext->ClearRenderTargetView(mFrameBufferRTV.Get(), mClearColor);

	//매 프레임 깊이 버퍼를 1.0(가장 먼 값)으로 초기화
	mDeviceContext->ClearDepthStencilView(mDepthStencilView.Get(),
		D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	//세 번째 인자에 nullptr 대신 DSV를 넘긴다
	mDeviceContext->OMSetRenderTargets(1, mFrameBufferRTV.GetAddressOf(), mDepthStencilView.Get());
}
void URenderer::SetViewMode(EViewModeIndex viewMode)
{
	mViewMode = viewMode;
	mbWireFrame = viewMode == EViewModeIndex::VMI_Wireframe;
}

void URenderer::BeginView(const FViewRect& rect)
{
	if (!rect.isValid())
	{
		return;
	}

	D3D11_VIEWPORT viewport = {};
	viewport.TopLeftX = rect.X;
	viewport.TopLeftY = rect.Y;
	viewport.Width = rect.Width;
	viewport.Height = rect.Height;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	mDeviceContext->RSSetViewports(1, &viewport);

	// 애매한 사각형 경계 자르기
	D3D11_RECT scissor = {};
	scissor.left = static_cast<LONG>(std::floor(rect.X));
	scissor.top = static_cast<LONG>(std::floor(rect.Y));
	scissor.right = static_cast<LONG>(std::ceil(rect.X + rect.Width));
	scissor.bottom = static_cast<LONG>(std::ceil(rect.Y + rect.Height));

	mDeviceContext->RSSetScissorRects(1, &scissor);
}

void URenderer::PrepareForUI()
{
	/*
	DeviceContext->ClearRenderTargetView(FrameBufferRTV, ClearColor);
	DeviceContext->RSSetViewports(1, &ViewportInfo);
	*/
	mDeviceContext->RSSetViewports(1, &mViewportInfo);
	mDeviceContext->OMSetRenderTargets(1, mFrameBufferRTV.GetAddressOf(), nullptr);
}

void URenderer::SetMaterialResources(
	ID3D11ShaderResourceView* diffuseTextureSRV,
	ID3D11ShaderResourceView* normalTextureSRV,
	ID3D11ShaderResourceView* specularTextureSRV,
	ID3D11SamplerState* samplerState)
{
	mDeviceContext->PSSetShaderResources(0, 1, &diffuseTextureSRV);
	mDeviceContext->PSSetShaderResources(1, 1, &normalTextureSRV);
	mDeviceContext->PSSetShaderResources(2, 1, &specularTextureSRV);
	mDeviceContext->PSSetSamplers(0, 1, &samplerState);
}

void URenderer::SetStaticMeshResources(
	ID3D11Buffer* const* vertexBuffer,
	ID3D11Buffer* indexBuffer)
{
	assert(vertexBuffer);
	uint32 offset = 0;
	mDeviceContext->IASetVertexBuffers(0, 1, vertexBuffer, &StrideNormalVertex, &offset);
	mDeviceContext->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);
}

void URenderer::PrepareSimplePrimitive()
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	prepareSimpleShader();

	auto& resources = *mGpuResourceManagerRef;

	mDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	mDeviceContext->RSSetState(&resources.GetRasterizerState(mbWireFrame ? RST_Wireframe : RST_Default));

	mDeviceContext->OMSetDepthStencilState(&resources.GetDepthStencilState(DSS_Default), 0);
	mDeviceContext->OMSetBlendState(&resources.GetBlendState(BST_Default), nullptr, 0xffffffff);
}

void URenderer::PrepareTexturedPrimitive()
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	prepareTextureShader();

	auto& resources = *mGpuResourceManagerRef;

	mDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	mDeviceContext->RSSetState(&resources.GetRasterizerState(mbWireFrame ? RST_Wireframe : RST_Default));

	mDeviceContext->OMSetDepthStencilState(&resources.GetDepthStencilState(DSS_Default), 0);
	mDeviceContext->OMSetBlendState(&resources.GetBlendState(BST_Default), nullptr, 0xffffffff);
}

void URenderer::PrepareLine()
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	prepareLineShader();

	auto& resources = *mGpuResourceManagerRef;

	mDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

	mDeviceContext->RSSetState(&resources.GetRasterizerState(mbWireFrame ? RST_Wireframe : RST_Default));

	mDeviceContext->OMSetDepthStencilState(&resources.GetDepthStencilState(DSS_Default), 0);
	mDeviceContext->OMSetBlendState(&resources.GetBlendState(BST_Default), nullptr, 0xffffffff);
}

void URenderer::PrepareFont()
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	prepareFontShader();

	auto& resources = *mGpuResourceManagerRef;

	mDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Always render solid
	mDeviceContext->RSSetState(&resources.GetRasterizerState(RST_Default));

	mDeviceContext->OMSetDepthStencilState(&resources.GetDepthStencilState(DSS_Default), 0);
	mDeviceContext->OMSetBlendState(&resources.GetBlendState(BST_Additive), nullptr, 0xffffffff);
}

void URenderer::PrepareUnicodeFont()
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	prepareUnicodeFontShader();

	auto& resources = *mGpuResourceManagerRef;

	mDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// 기존 PrepareFont와 동일하게 면으로 렌더링
	mDeviceContext->RSSetState(&resources.GetRasterizerState(RST_Default));

	mDeviceContext->OMSetBlendState(&resources.GetBlendState(BST_AlphaBlend), nullptr, 0xffffffff);
}

void URenderer::PrepareGizmo()
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	prepareSimpleShader();

	auto& resources = *mGpuResourceManagerRef;

	mDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Always render solid
	mDeviceContext->RSSetState(&resources.GetRasterizerState(RST_Default));

	mDeviceContext->OMSetDepthStencilState(&resources.GetDepthStencilState(DSS_Default), 0);
	mDeviceContext->OMSetBlendState(&resources.GetBlendState(BST_Default), nullptr, 0xffffffff);
}

void URenderer::PrepareParticle()
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	prepareParticleShader();

	auto& resources = *mGpuResourceManagerRef;

	mDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Always render solid
	mDeviceContext->RSSetState(&resources.GetRasterizerState(RST_Default));

	mDeviceContext->OMSetDepthStencilState(&resources.GetDepthStencilState(DSS_NoWrite), 0);
	mDeviceContext->OMSetBlendState(&resources.GetBlendState(BST_Additive), nullptr, 0xffffffff);
}

void URenderer::PrepareStaticMesh()
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	prepareStaticMeshShader();

	auto& resources = *mGpuResourceManagerRef;

	mDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	mDeviceContext->RSSetState(&resources.GetRasterizerState(mbWireFrame ? RST_Wireframe : RST_Default));

	mDeviceContext->OMSetDepthStencilState(&resources.GetDepthStencilState(DSS_Default), 0);
	mDeviceContext->OMSetBlendState(&resources.GetBlendState(BST_Default), nullptr, 0xffffffff);
}

void URenderer::PrepareHighlight()
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	//prepareSimpleShader();
	prepareStaticMeshShader();

	auto& resources = *mGpuResourceManagerRef;

	mDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Always render solid
	mDeviceContext->RSSetState(&resources.GetRasterizerState(RST_Default));

	// Set depth stencil state in the RenderHighlight method
	//DeviceContext->OMSetDepthStencilState(DepthStencilState[DSS_Default], 0);
	mDeviceContext->OMSetBlendState(&resources.GetBlendState(BST_Default), nullptr, 0xffffffff);
}

void URenderer::PrepareSimpleInstanced()
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	prepareInstancedShader();

	auto& resources = *mGpuResourceManagerRef;

	mDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	mDeviceContext->RSSetState(&resources.GetRasterizerState(mbWireFrame ? RST_Wireframe : RST_Default));

	mDeviceContext->OMSetDepthStencilState(&resources.GetDepthStencilState(DSS_Default), 0);
	mDeviceContext->OMSetBlendState(&resources.GetBlendState(BST_Default), nullptr, 0xffffffff);
}

void URenderer::prepareInstancedShader()
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	auto& resources = *mGpuResourceManagerRef;

	mDeviceContext->VSSetShader(&resources.GetVertexShader(VST_Instanced), nullptr, 0);
	mDeviceContext->PSSetShader(&resources.GetPixelShader(PST_Simple), nullptr, 0);
	mDeviceContext->IASetInputLayout(&resources.GetInputLayout(ILT_PositionColorMatrixTint));

	auto constantBuffer = &resources.GetConstantBuffer(CBT_Simple);
	mDeviceContext->VSSetConstantBuffers(0, 1, &constantBuffer);
}

void URenderer::prepareSimpleShader()
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	auto& resources = *mGpuResourceManagerRef;

	mDeviceContext->VSSetShader(&resources.GetVertexShader(VST_Simple), nullptr, 0);
	mDeviceContext->PSSetShader(&resources.GetPixelShader(PST_Simple), nullptr, 0);
	mDeviceContext->IASetInputLayout(&resources.GetInputLayout(ILT_PositionColor));

	auto constantBuffer = &resources.GetConstantBuffer(CBT_Simple);
	mDeviceContext->VSSetConstantBuffers(0, 1, &constantBuffer);
}

void URenderer::prepareTextureShader()
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	auto& resources = *mGpuResourceManagerRef;

	mDeviceContext->VSSetShader(&resources.GetVertexShader(VST_Texture), nullptr, 0);
	mDeviceContext->PSSetShader(&resources.GetPixelShader(PST_Texture), nullptr, 0);
	mDeviceContext->IASetInputLayout(&resources.GetInputLayout(ILT_PositionTexture));

	auto constantBuffer = &resources.GetConstantBuffer(CBT_Texture);
	mDeviceContext->VSSetConstantBuffers(0, 1, &constantBuffer);
	mDeviceContext->PSSetConstantBuffers(0, 1, &constantBuffer);
}

void URenderer::prepareBillboardTextureShader()
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	auto& resources = *mGpuResourceManagerRef;

	mDeviceContext->VSSetShader(&resources.GetVertexShader(VST_Billboard), nullptr, 0);
	mDeviceContext->PSSetShader(&resources.GetPixelShader(PST_Billboard), nullptr, 0);
	mDeviceContext->IASetInputLayout(&resources.GetInputLayout(ILT_PositionTexture));

	auto constantBuffer = &resources.GetConstantBuffer(CBT_BillboardTexture);
	mDeviceContext->VSSetConstantBuffers(0, 1, &constantBuffer);
	mDeviceContext->PSSetConstantBuffers(0, 1, &constantBuffer);
}

void URenderer::prepareLineShader()
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	auto& resources = *mGpuResourceManagerRef;

	mDeviceContext->VSSetShader(&resources.GetVertexShader(VST_Line), nullptr, 0);
	mDeviceContext->PSSetShader(&resources.GetPixelShader(PST_Line), nullptr, 0);
	mDeviceContext->IASetInputLayout(&resources.GetInputLayout(ILT_PositionColor));

	auto constantBuffer = &resources.GetConstantBuffer(CBT_Simple);
	mDeviceContext->VSSetConstantBuffers(0, 1, &constantBuffer);
}

void URenderer::prepareFontShader()
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	auto& resources = *mGpuResourceManagerRef;

	mDeviceContext->VSSetShader(&resources.GetVertexShader(VST_Font), nullptr, 0);
	mDeviceContext->PSSetShader(&resources.GetPixelShader(PST_Font), nullptr, 0);
	mDeviceContext->IASetInputLayout(&resources.GetInputLayout(ILT_PositionTexture));

	auto constantBuffer = &resources.GetConstantBuffer(CBT_Font);
	mDeviceContext->VSSetConstantBuffers(0, 1, &constantBuffer);
	mDeviceContext->PSSetConstantBuffers(0, 1, &constantBuffer);
}

void URenderer::prepareUnicodeFontShader()
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	auto& resources = *mGpuResourceManagerRef;

	mDeviceContext->VSSetShader(&resources.GetVertexShader(VST_Font), nullptr, 0);
	mDeviceContext->IASetInputLayout(&resources.GetInputLayout(ILT_PositionTexture));

	// MSDF 전용 픽셀 셰이더
	mDeviceContext->PSSetShader(&resources.GetPixelShader(PST_UnicodeFont), nullptr, 0);


	auto constantBuffer0 = &resources.GetConstantBuffer(CBT_Font);
	mDeviceContext->VSSetConstantBuffers(0, 1, &constantBuffer0);
	mDeviceContext->PSSetConstantBuffers(0, 1, &constantBuffer0);

	// b1: DistanceRange
	auto constantBuffer1 = &resources.GetConstantBuffer(CBT_UnicodeFont);
	mDeviceContext->PSSetConstantBuffers(1, 1, &constantBuffer1);
}

void URenderer::prepareParticleShader()
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	auto& resources = *mGpuResourceManagerRef;

	mDeviceContext->VSSetShader(&resources.GetVertexShader(VST_Particle), nullptr, 0);
	mDeviceContext->PSSetShader(&resources.GetPixelShader(PST_Particle), nullptr, 0);
	mDeviceContext->IASetInputLayout(&resources.GetInputLayout(ILT_PositionTexture));

	auto constantBuffer = &resources.GetConstantBuffer(CBT_Particle);
	mDeviceContext->VSSetConstantBuffers(0, 1, &constantBuffer);
	mDeviceContext->PSSetConstantBuffers(0, 1, &constantBuffer);
}

void URenderer::prepareStaticMeshShader()
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	auto& resources = *mGpuResourceManagerRef;

	mDeviceContext->VSSetShader(&resources.GetVertexShader(VST_StaticMesh), nullptr, 0);
	mDeviceContext->PSSetShader(&resources.GetPixelShader(PST_StaticMesh), nullptr, 0);
	mDeviceContext->IASetInputLayout(&resources.GetInputLayout(ILT_PositionNormalColorTexture));

	if (mFrameCB)
	{
		mDeviceContext->VSSetConstantBuffers(0, 1, mFrameCB.GetAddressOf());
	}
}

void URenderer::prepareHighlightMaskShader()
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	auto& resources = *mGpuResourceManagerRef;

	mDeviceContext->VSSetShader(&resources.GetVertexShader(VST_HighlightMask), nullptr, 0);
	mDeviceContext->PSSetShader(&resources.GetPixelShader(PST_HighlightMask), nullptr, 0);
	mDeviceContext->IASetInputLayout(&resources.GetInputLayout(ILT_Position));

	auto constantBuffer = &resources.GetConstantBuffer(CBT_HighlightMask);
	mDeviceContext->VSSetConstantBuffers(0, 1, &constantBuffer);
}

void URenderer::prepareHighlightOutlineShader()
{
	assert(mGpuResourceManagerRef && mDeviceContext);
	auto& resources = *mGpuResourceManagerRef;

	mDeviceContext->VSSetShader(&resources.GetVertexShader(VST_HighlightOutline), nullptr, 0);
	mDeviceContext->PSSetShader(&resources.GetPixelShader(PST_HighlightOutline), nullptr, 0);
	mDeviceContext->IASetInputLayout(nullptr);

	auto constantBuffer = &resources.GetConstantBuffer(CBT_HighlightOutline);
	//mDeviceContext->VSSetConstantBuffers(0, 1, &constantBuffer);
	mDeviceContext->PSSetConstantBuffers(0, 1, &constantBuffer);
}


void URenderer::RenderSimplePrimitive(ID3D11Buffer* pBuffer, UINT numVertices)
{
	UINT offset = 0;
	// Bind the vertex buffer
	mDeviceContext->IASetVertexBuffers(0, 1, &pBuffer, &StrideSimple, &offset);
	mDeviceContext->Draw(numVertices, 0);
}

void URenderer::RenderTexturePrimitive(ID3D11Buffer* pBuffer, UINT numVertices,
	ID3D11ShaderResourceView* texture, ID3D11SamplerState* samplerState, ID3D11Buffer* indexBuffer, UINT indexCount)
{
	UINT offset = 0;
	// Bind the vertex buffer
	mDeviceContext->IASetVertexBuffers(0, 1, &pBuffer, &StrideTextured, &offset);

	// Bind the texture resource
	mDeviceContext->PSSetShaderResources(0, 1, &texture);
	mDeviceContext->PSSetSamplers(0, 1, &samplerState);


	//DeviceContext->Draw(numVertices, 0);


	mDeviceContext->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);

	// 임시 큐브
	if (indexBuffer)
		mDeviceContext->DrawIndexed(indexCount, 0, 0);
	else
		mDeviceContext->Draw(numVertices, 0);
}


//void URenderer::RenderTexturedPrimitive(ID3D11Buffer* vertexBuffer,	UINT numVertices)
//{
//	if (!DeviceContext ||
//		!vertexBuffer ||
//		numVertices == 0 ||
//		!ConstantBuffer ||
//		!PrimitiveTextureVertexShader ||
//		!PrimitiveTexturePixelShader ||
//		!PrimitiveTextureLayout ||
//		!PrimitiveTextureSRV ||
//		!PrimitiveTextureSampler ||
//		!DepthStencilState)
//	{
//		return;
//	}
//
//	// 위치 + UV 정점 버퍼 연결
//	const UINT stride = sizeof(FVertexTextured);
//	const UINT offset = 0;
//
//	DeviceContext->IASetVertexBuffers(
//		0, 1, &vertexBuffer, &stride, &offset);
//
//	DeviceContext->IASetInputLayout(PrimitiveTextureLayout);
//
//	DeviceContext->IASetPrimitiveTopology(
//		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
//
//	// 텍스처용 셰이더 연결
//	DeviceContext->VSSetShader(PrimitiveTextureVertexShader, nullptr, 0);
//
//	DeviceContext->PSSetShader(PrimitiveTexturePixelShader, nullptr, 0);
//
//	//  GraphicsManager에서 갱신한 변환 행렬 연결
//	DeviceContext->VSSetConstantBuffers(0, 1, &ConstantBuffer);
//
//	// HLSL의 t0에 텍스처, s0에 샘플러 연결
//	DeviceContext->PSSetShaderResources(0, 1, &PrimitiveTextureSRV);
//
//	DeviceContext->PSSetSamplers(0, 1, &PrimitiveTextureSampler);
//
//	// 일반 불투명 Primitive용 상태 설정
//	DeviceContext->OMSetDepthStencilState(DepthStencilState, 0);
//
//	DeviceContext->OMSetBlendState(	nullptr, nullptr, 0xffffffff);
//
//	// 그리기
//	DeviceContext->Draw(numVertices, 0);
//
//	//  사용한 텍스처와 샘플러 연결 해제
//	ID3D11ShaderResourceView* nullSRV = nullptr;
//	ID3D11SamplerState* nullSampler = nullptr;
//
//	DeviceContext->PSSetShaderResources(0, 1, &nullSRV);
//
//	DeviceContext->PSSetSamplers(0, 1, &nullSampler);
//
//	// 기존 색상 셰이더로 돌아가기
//	PrepareSimpleShader();
//}

void URenderer::RenderFontTexture(uint32 numCharacter)
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	auto& resources = *mGpuResourceManagerRef;
	const FBuffer& fontBuffer = resources.GetFontBuffer();

	UINT offset = 0;
	// Bind the vertex buffer
	mDeviceContext->IASetVertexBuffers(0, 1,
		fontBuffer.Buffer.GetAddressOf(),
		&StrideTextured, &offset);

	// Bind the texture resource
	auto* fontTextureSRV = resources.FindTextureOrAdd(BuiltinAssets::EnglishFontAtlas);
	mDeviceContext->PSSetShaderResources(0, 1, &fontTextureSRV);

	auto* fontSamplerState = &resources.GetSamplerState(ESamplerStateType::SST_Clamp);
	mDeviceContext->PSSetSamplers(0, 1, &fontSamplerState);

	// Bind the index buffer
	mDeviceContext->IASetIndexBuffer(
		fontBuffer.IndexBuffer.Get(),
		DXGI_FORMAT_R32_UINT, 0);

	mDeviceContext->DrawIndexed(numCharacter * 6, 0, 0);
}

void URenderer::RenderUnicodeFontTexture(uint32 indexCount)
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	auto& resources = *mGpuResourceManagerRef;
	const FBuffer& fontBuffer = resources.GetUnicodeFontBuffer();

	UINT offset = 0;

	mDeviceContext->IASetVertexBuffers(0, 1, fontBuffer.Buffer.GetAddressOf(), &StrideTextured, &offset);

	mDeviceContext->IASetIndexBuffer(fontBuffer.IndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

	auto* fontTextureSRV = resources.FindTextureOrAdd(BuiltinAssets::KoreanFontAtlas);
	mDeviceContext->PSSetShaderResources(0, 1, &fontTextureSRV);

	auto* fontSamplerState = &resources.GetSamplerState(ESamplerStateType::SST_Clamp);
	mDeviceContext->PSSetSamplers(0, 1, &fontSamplerState);

	// 이미 인덱스 개수를 받으므로 6을 곱하지 않음
	mDeviceContext->DrawIndexed(indexCount, 0, 0);
}
void URenderer::RenderParticle(ID3D11ShaderResourceView* texture)
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	auto& resources = *mGpuResourceManagerRef;
	const FBuffer& buffer = *resources.FindImmutableBufferOrAdd(
		BuiltinAssets::BillboardQuadTextured);

	UINT offset = 0;

	// Bind the vertex buffer
	mDeviceContext->IASetVertexBuffers(0, 1, buffer.Buffer.GetAddressOf(), &StrideTextured, &offset);

	// Bind the texture resource
	mDeviceContext->PSSetShaderResources(0, 1, &texture);

	auto* particleSamplerState = &resources.GetSamplerState(ESamplerStateType::SST_Clamp);
	mDeviceContext->PSSetSamplers(0, 1, &particleSamplerState);

	// Bind the index buffer
	mDeviceContext->IASetIndexBuffer(buffer.IndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

	mDeviceContext->DrawIndexed(6, 0, 0);
}

void URenderer::DrawVertexBuffer(uint32 numVertices)
{
	assert(mDeviceContext);
	if (numVertices == 0) return;

	mDeviceContext->Draw(numVertices, 0);
}

void URenderer::DrawIndexedBuffer(uint32 indexCount, uint32 startIndex)
{
	assert(mDeviceContext);
	if (indexCount == 0) return;
	mDeviceContext->DrawIndexed(indexCount, startIndex, 0);
}

// 쌓아둔 선분 전체를 한 번의 Draw로 그린다.
// 토폴로지를 바꾸므로 반드시 이 함수 안에서 되돌린다. 안 그러면 뒤에 그리는 것들이 전부 깨진다.
void URenderer::RenderLines(const FVertexSimple* vertices, uint32 numVertices, const uint32* indices, uint32 numindices)
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	if (vertices == nullptr || numVertices == 0) return;
	if (indices == nullptr || numindices == 0) return;

	if (!mGpuResourceManagerRef->EnsureLineBuffer(numVertices, numindices))
	{
		assert(false && "Failed to ensure line buffer.");
		return;
	}

	auto& resources = *mGpuResourceManagerRef;
	auto* lineVertexBuffer = resources.GetLineBuffer().Buffer.Get();
	auto* lineIndexBuffer = resources.GetLineBuffer().IndexBuffer.Get();

	// WRITE_DISCARD: 이전 내용을 버리고 새 메모리를 받는다.
	// GPU가 지난 프레임 데이터를 아직 읽고 있어도 CPU가 기다리지 않는다.

	D3D11_MAPPED_SUBRESOURCE lineBufferMSR;
	if (FAILED(mDeviceContext->Map(lineVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &lineBufferMSR)))
	{
		return;
	}
	memcpy(lineBufferMSR.pData, vertices, numVertices * sizeof(FVertexSimple));
	mDeviceContext->Unmap(lineVertexBuffer, 0);

	// WRITE_DISCARD: 이전 내용을 버리고 새 메모리를 받는다.
	// GPU가 지난 프레임 데이터를 아직 읽고 있어도 CPU가 기다리지 않는다.
	D3D11_MAPPED_SUBRESOURCE lineBufferMSRI;
	if (FAILED(mDeviceContext->Map(lineIndexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &lineBufferMSRI)))
	{
		return;
	}
	memcpy(lineBufferMSRI.pData, indices, numindices * sizeof(uint32));
	mDeviceContext->Unmap(lineIndexBuffer, 0);

	// 직전에 메시 버퍼가 물려 있으므로 갈아끼워야 한다
	UINT offset = 0;
	mDeviceContext->IASetVertexBuffers(0, 1, &lineVertexBuffer, &StrideSimple, &offset);
	mDeviceContext->IASetIndexBuffer(lineIndexBuffer, DXGI_FORMAT_R32_UINT, 0);
	mDeviceContext->DrawIndexed(numindices, 0, 0);
}

void URenderer::RenderHighlight(ID3D11Buffer* pBuffer, uint32 Num, FMatrix viewProjectionMatrix,
	const FMatrix originalMatrix,
	int viewMin[2], int viewMax[2],
	ID3D11Buffer* indexBuffer, uint32 indexCount)
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	auto& resources = *mGpuResourceManagerRef;

	auto* defaultWhiteTexture = resources.FindTextureOrAdd(BuiltinAssets::DefaultWhiteTexture);
	auto& samplerState = resources.GetSamplerState(ESamplerStateType::SST_Clamp);

	/* Render Mask */
	{
		prepareHighlightMaskShader();

		ID3D11ShaderResourceView* nullSRV = nullptr;
		mDeviceContext->PSSetShaderResources(0, 1, &nullSRV);
		mDeviceContext->OMSetRenderTargets(1, mSelectionMaskRTV.GetAddressOf(), nullptr);
		const float clearColor[4] = { 0.f, 0.f, 0.f, 0.f };
		mDeviceContext->ClearRenderTargetView(mSelectionMaskRTV.Get(), clearColor);

		UpdateHighlightMaskConstant(originalMatrix, viewProjectionMatrix);

		// Render the object to the selection mask
		UINT offset = 0;
		auto* maskBuffer = &resources.GetConstantBuffer(CBT_HighlightMask);
		mDeviceContext->IASetVertexBuffers(0, 1, &pBuffer, &StrideNormalVertex, &offset);

		mDeviceContext->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);

		mDeviceContext->DrawIndexed(indexCount, 0, 0);
	}

	/* Render Outline */
	{
		prepareHighlightOutlineShader();

		mDeviceContext->OMSetRenderTargets(1, mFrameBufferRTV.GetAddressOf(), nullptr);

		const FLinearColor outlineColor = FLinearColor(1.f, 0.6f, 0.f, 1.f); // Orange color for the outline
		constexpr int outlineThickness = 3; // Thickness of the outline in pixels

		UpdateHighlightOutlineConstant(
			outlineColor,
			viewMin,
			viewMax,
			outlineThickness
			);

		// Render the outline using the selection mask
		mDeviceContext->PSSetShaderResources(0, 1, mSelectionMaskSRV.GetAddressOf());
		// Vertex buffer is not needed for full-screen quad rendering

		mDeviceContext->Draw(3, 0); // Draw a full-screen triangle
	}

	/* Reset State */
	mDeviceContext->OMSetRenderTargets(1, mFrameBufferRTV.GetAddressOf(), mDepthStencilView.Get());
}

void URenderer::releaseDepthStencilBuffer()
{
	mDepthStencilView.Reset();
	mDepthStencilBuffer.Reset();
}

void URenderer::releaseSelectionMaskResources()
{
	mSelectionMaskRTV.Reset();
	mSelectionMaskSRV.Reset();
}

void URenderer::UpdateFrameConstant(const FMatrix& viewProjection)
{
	assert(mDeviceContext && mFrameCB);

	D3D11_MAPPED_SUBRESOURCE msr = {};
	if (SUCCEEDED(mDeviceContext->Map(mFrameCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &msr)))
	{
		FFrameConstants* constants = (FFrameConstants*)msr.pData;
		constants->ViewProjection = viewProjection;
		mDeviceContext->Unmap(mFrameCB.Get(), 0);
	}
	mDeviceContext->VSSetConstantBuffers(0, 1, mFrameCB.GetAddressOf());
}

void URenderer::UpdateSimpleConstant(FMatrix world, FMatrix viewProjection, FLinearColor tint)
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	auto& constantBuffer = mGpuResourceManagerRef->GetConstantBuffer(CBT_Simple);

	D3D11_MAPPED_SUBRESOURCE constantbufferMSR;

	mDeviceContext->Map(&constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &constantbufferMSR); // update constant buffer every frame
	FConstants* constants = (FConstants*)constantbufferMSR.pData;
	{
		constants->World = world;
		constants->ViewProjection = viewProjection;
		constants->Tint = tint;
	}
	mDeviceContext->Unmap(&constantBuffer, 0);

}

void URenderer::UpdateTextureConstant(FMatrix world, FMatrix viewProjection, FLinearColor tint,
	FVector2 uvScale, FVector2 uvOffset)
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	auto& constantBuffer = mGpuResourceManagerRef->GetConstantBuffer(CBT_Texture);

	D3D11_MAPPED_SUBRESOURCE constantbufferMSR;
	mDeviceContext->Map(&constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &constantbufferMSR); // update constant buffer every frame
	FTextureConstants* constants = (FTextureConstants*)constantbufferMSR.pData;
	{
		constants->World = world;
		constants->ViewProjection = viewProjection;
		constants->Tint = tint;
		constants->UVOffset = uvOffset;
		constants->UVScale = uvScale;
	}
	mDeviceContext->Unmap(&constantBuffer, 0);
}

void URenderer::UpdateBillboardConstant(FVector3 location, FVector3 scale, FMatrix viewProjection,
	FVector3 cameraRight, FVector3 cameraUp,
	FLinearColor tint,
	FVector2 uvScale, FVector2 uvOffset)
{
	assert(mGpuResourceManagerRef && mDeviceContext);
	auto& constantBuffer = mGpuResourceManagerRef->GetConstantBuffer(CBT_BillboardTexture);

	D3D11_MAPPED_SUBRESOURCE constantbufferMSR;
	mDeviceContext->Map(&constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &constantbufferMSR); // update constant buffer every frame
	FBillboardConstants* constants = (FBillboardConstants*)constantbufferMSR.pData;
	{
		constants->Location = location;
		constants->Scale = scale;

		constants->ViewProjection = viewProjection;
		constants->Tint = tint;

		constants->UVOffset = uvOffset;
		constants->UVScale = uvScale;

		constants->CameraRight = cameraRight;
		constants->CameraUp = cameraUp;
	}
	mDeviceContext->Unmap(&constantBuffer, 0);

}

void URenderer::UpdateFontConstant(FVector3 location, FVector3 scale, FMatrix viewProjection,
	FVector3 cameraRight, FVector3 cameraUp,
	FLinearColor tint)
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	auto& constantBuffer = mGpuResourceManagerRef->GetConstantBuffer(CBT_Font);

	D3D11_MAPPED_SUBRESOURCE constantbufferMSR;
	mDeviceContext->Map(&constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &constantbufferMSR); // update constant buffer every frame
	FFontConstants* constants = (FFontConstants*)constantbufferMSR.pData;
	{
		constants->Location = location;
		constants->Scale = scale;
		constants->ViewProjection = viewProjection;
		constants->Tint = tint;
		constants->CameraRight = cameraRight;
		constants->CameraUp = cameraUp;
	}
	mDeviceContext->Unmap(&constantBuffer, 0);

}

void URenderer::UpdateParticleConstant(FVector3 location, FVector3 scale, FMatrix viewProjection,
	FVector3 cameraRight, FVector3 cameraUp,
	int32 numRows, int32 numCols, int32 currentFrame, int32 nextFrame, float frameRatio,
	FLinearColor tint
)
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	auto& constantBuffer = mGpuResourceManagerRef->GetConstantBuffer(CBT_Particle);

	D3D11_MAPPED_SUBRESOURCE constantbufferMSR;
	mDeviceContext->Map(&constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &constantbufferMSR); // update constant buffer every frame
	FParticleConstants* constants = (FParticleConstants*)constantbufferMSR.pData;
	{
		constants->Location = location;
		constants->Scale = scale;
		constants->ViewProjection = viewProjection;
		constants->Tint = tint;
		constants->CameraRight = cameraRight;
		constants->CameraUp = cameraUp;
		constants->NumRows = numRows;
		constants->NumCols = numCols;
		constants->CurrentFrame = currentFrame;
		constants->NextFrame = nextFrame;
		constants->FrameRatio = frameRatio;
	}
	mDeviceContext->Unmap(&constantBuffer, 0);
}

void URenderer::UpdateFontBuffer(const TArray<FVertexTextured>& vertices, const TArray<uint32>& indices, uint32 numCharacter)
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	auto& resources = *mGpuResourceManagerRef;

	if (!resources.EnsureFontBuffer(numCharacter))
	{
		assert(false && "Failed to ensure font buffer.");
		return;
	}

	auto& fontBuffer = resources.GetFontBuffer().Buffer;

	const uint32 numVertices = vertices.Num();


	// Map the vertex buffer and copy the vertex data
	D3D11_MAPPED_SUBRESOURCE mappedResource = {};
	mDeviceContext->Map(fontBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	memcpy(mappedResource.pData, vertices.GetData(), numVertices * sizeof(FVertexTextured));
	mDeviceContext->Unmap(fontBuffer.Get(), 0);
}

bool URenderer::UpdateUnicodeFontBuffer(const FTextMesh& textMesh)
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	auto& resources = *mGpuResourceManagerRef;

	if (!resources.EnsureUnicodeFontBuffer(textMesh.TextNum))
	{
		assert(false && "Failed to ensure unicode font buffer.");
		return false;
	}

	auto& fontBuffer = resources.GetUnicodeFontBuffer().Buffer;

	//if (!Device || !DeviceContext)
	//{
	//	return false;
	//}

	//if (textMesh.FontRenderMode != EFontRenderMode::MSDF
	//	|| textMesh.Vertices.Num() <= 0
	//	|| textMesh.Indices.Num() <= 0)
	//{
	//	return false;
	//}

	//const UINT vertexCount =
	//	static_cast<UINT>(textMesh.Vertices.Num());

	//const UINT indexCount =
	//	static_cast<UINT>(textMesh.Indices.Num());

	//// 현재 폰트 메시: 사각형마다 정점 4개, 인덱스 6개
	//if (vertexCount % 4 != 0
	//	|| indexCount % 6 != 0
	//	|| vertexCount / 4 != indexCount / 6)
	//{
	//	return false;
	//}

	//const UINT quadCount = vertexCount / 4;

	//constexpr UINT vertexSize =
	//	static_cast<UINT>(sizeof(FVertexTextured));

	//const UINT maxVertexCount =
	//	(std::numeric_limits<UINT>::max)() / vertexSize;

	//if (vertexCount > maxVertexCount)
	//{
	//	return false;
	//}

	//// 정점 버퍼가 부족하면 확장
	//if (!UnicodeFontVertexBuffer
	//	|| vertexCount > UnicodeFontVertexCapacity)
	//{
	//	UINT newCapacity = vertexCount;

	//	if (UnicodeFontVertexCapacity <= maxVertexCount / 2)
	//	{
	//		newCapacity = (std::max)(
	//			vertexCount, UnicodeFontVertexCapacity * 2);
	//	}

	//	D3D11_BUFFER_DESC desc{};
	//	desc.ByteWidth = newCapacity * vertexSize;
	//	desc.Usage = D3D11_USAGE_DYNAMIC;
	//	desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	//	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	//	Microsoft::WRL::ComPtr<ID3D11Buffer> newBuffer;

	//	const HRESULT hr = Device->CreateBuffer(
	//		&desc, nullptr, newBuffer.GetAddressOf());

	//	if (FAILED(hr))
	//	{
	//		return false;
	//	}

	//	if (UnicodeFontVertexBuffer)
	//	{
	//		UnicodeFontVertexBuffer->Release();
	//	}

	//	UnicodeFontVertexBuffer = newBuffer.Detach();
	//	UnicodeFontVertexCapacity = newCapacity;
	//}

	//// 사각형 개수에 맞는 인덱스 버퍼 확보
	//if (!ensureUnicodeFontIndexBuffer(quadCount))
	//{
	//	return false;
	//}

	uint32 vertexCount = static_cast<uint32>(textMesh.Vertices.Num());
	uint32 vertexSize = static_cast<uint32>(sizeof(FVertexTextured));

	// 정점의 위치와 UV 업로드
	D3D11_MAPPED_SUBRESOURCE mapped{};

	const HRESULT hr = mDeviceContext->Map(fontBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);

	if (FAILED(hr))
	{
		return false;
	}

	std::memcpy(mapped.pData, textMesh.Vertices.GetData(), vertexCount * vertexSize);

	mDeviceContext->Unmap(fontBuffer.Get(), 0);

	return true;
}

void URenderer::UpdateHighlightMaskConstant(FMatrix world, FMatrix viewProjection)
{
	assert(mGpuResourceManagerRef && mDeviceContext);
	auto& constantBuffer = mGpuResourceManagerRef->GetConstantBuffer(CBT_HighlightMask);
	D3D11_MAPPED_SUBRESOURCE constantbufferMSR;
	mDeviceContext->Map(&constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &constantbufferMSR); // update constant buffer every frame
	FMaskConstants* constants = (FMaskConstants*)constantbufferMSR.pData;
	{
		constants->World = world;
		constants->ViewProjection = viewProjection;
	}
	mDeviceContext->Unmap(&constantBuffer, 0);
}

void URenderer::UpdateHighlightOutlineConstant(FLinearColor outlineColor,
	int viewMin[2], int viewMax[2], int outlineThickness)
{
	assert(mGpuResourceManagerRef && mDeviceContext);
	auto& constantBuffer = mGpuResourceManagerRef->GetConstantBuffer(CBT_HighlightOutline);
	D3D11_MAPPED_SUBRESOURCE constantbufferMSR;
	mDeviceContext->Map(&constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &constantbufferMSR); // update constant buffer every frame
	FOutlineConstants* constants = (FOutlineConstants*)constantbufferMSR.pData;
	{
		constants->OutlineColor = outlineColor;
		constants->ViewMin[0] = viewMin[0];
		constants->ViewMin[1] = viewMin[1];
		constants->ViewMax[0] = viewMax[0];
		constants->ViewMax[1] = viewMax[1];
		constants->RadiusPixels = outlineThickness;
	}
	mDeviceContext->Unmap(&constantBuffer, 0);
}

void URenderer::UpdateBlendState(EBlendStateType blendState)
{
	assert(mGpuResourceManagerRef && mDeviceContext);
	assert(blendState < EBlendStateType::BST_Count);

	auto& resources = *mGpuResourceManagerRef;

	mDeviceContext->OMSetBlendState(&resources.GetBlendState(blendState), nullptr, 0xffffffff);
}



//void URenderer::UpdateParticleBuffer(const TArray<FVertexTextured>& vertices, const TArray<uint32>& indices)
//{
//	assert(ParticleVertexBuffer || BlendState[BST_Additive]);
//
//	const uint32 numVertices = vertices.Num();
//
//	// Map the vertex buffer and copy the vertex data
//	D3D11_MAPPED_SUBRESOURCE mappedResource = {};
//	DeviceContext->Map(ParticleVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
//	memcpy(mappedResource.pData, vertices.GetData(), numVertices * sizeof(FVertexTextured));
//	DeviceContext->Unmap(ParticleVertexBuffer, 0);
//
//	// Map the index buffer and copy the index data
//	D3D11_MAPPED_SUBRESOURCE mappedIndexResource = {};
//	DeviceContext->Map(ParticleIndexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedIndexResource);
//	memcpy(mappedIndexResource.pData, indices.GetData(), indices.Num() * sizeof(uint32));
//	DeviceContext->Unmap(ParticleIndexBuffer, 0);
//}

void URenderer::OnResize(UINT width, UINT height, float viewportWidth, float viewportHeight)
{
	if (!mSwapChain || width == 0 || height == 0) return;
	if (mViewportInfo.Width == viewportWidth && mViewportInfo.Height == viewportHeight) return;

	//해상도에 의존하는 프레임 버퍼와 뎁스 스텐실 버퍼를 재생성한다.
	mDeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
	releaseFrameBuffer();
	releaseDepthStencilBuffer();
	releaseSelectionMaskResources();

	HRESULT hr = mSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
	if (FAILED(hr)) return;

	DXGI_SWAP_CHAIN_DESC desc;
	mSwapChain->GetDesc(&desc);

	mViewportInfo = { 0.0f , 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };

	//상태는 이전에 생성한 걸 그대로 재사용
	createFrameBuffer();
	createDepthStencilBuffer(width, height);
	createSelectionMaskResources(width, height);
}

void URenderer::RenderFullscreenTexture(ID3D11ShaderResourceView* texture)
{
	assert(mGpuResourceManagerRef && mDeviceContext);

	UINT stride = sizeof(FVertexTextured);
	UINT offset = 0;

	auto& resources = *mGpuResourceManagerRef;

	auto* buffer = resources.FindImmutableBufferOrAdd(BuiltinAssets::LoadingScreenQuad);

	auto* vertexBuffer = buffer->Buffer.Get();
	mDeviceContext->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);

	auto* indexBuffer = buffer->IndexBuffer.Get();
	mDeviceContext->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);

	mDeviceContext->IASetInputLayout(&resources.GetInputLayout(ILT_PositionTexture));
	mDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	mDeviceContext->VSSetShader(&resources.GetVertexShader(VST_LoadingScreen), nullptr, 0);
	mDeviceContext->PSSetShader(&resources.GetPixelShader(PST_LoadingScreen), nullptr, 0);
	mDeviceContext->PSSetShaderResources(0, 1, &texture);

	auto* samplerState = &resources.GetSamplerState(ESamplerStateType::SST_Clamp);
	mDeviceContext->PSSetSamplers(0, 1, &samplerState);
	mDeviceContext->OMSetBlendState(&resources.GetBlendState(BST_Default), nullptr, 0xffffffff);

	mDeviceContext->OMSetDepthStencilState(nullptr, 0);
	mDeviceContext->DrawIndexed(6, 0, 0);
}

void URenderer::ClearDepth()
{
	mDeviceContext->ClearDepthStencilView(mDepthStencilView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
}
