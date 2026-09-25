#pragma once

#include <memory>

#include "Core/Object/Object.h"
#include "Actor.h"

#include "Rendering/RenderInfo.h"
#include "Rendering/FontResource.h"
//struct FRenderInfo;

class UWorld final : public UObject
{
	DECLARE_OBJECT(UWorld, UObject)
public:
	UWorld() = default;
	virtual ~UWorld();

	virtual void SerializeClass(json::JSON& outJson) const override;
	virtual void DeserializeClass(const json::JSON& inJson) override;

	void AddActor(std::unique_ptr<AActor> actor);
	bool RemoveActor(uint32 componentUUID);

	const TArray<FRenderInfo>& GetRenderInfos();
	TArray<std::unique_ptr<AActor>>& GetActors() { return mActors; }
	const TArray<std::unique_ptr<AActor>>& GetActors() const { return mActors; }

	void Update(float deltaTime);
	//void Render();
	void ClearRenderInfos();

	uint32 GetActorCount() const { return static_cast<uint32>(mActors.Num()); }

	/* Spawn Actor */
	template<typename TComponent, typename... Args>
		requires(std::derived_from<TComponent, USceneComponent>)
	AActor* SpawnActorWithRootComponent(const FName& name, const FFontResource* nameFontOrNull, Args&&... args);

private:
	int32 getActorIndex(uint32 actorUUID) const;

private:
	enum
	{
		DEFAULT_RESERVE_MEM = 1024U
	};

	// Todo: Must reserve
	TArray<std::unique_ptr<AActor>> mActors;

	// Todo: Maybe, move to FSceneManager
	TArray<FRenderInfo> mRenderInfos;
};

#include "World.inl"
