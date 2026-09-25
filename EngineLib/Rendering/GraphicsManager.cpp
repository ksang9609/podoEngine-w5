#include "GraphicsManager.h"

#include <algorithm>

#include "Core/Container/TQueue.h"
#include "Core/Math/Frustum.h" 
#include "Core/enum.h"
#include "Core/BuiltinAssets.h"
#include "Editor/Console.h"
#include "Engine/Actor.h"
#include "Engine/Components/NameComponent.h"
#include "Engine/Components/PrimitiveComponent.h"
#include "Rendering/SubUVMesh.h"

#include "Camera.h"
#include "Renderer.h"

FGraphicsManager::FGraphicsManager()
	: mGpuResourceManagerRef(nullptr)
	, mbWireFrame(false)
{
}

FGraphicsManager::~FGraphicsManager()
{
	mRenderer.reset();
}

void FGraphicsManager::Initialize(
	HWND hWindow,
	FGpuResourceManager& gpuResourceManager,
	FAssetManager& assetManager)
{
	mRenderer = std::make_unique<URenderer>();
	mRenderer->Initialize(hWindow, gpuResourceManager);

	mGpuResourceManagerRef = &gpuResourceManager;
	mAssetManagerRef = &assetManager;
}

void FGraphicsManager::BeginFrame()
{
	mRenderer->BeginFrame();
	// 그리는 순서가 중요하다: 가까운 것을 먼저, 먼 것을 나중에.
	// 깊이 테스트가 켜져 있으면 나중에 그린 FarCube 가 깊이 비교에서 탈락해
	// NearCube(주황)가 앞에 남고, 꺼져 있으면 FarCube(파랑)가 그 위를 덮어쓴다.
	//mRenderer->UpdateConstantViewProjection(viewProjection);
}

void FGraphicsManager::PrepareForUI()
{
	mRenderer->PrepareForUI();
}

//// TODO: remove outBillboardRenderQueue
//void QueueRenderQueue(
//	const TArray<FRenderInfo>& renderInfos,
//	TArray<const FRenderInfo*>& outSimpleRenderQueue,
//	TArray<const FRenderInfo*>& outTextureRenderQueue,
//	TArray<const FRenderInfo*>& outBillboardRenderQueue)
//{
//	for (const FRenderInfo& renderInfo : renderInfos)
//	{
//		if (renderInfo.ePrimitive == EPrimitive::EP_BillboardQuad)
//		{
//			outBillboardRenderQueue.Add(&renderInfo);
//		}
//		else if ((renderInfo.eRenderFlags & ERenderFlags::RF_TexturedPrimitive) == ERenderFlags::RF_None)
//		{
//			outTextureRenderQueue.Add(&renderInfo);
//		}
//		else
//		{
//			outSimpleRenderQueue.Add(&renderInfo);
//		}
//	}
//}
//
//bool RenderFlagMatch(ERenderFlags targetFlags, ERenderFlags renderFlags)
//{
//	return (static_cast<uint32>(targetFlags) & static_cast<uint32>(renderFlags)) != 0;
//}

// TODO: Combine worldaxis, bounding box into a single render queue type,
// since they are both line-based rendering and can be batched together.
// This will reduce the number of draw calls and improve performance.


void FGraphicsManager::updateRenderQueue(
	const TArray<FRenderInfo>& renderInfos,
	TMap<ERenderQueueType, TArray<const FRenderInfo*>>& outRenderQueueMap,
	const FFrustum* frustum, uint32 showFlags)
{
	for (const FRenderInfo& renderInfo : renderInfos)
	{
		if (frustum != nullptr)
		{
			if (!frustum->Intersects(renderInfo.WorldBounds))
			{
				continue;
			}
		}

		ERenderFlags renderFlags = renderInfo.eRenderFlags;

		if (HasAllRenderFlags(renderFlags, ERenderFlags::RF_Primitive) &&
			!HasAnyRenderFlags(renderFlags, ERenderFlags::RF_Billboard) &&
			HasViewShowFlag(showFlags, EEngineShowFlags::SF_Primitives))
		{
			// TODO: Unify all of these into just static mesh
			//if (renderInfo.ePrimitive == EPrimitive::EP_StaticMesh)
			//{
			outRenderQueueMap[RQT_StaticMesh].Add(&renderInfo);
			//}
			//else if (HasAllRenderFlags(renderFlags, ERenderFlags::RF_Texture))
			//{
			//	outRenderQueueMap[RQT_TexturedPrimitive].Add(&renderInfo);
			//}
			//else
			//{
			//	outRenderQueueMap[RQT_SimplePrimitive].Add(&renderInfo);
			//}
		}
		if (HasAllRenderFlags(renderFlags,
			ERenderFlags::RF_Billboard | ERenderFlags::RF_Text) &&
			HasViewShowFlag(showFlags, EEngineShowFlags::SF_BillboardText))
		{
			outRenderQueueMap[RQT_BillboardText].Add(&renderInfo);
		}
		if (HasAllRenderFlags(renderFlags, ERenderFlags::RF_WorldAxis) &&
			HasViewShowFlag(showFlags, EEngineShowFlags::SF_WorldAxis))
		{
			outRenderQueueMap[RQT_WorldAxis].Add(&renderInfo);
		}
		if (HasAllRenderFlags(renderFlags, ERenderFlags::RF_Gizmo))
		{
			outRenderQueueMap[RQT_Gizmo].Add(&renderInfo);
		}
		if (HasAllRenderFlags(renderFlags, ERenderFlags::RF_BoundingBox) &&
			HasViewShowFlag(showFlags, EEngineShowFlags::SF_BoundingBox))
		{
			outRenderQueueMap[RQT_BoundingBox].Add(&renderInfo);
		}
		if (HasAllRenderFlags(renderFlags, ERenderFlags::RF_Particle))
		{
			outRenderQueueMap[RQT_Particle].Add(&renderInfo);
		}
	}
}

void sortRenderQueueByDistance(TArray<const FRenderInfo*>& renderQueue, const FVector& cameraLocation, const FVector3& cameraForward)
{
	auto compare = [&cameraLocation, &cameraForward](const FRenderInfo* a, const FRenderInfo* b) {
		FVector3 toA = a->GetLocation() - cameraLocation;
		FVector3 toB = b->GetLocation() - cameraLocation;
		float distanceA = FVector3::dot(toA, cameraForward);
		float distanceB = FVector3::dot(toB, cameraForward);
		return distanceA > distanceB; // Sort in descending order of distance
		};


	std::sort(renderQueue.begin(), renderQueue.end(), compare);
}

void FGraphicsManager::RenderSceneView(
	const TArray<FRenderInfo>& scenerRenderInfos,
	const TArray<FRenderInfo>& axisRenderInfos,
	const FSceneView& view,
	const AActor* selectedActor)
{

	if (!view.isValid())
	{
		return;
	}

	mRenderer->BeginView(view.Rect);
	mRenderer->SetViewMode(view.viewMode);
	const FFrustum frustum = FFrustum::FrustumFromViewProjection(view.viewProjectionMatrix);

	// 인스턴스 테스트용(큐브 1만개 출력=
	// Prepare Render queue
	// renderInfos includes primtives, textured primitives, billboard, and gizmo render infos
	// Each render info is splitted into different render queues
	TMap<ERenderQueueType, TArray<const FRenderInfo*>> renderQueueMap;
	updateRenderQueue(scenerRenderInfos, renderQueueMap, &frustum, view.showFlags);
	updateRenderQueue(axisRenderInfos, renderQueueMap, nullptr, view.showFlags);

	sortRenderQueueByDistance(renderQueueMap[RQT_Particle], view.cameraLocation, view.cameraForward);

	// Simple primitives are currently routed through RQT_StaticMesh.
	renderTexturedPrimitive(renderQueueMap[RQT_TexturedPrimitive], view);
	renderStaticMesh(renderQueueMap[RQT_StaticMesh], view);

	renderBillboardText(renderQueueMap[RQT_BillboardText], view);

	// Line Buffer에 넣기전에 Buffer의 용량을 미리 지정하여 동적할당 방지
	CalculateLineBuffer(renderQueueMap[RQT_BoundingBox]);

	//월드 축. 액터 뒤에 그려서 같은 깊이 버퍼로 가려지게 한다 (기즈모와 달리 깊이를 지우지 않는다)
	renderWorldAxis(renderQueueMap[RQT_WorldAxis]);

	if (view.HasShowFlag(EEngineShowFlags::SF_Grid))
	{
		renderGrid(view);
	}
	renderBoundingBox(renderQueueMap[RQT_BoundingBox], view.cameraRotation);
	FlushLines(view);

	renderParticle(renderQueueMap[RQT_Particle], view);

	//강조
	if (selectedActor)
	{
		FRenderInfo clickedRenderInfo;
		selectedActor->GetFirstRenderInfo(clickedRenderInfo);
		renderHighLight(clickedRenderInfo, view);
	}
}

void FGraphicsManager::RenderGizmoView(const TArray<FRenderInfo>& gizmoRenderInfos, const FSceneView& view)
{
	if (!view.isValid() || gizmoRenderInfos.IsEmpty())
	{
		return;
	}

	mRenderer->BeginView(view.Rect);

	TMap<ERenderQueueType, TArray<const FRenderInfo*>> renderQueueMap;

	updateRenderQueue(gizmoRenderInfos, renderQueueMap, nullptr, view.showFlags);
	renderGizmo(renderQueueMap[RQT_Gizmo], view);
}

void FGraphicsManager::renderSimplePrimitive(const TArray<const FRenderInfo*>& renderInfos, const FSceneView& view)
{
	assert(mGpuResourceManagerRef);
	auto& resources = *mGpuResourceManagerRef;

	mRenderer->PrepareSimplePrimitive();
	for (const FRenderInfo* renderInfo : renderInfos)
	{
		FMatrix worldTransform = renderInfo->WorldTransformMatrix;
		mRenderer->UpdateSimpleConstant(worldTransform, view.viewProjectionMatrix, renderInfo->Color);
		const FBuffer* vertexBuffer = resources.FindImmutableBufferOrAdd(renderInfo->MeshName);
		if (vertexBuffer == nullptr)
		{
			UE_LOG(Error, Render, "Vertex buffer not found for primitive type.");
			continue;
		}
		mRenderer->RenderSimplePrimitive(vertexBuffer->Buffer.Get(), vertexBuffer->SourceNum);
	}
}

void FGraphicsManager::renderGizmo(const TArray<const FRenderInfo*>& renderInfos, const FSceneView& view)
{
	assert(mGpuResourceManagerRef);
	auto& resources = *mGpuResourceManagerRef;

	mRenderer->PrepareGizmo();
	for (const FRenderInfo* renderInfo : renderInfos)
	{
		FMatrix worldTransform = renderInfo->WorldTransformMatrix;
		mRenderer->UpdateSimpleConstant(worldTransform, view.viewProjectionMatrix, renderInfo->Color);
		const FBuffer* vertexBuffer = resources.FindImmutableBufferOrAdd(renderInfo->MeshName);
		if (vertexBuffer == nullptr)
		{
			UE_LOG(Error, Render, "Vertex buffer not found for primitive type.");
			continue;
		}
		mRenderer->RenderSimplePrimitive(vertexBuffer->Buffer.Get(), vertexBuffer->SourceNum);
	}
}

void FGraphicsManager::renderParticle(const TArray<const FRenderInfo*>& renderInfos, const FSceneView& view)
{
	assert(mGpuResourceManagerRef);
	auto& resources = *mGpuResourceManagerRef;

	mRenderer->PrepareParticle();

	FVector3 cameraRight = view.cameraRight;
	FVector3 cameraUp = view.cameraUp;
	for (const FRenderInfo* renderInfo : renderInfos)
	{
		//mRenderer->UpdateBillboardConstant(
		//	renderInfo->GetLocation(), renderInfo->GetScale(),
		//	mViewUnifiedProjectionMatrix,
		//	cameraRight, cameraUp,
		//	renderInfo->Color,
		//	renderInfo->SubUVMesh->UVScale, renderInfo->SubUVMesh->UVOffset);
		mRenderer->UpdateParticleConstant(
			renderInfo->GetLocation(), renderInfo->GetScale(),
			view.viewProjectionMatrix,
			cameraRight, cameraUp,
			renderInfo->numRows, renderInfo->numCols,
			renderInfo->currentFrame, renderInfo->nextFrame, renderInfo->frameRatio,
			renderInfo->Color
		);

		mRenderer->UpdateBlendState(renderInfo->BlendStateType);

		auto* texture = resources.FindTextureOrAdd(renderInfo->TextureName);
		if (texture == nullptr)
		{
			UE_LOG(Error, Render, "Primitive texture not found for primitive type.");
			continue;
		}
		mRenderer->RenderParticle(texture);

	}
}

void FGraphicsManager::renderStaticMesh(const  TArray<const FRenderInfo*>& renderInfos, const FSceneView& view)
{
	assert(mGpuResourceManagerRef);
	auto& resources = *mGpuResourceManagerRef;
	auto& assets = *mAssetManagerRef;

	mRenderer->PrepareStaticMesh();
	for (const FRenderInfo* renderInfo : renderInfos)
	{
		if (renderInfo->StaticMesh == nullptr)
		{
			continue;
		}

		FMatrix worldTransform = renderInfo->WorldTransformMatrix;
		mRenderer->UpdateTextureConstant(worldTransform, view.viewProjectionMatrix, renderInfo->Color);

		const FBuffer* buffer = resources.FindImmutableBufferOrAdd(renderInfo->MeshName);
		if (buffer == nullptr)
		{
			UE_LOG(Error, Render, "Static mesh buffer not found.");
			continue;
		}
		// section이 없는 경우, 기존 컴포넌트 텍스처를 사용하여 그린다.
		if (renderInfo->StaticMesh->Sections.IsEmpty())
		{
			ID3D11ShaderResourceView* texture = nullptr;
			if (HasAllRenderFlags(renderInfo->eRenderFlags, ERenderFlags::RF_Texture))
			{
				// TODO: Use the texture from the renderInfo if available
				texture = resources.FindTextureOrAdd(renderInfo->TextureName);
				if (texture == nullptr)
				{
					UE_LOG(Warning, Render, "Primitive texture not found for primitive type. Default white texture is used.");
					texture = resources.FindTextureOrAdd(BuiltinAssets::DefaultWhiteTexture);
				}
			}
			else {
				texture = resources.FindTextureOrAdd(BuiltinAssets::DefaultWhiteTexture);
			}

			//mRenderer->RenderStaticMesh(vertexBuffer->Buffer, vertexBuffer->SourceNum, texture->SRV, texture->Sampler);
			mRenderer->RenderStaticMesh(buffer->Buffer.Get(), buffer->SourceNum,
				texture,
				nullptr,
				nullptr,
				&resources.GetSamplerState(SST_Default),
				buffer->IndexBuffer.Get(), buffer->IndexCount);

			continue;
		}
		// OBJ 메시: 섹션마다 재질과 텍스처를 선택해서 그린다.
		for (const FStaticMeshSection& section : renderInfo->StaticMesh->Sections)
		{

			const FMaterial* material = nullptr;

			ID3D11ShaderResourceView* diffuseTexture = nullptr;
			ID3D11ShaderResourceView* normalTexture = nullptr;
			ID3D11ShaderResourceView* specularTexture = nullptr;

			// Find the material only if the render flags indicate that textures should be used
			if (HasAllRenderFlags(renderInfo->eRenderFlags, ERenderFlags::RF_Texture))
			{
				if (section.MaterialSlotIndex >= 0 && section.MaterialSlotIndex < renderInfo->Materials.Num())
				{
					material = &renderInfo->Materials[section.MaterialSlotIndex];
				}

				if (!material)
				{
					UE_LOG_F(Warning, Render, "Material not found for section {} of static mesh {}. Using default white material.",
						section.Name, renderInfo->MeshName.ToString());

					material = assets.FindMaterialAssetOrNull(BuiltinAssets::DefaultMaterial)->GetMaterial();
				}
			}
			else
			{
				material = assets.FindMaterialAssetOrNull(BuiltinAssets::DefaultMaterial)->GetMaterial();
			}

			if (material->DiffuseTexture.IsValid())
			{
				diffuseTexture = resources.FindTextureOrAdd(material->DiffuseTexture);
			}
			if (material->NormalTexture.IsValid())
			{
				normalTexture = resources.FindTextureOrAdd(material->NormalTexture);
			}

			if (material->SpecularTexture.IsValid())
			{
				specularTexture = resources.FindTextureOrAdd(material->SpecularTexture);
			}


			if (!diffuseTexture && HasAllRenderFlags(renderInfo->eRenderFlags, ERenderFlags::RF_Texture) &&
				renderInfo->TextureName.IsValid())
			{
				diffuseTexture = resources.FindTextureOrAdd(renderInfo->TextureName);
			}

			// 아무 텍스처도 없으면 흰색 텍스처
			if (diffuseTexture == nullptr)
			{
				diffuseTexture = resources.FindTextureOrAdd(BuiltinAssets::DefaultWhiteTexture);
			}

			FLinearColor finalTint = renderInfo->Color;

			if (material)
			{
				finalTint.R *= material->DiffuseColor.x;
				finalTint.G *= material->DiffuseColor.y;
				finalTint.B *= material->DiffuseColor.z;
				finalTint.A = 1.0f;
			}

			// 우선 기존 컴포넌트 색상 유지
			FVector2 uvOffset = renderInfo->SubUVMesh
				? renderInfo->SubUVMesh->UVOffset
				: FVector2(0.0f, 0.0f);
			FVector2 uvScale = renderInfo->SubUVMesh
				? renderInfo->SubUVMesh->UVScale
				: FVector2(1.0f, 1.0f);

			mRenderer->UpdateTextureConstant(
				worldTransform, view.viewProjectionMatrix, finalTint,
				uvScale, uvOffset
			);

			mRenderer->RenderStaticMesh(
				buffer->Buffer.Get(),
				buffer->SourceNum,
				diffuseTexture,
				normalTexture,
				specularTexture,
				&resources.GetSamplerState(SST_Wrap),
				buffer->IndexBuffer.Get(),
				section.IndexCount,
				section.StartIndex);
		}

	}

}

void FGraphicsManager::renderTexturedPrimitive(const TArray<const FRenderInfo*>& renderInfos, const FSceneView& view)
{
	assert(mGpuResourceManagerRef);
	auto& resources = *mGpuResourceManagerRef;

	mRenderer->PrepareTexturedPrimitive();
	for (const FRenderInfo* renderInfo : renderInfos)
	{
		FMatrix worldTransform = renderInfo->WorldTransformMatrix;

		FVector2 uvScale = renderInfo->SubUVMesh
			? renderInfo->SubUVMesh->UVScale
			: FVector2(1.0f, 1.0f);
		FVector2 uvOffset = renderInfo->SubUVMesh
			? renderInfo->SubUVMesh->UVOffset
			: FVector2(0.0f, 0.0f);

		mRenderer->UpdateTextureConstant(
			worldTransform, view.viewProjectionMatrix, renderInfo->Color,
			uvScale, uvOffset
		);
		const FBuffer* buffer = resources.FindImmutableBufferOrAdd(renderInfo->MeshName);
		if (buffer == nullptr)
		{
			UE_LOG(Error, Render, "Error: Textured vertex buffer not found for primitive type.");
			continue;
		}
		auto* texture = resources.FindTextureOrAdd(renderInfo->MeshName);
		if (texture == nullptr)
		{
			UE_LOG(Error, Render, "Error: Primitive texture not found for primitive type.");
			continue;
		}

		mRenderer->RenderTexturePrimitive(
			buffer->Buffer.Get(),
			buffer->SourceNum,
			texture,
			&resources.GetSamplerState(SST_Default),
			buffer->IndexBuffer.Get(), buffer->IndexCount); // 마지막 인수에 전달
	}
}

// 인스턴싱 적용한 심플 프리미티브 출력
//void FGraphicsManager::renderSimplePrimitiveInstanced(const TArray<const FRenderInfo*>& renderInfos, const FCamera& camera)
//{
//	assert(mGpuResourceManagerRef);
//	auto& resources = *mGpuResourceManagerRef;
//
//	if (renderInfos.IsEmpty())
//		return;
//
//	// 같은 프리미티브끼리 World, Tint를 모은다.
//	TMap<FName, TArray<FInstanceData>> batches;
//
//	for (const FRenderInfo* renderInfo : renderInfos)
//	{
//		if (!renderInfo)
//			continue;
//
//		FInstanceData instance{};
//		instance.World = renderInfo->WorldTransformMatrix;
//		instance.Tint = renderInfo->Color;
//
//		batches[renderInfo->MeshName].Add(instance);
//	}
//
//	// 모든 인스턴스가 공유하는 카메라 행렬.
//	// Prepare()에서 계산한 ViewProjection을 사용한다.
//	// 개별 World와 Tint는 위의 인스턴스 배열로 전달한다.
//	mRenderer->PrepareSimpleInstanced();
//	mRenderer->UpdateSimpleConstant(FMatrix::Identity, mViewUnifiedProjectionMatrix, FLinearColor(0, 0, 0, 0));
//
//	//  프리미티브 종류마다 한 번씩 그린다.
//	for (auto& [meshName, instances] : batches)
//	{
//		const FBuffer& buffer = resources.GetInstanceBuffer();
//
//		if (!buffer.Buffer || buffer.SourceNum == 0)
//		{
//			UE_LOG(Error, Render, "Primitive vertex buffer not found.");
//			continue;
//		}
//
//		// 기존 일반 프리미티브는 Draw()용 정점 배열
//		// DrawIndexedInstanced()에 연결하기 위해
//		// 0, 1, 2, ... 순서의 인덱스를 최초 한 번만 생성
//		if (!buffer.IndexBuffer)
//		{
//			TArray<UINT> indices;
//			indices.Reserve(buffer.SourceNum);
//
//			for (UINT i = 0; i < buffer.SourceNum; ++i)
//			{
//				indices.Add(i);
//			}
//
//			buffer.IndexBuffer = buffer.IndexBuffer;
//
//			if (!buffer.IndexBuffer)
//			{
//				UE_LOG(Error, Render, "Primitive index buffer creation failed.");
//				continue;
//			}
//
//			buffer.IndexCount = buffer.SourceNum;
//		}
//
//		const bool success = mRenderer->RenderSimpleInstanced(
//			buffer.Buffer,
//			buffer->IndexBuffer,
//			buffer->IndexCount,
//			&instances[0],
//			static_cast<UINT>(instances.Num()));
//
//		if (!success)
//		{
//			UE_LOG(Error, Render, "Instanced primitive rendering failed.");
//		}
//	}
//}

void FGraphicsManager::renderBillboardText(const TArray<const FRenderInfo*>& renderInfos, const FSceneView& view)
{
	assert(mGpuResourceManagerRef);
	auto& resources = *mGpuResourceManagerRef;

	// Render Billboard Quads
	// TODO: Remove dedicated render path for billboard quads if possible
	//mRenderer->PrepareFont();
	FVector3 cameraRight = view.cameraRight;
	FVector3 cameraUp = view.cameraUp;
	for (const FRenderInfo* renderInfo : renderInfos)
	{
		const FTextMesh* textMesh = renderInfo->Textmesh;

		if (textMesh == nullptr || textMesh->Indices.Num() == 0)
		{
			continue;
		}

		mRenderer->UpdateFontConstant(
			renderInfo->GetLocation(), renderInfo->GetScale(),
			view.viewProjectionMatrix,
			cameraRight, cameraUp,
			renderInfo->Color
		);

		if (textMesh->FontRenderMode == EFontRenderMode::MSDF)
		{
			mRenderer->PrepareUnicodeFont();

			if (!mRenderer->UpdateUnicodeFontBuffer(*textMesh))
			{
				continue;
			}

			mRenderer->RenderUnicodeFontTexture(textMesh->Indices.Num());
		}
		// 기존 ASCII 폰트 방식
		else
		{
			mRenderer->PrepareFont();

			mRenderer->UpdateFontBuffer(
				renderInfo->Textmesh->Vertices, renderInfo->Textmesh->Indices,
				renderInfo->Textmesh->TextNum);

			mRenderer->RenderFontTexture(renderInfo->Textmesh->TextNum);
		}

		/*FMatrix worldTransform = renderInfo->GetTransformMatrix(camera.Rotation);
		mRenderer->UpdateConstant(worldTransform, mViewUnifiedProjectionMatrix, renderInfo->Color);*/
		/*mRenderer->UpdateFontBuffer(
			renderInfo->Textmesh->Vertices, renderInfo->Textmesh->Indices,
			renderInfo->Textmesh->TextNum);
		mRenderer->RenderFontTexture(renderInfo->Textmesh->TextNum);*/
	}
}

void FGraphicsManager::DrawLine(const FVector& start, const FVector& end, const FVector4& color)
{
	assert(mGpuResourceManagerRef);
	auto& resources = *mGpuResourceManagerRef;

	// 월드 좌표 그대로 넣는다. 그래서 그릴 때 World 행렬이 단위행렬이다
	uint32 mStartOffset = mLineVertices.Num();

	mLineVertices.Add({ start.x, start.y, start.z, color.x, color.y, color.z, color.w });
	mLineVertices.Add({ end.x,   end.y,   end.z,   color.x, color.y, color.z, color.w });

	// Index Buffer 업데이트
	mLineIndices.Add(mStartOffset);
	mLineIndices.Add(mStartOffset + 1);
}

void FGraphicsManager::DrawAABBLine(const FBoundingBox& bounds, const FVector4& color)
{
	const FVector3& boundsMin = bounds.min;
	const FVector3& boundsMax = bounds.max;

	const FVector3 corners[8] =
	{
		{ boundsMin.x, boundsMin.y, boundsMin.z },
		{ boundsMax.x, boundsMin.y, boundsMin.z },
		{ boundsMin.x, boundsMax.y, boundsMin.z },
		{ boundsMax.x, boundsMax.y, boundsMin.z },

		{ boundsMin.x, boundsMin.y, boundsMax.z },
		{ boundsMax.x, boundsMin.y, boundsMax.z },
		{ boundsMin.x, boundsMax.y, boundsMax.z },
		{ boundsMax.x, boundsMax.y, boundsMax.z }
	};

	const uint32 baseVertex =
		static_cast<uint32>(mLineVertices.Num());

	for (const FVector3& corner : corners)
	{
		mLineVertices.Add({
			corner.x, corner.y, corner.z,
			color.x, color.y, color.z, color.w
			});
	}

	static constexpr uint32 indices[] =
	{
		0, 1, 1, 3, 3, 2, 2, 0,
		4, 5, 5, 7, 7, 6, 6, 4,
		0, 4, 1, 5, 2, 6, 3, 7
	};

	for (uint32 index : indices)
	{
		mLineIndices.Add(baseVertex + index);
	}
}

void FGraphicsManager::renderWorldAxis(const TArray<const FRenderInfo*>& renderInfos)
{
	// far plane이 100이라 그 안쪽으로 잡아야 잘리지 않는다
	constexpr float AXIS_LENGTH = 50.0f;
	// 세 축이 원점에서 정확히 겹치면 깊이 다툼이 생긴다. 눈에 안 띌 만큼만 띄운다
	constexpr float AXIS_ORIGIN_GAP = 0.01f;
	// 음의 방향은 어둡게 깔아 +쪽과 구분한다 (언리얼 에디터와 같은 처리)
	constexpr float NEGATIVE_DIM = 0.25f;

	const FVector axisDirections[3] =
	{
		FVector(1.0f, 0.0f, 0.0f),
		FVector(0.0f, 1.0f, 0.0f),
		FVector(0.0f, 0.0f, 1.0f),
	};
	const FVector4 axisColors[3] =
	{
		FVector4(1.0f, 0.0f, 0.0f, 1.0f),   // X = 빨강
		FVector4(0.0f, 1.0f, 0.0f, 1.0f),   // Y = 초록
		FVector4(0.0f, 0.4f, 1.0f, 1.0f),   // Z = 파랑
	};

	for (const FRenderInfo* renderInfo : renderInfos)
	{
		for (int32 i = 0; i < 3; ++i)
		{
			const FVector& direction = axisDirections[i];
			const FVector4& color = axisColors[i];
			const FVector4 dimColor(
				color.x * NEGATIVE_DIM,
				color.y * NEGATIVE_DIM,
				color.z * NEGATIVE_DIM,
				color.w);

			DrawLine(direction * AXIS_ORIGIN_GAP, direction * AXIS_LENGTH, color);
			DrawLine(direction * -AXIS_ORIGIN_GAP, direction * -AXIS_LENGTH, dimColor);
		}
	}
}

void FGraphicsManager::renderGrid(const FSceneView& view)
{
	int LineCount = (mgridExtent / 2) / mgridSpacing;
	for (int32 i = -LineCount; i <= LineCount;i++)
	{
		float Spaceline = i * mgridSpacing;
		if (view.HasShowFlag(EEngineShowFlags::SF_WorldAxis)) {
			if (Spaceline == 0) continue;
		}
		DrawLine(FVector3(Spaceline, -mgridExtent / 2.0f, 0), FVector3(Spaceline, mgridExtent / 2.0f, 0), FVector4(0.3f, 0.3f, 0.3f, 1.0f));  // Y축 기준 Grid
		DrawLine(FVector3(-mgridExtent / 2.0f, Spaceline, 0), FVector3(mgridExtent / 2.0f, Spaceline, 0), FVector4(0.3f, 0.3f, 0.3f, 1.0f)); // X축 기준 Grid
	}
}

void FGraphicsManager::renderBoundingBox(const TArray<const FRenderInfo*>& renderInfos, const FRotator& cameraRotation)
{
	assert(mGpuResourceManagerRef);
	auto& resources = *mGpuResourceManagerRef;

	for (const FRenderInfo* renderInfo : renderInfos)
	{
		// Billboard는 카메라 회전이 실제 렌더 행렬에 포함되므로(카메라 방향에 따라 월드 변환이 바뀜)
		// 현재 카메라 기준으로 WorldBounds를 갱신
		const FBoundingBox bounds = renderInfo->MeshName == BuiltinAssets::BillboardQuadTextured
			? TransformBoundingBox(
				renderInfo->LocalBounds,
				renderInfo->GetTransformMatrix(cameraRotation))
			: renderInfo->WorldBounds;

		DrawAABBLine(
			bounds,
			FVector4(1.0f, 1.0f, 1.0f, 1.0f));
	}
}

void FGraphicsManager::FlushLines(const FSceneView& view)
{
	if (mLineVertices.Num() == 0) return;

	mRenderer->PrepareLine();

	// 선분 좌표가 이미 월드 공간이라 World는 단위행렬.
	// Tint.a = 0 이면 셰이더의 lerp가 정점 색을 그대로 통과시킨다
	mRenderer->UpdateSimpleConstant(FMatrix::Identity, view.viewProjectionMatrix, FLinearColor(0, 0, 0, 0));
	mRenderer->RenderLines(&mLineVertices[0], mLineVertices.Num(), &mLineIndices[0], mLineIndices.Num());

	// 안 비우면 매 프레임 누적돼 버퍼가 넘친다. 용량은 유지한 채 개수만 0으로
	mLineVertices.Reset(0);
	mLineIndices.Reset(0);
}

void FGraphicsManager::RenderLoadingScreen()
{
	assert(mGpuResourceManagerRef);
	auto& resources = *mGpuResourceManagerRef;

	GetRenderer()->PrepareForUI();

	auto* texture = resources.FindTextureOrAdd(BuiltinAssets::LoadingScreenTexture);
	GetRenderer()->RenderFullscreenTexture(texture);
	Display();
}

void FGraphicsManager::Display()
{
	//mRenderer->RenderFontTexture(
	//	FMatrix::Identity,
	//	FMatrix::Identity
	//);
	mRenderer->SwapBuffer();
}

void FGraphicsManager::Update(float deltaTime)
{

	// 테스트용: deltaTime이 초 단위라는 전제
	//static float elapsed = 0.0f;
	//static size_t index = 0;

	//static std::string testTexts[] = {
	//	"ABC",
	//	"XYZ",
	//	"ABCDEFGHIJKLMNOPQRSTUVWXYZ",
	//	"Hi",
	//	"",
	//	"Back!",
	//	"sdffffffffffffffffffffffffffffffffffffffffffffff"
	//};

	//elapsed += deltaTime;

	//if (elapsed >= 1.0f)
	//{
	//	elapsed = 0.0f;

	//	if (!mRenderer->CreateFontAtlasQuad(&testTexts[index]))
	//	{
	//		UE_LOG(Error, Render, "Failed to update font text.");
	//	}

	//	index = (index + 1)
	//		% (sizeof(testTexts) / sizeof(testTexts[0]));
	//}
}

URenderer* FGraphicsManager::GetRenderer() const
{
	assert(mRenderer != nullptr);

	return mRenderer.get();
}

FVector FGraphicsManager::GetPrimitiveCenter(FName meshName)
{
	if (meshName == BuiltinAssets::SphereMesh)
	{
		return FVector(0, 0, 0);
	}

	return FVector(0, 0, 0);
}

FVector GetMeshCenter(FBoundingBox bounds)
{
	return (bounds.min + bounds.max) * 0.5f;
}

// 테두리가 화면에서 차지할 두께(픽셀). 물체 크기와 카메라 거리 어느 쪽에도 영향받지 않는다.
static constexpr float OUTLINE_PIXELS = 3.0f;

// 월드 공간 반지름이 worldHalfExtent인 축을 worldThickness 만큼 키우는 배율
static float GetOutlineAxisScale(float worldHalfExtent, float worldThickness)
{
	if (worldHalfExtent <= SMALL_NUMBER)
	{
		return 1.0f;   // 납작하게 눌린 축은 건드리지 않는다. 안 그러면 배율이 발산한다
	}

	return 1.0f + worldThickness / worldHalfExtent;
}

FVector FGraphicsManager::GetPrimitiveHalfExtent(FName meshName)
{
	if (meshName == BuiltinAssets::SphereMesh)
	{
		return FVector(1.0f, 1.0f, 1.0f);
	}
	else if (meshName == BuiltinAssets::CubeMesh)
	{
		return FVector(0.5f, 0.5f, 0.5f);
	}
	else
	{
		return FVector(0.5f, 0.5f, 0.5f);
	}
}

FVector GetMeshHalfExtent(FBoundingBox bounds)
{
	return (bounds.max - bounds.min) * 0.5f;
}

float FGraphicsManager::GetGridWidth() const
{
	return mgridSpacing;
}

void FGraphicsManager::SetGridWidth(float width)
{
	mgridSpacing = width;
}

void FGraphicsManager::renderHighLight(
	const FRenderInfo& renderInfo,
	const FSceneView& view)
{
	assert(mGpuResourceManagerRef);

	if (!view.isValid())
	{
		return;
	}

	// Don't render highlight if the render info is a billboard
	if (HasAllRenderFlags(renderInfo.eRenderFlags, ERenderFlags::RF_Billboard))
	{
		return;
	}

	auto& resources = *mGpuResourceManagerRef;
	const FBuffer* buffer =
		resources.FindImmutableBufferOrAdd(renderInfo.MeshName);

	if (buffer == nullptr)
	{
		UE_LOG(Error, Render, "Highlight mesh buffer not found.");
		return;
	}

	mRenderer->PrepareHighlight();

	const FMatrix worldTransform =
		renderInfo.GetTransformMatrix(view.cameraRotation);

	int viewMin[2] = {
		static_cast<int>(std::floor(view.Rect.X)),
		static_cast<int>(std::floor(view.Rect.Y))
	};
	int viewMax[2] = {
		static_cast<int>(std::ceil(view.Rect.X + view.Rect.Width)),
		static_cast<int>(std::ceil(view.Rect.Y + view.Rect.Height))
	};

	mRenderer->RenderHighlight(
		buffer->Buffer.Get(),
		buffer->SourceNum,
		view.viewProjectionMatrix,
		worldTransform,
		viewMin,
		viewMax,
		buffer->IndexBuffer.Get(),
		buffer->IndexCount);
}

bool FGraphicsManager::HasShowFlag(EEngineShowFlags flag) const
{
	return (mShowFlags & static_cast<uint32>(flag)) != 0;
}

void FGraphicsManager::SetShowFlag(
	EEngineShowFlags flag,
	bool bEnable)
{
	const uint32 flagValue = static_cast<uint32>(flag);

	if (bEnable)
	{
		mShowFlags |= flagValue;
	}
	else
	{
		mShowFlags &= ~flagValue;
	}
}

void FGraphicsManager::SetViewMode(EViewModeIndex viewMode)
{
	mViewMode = viewMode;
	mbWireFrame =
		viewMode == EViewModeIndex::VMI_Wireframe;
}

void FGraphicsManager::CalculateLineBuffer(
	const TArray<const FRenderInfo*>& renderInfos)
{
	const uint32 gridElementCount = static_cast<uint32>(
		(mgridExtent / mgridSpacing) * 4.0f);
	const uint32 renderInfoCount =
		static_cast<uint32>(renderInfos.Num());

	mLineIndices.Reserve(
		6 + gridElementCount + renderInfoCount * 24);
	mLineVertices.Reserve(
		6 + gridElementCount + renderInfoCount * 8);
}
