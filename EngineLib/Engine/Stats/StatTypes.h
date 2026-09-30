#pragma once

#include "../../Core/Core.h"

enum class EStatGroup : uint8
{
	None = 0,
	FPS = 1 << 0,
	Memory = 1 << 1,
	Picking = 1 << 2,
	All = ~0u
};

constexpr uint8 toStatMask(EStatGroup group)
{
	return static_cast<uint8>(group);
}

struct FFrameStatSnapshot
{
	float FPS = 0.0f;
	float FrameTimeMs = 0.0f;

	float SmoothedFPS = 0.0f;
	float SmoothedFrameTimeMs = 0.0f;

	bool HasGpuTime = false;
	double GpuTimeMs = 0.0f;
};

struct FMemoryStatSnapshot
{
	uint64 PrivateBytes = 0;
	uint64 WorkingSetBytes = 0;
	uint64 PeakWorkingSetBytes = 0;

	// 현재 프로젝트의 전역 new/delete 추적값.
	// 모든 프로세스 메모리를 의미하지 않는다.
	uint64 TrackedAllocationBytes = 0;
	uint64 TrackedAllocationCount = 0;

	uint32 UObjectCount = 0;
	uint32 ActorCount = 0;
	uint32 ComponentCount = 0;
	uint32 RenderInfoCount = 0;

	uint32 StaticMeshCount = 0;
	uint32 MaterialCount = 0;

	uint32 GpuBufferCount = 0;
	uint32 GpuTextureCount = 0;
};

struct FPickingStatSnapshot
{
	uint32 PickingCount = 0;
	double LastPickTimeMs = 0;
	double TotalPickTimeMs = 0;
};

struct FStatSnapshot
{
	FFrameStatSnapshot Frame;
	FMemoryStatSnapshot Memory;
	FPickingStatSnapshot Picking;
};
