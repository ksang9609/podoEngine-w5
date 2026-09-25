#include "StatManager.h"

#include <Windows.h>
#include <Psapi.h>

#include "../../Core/FrameTimer.h"
#include "../../Core/AssetManager.h"
#include "../../Core/Object/Object.h"

#include "../../Engine/EngineStatics.h"
#include "../../Engine/SceneManager.h"
#include "../../Engine/World.h"
#include "../../Engine/Actor.h"

#include "../../Rendering/GpuResourceManager.h"

#pragma comment(lib, "Psapi.lib") // 현재 프로세스 RAM 사용량 표시를 하기 위함

bool FStatManager::Toggle(EStatGroup group)
{
	const uint8 mask = toStatMask(group);
	mEnabledMask ^= mask;

	const bool enabled = IsEnabled(group);

	if (group == EStatGroup::Memory && enabled)
	{
		mbMemoryDirty = true;
	}

	return enabled;
}

void FStatManager::DisableAll()
{
	mEnabledMask = 0;
}

bool FStatManager::IsEnabled(EStatGroup group) const
{
	const uint8 mask = toStatMask(group);
	return (mEnabledMask & mask) != 0;
}

bool FStatManager::HasAnyEnabledStat() const
{
	return mEnabledMask != 0;
}

void FStatManager::UpdateFrame(const FFrameTimer& frameTimer)
{
	const float frameTimeMs = frameTimer.GetFrameTimeMilliseconds();

	const float fps = frameTimeMs > 0.0f ? 1000.0f / frameTimeMs : 0.0f;

	mSnapshot.Frame.FrameTimeMs = frameTimeMs;
	mSnapshot.Frame.FPS = fps;

	if (!mbFrameInitialized)
	{
		mSnapshot.Frame.SmoothedFrameTimeMs = frameTimeMs;
		mSnapshot.Frame.SmoothedFPS = fps;
		mbFrameInitialized = true;
		return;
	}

	// 값이 너무 튀지 않도록 exponential moving average 사용
	constexpr float smoothingAlpha = 0.1f;

	mSnapshot.Frame.SmoothedFrameTimeMs += (frameTimeMs - mSnapshot.Frame.SmoothedFrameTimeMs) * smoothingAlpha;
	mSnapshot.Frame.SmoothedFPS = mSnapshot.Frame.SmoothedFrameTimeMs > 0.0f ? 1000.0f / mSnapshot.Frame.SmoothedFrameTimeMs : 0.0f;
}

void FStatManager::UpdateMemory(float deltaTime, const FStatCollectionSources& sources)
{
	if (!IsEnabled(EStatGroup::Memory))
	{
		return;
	}

	mMemorySampleElapsed += deltaTime;

	constexpr float sampleInterval = 0.5f;

	if (!mbMemoryDirty && mMemorySampleElapsed < sampleInterval)
	{
		return;
	}

	mMemorySampleElapsed = 0.0f;
	mbMemoryDirty = false;

	CollectMemory(sources);
}

void FStatManager::CollectMemory(const FStatCollectionSources& sources)
{
	FMemoryStatSnapshot& memory = mSnapshot.Memory;

	PROCESS_MEMORY_COUNTERS_EX processMemory{};
	processMemory.cb = sizeof(processMemory);

	if (GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&processMemory),
		sizeof(processMemory)))
	{
		memory.PrivateBytes = static_cast<uint64>(processMemory.PrivateUsage);
		memory.WorkingSetBytes = static_cast<uint64>(processMemory.WorkingSetSize);
		memory.PeakWorkingSetBytes = static_cast<uint64>(processMemory.PeakWorkingSetSize);
	}

	memory.TrackedAllocationBytes = UEngineStatics::sTotalAllocationBytes;
	memory.TrackedAllocationCount = UEngineStatics::sTotalAllocationCount;
	memory.UObjectCount = static_cast<uint32>(UObject::GetGObjectArray().Num());

	memory.ActorCount = 0;
	memory.ComponentCount = 0;
	memory.RenderInfoCount = 0;

	const UWorld* world = sources.SceneManager.GetCurrentWorld();

	if (world != nullptr)
	{
		memory.ActorCount = world->GetActorCount();

		for (const auto& actor : world->GetActors())
		{
			if (actor != nullptr)
			{
				memory.ComponentCount += static_cast<uint32>(actor->GetComponents().Num());
			}
		}

		memory.RenderInfoCount =static_cast<uint32>(sources.SceneManager.GetRenderInfos().Num());
	}

	memory.StaticMeshCount = sources.AssetManager.GetStaticMeshAssetCount();
	memory.MaterialCount = sources.AssetManager.GetMaterialAssetCount();
	memory.GpuBufferCount =sources.GpuResourceManager.GetImmutableBufferCount();
	memory.GpuTextureCount =sources.GpuResourceManager.GetTextureCount();
}
