// EngineLib/Core/Math/BVH.cpp

#include "BVH.h"

#include <algorithm>

#include "Core/Math/RayCast.h"
#include "Rendering/RenderInfo.h"

namespace
{
	FBoundingBox mergeBounds(
		const FBoundingBox& a,
		const FBoundingBox& b)
	{
		FBoundingBox result;

		for (int axis = 0; axis < 3; ++axis)
		{
			result.min[axis] = std::min(a.min[axis], b.min[axis]);
			result.max[axis] = std::max(a.max[axis], b.max[axis]);
		}
		return result;
	}

	FBoundingBox makeTriangleBounds(
		const FVector& v0,
		const FVector& v1,
		const FVector& v2)
	{
		FBoundingBox result{ v0, v0 };

		result = mergeBounds(result, FBoundingBox{ v1, v1 });
		result = mergeBounds(result, FBoundingBox{ v2, v2 });

		return result;
	}
}

void FMeshBVH::Build(
	const TArray<FNormalVertex>& vertices,
	const TArray<uint32>& indices,
	uint32 maxLeafSize
)
{
	Clear();

	if (indices.IsEmpty())
	{
		return;
	}
	if (indices.Num() % 3 != 0)
	{
		assert(false);
		return;
	}

	// Check the validity of the indices
	for (uint32 i = 0; i < indices.Num(); ++i)
	{
		if (indices[i] >= vertices.Num())
		{
			assert(false);
			return;
		}
	}

	maxLeafSize = std::clamp(maxLeafSize, 1u, 4u);

	const uint32 triangleCount = indices.Num() / 3;

	TArray<FBVHBuildPrimitive> primitives;
	primitives.Reserve(triangleCount);

	for (uint32 triangle = 0; triangle < triangleCount; ++triangle)
	{
		const FVector& v0 = vertices[indices[triangle * 3 + 0]].pos;
		const FVector& v1 = vertices[indices[triangle * 3 + 1]].pos;
		const FVector& v2 = vertices[indices[triangle * 3 + 2]].pos;

		primitives.Add({
			makeTriangleBounds(v0, v1, v2), // Bounds
			(v0 + v1 + v2) / 3.0f,			// Center
			triangle						// TriangleIndex
			});
	}

	buildNode(primitives, 0, triangleCount, maxLeafSize, vertices, indices);
}

bool FMeshBVH::Raycast(
	const FVector& localStart,
	const FVector& localEnd,
	const TArray<FNormalVertex>& vertices,
	const TArray<uint32>& indices,
	FRayTriangleHit& outHit,
	float tMax
) const
{
	assert(tMax >= 0.0f && tMax <= 1.0f);

	if (IsEmpty())
		return false;

	float enter, exit;

	// Check if the ray intersects the root node's bounding box
	if (!Raycast::IntersectSegmentAABB(
		localStart, localEnd, mNodes[0].Bounds,
		tMax, enter, exit))
	{
		return false;
	}

	float closestT = tMax;
	FRayTriangleHit hit;

	// Traverse the BVH starting from the root node
	if (!traverseNode(
		0, localStart, localEnd,
		vertices, indices, closestT, hit
	))
	{
		return false;
	}

	outHit = hit;
	return true;
}

bool FMeshBVH::traverseNode(
	int32 nodeIndex,
	const FVector& start,
	const FVector& end,
	const TArray<FNormalVertex>& vertices,
	const TArray<uint32>& indices,
	float& closestT,
	FRayTriangleHit& outHit) const
{
	const FBVHNode& node = mNodes[nodeIndex];

	// Check the intersection with the triangles
	if (node.IsLeaf())
	{
		// TODO: Use SIMD to accelerate triangle intersection tests
		//for (uint32 i = node.First; i < node.First + node.Count; ++i)
		//{
		//	const uint32 triangleIndex = mTriangleOrder[i];
		//	const uint32 base = triangleIndex * 3;

		//	const FVector& v0 = vertices[indices[base + 0]].pos;
		//	const FVector& v1 = vertices[indices[base + 1]].pos;
		//	const FVector& v2 = vertices[indices[base + 2]].pos;

		//	FRayTriangleHit hit;
		//	if (Raycast::IntersectSegmentTriangle(
		//		start, end, v0, v1, v2,
		//		closestT, hit))
		//	{
		//		closestT = hit.T;

		//		hit.TriangleIndex = triangleIndex;
		//		outHit = hit;
		//		found = true;
		//	}

		FRayTriangleHit hit;
		if (!Raycast::IntersectSegmentTriangle4x(
			start, end,
			mTrianglePackets[node.PacketIndex],
			closestT,
			hit))
		{
			return false;
		}

		closestT = hit.T;
		outHit = hit;
		return true;
	}

	int32 nearChild = node.LeftChildIndex;
	int32 farChild = node.RightChildIndex;

	float nearEnter, nearExit;
	float farEnter, farExit;

	const bool hitLeft = Raycast::IntersectSegmentAABB(
		start, end, mNodes[nearChild].Bounds,
		closestT, nearEnter, nearExit);

	const bool hitRight = Raycast::IntersectSegmentAABB(
		start, end, mNodes[farChild].Bounds,
		closestT, farEnter, farExit);

	if (!hitLeft && !hitRight)
	{
		return false;
	}

	if (!hitLeft)
	{
		return traverseNode(
			farChild, start, end,
			vertices, indices, closestT, outHit);
	}

	if (!hitRight)
	{
		return traverseNode(
			nearChild, start, end,
			vertices, indices, closestT, outHit);
	}

	if (farEnter < nearEnter)
	{
		std::swap(nearChild, farChild);
		std::swap(nearEnter, farEnter);
	}

	bool found = traverseNode(
		nearChild, start, end,
		vertices, indices, closestT, outHit);

	// closestT might have been updated,
	// so we need to check if we should traverse the far child
	if (farEnter <= closestT)
	{
		const bool farFound = traverseNode(
			farChild, start, end,
			vertices, indices, closestT, outHit);

		found = found || farFound;
	}

	return found;
}

void FMeshBVH::Clear()
{
	mNodes.Reset(0);
	mTrianglePackets.Reset(0);
}

bool FMeshBVH::IsEmpty() const
{
	return mNodes.IsEmpty();
}

int32 FMeshBVH::buildNode(
	TArray<FBVHBuildPrimitive>& primitives,
	uint32 first,
	uint32 count,
	uint32 maxLeafSize,
	const TArray<FNormalVertex>& vertices,
	const TArray<uint32>& indices)
{
	FBoundingBox bounds = primitives[first].Bounds;

	FBoundingBox centerBounds{
		primitives[first].Center,
		primitives[first].Center
	};

	for (uint32 i = first + 1; i < first + count; ++i)
	{
		bounds = mergeBounds(bounds, primitives[i].Bounds);

		const FVector& center = primitives[i].Center;
		centerBounds = mergeBounds(
			centerBounds,
			FBoundingBox{ center, center }
		);
	}

	const int32 nodeIndex = static_cast<int32>(mNodes.Num());

	FBVHNode node;
	node.Bounds = bounds;
	mNodes.Add(node);


	if (count <= maxLeafSize)
	{
		// Make packet
		const FTriangle4 packet = packTriangles(
			primitives,
			first, count,
			vertices, indices);

		mNodes[nodeIndex].PacketIndex =
			static_cast<int32>(mTrianglePackets.Add(packet));

		return nodeIndex;
	}

	// Determine the axis with the largest extent
	const FVector extent = centerBounds.max - centerBounds.min;

	int axis = 0;
	if (extent[1] > extent[axis]) axis = 1;
	if (extent[2] > extent[axis]) axis = 2;

	const uint32 mid = first + count / 2;

	std::nth_element(
		primitives.begin() + first,
		primitives.begin() + mid,
		primitives.begin() + first + count,
		[axis](const auto& a, const auto& b) -> bool
		{
			if (a.Center[axis] < b.Center[axis]) return true;
			if (a.Center[axis] > b.Center[axis]) return false;

			// If centers are equal, sort by triangle index to ensure deterministic order
			return a.TriangleIndex < b.TriangleIndex;
		}
	);

	const int32 left = buildNode(
		primitives, first, mid - first, maxLeafSize, vertices, indices);

	const int32 right = buildNode(
		primitives, mid, first + count - mid, maxLeafSize, vertices, indices);

	// Update the current node with child indices
	mNodes[nodeIndex].LeftChildIndex = left;
	mNodes[nodeIndex].RightChildIndex = right;

	return nodeIndex;
}

FTriangle4 FMeshBVH::packTriangles(
	const TArray<FBVHBuildPrimitive>& primitives,
	uint32 first,
	uint32 count,
	const TArray<FNormalVertex>& vertices,
	const TArray<uint32>& indices)
{
	assert(count >= 1 && count <= 4);

	// [정점 V0/V1/V2][좌표 X/Y/Z][삼각형 lane]
	float data[3][3][4]{};

	FTriangle4 packet{};
	packet.Count = count;

	for (uint32 lane = 0; lane < count; ++lane)
	{
		const uint32 triangleIndex =
			primitives[first + lane].TriangleIndex;

		const uint32 base = triangleIndex * 3;

		for (uint32 corner = 0; corner < 3; ++corner)
		{
			const FVector& position =
				vertices[indices[base + corner]].pos;

			for (uint32 axis = 0; axis < 3; ++axis)
				data[corner][axis][lane] = position[axis];
		}
		packet.TriangleIndices[lane] = static_cast<int32>(triangleIndex);
	}

	packet.V0.X = _mm_loadu_ps(data[0][0]);
	packet.V0.Y = _mm_loadu_ps(data[0][1]);
	packet.V0.Z = _mm_loadu_ps(data[0][2]);

	packet.V1.X = _mm_loadu_ps(data[1][0]);
	packet.V1.Y = _mm_loadu_ps(data[1][1]);
	packet.V1.Z = _mm_loadu_ps(data[1][2]);

	packet.V2.X = _mm_loadu_ps(data[2][0]);
	packet.V2.Y = _mm_loadu_ps(data[2][1]);
	packet.V2.Z = _mm_loadu_ps(data[2][2]);

	return packet;
}


// ------------------------
// 월드 공간 오브젝트 BVH 
// ------------------------

void FWorldBVH::Clear()
{
	mNodes.Reset(0);
	mObjectOrder.Reset(0);
}

void FWorldBVH::Build(const TArray<const FRenderInfo*>& renderInfos, uint32 maxLeafSize)
{
	Clear();

	if (renderInfos.IsEmpty())
	{
		return;
	}

	if (maxLeafSize < 1)
	{
		maxLeafSize = 1;
	}

	const uint32 objectCount = static_cast<uint32>(renderInfos.Num());

	TArray<FWorldBVHBuildObject> objects;
	objects.Reserve(objectCount);

	for (uint32 i = 0; i < objectCount; ++i)
	{
		const FBoundingBox& box = renderInfos[i]->WorldBounds;

		objects.Add({box, (box.min + box.max) * 0.5f, i});	// Bounds, Center, ObjectIndex
	}

	mObjectOrder.SetNum(static_cast<int32>(objectCount));

	buildNode(objects, 0, objectCount, maxLeafSize);
}

int32 FWorldBVH::buildNode(
	TArray<FWorldBVHBuildObject>& objects,
	uint32 first,
	uint32 count,
	uint32 maxLeafSize)
{
	// 개수가 적어질때까지 왼쪽오른쪽 갈라서 계속 재귀

	FBoundingBox bounds = objects[first].Bounds;	// 사과 박스 몸통 (ray 검사용)

	FBoundingBox centerBounds{ objects[first].Center,objects[first].Center };	// 사과 중심점(축 검사용)

	for (uint32 i = first + 1; i < first + count; ++i)
	{
		bounds = mergeBounds(bounds, objects[i].Bounds);

		const FVector& center = objects[i].Center;
		centerBounds = mergeBounds(centerBounds, FBoundingBox{ center, center });
	}

	const int32 nodeIndex = static_cast<int32>(mNodes.Num());

	FWorldBVHNode node;
	node.Bounds = bounds;
	mNodes.Add(node);

	if (count <= maxLeafSize)
	{
		// 개수 적으므로 리프로 끝! 
		for (uint32 i = 0; i < count; ++i)
		{
			mObjectOrder[static_cast<int32>(first + i)] = objects[first + i].ObjectIndex;
		}

		mNodes[nodeIndex].First = static_cast<int32>(first);
		mNodes[nodeIndex].Count = static_cast<int32>(count);

		return nodeIndex;
	}

	// 개수 많으므로 반으로 갈라야한다!

	// 중심점이 가장 넓게 퍼진 축으로 가른다!
	const FVector extent = centerBounds.max - centerBounds.min;

	int axis = 0;
	if (extent[1] > extent[axis]) axis = 1;
	if (extent[2] > extent[axis]) axis = 2;

	const uint32 mid = first + count / 2;

	// 시간복잡도 O(N)
	std::nth_element(
		objects.begin() + first,
		objects.begin() + mid,
		objects.begin() + first + count,
		[axis](const FWorldBVHBuildObject& a, const FWorldBVHBuildObject& b) -> bool
		{
			if (a.Center[axis] < b.Center[axis]) return true;
			if (a.Center[axis] > b.Center[axis]) return false;

			return a.ObjectIndex < b.ObjectIndex;
		}
	);

	const int32 left = buildNode(objects, first, mid - first, maxLeafSize);
	const int32 right = buildNode(objects, mid, first + count - mid, maxLeafSize);

	mNodes[nodeIndex].LeftChildIndex = left;
	mNodes[nodeIndex].RightChildIndex = right;

	return nodeIndex;
}

void FWorldBVH::Raycast(
	const FVector& start,
	const FVector& end,
	TArray<uint32>& outCandidates) const
{
	outCandidates.Reset(0);

	if (IsEmpty())
	{
		return;
	}

	if (!Raycast::IntersectSegmentAABB(start, end, mNodes[0].Bounds, 1.0f))
	{
		return;
	}

	traverseNode(0, start, end, outCandidates);
}

void FWorldBVH::traverseNode(
	int32 nodeIndex,
	const FVector& start,
	const FVector& end,
	TArray<uint32>& outCandidates) const
{
	const FWorldBVHNode& node = mNodes[nodeIndex];

	if (node.IsLeaf())
	{
		for (int32 i = 0; i < node.Count; ++i)
		{
			outCandidates.Add(mObjectOrder[node.First + i]);
		}
		return;
	}

	if (Raycast::IntersectSegmentAABB(
		start, end, mNodes[node.LeftChildIndex].Bounds, 1.0f))
	{
		traverseNode(node.LeftChildIndex, start, end, outCandidates);
	}

	if (Raycast::IntersectSegmentAABB(
		start, end, mNodes[node.RightChildIndex].Bounds, 1.0f))
	{
		traverseNode(node.RightChildIndex, start, end, outCandidates);
	}
}
