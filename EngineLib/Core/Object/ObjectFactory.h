#pragma once

#include <functional>

#include "Core/BuiltinAssets.h"
#include "Core/enum.h"
#include "Core/Math/Vector.h"
#include "Core/Math/Rotator.h"
#include "Core/Container/TMap.h"
#include "Core/Name.h"
//#include "Engine/Components/SceneComponent.h"
//#include "Engine/Actor.h"

namespace json { class JSON; }

class UObject;
class AActor;
class USceneComponent;
class FClassInfo;
class FFontResource;
class UStaticMesh;
class FAssetManager;

struct FObjectFactory
{
	// TODO?: Rename?
	static void SetDefaultFont(const FFontResource& defaultFontResource);
	static void SetAssetManager(const FAssetManager& assetManager);

	static const FFontResource* GetDefaultFontResource();

	static UObject* ConstructUnInitializedObject(const FClassInfo* classInfo);
	static UObject* LoadObject(const FClassInfo* classInfo, const json::JSON& inJson);

	template<typename TObject, typename... Args>
		requires std::derived_from<TObject, UObject>
	static TObject* ConstructObject(Args&& ...args);

	template<typename TObject>
		requires std::derived_from<TObject, UObject>
	static TObject* ConstructUnInitializedObject();

	template<typename TObject>
		requires std::derived_from<TObject, UObject>
	static TObject* ConstructUnInitializedObject(const FName& Name);

	template<typename TObject, typename... Args>
		requires std::derived_from<TObject, UObject>
	static TObject* ConstructObjectWithName(const FName& Name, Args&&... args);

	template<typename TObject>
		requires std::derived_from<TObject, UObject>
	static TObject* LoadObject(const json::JSON& inJson);


	static const FClassInfo* GetClassInfoByName(const FString& className);

	static bool RegisterClassInfo(FString className, const FClassInfo* classInfo);

private:
	// TODO: Automate the registration of class info for all UObject-derived classes.
	static TMap<FName, std::function<const FClassInfo* ()>> mClassInfoMap;

	// TODO?: Does really need a default font resource?
	static const FFontResource* mDefaultFontResource;
	static const FAssetManager* mAssetManagerRef;

	static AActor* createActorWithRootComponent(const FName& Name, USceneComponent* rootComponent);
};


#include "ObjectFactory.inl"
