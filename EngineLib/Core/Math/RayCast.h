// EngineLib/Core/Math/RayCast.h

#pragma once

#include <immintrin.h>
#include <algorithm>

#include "Core/Core.h"
#include "Core/Math/Vector.h"
#include "Core/Math/FBoundingBox.h"

struct alignas(16) FVector4x
{
	__m128 X, Y, Z;
};

struct alignas(16) FTriangle4
{
	FVector4x V0;
	FVector4x V1;
	FVector4x V2;

	int32 TriangleIndices[4] = { -1, -1, -1, -1 };
	uint32 Count = 0; // Number of valid triangles in this packet
};

struct FRayTriangleHit
{
	// P(t) = RayStart + t * (RayEnd - RayStart)
	float T = 1.0f;

	// barycentric coordinates
	float U = 0.0f;
	float V = 0.0f;

	// -1 if no triangle information is available
	int32 TriangleIndex = -1;
	bool bHit = false;
};

namespace Raycast
{
	// Slab algorithm for ray-AABB intersection.
	inline bool IntersectSegmentAABB(
		const FVector& rayStart,
		const FVector& rayEnd,
		const FBoundingBox& bounds,
		float tMax,
		float& outEnter,
		float& outExit)
	{
		const FVector direction = rayEnd - rayStart;

		float enter = 0.0f;
		float exit = tMax;

		for (int axis = 0; axis < 3; ++axis)
		{
			const float origin = rayStart[axis];
			const float dir = direction[axis];

			// Handle the case where the ray is parallel to the slab
			const float minValue = bounds.min[axis];
			const float maxValue = bounds.max[axis];
			if (fabsf(dir) < 1e-6f)
			{
				if (origin < minValue || origin > maxValue)
				{
					return false;
				}
				continue;
			}

			float t0 = (minValue - origin) / dir;
			float t1 = (maxValue - origin) / dir;
			if (t0 > t1)
			{
				std::swap(t0, t1);
			}

			enter = (std::max)(enter, t0);
			exit = (std::min)(exit, t1);

			if (enter > exit)
			{
				return false;
			}
		}

		outEnter = enter;
		outExit = exit;
		return true;
	}

	inline bool IntersectSegmentAABB(
		const FVector& rayStart,
		const FVector& rayEnd,
		const FBoundingBox& bounds,
		float tMax)
	{
		float enter, exit;
		return IntersectSegmentAABB(rayStart, rayEnd, bounds, tMax, enter, exit);
	}

	inline bool IntersectSegmentTriangle(
		const FVector& rayStart,
		const FVector& rayEnd,
		const FVector& v0,
		const FVector& v1,
		const FVector& v2,
		float tMax,
		FRayTriangleHit& outHit)
	{
		constexpr float episilon = 1e-6f;

		// Möller–Trumbore intersection algorithm
		// Ray: P(t) = rayStart + t * (rayEnd - rayStart)
		// Triangle: v0, v1, v2
		// rayStart + t * (rayEnd - rayStart) = v0 + u * (v1 - v0) + v * (v2 - v0)

		const FVector direction = rayEnd - rayStart;
		const FVector edge1 = v1 - v0;
		const FVector edge2 = v2 - v0;

		const FVector p = FVector::cross(direction, edge2);
		const float determinant = FVector::dot(edge1, p);

		if (fabsf(determinant) < episilon)
		{
			return false; // Ray is parallel to the triangle
		}

		const float inverseDeterminant = 1.0f / determinant;
		const FVector offset = rayStart - v0;

		const float u = FVector::dot(offset, p) * inverseDeterminant;
		if (u < 0.0f || u > 1.0f)
		{
			return false; // Intersection is outside the triangle
		}

		const FVector q = FVector::cross(offset, edge1);

		const float v = FVector::dot(direction, q) * inverseDeterminant;
		if (v < 0.0f || u + v > 1.0f)
		{
			return false; // Intersection is outside the triangle
		}

		const float t = FVector::dot(edge2, q) * inverseDeterminant;
		if (t < 0.0f || t > tMax)
		{
			return false; // Intersection is outside the segment
		}

		outHit.T = t;
		outHit.U = u;
		outHit.V = v;
		return true;
	}

	inline bool IntersectSegmentTriangle4x(
		const FVector& rayStart,
		const FVector& rayEnd,
		const FTriangle4& triangle,
		float tMax,
		FRayTriangleHit& outHit)
	{
		assert(triangle.Count <= 4);
		if (triangle.Count == 0)
		{
			return false;
		}

		const FVector direction = rayEnd - rayStart;
		const __m128 rayOrigX = _mm_set1_ps(rayStart.x);
		const __m128 rayOrigY = _mm_set1_ps(rayStart.y);
		const __m128 rayOrigZ = _mm_set1_ps(rayStart.z);
		const __m128 rayDirX = _mm_set1_ps(direction.x);
		const __m128 rayDirY = _mm_set1_ps(direction.y);
		const __m128 rayDirZ = _mm_set1_ps(direction.z);
		const __m128 vZero = _mm_setzero_ps();
		const __m128 vOne = _mm_set1_ps(1.0f);
		const __m128 vEpsilon = _mm_set1_ps(1e-6f);

		const __m128 e1_X = _mm_sub_ps(triangle.V1.X, triangle.V0.X);
		const __m128 e1_Y = _mm_sub_ps(triangle.V1.Y, triangle.V0.Y);
		const __m128 e1_Z = _mm_sub_ps(triangle.V1.Z, triangle.V0.Z);

		const __m128 pvecX = _mm_sub_ps(_mm_mul_ps(rayDirY, _mm_sub_ps(triangle.V2.Z, triangle.V0.Z)), _mm_mul_ps(rayDirZ, _mm_sub_ps(triangle.V2.Y, triangle.V0.Y)));
		const __m128 pvecY = _mm_sub_ps(_mm_mul_ps(rayDirZ, _mm_sub_ps(triangle.V2.X, triangle.V0.X)), _mm_mul_ps(rayDirX, _mm_sub_ps(triangle.V2.Z, triangle.V0.Z)));
		const __m128 pvecZ = _mm_sub_ps(_mm_mul_ps(rayDirX, _mm_sub_ps(triangle.V2.Y, triangle.V0.Y)), _mm_mul_ps(rayDirY, _mm_sub_ps(triangle.V2.X, triangle.V0.X)));
		// Det = Edge1 · Pvec (FMA 내적)
		const __m128 det = _mm_fmadd_ps(e1_X, pvecX, _mm_fmadd_ps(e1_Y, pvecY, _mm_mul_ps(e1_Z, pvecZ)));

		// 부호 비트를 지워 절댓값(|Det|)을 구함
		const __m128 absDet = _mm_andnot_ps(_mm_set1_ps(-0.0f), det);
		__m128 validMask = _mm_cmpgt_ps(absDet, vEpsilon);
		// 4개 삼각형 모두 평행하면 즉시 탈출
		if (_mm_movemask_ps(validMask) == 0)
		{
			return false;
		}
		const __m128 invDet = _mm_div_ps(vOne, det);
		// 3. Tvec = RayStart - V0  및  U 좌표 계산
		const __m128 tvecX = _mm_sub_ps(rayOrigX, triangle.V0.X);
		const __m128 tvecY = _mm_sub_ps(rayOrigY, triangle.V0.Y);
		const __m128 tvecZ = _mm_sub_ps(rayOrigZ, triangle.V0.Z);

		const __m128 dotTP = _mm_fmadd_ps(tvecX, pvecX, _mm_fmadd_ps(tvecY, pvecY, _mm_mul_ps(tvecZ, pvecZ)));
		const __m128 u = _mm_mul_ps(dotTP, invDet);
		// 0.0f <= U <= 1.0f 검사 (조기 탈출)
		validMask = _mm_and_ps(validMask, _mm_cmpge_ps(u, vZero));
		validMask = _mm_and_ps(validMask, _mm_cmple_ps(u, vOne));
		if (_mm_movemask_ps(validMask) == 0)
		{
			return false; // 4개 모두 삼각형 밖
		}
		// 4. Qvec = Tvec x Edge1  및  V 좌표 계산
		const __m128 qvecX = _mm_sub_ps(_mm_mul_ps(tvecY, e1_Z), _mm_mul_ps(tvecZ, e1_Y));
		const __m128 qvecY = _mm_sub_ps(_mm_mul_ps(tvecZ, e1_X), _mm_mul_ps(tvecX, e1_Z));
		const __m128 qvecZ = _mm_sub_ps(_mm_mul_ps(tvecX, e1_Y), _mm_mul_ps(tvecY, e1_X));
		const __m128 dotDQ = _mm_fmadd_ps(rayDirX, qvecX, _mm_fmadd_ps(rayDirY, qvecY, _mm_mul_ps(rayDirZ, qvecZ)));
		const __m128 v = _mm_mul_ps(dotDQ, invDet);
		// V >= 0.0f  &&  U + V <= 1.0f 검사 (조기 탈출)
		validMask = _mm_and_ps(validMask, _mm_cmpge_ps(v, vZero));
		validMask = _mm_and_ps(validMask, _mm_cmple_ps(_mm_add_ps(u, v), vOne));
		if (_mm_movemask_ps(validMask) == 0)
		{
			return false;
		}
		// 5. T 거리 계산 및 선분 범위(0.0f <= T <= tMax) 판정
		const __m128 dotEQ = _mm_fmadd_ps(_mm_sub_ps(triangle.V2.X, triangle.V0.X), qvecX,
			_mm_fmadd_ps(_mm_sub_ps(triangle.V2.Y, triangle.V0.Y), qvecY,
				_mm_mul_ps(_mm_sub_ps(triangle.V2.Z, triangle.V0.Z), qvecZ)));
		const __m128 t = _mm_mul_ps(dotEQ, invDet);
		// T >= 0.0f && T <= tMax
		validMask = _mm_and_ps(validMask, _mm_cmpge_ps(t, vZero));
		validMask = _mm_and_ps(validMask, _mm_cmple_ps(t, _mm_set1_ps(tMax)));
		const int hitMask = _mm_movemask_ps(validMask);
		if (hitMask == 0)
		{
			return false;
		}
		// 6. 살아남은 삼각형 중 가장 가까운 것(최소 T) 추출
		alignas(16) float arrT[4];
		alignas(16) float arrU[4];
		alignas(16) float arrV[4];
		_mm_store_ps(arrT, t);
		_mm_store_ps(arrU, u);
		_mm_store_ps(arrV, v);
		float bestT = tMax;
		int bestIndex = -1;
		// 유효한 triangle.Count 범위 내에서만 최소 T 탐색
		for (uint32 i = 0; i < triangle.Count; ++i)
		{
			if ((hitMask & (1 << i)) && (arrT[i] < bestT))
			{
				bestT = arrT[i];
				bestIndex = i;
			}
		}
		if (bestIndex != -1)
		{
			outHit.T = bestT;
			outHit.U = arrU[bestIndex];
			outHit.V = arrV[bestIndex];
			outHit.TriangleIndex = triangle.TriangleIndices[bestIndex];
			outHit.bHit = true;
			return true;
		}
		return false;
	}
}
