
#include "SceneManager.h"

#include <algorithm>
#include <format>

#include "Core/Container/TArray.h"
#include "Core/IO/FileManager.h"
#include "Core/IO/JsonUtil.h"
#include "Core/Object/ObjectFactory.h"
#include "Core/enum.h"
#include "Editor/Console.h"
#include "Editor/FEditorViewportClient.h"
#include "Engine/Components/PrimitiveComponent.h"
#include "Engine/EngineStatics.h"
#include "Engine/World.h"
#include "Rendering/Camera.h"

#include "ThirdParty/ImGui/imgui.h"
#include "ThirdParty/ImGui/imgui_impl_dx11.h"
#include "ThirdParty/ImGui/imgui_impl_win32.h"

#include "Core/FrameTimer.h"
#include "Engine/Components/ActorComponent.h"
#include "Engine/Components/CubeComponent.h"

FSceneManager::~FSceneManager()
{
	delete mCurrentWorld;
}

void FSceneManager::Update(float deltaTime)
{
	// Todo: Save / Load
	{

	}

	mCurrentWorld->Update(deltaTime);
}

void FSceneManager::NewScene()
{
	if (mCurrentWorld != nullptr)
	{
		delete mCurrentWorld;
	}

	UEngineStatics::SetNextUUID(0);
	ResetSelectedActor();
	mCurrentWorld = FObjectFactory::ConstructObject<UWorld>();
}

void FSceneManager::DeleteScene()
{
	if (mCurrentWorld != nullptr)
	{
		delete mCurrentWorld;
		mCurrentWorld = nullptr;
	}
	ResetSelectedActor();
}

//void FSceneManager::SaveScene(
//	std::string_view sceneName,
//	const FFileManager& fileManager)
//{
//	FString fileName = kSceneDataDir;
//	fileName += sceneName;
//	fileName += kSceneDataSuffix;
//
//	// Read the current scene data to read the Version
//	uint32 version = 0;
//
//	try
//	{
//		FString readSceneString = fileManager.ReadFileToString(fileName);
//		json::JSON readSceneJson = json::JSON::Load(readSceneString);
//
//		if (!readSceneJson.hasKey("Version") || readSceneJson.at("Version").JSONType() != json::JSON::Class::Integral)
//		{
//			version = 0;
//		}
//		else
//		{
//			version = readSceneJson.at("Version").ToInt();
//		}
//	}
//	catch (const std::exception& e)
//	{
//		// If the file does not exist or cannot be read, we can assume it's a new scene and set version to 0
//		version = 0;
//	}
//
//	json::JSON writeSceneJson = json::JSON::Make(json::JSON::Class::Object);
//	json::JSON worldJson = json::JSON::Make(json::JSON::Class::Object);
//	mCurrentWorld->SerializeClass(worldJson);
//
//	writeSceneJson["Version"] = version;
//	writeSceneJson["NextUUID"] = UEngineStatics::GetNextUUID();
//	writeSceneJson["World"] = worldJson;
//
//	FString jsonString = FString(writeSceneJson.dump(1, "  "));
//	fileManager.WriteStringToFile(fileName, jsonString);
//}
//
//void FSceneManager::LoadScene(std::string_view filePath, const FFileManager& fileManager)
//{
//	FString jsonString;
//
//	try
//	{
//		jsonString = fileManager.ReadFileToString(filePath);
//	}
//	catch (const std::exception& e)
//	{
//		UE_LOG_F(Error, Core, "Failed to read scene file {}: {}", filePath, e.what());
//		return;
//	}
//
//	try
//	{
//		json::JSON readSceneJson = json::JSON::Load(jsonString);
//
//		if (!readSceneJson.hasKey("NextUUID") || readSceneJson.at("NextUUID").JSONType() != json::JSON::Class::Integral)
//		{
//			throw std::runtime_error("Scene file does not contain a valid NextUUID.");
//		}
//
//		if (!readSceneJson.hasKey("World") || readSceneJson.at("World").JSONType() != json::JSON::Class::Object)
//		{
//			throw std::runtime_error("Scene file does not contain a valid World.");
//		}
//
//		const uint32 nextUUID = readSceneJson.at("NextUUID").ToInt();
//		const json::JSON& worldJson = readSceneJson.at("World");
//
//		UWorld* newWorld = FObjectFactory::LoadObject<UWorld>(worldJson);
//
//		if (!newWorld)
//		{
//			throw std::runtime_error("Failed to load world.");
//		}
//
//		delete mCurrentWorld;
//		mCurrentWorld = newWorld;
//
//		UEngineStatics::SetNextUUID(nextUUID);
//		ResetSelectedActor();
//	}
//	catch (const std::exception& e)
//	{
//		UE_LOG_F(Error, Core, "Failed to load scene file {}: {}", filePath, e.what());
//	}
//}

void FSceneManager::ReplaceWorld(UWorld* newWorld)
{
	if (!newWorld)
		return;

	delete mCurrentWorld;
	mCurrentWorld = newWorld;

	ResetSelectedActor();
}

void FSceneManager::RemoveActor(AActor* actor)
{
	if (mSelectedActor == actor)
	{
		ResetSelectedActor();
	}

	assert(mCurrentWorld != nullptr);
	mCurrentWorld->RemoveActor(actor->UUID);
}

void  FSceneManager::SetSelectedActor(AActor* actor)
{
	if (actor == nullptr)
	{
		UE_LOG_F(Warning, Core, "SetSelectedActor: Attempted to set selected actor to nullptr.");
		return;
	}

	if (actor == mSelectedActor)
	{
		UE_LOG_F(Log, Core, "SetSelectedActor: Actor with UUID {} is already selected.", actor->UUID);
		return; // No change
	}

	UE_LOG_F(Log, Core, "SetSelectedActor: Actor with UUID {} is now selected.", actor->UUID);
	mSelectedActor = actor;
}

float FSceneManager::GetPanelWidth() const
{
	return mPanelWidth;
}

const TArray<FRenderInfo>& FSceneManager::GetRenderInfos() const
{
	if (mCurrentWorld)
	{
		return mCurrentWorld->GetRenderInfos();
	}

	return TArray<FRenderInfo>();
}

const TArray<FRenderInfo>& FSceneManager::GetAxisRenderInfos() const
{
	static TArray<FRenderInfo> axisRenderInfos;

	if (axisRenderInfos.IsEmpty())
	{
		FRenderInfo renderInfo{};
		renderInfo.eRenderFlags = ERenderFlags::RF_WorldAxis;

		axisRenderInfos.Add(renderInfo);
	}

	return axisRenderInfos;
}
