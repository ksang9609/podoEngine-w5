#pragma once
#include "Core/Core.h"
#include "Core/Container/TArray.h"
#include "Vector.h"
#include "Rendering/RenderInfo.h"
#include "FBoundingBox.h"
#include "Frustum.h"

struct FOctreeNode {
	FVector Center;
	float HalfSize;

	uint32 ChildStart;
	uint32 ObjectStart;
	uint32 ObjectCount;
};

struct FOctreeBuildObject {
	FVector Center;
	float Radius;
};

class FOctree {
public:
	void Build(const TArray<const FRenderInfo*>& renderInfos);
	void Raycast(const FVector& origin, const FVector& direction, TArray<uint32>& outCandidates) const;
	void MarkObjectMoved(uint32 objectIndex);

	void AppendObjects(int32 newTotal);
	bool NeedsRebuild() const;

public:
	int32  GetNodeCount()    const { return mNodes.Num(); }
	int32  GetInsideCount()  const { return mInsideIndices.Num(); }
	int32  GetOutsideCount() const { return mOutsideObjects.Num(); }
	uint32 GetRootObjectCount() const { return mNodes.IsEmpty() ? 0u : mNodes[0].ObjectCount; }
	uint32 GetMaxNodeObjectCount() const;	
	float GetRootHalfSize() const { return mRootHalfSize; }

	const TArray<uint32>& GetStrayObjects() const { return mOutsideObjects; }
	bool IsStale(uint32 objectIndex) const { return objectIndex < static_cast<uint32>(mStaleFlags.Num()) && mStaleFlags[objectIndex] != 0; }


public:
	void FrustumCull(const FFrustum & frustum, const FVector & cameraPos, TArray<uint32> & outInside, TArray<uint32>& outIntersect) const;


private:
	void createRootNode(const TArray<const FRenderInfo*>& renderInfos);
	void insertAllObjects(const TArray<const FRenderInfo*>& renderInfos);
	bool outsideRoot(const FBoundingBox& box) const;

	void insertObject(uint32 nodeIndex, uint32 objectIndex, uint32 nodeDepth);
	void subDivide(uint32 nodeIndex);
	void flattenObjects();

	void raycastNode(uint32 nodeIndex, const FVector& origin, const FVector& invDir, TArray <uint32>& outCandidates) const;
	bool intersectLooseBounds(uint32 nodeIndex, const FVector& origin, const FVector& invDir, float tMax, float& outTEnter) const;

private:
	void addSubtreeAll(uint32 nodeIndex, const FVector& cameraPos, TArray <uint32>& outVisible) const;
	void cullNode(uint32 nodeIndex, const FVector& cameraPos, const FFrustum& frustum, TArray <uint32>& outInside, TArray <uint32>& outIntersect) const;


private:
	TArray <FOctreeNode> mNodes;
	TArray <uint32> mInsideIndices;
	TArray <uint32> mOutsideObjects;
	TArray <uint8> mStaleFlags;	// 해당 자리 믿지 마라!

	TArray <FOctreeBuildObject> mBuildObject;	// 조회용
	TArray<TArray<uint32>> mBuildBuckets;		// 저장용

	static constexpr float margin = 1.5f;
	static constexpr int32 MaxObjectCount = 8;
	static constexpr uint32 MaxDepth = 8;
	static constexpr int32 MinStrayForRebuild = 16;

	FVector mRootCenter;
	float   mRootHalfSize = 0.0f;
	bool    mbHasRootBox = false;
};
