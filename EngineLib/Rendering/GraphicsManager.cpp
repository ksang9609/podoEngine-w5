#include "GraphicsManager.h"

#include <algorithm>

#include "ThirdParty/Meshoptimizer/meshoptimizer.h"

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

namespace
{
	// TODO: rename
	int32 generateMeshLod(UStaticMesh& staticMesh, const FSceneView& view,
		float screenSize, float radius,
		FGpuResourceManager& gpuResourceManager)
	{
		if (radius <= 0.0001f)
		{
			radius = 1.0f;
		}
		if (screenSize >= 0.35f)
		{
			return 0; // No need to generate LODs for large screen sizes
		}

		constexpr float maxScreenSizes[] = { 0.35f, 0.15f, 0.06f, 0.02f, 0.01f };
		constexpr float minScreenSizes[] = { 0.15f, 0.06f, 0.02f, 0.01f, 0.0f };
		constexpr float reductionRatios[] = { 0.5f, 0.25f, 0.125f, 0.05f, 0.01f };

		const FStaticMeshLOD& baseLOD = staticMesh.GetStaticMeshAsset()->LODs[0];
		if (baseLOD.Vertices.IsEmpty() || baseLOD.Indices.IsEmpty())
		{
			return 0;
		}

		float simplifyScale = meshopt_simplifyScale(
			&baseLOD.Vertices[0].pos.x,
			baseLOD.Vertices.Num(),
			sizeof(FNormalVertex)
		);
		if (simplifyScale <= 0.0f)
		{
			simplifyScale = 1.0f;
		}

		const float pixelError = 2.0f;
		const float viewHeight = (view.Rect.Height > 0.0f) ? view.Rect.Height : 720.0f;

		// Generate LODs up to 4 levels (LOD 0, 1, 2, 3)
		int32 startLod = staticMesh.GetLODCount() - 1;
		for (int32 i = startLod; i < 3; ++i)
		{
			float targetError =
				(2.0f * radius * pixelError) /
				(simplifyScale * viewHeight * maxScreenSizes[i]);

			if (!staticMesh.GenerateLOD(reductionRatios[i], minScreenSizes[i], targetError))
			{
				break;
			}

			const FStaticMeshLOD& newLOD = staticMesh.GetStaticMeshAsset()->LODs.Last();
			gpuResourceManager.CreateBuffer(
				newLOD.BufferKey,
				newLOD.Vertices,
				newLOD.Indices);

			if (screenSize >= minScreenSizes[i])
			{
				return staticMesh.GetLODCount() - 1;
			}	
		}
		return staticMesh.GetLODCount() - 1;
	}

	int32 calculateMeshLODIndex(const FRenderInfo* renderInfo, const FSceneView& view, FGpuResourceManager& gpuResourceManager)
	{
		const FStaticMesh* staticMesh = renderInfo->StaticMeshAsset->GetStaticMeshAsset();
		if (!staticMesh || staticMesh->LODs.IsEmpty())
		{
			return 0;
		}

		// Get distance between camera and object
		const FVector3 objectPos = renderInfo->GetLocation();
		const FVector3 cameraPos = view.cameraLocation;

		float distance = (objectPos - cameraPos).Length();
		if (distance <= 0.0001f)
		{
			return 0;
		}
		// Get Radius from object's bounding box
		float radius = ((renderInfo->WorldBounds.max - renderInfo->WorldBounds.min) * 0.5f).Length();
		if (radius <= 0.0001f)
		{
			radius = 1.0f;
		}

		// Get tan(FOV / 2) from Projection matrix[1][1]
		float projScale = view.projectionMatrix.M[1][1] * view.orthoDistance; // 1 / tan(FOV / 2)

		// Calculate object's screen size
		float screenSize = (radius * projScale) / distance;

		for (int32 i = 0; i < staticMesh->LODs.Num(); ++i)
		{
			if (screenSize >= staticMesh->LODs[i].ScreenSize)
			{
				return i;
			}
		}

		// If no LOD matches and we already generated max LODs, return the lowest detail LOD
		if (staticMesh->LODs.Num() >= 4)
		{
			return staticMesh->LODs.Num() - 1;
		}

		// If no LOD matches, generate new LODs if possible
		UStaticMesh* mutableStaticMesh = const_cast<UStaticMesh*>(renderInfo->StaticMeshAsset);
		return generateMeshLod(*mutableStaticMesh, view,
			screenSize, radius,
			gpuResourceManager);
	}

	void sortStaticMeshRenderQueue(
		const TArray<const FRenderInfo*>& renderQueue,
		TArray<FStaticMeshRenderQueueEntry>& outSortedQueue,
		const FSceneView& view,
		const UMaterial& defaultMaterialAsset,
		FGpuResourceManager& gpuResourceManager)
	{
		outSortedQueue.Reset(0);
		const int32 totalQueueCount = renderQueue.Num();
		if (totalQueueCount == 0)
		{
			return;
		}

		outSortedQueue.Reserve(totalQueueCount);

		const FVector3 cameraPos = view.cameraLocation;
		const float projScale = view.projectionMatrix.M[1][1] * view.orthoDistance;

		// Fast path for homogeneous scene (e.g. 50,000 apple cubes with same mesh & material)
		// O(N) Counting Sort: ZERO std::sort overhead, ZERO sqrtf, ZERO divisions!
		const UStaticMesh* firstStaticMeshAsset = renderQueue[0]->StaticMeshAsset;
		const UMaterial* firstMaterialAsset = renderQueue[0]->Materials.Num() > 0 ? renderQueue[0]->Materials[0] : &defaultMaterialAsset;
		bool bSingleAssetAndMaterial = true;

		// Ensure primary mesh has generated LODs up to 4 levels (runs once on startup)
		if (firstStaticMeshAsset)
		{
			const FStaticMesh* sm = firstStaticMeshAsset->GetStaticMeshAsset();
			if (sm && sm->LODs.Num() < 4)
			{
				float r = ((renderQueue[0]->WorldBounds.max - renderQueue[0]->WorldBounds.min) * 0.5f).Length();
				UStaticMesh* mutableMesh = const_cast<UStaticMesh*>(firstStaticMeshAsset);
				generateMeshLod(*mutableMesh, view, 0.05f, r, gpuResourceManager);
			}
		}

		constexpr int32 MAX_LODS = 8;
		float thresholdDistSq[MAX_LODS] = {};
		int32 primaryLodCount = 0;
		const FStaticMesh* primaryMesh = firstStaticMeshAsset ? firstStaticMeshAsset->GetStaticMeshAsset() : nullptr;

		if (primaryMesh)
		{
			primaryLodCount = (std::min)(primaryMesh->LODs.Num(), MAX_LODS);
			float r = ((renderQueue[0]->WorldBounds.max - renderQueue[0]->WorldBounds.min) * 0.5f).Length();
			if (r <= 0.0001f) r = 1.0f;

			for (int32 i = 0; i < primaryLodCount; ++i)
			{
				float screenSizeThreshold = primaryMesh->LODs[i].ScreenSize;
				if (screenSizeThreshold > 0.00001f)
				{
					float maxDist = (r * projScale) / screenSizeThreshold;
					thresholdDistSq[i] = maxDist * maxDist;
				}
				else
				{
					thresholdDistSq[i] = 1e18f;
				}
			}
		}

		if (primaryMesh && primaryLodCount > 0)
		{
			// Precompute base entry template for each LOD level
			FStaticMeshRenderQueueEntry templateEntries[MAX_LODS];
			const FMaterial* mat = firstMaterialAsset ? firstMaterialAsset->GetMaterial() : defaultMaterialAsset.GetMaterial();
			const uint32 materialKey = static_cast<uint32>(firstMaterialAsset ? firstMaterialAsset->UUID : defaultMaterialAsset.UUID);

			for (int32 i = 0; i < primaryLodCount; ++i)
			{
				const FStaticMeshLOD* meshLod = &primaryMesh->LODs[i];
				const uint32 meshKey = meshLod->BufferKey.GetDisplayId().ToUnstableInt();
				templateEntries[i].SortKey = FSortKey(0, 0, 0, materialKey, meshKey, true);
				templateEntries[i].StaticMeshLOD = meshLod;
				templateEntries[i].Material = mat;
				if (!meshLod->Sections.IsEmpty())
				{
					templateEntries[i].StartIndex = meshLod->Sections[0].StartIndex;
					templateEntries[i].IndexCount = meshLod->Sections[0].IndexCount;
				}
			}

			// Pass 1: Classify each object into LOD, count occurrences, and verify scene homogeneity
			static std::vector<uint8> sLodIndices;
			if (sLodIndices.size() < static_cast<size_t>(totalQueueCount))
			{
				sLodIndices.resize(totalQueueCount);
			}

			int32 lodCounts[MAX_LODS] = {};
			const float th0 = thresholdDistSq[0];
			const float th1 = thresholdDistSq[1];
			const float th2 = thresholdDistSq[2];

			for (int32 k = 0; k < totalQueueCount; ++k)
			{
				const FRenderInfo* info = renderQueue[k];
				if (info->StaticMeshAsset != firstStaticMeshAsset) [[unlikely]]
				{
					bSingleAssetAndMaterial = false;
					break;
				}

				const FVector3 pos = info->GetLocation();
				const float dx = pos.x - cameraPos.x;
				const float dy = pos.y - cameraPos.y;
				const float dz = pos.z - cameraPos.z;
				const float distSq = dx * dx + dy * dy + dz * dz;

				// Branchless LOD calculation (for primaryLodCount == 4)
				uint8 lodIdx = static_cast<uint8>((distSq > th0) + (distSq > th1) + (distSq > th2));
				if (lodIdx >= primaryLodCount) [[unlikely]]
				{
					lodIdx = static_cast<uint8>(primaryLodCount - 1);
				}
				sLodIndices[k] = lodIdx;
				lodCounts[lodIdx]++;
			}

			if (bSingleAssetAndMaterial)
			{
				// Compute prefix sums for destination offsets
				int32 lodOffsets[MAX_LODS] = {};
				int32 lodStarts[MAX_LODS] = {};
				lodOffsets[0] = 0;
				lodStarts[0] = 0;
				for (int32 i = 1; i < primaryLodCount; ++i)
				{
					lodOffsets[i] = lodOffsets[i - 1] + lodCounts[i - 1];
					lodStarts[i] = lodOffsets[i];
				}

				// Pass 2: Place only 8-byte RenderInfo pointer into sorted positions (eliminates 40-byte scattered writes)
				outSortedQueue.SetNum(totalQueueCount);
				for (int32 k = 0; k < totalQueueCount; ++k)
				{
					const uint8 lodIdx = sLodIndices[k];
					const int32 destIndex = lodOffsets[lodIdx]++;
					outSortedQueue[destIndex].RenderInfo = renderQueue[k];
				}

				// Sequential linear fill of template attributes per LOD bucket (L1 Cache Streaming friendly)
				for (int32 lod = 0; lod < primaryLodCount; ++lod)
				{
					const int32 start = lodStarts[lod];
					const int32 end = lodOffsets[lod];
					const auto& tmpl = templateEntries[lod];
					for (int32 idx = start; idx < end; ++idx)
					{
						outSortedQueue[idx].SortKey = tmpl.SortKey;
						outSortedQueue[idx].StartIndex = tmpl.StartIndex;
						outSortedQueue[idx].IndexCount = tmpl.IndexCount;
						outSortedQueue[idx].StaticMeshLOD = tmpl.StaticMeshLOD;
						outSortedQueue[idx].Material = tmpl.Material;
					}
				}
				return;
			}
		}

		// Fallback for heterogeneous scenes
		const UStaticMesh* lastStaticMeshAsset = nullptr;
		const FStaticMesh* lastStaticMesh = nullptr;

		const UMaterial* lastMaterialAsset = nullptr;
		const FMaterial* lastMaterial = nullptr;
		FSortKey lastSortKey;
		ERenderFlags lastFlags = ERenderFlags::RF_None;
		uint32 lastMeshKey = 0;

		for (const FRenderInfo* renderInfo : renderQueue)
		{
			assert(renderInfo->StaticMeshAsset != nullptr);

			if (renderInfo->StaticMeshAsset != lastStaticMeshAsset)
			{
				lastStaticMeshAsset = renderInfo->StaticMeshAsset;
				lastStaticMesh = lastStaticMeshAsset->GetStaticMeshAsset();
				assert(lastStaticMesh != nullptr);
			}

			const int32 meshLodIndex = calculateMeshLODIndex(renderInfo, view, gpuResourceManager);
			const FStaticMeshLOD* meshLod = &lastStaticMesh->LODs[meshLodIndex];
			const uint32 meshKey = meshLod->BufferKey.GetDisplayId().ToUnstableInt();

			const TArray<FStaticMeshSection>& sections = meshLod->Sections;
			const TArray<const UMaterial*>& materials = renderInfo->Materials;
			const int32 sectionCount = sections.Num();

			for (int32 sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex)
			{
				const UMaterial* materialAsset = nullptr;
				if (HasAllRenderFlags(renderInfo->eRenderFlags, ERenderFlags::RF_Texture))
				{
					materialAsset = materials[sectionIndex];
				}
				else
				{
					materialAsset = &defaultMaterialAsset;
				}

				FSortKey sortKey;
				const FMaterial* mat = nullptr;

				if (materialAsset == lastMaterialAsset && meshKey == lastMeshKey && renderInfo->eRenderFlags == lastFlags)
				{
					sortKey = lastSortKey;
					mat = lastMaterial;
				}
				else
				{
					lastMaterialAsset = materialAsset;
					lastMaterial = materialAsset->GetMaterial();
					lastFlags = renderInfo->eRenderFlags;
					mat = lastMaterial;

					const uint32 materialKey = static_cast<uint32>(materialAsset->UUID);
					constexpr uint32 passKey = 0;
					constexpr uint32 depthKey = 0;
					constexpr uint32 pipelineKey = 0;
					lastSortKey = FSortKey(passKey, depthKey, pipelineKey, materialKey, meshKey, true);
					sortKey = lastSortKey;
				}

				FStaticMeshRenderQueueEntry entry;
				entry.SortKey = sortKey;
				entry.RenderInfo = renderInfo;
				entry.StartIndex = sections[sectionIndex].StartIndex;
				entry.IndexCount = sections[sectionIndex].IndexCount;
				entry.StaticMeshLOD = meshLod;
				entry.Material = mat;

				outSortedQueue.Add(entry);
			}
		}

		if (outSortedQueue.Num() > 1)
		{
			bool bNeedsSort = false;
			const uint64 firstKey = outSortedQueue[0].SortKey.Key;
			for (const auto& entry : outSortedQueue)
			{
				if (entry.SortKey.Key != firstKey)
				{
					bNeedsSort = true;
					break;
				}
			}

			if (bNeedsSort)
			{
				std::sort(outSortedQueue.begin(), outSortedQueue.end(),
					[](const FStaticMeshRenderQueueEntry& a, const FStaticMeshRenderQueueEntry& b) -> bool {
						return a.SortKey.Key < b.SortKey.Key;
					});
			}
		}
	}
}

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
	const TArray<const FRenderInfo*>& renderInfos,
	const TArray<uint32>* objectIndices,
	TMap<ERenderQueueType, TArray<const FRenderInfo*>>& outRenderQueueMap,
	const FFrustum* frustum, uint32 showFlags
	)
{
	const int32 count = objectIndices ? objectIndices->Num() : renderInfos.Num();
	if (count == 0) return;

	auto& staticMeshQueue = outRenderQueueMap[RQT_StaticMesh];
	auto& billboardQueue = outRenderQueueMap[RQT_BillboardText];
	auto& worldAxisQueue = outRenderQueueMap[RQT_WorldAxis];
	auto& gizmoQueue = outRenderQueueMap[RQT_Gizmo];
	auto& boundingBoxQueue = outRenderQueueMap[RQT_BoundingBox];
	auto& particleQueue = outRenderQueueMap[RQT_Particle];

	staticMeshQueue.Reserve(staticMeshQueue.Num() + count);

	for (int32 k = 0; k < count; k++)
	{
		const FRenderInfo* renderInfo = objectIndices ? renderInfos[(*objectIndices)[k]] : renderInfos[k];

		if (frustum != nullptr)
		{
			if (!frustum->Intersects(renderInfo->WorldBounds))
			{
				continue;
			}
		}

		ERenderFlags renderFlags = renderInfo->eRenderFlags;

		if (HasAllRenderFlags(renderFlags, ERenderFlags::RF_Primitive) &&
			!HasAnyRenderFlags(renderFlags, ERenderFlags::RF_Billboard) &&
			HasViewShowFlag(showFlags, EEngineShowFlags::SF_Primitives))
		{
			staticMeshQueue.Add(renderInfo);
		}
		else if (HasAllRenderFlags(renderFlags,
			ERenderFlags::RF_Billboard | ERenderFlags::RF_Text) &&
			HasViewShowFlag(showFlags, EEngineShowFlags::SF_BillboardText))
		{
			billboardQueue.Add(renderInfo);
		}
		else if (HasAllRenderFlags(renderFlags, ERenderFlags::RF_WorldAxis) &&
			HasViewShowFlag(showFlags, EEngineShowFlags::SF_WorldAxis))
		{
			worldAxisQueue.Add(renderInfo);
		}
		else if (HasAllRenderFlags(renderFlags, ERenderFlags::RF_Gizmo))
		{
			gizmoQueue.Add(renderInfo);
		}
		else if (HasAllRenderFlags(renderFlags, ERenderFlags::RF_BoundingBox) &&
			HasViewShowFlag(showFlags, EEngineShowFlags::SF_BoundingBox))
		{
			boundingBoxQueue.Add(renderInfo);
		}
		else if (HasAllRenderFlags(renderFlags, ERenderFlags::RF_Particle))
		{
			particleQueue.Add(renderInfo);
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
	const TArray<const FRenderInfo*>& scenerRenderInfos,
	const TArray<const FRenderInfo*>& axisRenderInfos,
	const FSceneView& view,
	const AActor* selectedActor,
	const FOctree & octree)
{

	if (!view.isValid())
	{
		return;
	}

	if (view.HiZBuffer && !view.HiZBuffer->IsInitialized())
	{
		view.HiZBuffer->Initialize(mRenderer->GetDevice());
	}
		

	mRenderer->BeginView(view.Rect);
	mRenderer->SetViewMode(view.viewMode);
	const FFrustum frustum = FFrustum::FrustumFromViewProjection(view.viewProjectionMatrix);

	octree.FrustumCull(frustum, view.cameraLocation, mCullInside, mCullIntersect);

	for (uint32 idx : mCullIntersect)
	{
		const FRenderInfo* renderInfo = scenerRenderInfos[idx];
		if (frustum.InterSectsSIMD(renderInfo->WorldBounds))
		{
			mCullInside.Add(idx);
		}
	}
	// Prepare Render queue
	mRenderQueueMap[RQT_SimplePrimitive].Reset(0);
	mRenderQueueMap[RQT_TexturedPrimitive].Reset(0);
	mRenderQueueMap[RQT_BillboardText].Reset(0);
	mRenderQueueMap[RQT_WorldAxis].Reset(0);
	mRenderQueueMap[RQT_Gizmo].Reset(0);
	mRenderQueueMap[RQT_BoundingBox].Reset(0);
	mRenderQueueMap[RQT_Particle].Reset(0);
	mRenderQueueMap[RQT_StaticMesh].Reset(0);

	updateRenderQueue(scenerRenderInfos, &mCullInside, mRenderQueueMap, nullptr, view.showFlags);
	//updateRenderQueue(scenerRenderInfos, &mCullIntersect, mRenderQueueMap, &frustum, view.showFlags);
	updateRenderQueue(axisRenderInfos, nullptr, mRenderQueueMap, nullptr, view.showFlags);

	sortRenderQueueByDistance(mRenderQueueMap[RQT_Particle], view.cameraLocation, view.cameraForward);

	// Simple primitives are currently routed through RQT_StaticMesh.
	renderTexturedPrimitive(mRenderQueueMap[RQT_TexturedPrimitive], view);
	renderStaticMesh(mRenderQueueMap[RQT_StaticMesh], view);

	renderBillboardText(mRenderQueueMap[RQT_BillboardText], view);

	// Line Buffer에 넣기전에 Buffer의 용량을 미리 지정하여 동적할당 방지
	CalculateLineBuffer(mRenderQueueMap[RQT_BoundingBox]);

	//월드 축. 액터 뒤에 그려서 같은 깊이 버퍼로 가려지게 한다 (기즈모와 달리 깊이를 지우지 않는다)
	renderWorldAxis(mRenderQueueMap[RQT_WorldAxis]);

	if (view.HasShowFlag(EEngineShowFlags::SF_Grid))
	{
		renderGrid(view);
	}
	renderBoundingBox(mRenderQueueMap[RQT_BoundingBox], view.cameraRotation);
	FlushLines(view);

	renderParticle(mRenderQueueMap[RQT_Particle], view);

	//강조
	if (selectedActor)
	{
		FRenderInfo clickedRenderInfo;
		selectedActor->GetFirstRenderInfo(clickedRenderInfo);
		renderHighLight(clickedRenderInfo, view);
	}

	if (mbEnableHiZ)
	{
		ID3D11DeviceContext* context = mRenderer->GetDeviceContext();
		ID3D11Device* device = mRenderer->GetDevice();
		ID3D11ShaderResourceView* depthSRV = mRenderer->GetDepthBufferSRV();
		const TArray<const FRenderInfo*>& staticMeshQueue = mRenderQueueMap[RQT_StaticMesh];
		D3D11_VIEWPORT totalVP = mRenderer->GetViewportInfo();

		if (depthSRV && staticMeshQueue.Num() > 0)
		{
			// To prevent hazard
			ID3D11RenderTargetView* nullRTV = nullptr;
			context->OMSetRenderTargets(1, &nullRTV, nullptr);

			// Create Mipmap
			view.HiZBuffer->BuildHiZ(
				context,
				depthSRV,
				view.Rect.X,
				view.Rect.Y,
				view.Rect.Width,
				view.Rect.Height,
				totalVP.Width,
				totalVP.Height
			);

			// Copy staging for next frame
			view.HiZBuffer->ExecuteOcclusionCull(
				context,
				device,
				staticMeshQueue,
				view.viewProjectionMatrix,
				(float)view.Rect.Width,
				(float)view.Rect.Height,
				view.nearZ
			);

			// Restore RTV
			ID3D11RenderTargetView* mainRTV = mRenderer->GetFrameBufferRTV();
			ID3D11DepthStencilView* mainDSV = mRenderer->GetDepthStencilView();
			context->OMSetRenderTargets(1, &mainRTV, mainDSV);
		}
	}
}

void FGraphicsManager::RenderGizmoView(const TArray<const FRenderInfo*>& gizmoRenderInfos, const FSceneView& view)
{
	if (!view.isValid() || gizmoRenderInfos.IsEmpty())
	{
		return;
	}

	mRenderer->BeginView(view.Rect);

	mRenderQueueMap[RQT_Gizmo].Reset(0);

	updateRenderQueue(gizmoRenderInfos, nullptr, mRenderQueueMap, nullptr, view.showFlags);
	renderGizmo(mRenderQueueMap[RQT_Gizmo], view);
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
			*renderInfo->numRows, *renderInfo->numCols,
			*renderInfo->currentFrame, *renderInfo->nextFrame, *renderInfo->frameRatio,
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

void FGraphicsManager::renderStaticMesh(const TArray<const FRenderInfo*>& renderInfos, const FSceneView& view)
{
	if (renderInfos.Num() == 0)
	{
		return;
	}

	if (mbEnableHiZ)
	{
		mVisibleRenderInfos.Reset(0);
		hiZOcclusionCulling(view.HiZBuffer, renderInfos, mVisibleRenderInfos);
	}

	assert(mGpuResourceManagerRef);
	auto& resources = *mGpuResourceManagerRef;
	auto& assets = *mAssetManagerRef;

	mRenderer->PrepareStaticMesh();

	// Sort the renderInfos based on criteria
	mStaticMeshRenderQueue.Reset(0);

	const UMaterial* defaultMaterialAsset = assets.FindMaterialAssetOrNull(BuiltinAssets::DefaultMaterial);
	assert(defaultMaterialAsset != nullptr);
	sortStaticMeshRenderQueue((mbEnableHiZ ? mVisibleRenderInfos : renderInfos),
		mStaticMeshRenderQueue,
		view,
		*defaultMaterialAsset,
		*mGpuResourceManagerRef);

	// Update frame constants (Slot 0) once per frame
	mRenderer->UpdateFrameConstant(view.viewProjectionMatrix);

	ID3D11DeviceContext* context = mRenderer->GetDeviceContext();
	ID3D11DeviceContext1* context1 = mRenderer->GetDeviceContext1();
	ID3D11Buffer* perObjectCB = mRenderer->GetPerObjectConstantBuffer();

	const uint32 totalObjects = mStaticMeshRenderQueue.Num();
	constexpr uint32 CHUNK_SIZE = 256;

	// Cache last used material and mesh to minimize state changes
	const FStaticMeshLOD* lastUsedMeshLOD = nullptr;
	const FMaterial* lastUsedMaterial = nullptr;
	const FBuffer* lastUsedBuffer = nullptr;
	bool bHasIndexBuffer = false;
	UINT currentIndexCount = 0;
	UINT currentStartIndex = 0;
	constexpr UINT kNumConstants = 16;

	for (uint32 chunkStart = 0; chunkStart < totalObjects; chunkStart += CHUNK_SIZE)
	{
		const uint32 currentBatchCount = (std::min)(CHUNK_SIZE, totalObjects - chunkStart);
		const FStaticMeshRenderQueueEntry* pChunk = &mStaticMeshRenderQueue[chunkStart];

		// Batch Map (1 Map per 256 objects instead of 256 Maps)
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (SUCCEEDED(context->Map(perObjectCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		{
			FPerObjectConstants* dst = static_cast<FPerObjectConstants*>(mapped.pData);
			const FMaterial* chunkMaterial = pChunk[0].Material;
			const FVector3 matDiffuse = chunkMaterial ? chunkMaterial->DiffuseColor : FVector3(1.0f, 1.0f, 1.0f);
			const bool bDefaultDiffuse = (matDiffuse.x == 1.0f && matDiffuse.y == 1.0f && matDiffuse.z == 1.0f);
			const FVector2 defaultUVScale(1.0f, 1.0f);
			const FVector2 defaultUVOffset(0.0f, 0.0f);

			if (bDefaultDiffuse)
			{
				for (uint32 i = 0; i < currentBatchCount; ++i)
				{
					const FRenderInfo* rInfo = pChunk[i].RenderInfo;
					dst[i].World = rInfo->WorldTransformMatrix;
					dst[i].Tint = rInfo->Color;
					const auto* subUV = rInfo->SubUVMesh;
					dst[i].UVScale = subUV ? subUV->UVScale : defaultUVScale;
					dst[i].UVOffset = subUV ? subUV->UVOffset : defaultUVOffset;
				}
			}
			else
			{
				for (uint32 i = 0; i < currentBatchCount; ++i)
				{
					const FRenderInfo* rInfo = pChunk[i].RenderInfo;
					dst[i].World = rInfo->WorldTransformMatrix;

					FLinearColor finalTint = rInfo->Color;
					finalTint.R *= matDiffuse.x;
					finalTint.G *= matDiffuse.y;
					finalTint.B *= matDiffuse.z;
					finalTint.A = 1.0f;
					dst[i].Tint = finalTint;

					const auto* subUV = rInfo->SubUVMesh;
					dst[i].UVScale = subUV ? subUV->UVScale : defaultUVScale;
					dst[i].UVOffset = subUV ? subUV->UVOffset : defaultUVOffset;
				}
			}
			context->Unmap(perObjectCB, 0);
		}

		// Draw calls in the chunk (zero Map/Unmap)
		if (context1)
		{
			for (uint32 i = 0; i < currentBatchCount; ++i)
			{
				const auto& entry = pChunk[i];

				// Set Material resources only if the material has changed
				if (entry.Material != lastUsedMaterial) [[unlikely]]
				{
					ID3D11ShaderResourceView* diffuseTexture = nullptr;
					ID3D11ShaderResourceView* normalTexture = nullptr;
					ID3D11ShaderResourceView* specularTexture = nullptr;
					if (entry.Material->DiffuseTexture.IsValid())
					{
						diffuseTexture = resources.FindTextureOrAdd(entry.Material->DiffuseTexture);
					}
					if (entry.Material->NormalTexture.IsValid())
					{
						normalTexture = resources.FindTextureOrAdd(entry.Material->NormalTexture);
					}
					if (entry.Material->SpecularTexture.IsValid())
					{
						specularTexture = resources.FindTextureOrAdd(entry.Material->SpecularTexture);
					}
					mRenderer->SetMaterialResources(
						diffuseTexture,
						normalTexture,
						specularTexture,
						&resources.GetSamplerState(SST_Wrap)
					);
					lastUsedMaterial = entry.Material;
				}

				// Set Mesh resources only if the mesh has changed
				if (entry.StaticMeshLOD != lastUsedMeshLOD) [[unlikely]]
				{
					lastUsedBuffer = resources.FindImmutableBufferOrAdd(entry.StaticMeshLOD->BufferKey);
					if (lastUsedBuffer == nullptr)
					{
						UE_LOG(Error, Render, "Static mesh buffer not found.");
						assert(false);
						continue;
					}
					mRenderer->SetStaticMeshResources(
						lastUsedBuffer->Buffer.GetAddressOf(),
						lastUsedBuffer->IndexBuffer.Get()
					);
					bHasIndexBuffer = (lastUsedBuffer->IndexBuffer != nullptr);
					currentIndexCount = static_cast<UINT>(entry.IndexCount);
					currentStartIndex = static_cast<UINT>(entry.StartIndex);
					lastUsedMeshLOD = entry.StaticMeshLOD;
				}

				// Bind offset constant buffer for current object (16 float4 vectors = 256 bytes, VS only)
				const UINT firstConstant = i << 4;
				context1->VSSetConstantBuffers1(1, 1, &perObjectCB, &firstConstant, &kNumConstants);

				if (bHasIndexBuffer)
				{
					context->DrawIndexed(currentIndexCount, currentStartIndex, 0);
				}
				else
				{
					context->Draw(lastUsedBuffer->SourceNum, 0);
				}
			}
		}
		else
		{
			for (uint32 i = 0; i < currentBatchCount; ++i)
			{
				const auto& entry = pChunk[i];

				if (entry.Material != lastUsedMaterial)
				{
					ID3D11ShaderResourceView* diffuseTexture = nullptr;
					ID3D11ShaderResourceView* normalTexture = nullptr;
					ID3D11ShaderResourceView* specularTexture = nullptr;
					if (entry.Material->DiffuseTexture.IsValid())
					{
						diffuseTexture = resources.FindTextureOrAdd(entry.Material->DiffuseTexture);
					}
					if (entry.Material->NormalTexture.IsValid())
					{
						normalTexture = resources.FindTextureOrAdd(entry.Material->NormalTexture);
					}
					if (entry.Material->SpecularTexture.IsValid())
					{
						specularTexture = resources.FindTextureOrAdd(entry.Material->SpecularTexture);
					}
					mRenderer->SetMaterialResources(
						diffuseTexture,
						normalTexture,
						specularTexture,
						&resources.GetSamplerState(SST_Wrap)
					);
					lastUsedMaterial = entry.Material;
				}

				if (entry.StaticMeshLOD != lastUsedMeshLOD)
				{
					lastUsedBuffer = resources.FindImmutableBufferOrAdd(entry.StaticMeshLOD->BufferKey);
					if (lastUsedBuffer == nullptr)
					{
						continue;
					}
					mRenderer->SetStaticMeshResources(
						lastUsedBuffer->Buffer.GetAddressOf(),
						lastUsedBuffer->IndexBuffer.Get()
					);
					bHasIndexBuffer = (lastUsedBuffer->IndexBuffer != nullptr);
					currentIndexCount = static_cast<UINT>(entry.IndexCount);
					currentStartIndex = static_cast<UINT>(entry.StartIndex);
					lastUsedMeshLOD = entry.StaticMeshLOD;
				}

				if (bHasIndexBuffer)
				{
					context->DrawIndexed(currentIndexCount, currentStartIndex, 0);
				}
				else
				{
					context->Draw(lastUsedBuffer->SourceNum, 0);
				}
			}
		}
	}
}

//for (const FRenderInfo* renderInfo : renderInfos)
//{
//	if (renderInfo->StaticMeshAsset == nullptr)
//	{
//		continue;
//	}

//	const FStaticMesh& staticMesh = *renderInfo->StaticMeshAsset->GetStaticMeshAsset();

//	FMatrix worldTransform = renderInfo->WorldTransformMatrix;
//	mRenderer->UpdateTextureConstant(worldTransform, view.viewProjectionMatrix, renderInfo->Color);

//	const FBuffer* buffer = resources.FindImmutableBufferOrAdd(renderInfo->MeshName);
//	if (buffer == nullptr)
//	{
//		UE_LOG(Error, Render, "Static mesh buffer not found.");
//		continue;
//	}
//	// section이 없는 경우, 기존 컴포넌트 텍스처를 사용하여 그린다.
//	// TODO?:	Is there any static mesh that has no sections?
//	//			If not, we don't need to manage this case.
//	if (staticMesh.Sections.IsEmpty())
//	{
//		ID3D11ShaderResourceView* texture = nullptr;
//		if (HasAllRenderFlags(renderInfo->eRenderFlags, ERenderFlags::RF_Texture))
//		{
//			// TODO: Use the texture from the renderInfo if available
//			texture = resources.FindTextureOrAdd(renderInfo->TextureName);
//			if (texture == nullptr)
//			{
//				UE_LOG(Warning, Render, "Primitive texture not found for primitive type. Default white texture is used.");
//				texture = resources.FindTextureOrAdd(BuiltinAssets::DefaultWhiteTexture);
//			}
//		}
//		else {
//			texture = resources.FindTextureOrAdd(BuiltinAssets::DefaultWhiteTexture);
//		}

//		mRenderer->SetMaterialResources(
//			texture,
//			nullptr,
//			nullptr,
//			&resources.GetSamplerState(SST_Wrap)
//		);

//		mRenderer->SetStaticMeshResources(
//			buffer->Buffer.GetAddressOf(),
//			buffer->IndexBuffer.Get()
//		);

//		if (buffer->IndexBuffer)
//		{
//			mRenderer->DrawIndexedBuffer(buffer->IndexCount, 0);
//		}
//		else
//		{
//			mRenderer->DrawVertexBuffer(buffer->SourceNum);
//		}

//		continue;
//	}
//	// OBJ 메시: 섹션마다 재질과 텍스처를 선택해서 그린다.
//	for (const FStaticMeshSection& section : staticMesh.Sections)
//	{

//		const FMaterial* material = nullptr;

//		ID3D11ShaderResourceView* diffuseTexture = nullptr;
//		ID3D11ShaderResourceView* normalTexture = nullptr;
//		ID3D11ShaderResourceView* specularTexture = nullptr;

//		// Find the material only if the render flags indicate that textures should be used
//		if (HasAllRenderFlags(renderInfo->eRenderFlags, ERenderFlags::RF_Texture))
//		{
//			if (section.MaterialSlotIndex >= 0 && section.MaterialSlotIndex < renderInfo->Materials.Num())
//			{
//				const UMaterial* materialAsset = renderInfo->Materials[section.MaterialSlotIndex];
//				if (materialAsset)
//				{
//					material = materialAsset->GetMaterial();
//				}
//			}

//			if (!material)
//			{
//				UE_LOG_F(Warning, Render, "Material not found for section {} of static mesh {}. Using default white material.",
//					section.Name, renderInfo->MeshName.ToString());

//				material = assets.FindMaterialAssetOrNull(BuiltinAssets::DefaultMaterial)->GetMaterial();
//			}
//		}
//		else
//		{
//			material = assets.FindMaterialAssetOrNull(BuiltinAssets::DefaultMaterial)->GetMaterial();
//		}

//		if (material->DiffuseTexture.IsValid())
//		{
//			diffuseTexture = resources.FindTextureOrAdd(material->DiffuseTexture);
//		}
//		if (material->NormalTexture.IsValid())
//		{
//			normalTexture = resources.FindTextureOrAdd(material->NormalTexture);
//		}

//		if (material->SpecularTexture.IsValid())
//		{
//			specularTexture = resources.FindTextureOrAdd(material->SpecularTexture);
//		}


//		if (!diffuseTexture && HasAllRenderFlags(renderInfo->eRenderFlags, ERenderFlags::RF_Texture) &&
//			renderInfo->TextureName.IsValid())
//		{
//			diffuseTexture = resources.FindTextureOrAdd(renderInfo->TextureName);
//		}

//		// 아무 텍스처도 없으면 흰색 텍스처
//		if (diffuseTexture == nullptr)
//		{
//			diffuseTexture = resources.FindTextureOrAdd(BuiltinAssets::DefaultWhiteTexture);
//		}

//		FLinearColor finalTint = renderInfo->Color;

//		if (material)
//		{
//			finalTint.R *= material->DiffuseColor.x;
//			finalTint.G *= material->DiffuseColor.y;
//			finalTint.B *= material->DiffuseColor.z;
//			finalTint.A = 1.0f;
//		}

//		// 우선 기존 컴포넌트 색상 유지
//		FVector2 uvOffset = renderInfo->SubUVMesh
//			? renderInfo->SubUVMesh->UVOffset
//			: FVector2(0.0f, 0.0f);
//		FVector2 uvScale = renderInfo->SubUVMesh
//			? renderInfo->SubUVMesh->UVScale
//			: FVector2(1.0f, 1.0f);

//		mRenderer->UpdateTextureConstant(
//			worldTransform, view.viewProjectionMatrix, finalTint,
//			uvScale, uvOffset
//		);

//		mRenderer->SetMaterialResources(
//			diffuseTexture,
//			normalTexture,
//			specularTexture,
//			&resources.GetSamplerState(SST_Wrap)
//		);

//		mRenderer->SetStaticMeshResources(
//			buffer->Buffer.GetAddressOf(),
//			buffer->IndexBuffer.Get()
//		);

//		if (buffer->IndexBuffer)
//		{
//			mRenderer->DrawIndexedBuffer(section.IndexCount, section.StartIndex);
//		}
//		else
//		{
//			mRenderer->DrawVertexBuffer(buffer->SourceNum);
//		}
//	}

//}

//}

void FGraphicsManager::hiZOcclusionCulling(FHiZBuffer* inHiZBuffer, const TArray<const FRenderInfo*>& inRenderInfos, TArray<const FRenderInfo*>& outRenderInfos)
{
	outRenderInfos.Reset(0);

	ID3D11DeviceContext* context = mRenderer->GetDeviceContext();
	uint32 maskCount = 0;
	const uint32* visibilityMask = nullptr;
	if (mbEnableHiZ && inHiZBuffer)
	{
		visibilityMask = inHiZBuffer->ReadbackVisibility(context, maskCount);
	}

	const int32 numInfos = inRenderInfos.Num();
	const FRenderInfo* const* rawInfos = inRenderInfos.GetData();

	if (!visibilityMask || maskCount == 0)
	{
		outRenderInfos.Reserve(numInfos);
		for (int32 i = 0; i < numInfos; ++i)
		{
			outRenderInfos.Add(rawInfos[i]);
		}
		return;
	}

	outRenderInfos.Reserve(numInfos);
	for (int32 i = 0; i < numInfos; ++i)
	{
		const FRenderInfo* info = rawInfos[i];
		const uint32 id = info->ObejctID.InternalIndex;
		if (id >= maskCount || visibilityMask[id] != 0)
		{
			outRenderInfos.Add(info);
		}
	}

	inHiZBuffer->UnmapVisibility(context);
}

void FGraphicsManager::SetEnableHiZ(bool bEnable)
{
	if (mbEnableHiZ != bEnable)
	{
		mbEnableHiZ = bEnable;
		if (mbEnableHiZ)
		{
			//mHiZBuffer.ResetStagingState();			
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
