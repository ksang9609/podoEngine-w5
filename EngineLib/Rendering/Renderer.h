#pragma once

#include <d3d11.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include "Core/enum.h"
#include "Core/Math/Matrix.h"
#include "Core/Math/Vector.h"

#include "ShaderConstants.h"
#include "RenderInfo.h"
#include "VertexType.h"
#include "GpuTimer.h"

#pragma comment(lib, "user32")
#pragma comment(lib, "d3d11")
#pragma comment(lib, "d3dcompiler")

// 선분 하나당 정점 2개. 축 6개 + 앞으로 붙을 그리드까지 감당할 만큼 잡아둔다
//static constexpr uint32 LINE_VERTEX_CAPACITY = 8192;
//static constexpr uint32 LINE_INDEX_CAPACITY = 16384;

constexpr uint32 StridePosition = sizeof(FVector);
constexpr uint32 StrideSimple = sizeof(FVertexSimple);
constexpr uint32 StrideTextured = sizeof(FVertexTextured);
constexpr uint32 StrideNormalVertex = sizeof(FNormalVertex);

class FGpuResourceManager;

struct FViewRect;

class URenderer
{
	template<typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

public:
	/* Create */
	void Initialize(HWND hWindow, FGpuResourceManager& gpuResourceManagerRef);

	ID3D11Device* GetDevice() const { return mDevice.Get(); }
	ID3D11DeviceContext* GetDeviceContext() const { return mDeviceContext.Get(); }
	ID3D11DeviceContext1* GetDeviceContext1() const { return mDeviceContext1.Get(); }
	ID3D11Buffer* GetPerObjectConstantBuffer() const { return mPerObjectCB.Get(); }
	ID3D11ShaderResourceView* GetDepthBufferSRV() const { return mDepthBufferSRV.Get(); }
	ID3D11RenderTargetView* GetFrameBufferRTV() const { return mFrameBufferRTV.Get(); }
	ID3D11DepthStencilView* GetDepthStencilView() const { return mDepthStencilView.Get(); }

	// Release all resources that this render holds.
	void Release();

	// Gloabal prepare method
	void BeginFrame();
	void SetViewMode(EViewModeIndex viewMode);
	void BeginView(const FViewRect& rect);
	void PrepareForUI();

	/* Set render states */
	void SetMaterialResources(
		ID3D11ShaderResourceView* diffuseTextureSRV,
		ID3D11ShaderResourceView* normalTextureSRV,
		ID3D11ShaderResourceView* specularTextureSRV,
		ID3D11SamplerState* samplerState);
	void SetStaticMeshResources(
		ID3D11Buffer* const* vertexBuffer,
		ID3D11Buffer* indexBuffer);

	/* Prepare methods for each rendering type */
	void PrepareSimplePrimitive();
	void PrepareSimpleInstanced();
	void PrepareTexturedPrimitive();
	void PrepareLine();
	void PrepareFont();
	void PrepareGizmo();
	void PrepareHighlight();
	// 셰이더, 입력 레이아웃, 블렌딩 상태 설정
	void PrepareUnicodeFont();
	void PrepareParticle();
	void PrepareStaticMesh();

	/* Update methods for each rendering type */
	void UpdateFrameConstant(const FMatrix& viewProjection);
	void UpdateSimpleConstant(FMatrix world, FMatrix viewProjection, FLinearColor tint = FLinearColor(0, 0, 0, 0));
	void UpdateTextureConstant(FMatrix world, FMatrix viewProjection, FLinearColor tint = FLinearColor(0, 0, 0, 0),
		FVector2 uvScale = { 1.0f, 1.0f }, FVector2 uvOffset = { 0.0f, 0.0f });
	void UpdateBillboardConstant(FVector3 location, FVector3 scale, FMatrix viewProjection,
		FVector3 cameraRight, FVector3 cameraUp,
		FLinearColor tint = FLinearColor(0, 0, 0, 0),
		FVector2 uvScale = { 1.0f, 1.0f }, FVector2 uvOffset = { 0.0f, 0.0f });
	void UpdateFontConstant(FVector3 location, FVector3 scale, FMatrix viewProjection,
		FVector3 cameraRight, FVector3 cameraUp,
		FLinearColor tint = FLinearColor(0, 0, 0, 0));
	void UpdateParticleConstant(FVector3 location, FVector3 scale, FMatrix viewProjection,
		FVector3 cameraRight, FVector3 cameraUp,
		int32 numRows = 1, int32 numCols = 1, int32 currentFrame = 0, int32 nextFrame = 0, float frameRatio = 0.0f,
		FLinearColor tint = FLinearColor(0, 0, 0, 0)
	);
	void UpdateHighlightMaskConstant(FMatrix world, FMatrix viewProjection);
	void UpdateHighlightOutlineConstant(FLinearColor outlineColor,
		int viewMin[2], int viewMax[2], int outlineThickness);

	void UpdateFontBuffer(const TArray<FVertexTextured>& vertices, const TArray<uint32>& indices, uint32 numCharacter);
	bool UpdateUnicodeFontBuffer(const FTextMesh& textMesh);

	void UpdateBlendState(EBlendStateType blendStateType);

	/* Render methods for each rendering type */
	void RenderSimplePrimitive(ID3D11Buffer* pBuffer, UINT numVertices);
	void RenderTexturePrimitive(ID3D11Buffer* pBuffer, UINT numVertices,
		ID3D11ShaderResourceView* texture, ID3D11SamplerState* samplerState, ID3D11Buffer* indexBuffer = nullptr, UINT numIndices = 0);
	// Render Text in the FontTextureBuffer and FontIndexBuffer.
	// It doesn't recieve buffer parameters since it use the internal buffers.
	void RenderFontTexture(uint32 numCharacter);
	void RenderLines(const FVertexSimple* vertices, uint32 numVertices, const uint32* indices, uint32 numindices);
	void RenderHighlight(ID3D11Buffer* pBuffer, uint32 Num, FMatrix viewProjectionMatrix,
		const FMatrix originalMatrix,
		int viewMin[2], int viewMax[2],
		ID3D11Buffer* indexBuffer, uint32 indexCount);


	void RenderUnicodeFontTexture(uint32 indexCount);
	void RenderParticle(ID3D11ShaderResourceView* texture);
	bool RenderSimpleInstanced(
		ID3D11Buffer* vertexBuffer,
		ID3D11Buffer* indexBuffer,
		UINT indexCount,
		const FInstanceData* instances,
		UINT instanceCount);

	// Directly draw the vertex buffer or index buffer without any additional setup.
	// Other rendering states must be set up before calling these functions.
	void DrawVertexBuffer(uint32 numVertices);
	void DrawIndexedBuffer(uint32 numIndices, uint32 startIndex = 0);

	void SwapBuffer();

	/* Getter */
	D3D11_VIEWPORT GetViewportInfo() const { return mViewportInfo; }


	//Initialize
	void ClearDepth();
	//=============================================
	//해상도 변경 시 호출
	//void OnResize(UINT Width, UINT Height);
	void OnResize(UINT width, UINT height, float viewportWidth, float viewportHeight);

	void RenderFullscreenTexture(ID3D11ShaderResourceView* texture);

	/* Timer */
	inline bool HasGpuTime() const { return mGpuTimer.HasResult(); }
	inline double GetGpuTimeMilliseconds() const { return mGpuTimer.GetMilliseconds(); }

private:
	/* Refernece */
	FGpuResourceManager* mGpuResourceManagerRef = nullptr;

	EViewModeIndex mViewMode = EViewModeIndex::VMI_Lit;
	bool mbWireFrame = false;

	/* Direct3D resources */
	ComPtr<ID3D11Device> mDevice = nullptr;
	ComPtr<ID3D11DeviceContext> mDeviceContext = nullptr;
	ComPtr<IDXGISwapChain> mSwapChain = nullptr;

	/* Render target and depth stencil resources */
	ComPtr<ID3D11Texture2D> mFrameBuffer = nullptr;
	ComPtr<ID3D11RenderTargetView> mFrameBufferRTV = nullptr;
	ComPtr<ID3D11Texture2D> mDepthStencilBuffer = nullptr;			// 실제 깊이값이 저장될 메모리
	ComPtr<ID3D11DepthStencilView> mDepthStencilView = nullptr;		// 그 메모리를 "출력 대상"으로 보는 뷰
	ComPtr<ID3D11ShaderResourceView> mDepthBufferSRV = nullptr;

	/* Resources for highlighting */
	ComPtr<ID3D11Texture2D> mSelectionMaskTexture = nullptr;
	ComPtr<ID3D11RenderTargetView> mSelectionMaskRTV = nullptr;
	ComPtr<ID3D11ShaderResourceView> mSelectionMaskSRV = nullptr;

	ComPtr<ID3D11DeviceContext1> mDeviceContext1 = nullptr;
	ComPtr<ID3D11Buffer> mFrameCB = nullptr;
	ComPtr<ID3D11Buffer> mPerObjectCB = nullptr;

	FLOAT mClearColor[4] = { 0.025f, 0.025f, 0.025f, 1.0f };
	D3D11_VIEWPORT mViewportInfo;

	/* Timer */
	FGpuTimer mGpuTimer;

	/* Create methods for each resources*/
	void createDeviceAndSwapChain(HWND hWindow);
	void createFrameBuffer();
	void createDepthStencilBuffer(UINT width, UINT height);
	void createSelectionMaskResources(UINT width, UINT height);
	void createOffsetConstantBuffers();

	/* Prepare methods for each shader */
	void prepareSimpleShader();
	void prepareInstancedShader();
	void prepareTextureShader();
	void prepareBillboardTextureShader();
	void prepareLineShader();
	void prepareFontShader();
	void prepareUnicodeFontShader();
	void prepareParticleShader();
	void prepareStaticMeshShader();
	void prepareHighlightMaskShader();
	void prepareHighlightOutlineShader();


	/* Release methods for all resources */
	void releaseDeviceAndSwapChain();
	void releaseFrameBuffer();
	void releaseDepthStencilBuffer();
	void releaseSelectionMaskResources();
};

