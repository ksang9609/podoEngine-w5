#pragma once

#include <string_view>

#include "Engine/Serialization/SceneData.h"
#include "Core/Container/TArray.h"
#include "Rendering/RenderInfo.h"
#include "Core/enum.h"
#include "Core/Math/FOctree.h"
#include "Core/Math/BVH.h"
#include "Editor/EditorSetting.h"

inline constexpr std::string_view kSceneDataDir = "SceneData\\";
inline constexpr std::string_view kSceneDataSuffix = ".Scene";

class FFileManager;
class FFrameTimer;
class FEditorViewportClient;
class FGraphicsManager;
class UWorld;

//struct FGuiReference
//{
//	const FFrameTimer& FrameTimer;
//	FGraphicsManager* GraphicsManager;
//	FEditorViewportClient* ViewportClient;
//	const FFileManager* FileManager;
//};

//struct FGuiInputField
//{
//	/* Spawn Actor */
//	EPrimitive PrimitiveType = EPrimitive::EP_Cube;
//	int32 SpawnCount = 1;
//
//	/* Scene Control */
//	char SceneName[512] = "Default";
//
//	/* Object Lists */
//	TArray<UObject*> SortedObjectLists;
//	uint64 LastGUObjectRevision = -1;
//};

class FSceneManager
{
public:
	FSceneManager() = default;
	~FSceneManager();

	void Update(float deltaTime);

	const TArray<const FRenderInfo*>& GetRenderInfos() const;
	const TArray<const FRenderInfo*>& GetAxisRenderInfos() const;

	// Clear world
	void NewScene();
	void DeleteScene();

	//void SaveScene(std::string_view sceneName, const FFileManager& fileManager);
	//void LoadScene(std::string_view filePath, const FFileManager& fileManager);

	void ReplaceWorld(UWorld* newWorld);

	UWorld* GetCurrentWorld() const { return mCurrentWorld; }

	AActor* GetSelectedActor() const { return mSelectedActor; }

	void RemoveActor(AActor* actor);

	bool IsActorSelected() const { return mSelectedActor != nullptr; }
	void SetSelectedActor(AActor* actor);
	void ResetSelectedActor() { mSelectedActor = nullptr; }

	float GetPanelWidth() const;

	const FOctree &GetOctree() const { return mOctree; }
	const FWorldBVH& GetWorldBVH() const { return mWorldBVH; }

	void MarkOctreeDirty() { mbOctreeDirty = true; }
	void NotifyObjectMoved(uint32 objectIndex);
	void NotifySelectedActorMoved();
	void FinishObjectMove();

	
private:
	//static constexpr float MIN_WIDTH_RATIO = 0.2f;
	//static constexpr float MAX_WIDTH_RATIO = 0.6f;

	//static constexpr float CONTROL_PANEL_HEIGHT_RATIO = 0.4f;
	//static constexpr float WINDOW_PROPERTY_HEIGHT_RATIO = 0.3f;

	float mPanelWidth;

	UWorld* mCurrentWorld = nullptr;
	AActor* mSelectedActor = nullptr;
	std::string LoadScenename;

	FEditorSetting mEditorSetting;

	FOctree mOctree;
	FWorldBVH mWorldBVH;   // 옥트리와의 피킹 성능 비교용
	bool mbOctreeDirty = true;
	int32 mLastRenderInfoCount = -1;
};
