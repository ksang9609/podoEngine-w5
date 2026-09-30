// EngineLib/Core/Math/BVH.h

#pragma once

#include "Core/Core.h"
#include "Core/Math/FBoundingBox.h"
#include "Core/Math/RayCast.h"
#include "Core/Container/TArray.h"

#include "Rendering/VertexType.h"

struct FBVHNode
{
	FBoundingBox Bounds;
	int32 LeftChildIndex = -1;  // -1 indicates no child
	int32 RightChildIndex = -1; // -1 indicates no child

	int32 PacketIndex = -1;

	bool IsLeaf() const { return PacketIndex >= 0; }
};

// Only used during BVH construction, not stored in the final BVH structure
struct FBVHBuildPrimitive
{
	FBoundingBox Bounds;
	FVector Center;
	uint32 TriangleIndex;
};

class FMeshBVH
{
public:
	void Build(
		const TArray<FNormalVertex>& vertices,
		const TArray<uint32>& indices,
		uint32 maxLeafSize = 4);

	bool Raycast(
		const FVector& localStart,
		const FVector& localEnd,
		const TArray<FNormalVertex>& vertices,
		const TArray<uint32>& indices,
		FRayTriangleHit& outHit,
		float tMax = 1.0f) const;

	void Clear();
	bool IsEmpty() const;


private:
	TArray<FBVHNode> mNodes;
	TArray<FTriangle4> mTrianglePackets;

	int32 buildNode(
		TArray<FBVHBuildPrimitive>& primitives,
		uint32 first,
		uint32 count,
		uint32 maxLeafSize,
		const TArray<FNormalVertex>& vertices,
		const TArray<uint32>& indices);

	bool traverseNode(
		int32 nodeIndex,
		const FVector& start,
		const FVector& end,
		const TArray<FNormalVertex>& vertices,
		const TArray<uint32>& indices,
		float& closestT,
		FRayTriangleHit& outHit) const;

	FTriangle4 packTriangles(
		const TArray<FBVHBuildPrimitive>& primitives,
		uint32 first,
		uint32 count,
		const TArray<FNormalVertex>& vertices,
		const TArray<uint32>& indices);
};



// --------------------------
// 월드 공간 오브젝트 BVH 
// --------------------------

struct FRenderInfo;

struct FWorldBVHNode
{
	FBoundingBox Bounds;
	int32 LeftChildIndex = -1;
	int32 RightChildIndex = -1;

	int32 First = -1;   // -1: 내부 노드
	int32 Count = 0;

	bool IsLeaf() const { return First >= 0; }
};

struct FWorldBVHBuildObject
{
	FBoundingBox Bounds;
	FVector Center;
	uint32 ObjectIndex;
};

class FWorldBVH
{
public:
	void Build(const TArray<const FRenderInfo*>& renderInfos, uint32 maxLeafSize = 4);

	void Raycast(const FVector& start, const FVector& end, TArray<uint32>& outCandidates) const;

	void Clear();
	bool IsEmpty() const { return mNodes.IsEmpty(); }

	int32 GetNodeCount() const { return mNodes.Num(); }

private:
	int32 buildNode(TArray<FWorldBVHBuildObject>& objects, uint32 first, uint32 count, uint32 maxLeafSize);
	void traverseNode(int32 nodeIndex, const FVector& start, const FVector& end, TArray<uint32>& outCandidates) const;

	TArray<FWorldBVHNode> mNodes;
	TArray<uint32> mObjectOrder;
};
