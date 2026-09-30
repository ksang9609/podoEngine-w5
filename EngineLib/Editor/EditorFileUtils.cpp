#include "EditorFileUtils.h"

#include "Core/Object/ObjectFactory.h"
#include "Core/IO/FileManager.h"
#include "Editor/Console.h"
#include "Editor/SceneConverter.h"

#include "Engine/EngineStatics.h"
#include "Engine/World.h"
#include "Engine/Serialization/JsonWriter.h"
#include "Engine/Serialization/JsonReader.h"

#include <filesystem>
#include <Windows.h>
#include <commdlg.h>

FString FEditorFileUtils::mCurrentScenePath = "";

bool FEditorFileUtils::SaveScene(UWorld* world, FCamera* perspectiveCamera)
{
	if (!world)
	{
		UE_LOG_F(Error, Core, "SaveScene failed: World is null");
		return false;
	}

	// 저장 경로 결정
	if (mCurrentScenePath.IsEmpty())
	{
		UE_LOG_F(Log, Core, "SaveScene: No current scene path. Redirecting to Save As");
		return SaveSceneAs(world, perspectiveCamera);
	}

	if (!saveSceneToPath(world, mCurrentScenePath, perspectiveCamera))
	{
		return false;
	}

	UE_LOG_F(Log, Core, "Scene saved: {}", mCurrentScenePath.CStr());
	return true;
}

bool FEditorFileUtils::SaveSceneAs(UWorld* world, FCamera* perspectiveCamera)
{
	if (!world)
	{
		UE_LOG_F(Error, Core, "SaveSceneAs failed: World is null");
		return false;
	}

	FString filePath = openSaveSceneDialog();

	if (filePath.IsEmpty())
	{
		UE_LOG_F(Log, Core, "SaveSceneAs canceled");
		return false;
	}

	std::filesystem::path normalizedPath = std::filesystem::absolute(filePath.CStr()).lexically_normal();
	FString normalizedScenePath(normalizedPath.string());

	if (!saveSceneToPath(world, normalizedScenePath, perspectiveCamera))
	{
		UE_LOG_F(Error, Core, "SaveSceneAs failed: {}", normalizedPath.string().c_str());
		return false;
	}

	mCurrentScenePath = normalizedPath.string();

	UE_LOG_F(Log, Core, "Scene saved as: {}", mCurrentScenePath.CStr());

	return true;
}

bool FEditorFileUtils::saveSceneToPath(UWorld* world, const FString& filePath, FCamera* perspectiveCamera)
{
	FJsonWriter jsonWriter(filePath.CStr());

	// Serialize the world and camera to JSON
	uint32 nextUUID = UEngineStatics::GetNextUUID();
	jsonWriter << TNamedValue{ "NextUUID", nextUUID };
	jsonWriter << TNamedValue{ "World", world };
	jsonWriter << TNamedValue{ "PerspectiveCamera", perspectiveCamera };

	if (jsonWriter.HasError())
	{
		UE_LOG_F(Error, Core, "Scene write failed: Error during serialization");
		assert(false);
		return false;
	}

	jsonWriter.SaveToFile(filePath.CStr());

	return true;
}

FLoadedScene FEditorFileUtils::LoadScene()
{
	FLoadedScene result;

	FString filePath = openLoadSceneDialog();

	if (filePath.IsEmpty())
	{
		UE_LOG_F(Log, Core, "LoadScene canceled");
		return result;
	}

	std::filesystem::path normalizedPath = std::filesystem::absolute(filePath.CStr()).lexically_normal();
	FString normalizedScenePath(normalizedPath.string());

	FJsonReader jsonReader(normalizedPath.string());

	uint32 nextUUID = 0;
	jsonReader << TNamedValue{ "NextUUID", nextUUID };
	if (jsonReader.HasError())
	{
		UE_LOG_F(Error, Core, "LoadScene failed: NextUUID not found in the scene file");
		assert(false);
		return {};
	}

	if (!jsonReader.BeginObject("World"))
	{
		UE_LOG_F(Error, Core, "LoadScene failed: World object not found in the scene file");
		assert(false);
		return {};
	}
	UWorld* loadedWorld = FObjectFactory::LoadObject<UWorld>(jsonReader);

	jsonReader.EndObject();
	if (jsonReader.HasError() || !loadedWorld)
	{
		UE_LOG_F(Error, Core, "LoadScene failed: World creation failed");
		assert(false);
		delete loadedWorld;
		return {};
	}

	result.World = loadedWorld;

	FCamera camera;
	jsonReader << TNamedValue{ "PerspectiveCamera", camera };

	if (jsonReader.HasError())
	{
		UE_LOG_F(Log, Core, "LoadScene: No PerspectiveCamera found in the scene file");
		assert(false);
		delete loadedWorld;
		return {};
	}

	UEngineStatics::SetNextUUID(nextUUID);
	result.PerspectiveCamera = std::move(camera);

	return result;

}

bool FEditorFileUtils::ConvertLegacySceneToNewFormat()
{
	FString filePath = openLegacySceneDialog();
	if (filePath.IsEmpty())
	{
		UE_LOG_F(Log, Core, "ConvertLegacySceneToNewFormat canceled");
		return false;
	}
	std::filesystem::path normalizedPath = std::filesystem::absolute(filePath.CStr()).lexically_normal();
	FString normalizedScenePath(normalizedPath.string());
	FString newFilePath = normalizedScenePath;
	newFilePath.Append(std::string_view("_converted.Scene"));

	std::string errorMessage;

	if (!FSceneConverter::ConvertFile(normalizedScenePath.CStr(), newFilePath.CStr(), errorMessage))
	{
		UE_LOG_F(Error, Core, "ConvertLegacySceneToNewFormat failed: Conversion failed: {}", errorMessage.c_str());
		return false;
	}
	return true;
}

FString FEditorFileUtils::openSaveSceneDialog()
{
	char filePath[MAX_PATH] = {};

	// OPENFILENAME 설정
	OPENFILENAMEA openFileName = {};
	openFileName.lStructSize = sizeof(OPENFILENAMEA);
	openFileName.hwndOwner = nullptr;
	openFileName.lpstrFile = filePath;
	openFileName.nMaxFile = MAX_PATH;

	openFileName.lpstrFilter = "Scene Files (*.Scene)\0*.Scene\0" "All Files (*.*)\0*.*\0";
	openFileName.lpstrDefExt = "Scene";
	openFileName.lpstrInitialDir = "EngineLib/Assets/SceneData/";
	openFileName.nFilterIndex = 1;

	openFileName.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;


	// FileName 반환
	if (!GetSaveFileNameA(&openFileName))
	{
		return FString("");
	}

	return FString(filePath);
}

FString FEditorFileUtils::openLoadSceneDialog()
{
	char filePath[MAX_PATH] = {};

	// OPENFILENAME 설정
	OPENFILENAMEA openFileName = {};
	openFileName.lStructSize = sizeof(OPENFILENAMEA);
	openFileName.hwndOwner = nullptr;
	openFileName.lpstrFile = filePath;
	openFileName.nMaxFile = MAX_PATH;

	openFileName.lpstrFilter = "Scene Files (*.Scene)\0*.Scene\0" "All Files (*.*)\0*.*\0";
	openFileName.lpstrDefExt = "Scene";
	openFileName.lpstrInitialDir = "SceneData/";
	openFileName.nFilterIndex = 1;

	openFileName.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;


	// FileName 반환
	if (!GetOpenFileNameA(&openFileName))
	{
		return FString("");
	}

	return FString(filePath);
}

FString FEditorFileUtils::openLegacySceneDialog()
{
	char filePath[MAX_PATH] = {};
	// OPENFILENAME 설정
	OPENFILENAMEA openFileName = {};
	openFileName.lStructSize = sizeof(OPENFILENAMEA);
	openFileName.hwndOwner = nullptr;
	openFileName.lpstrFile = filePath;
	openFileName.nMaxFile = MAX_PATH;

	openFileName.lpstrFilter = "Legacy Scene Files (*.scene)\0*.scene\0" "All Files (*.*)\0*.*\0";
	openFileName.lpstrDefExt = "scene";
	openFileName.lpstrInitialDir = "SceneData/";
	openFileName.nFilterIndex = 1;

	openFileName.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

	if (!GetOpenFileNameA(&openFileName))
	{
		return FString("");
	}

	return FString(filePath);
}
