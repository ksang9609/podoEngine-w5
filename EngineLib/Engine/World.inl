// EngineLib/Engine/World.inl

#include "Engine/Components/NameComponent.h"

template<typename TComponent, typename... Args>
	requires(std::derived_from<TComponent, USceneComponent>)
AActor* UWorld::SpawnActorWithRootComponent(const FName& name, const FFontResource* nameFontOrNull, Args&&... args)
{
	TComponent* rootComponent = FObjectFactory::ConstructObjectWithName<TComponent>(name, std::forward<Args>(args)...);
	if (!rootComponent)
	{
		return nullptr;
	}

	AActor* actor = FObjectFactory::ConstructObjectWithName<AActor>(name);
	if (!actor)
	{
		// If actor creation fails, clean up the root component to avoid memory leaks
		delete rootComponent;
		return nullptr;
	}

	actor->AddRootSceneComponent(rootComponent);

	if (nameFontOrNull)
	{
		// Add name component
		UNameComponent* nameComponent = FObjectFactory::ConstructObjectWithName<UNameComponent>(
			FName("NameComponent"),
			name.ToString(), FVector3{0, 0, 0.2}, *nameFontOrNull);

		actor->AddComponent(nameComponent);
		nameComponent->AttachTo(*rootComponent);
	}

	AddActor(std::unique_ptr<AActor>(actor));
	return actor;
}
