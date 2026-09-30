#pragma once

#include <filesystem>
#include <memory>
#include <vector>
#include <windows.h>

#include "Core/FrameTimer.h"
#include "Core/Math/Vector.h"
#include "Core/Object/Object.h"
#include "Core/Math/Quat.h"


#include "Editor/EditorCommands.h"
#include "Platform/WindowApplication.h"

class AActor;
class FAssetManager;
class FCamera;
class FFontResource;
class FGpuResourceManager;
class FGraphicsManager;
class FSceneManager;

class FObjViewerApplication
{
public:
	FObjViewerApplication();
	~FObjViewerApplication();

	int Run(HINSTANCE instance, int showCommand);

private:

	void ProcessViewerCommands(const FEditorCommands& commands);
	void ProcessViewerCommand(const FSetStaticMeshComponentMaterialCommand& command);

	bool InitializeRenderer();
	bool InitializeImGui();

	void ResetCameraPosition();
	void ApplyOrbitCameraTransform();
	void UpdateOrbitCamera();

	void ScanObjFiles();
	bool LoadObjFile(const std::filesystem::path& objPath);
	void RenderObjList(FEditorCommands& outCommands);

	void Tick();
	void ShutdownRenderer();

	static LRESULT CALLBACK WindowProc(
		HWND window,
		UINT message,
		WPARAM wParam,
		LPARAM lParam);

	HWND mWindow = nullptr;

	UINT mClientWidth = 1280;
	UINT mClientHeight = 720;
	bool mbPendingResize = false;

	FVector mOrbitPivot = FVector(0.0f, 0.0f, 0.0f);

	// Object와의 거리
	float mOrbitDistance = 120.0f;

	FQuat mOrbitRotation = FQuat::Identity();
	FVector mArcballStartVector = FVector(0.0f, 0.0f, 0.0f);
	FQuat mArcballStartRotation = FQuat::Identity();

	bool mbOrbitDragging = false;
	bool mbPanDragging = false;
	FVector mInitialOrbitPivot = FVector(0.0f, 0.0f, 0.0f);
	float mInitialOrbitDistance = 6.0f;

	bool mbImGuiInitialized = false;

	FFrameTimer mFrameTimer{ 12000 };

	// 입력을 받을 변수
	FWindowApplication mWindowApplication;

	std::vector<std::filesystem::path> mObjFilePaths;
	int32 mSelectedObjIndex = -1;

	std::unique_ptr<FAssetManager> mAssetManager;
	std::unique_ptr<FGpuResourceManager> mGpuResourceManager;
	std::unique_ptr<FGraphicsManager> mGraphicsManager;
	std::unique_ptr<FCamera> mCamera;
	std::unique_ptr<FFontResource> mDefaultFontResource;
	std::unique_ptr<FSceneManager> mSceneManager;
	AActor* mDisplayedActor = nullptr;
};
