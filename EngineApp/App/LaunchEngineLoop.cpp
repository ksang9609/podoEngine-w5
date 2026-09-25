#include "LaunchEngineLoop.h"

#include <stdexcept>
#include <windows.h>

#include "Core/Name.h"
#include "Core/BuiltinAssets.h"
#include "Core/Object/Object.h"
#include "Core/Object/ObjectFactory.h"
#include "Core/Object/ObjectIterator.h"
#include "Editor/Console.h"
#include "Editor/EditorUIManager.h"
#include "Editor/EditorFileUtils.h"
#include "Editor/EditorViewportManager.h"

#include "Editor/Viewport.h"
#include "Engine/Actor.h"
#include "Engine/Components/CubeComponent.h"
#include "Engine/Components/SphereComponent.h"
#include "Engine/Components/ParticleSubUVComponent.h"
#include "Engine/Components/StaticMeshComponent.h"
#include "Engine/SceneManager.h"
#include "Engine/World.h"
#include "Platform/WindowApplication.h"
#include "Rendering/GraphicsManager.h"
#include "Rendering/Primitives/GizmoArrow.h"
#include "Rendering/Renderer.h"
#include "Rendering/FontResource.h"
#include "Rendering/SceneView.h"

#include "ThirdParty/ImGui/imgui.h"
#include "ThirdParty/ImGui/imgui_impl_dx11.h"
#include "ThirdParty/ImGui/imgui_impl_win32.h"

// Primitive vertices definitions
#include "Rendering/Primitives/Circle.h"
#include "Rendering/Primitives/Cube.h"
#include "Rendering/Primitives/Primitives.h"
#include "Rendering/Primitives/Sphere.h"
#include "Rendering/Primitives/TexturedPrimitives.h"
#include "Rendering/Primitives/Triangle.h"

void FEngineLoop::Init(HINSTANCE hInstance, WNDPROC WndProc)
{
	// Initialize window infos
	WCHAR WindowClass[] = L"JungleWindowClass";
	WCHAR Title[] = L"PODO";
	WNDCLASSW wndclass = { 0, WndProc, 0, 0, 0, 0, 0, 0, 0, WindowClass };
	RegisterClassW(&wndclass);

	HWND hWnd = CreateWindowExW(
		0,
		WindowClass,
		Title,
		WS_VISIBLE | WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, 600, 1024,
		nullptr, nullptr, hInstance, nullptr
	);

	// 창을 화면 크기에 맞게 최대화하여 표시
	ShowWindow(hWnd, SW_SHOWMAXIMIZED);
	UpdateWindow(hWnd);

	// 최대화된 후의 실제 클라이언트 크기를 구해 콘솔에 전달
	RECT clientRect;
	GetClientRect(hWnd, &clientRect);
	int clientWidth = clientRect.right - clientRect.left;
	int clientHeight = clientRect.bottom - clientRect.top;

	RAWINPUTDEVICE rid = {};
	rid.usUsagePage = 0x01;		// Generic Desktop
	rid.usUsage = 0x02;			// Mouse
	rid.dwFlags = 0;		// 포커스 있을 때만 수신
	rid.hwndTarget = hWnd;
	RegisterRawInputDevices(&rid, 1, sizeof(rid));

	/* Init Managers */
	mGraphicsManager = new FGraphicsManager();
	FrameTimer = new FFrameTimer(120);
	mAssetManager = std::make_unique<FAssetManager>();
	mGpuResourceManager = std::make_unique<FGpuResourceManager>();

	mEditorViewportManager = new FEditorViewportManager();
	if (!mEditorViewportManager->Initialize(*mAssetManager))
	{
		throw std::runtime_error("Failed to initialize the editor viewport manager.");
	}

	FViewport* initialViewport = mEditorViewportManager->getActiveViewport();
	if (initialViewport == nullptr)
	{
		throw std::runtime_error("The initial editor viewport was not created.");
	}

	viewportClient = &initialViewport->getClient();

	mSceneManager = new FSceneManager();
	mFileManager = new FFileManager();

	mGraphicsManager->Initialize(hWnd, *mGpuResourceManager, *mAssetManager);
	mGpuResourceManager->Initialize(
		*mAssetManager, *mGraphicsManager->GetRenderer()->GetDevice());


	mGraphicsManager->RenderLoadingScreen();

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui_ImplWin32_Init((void*)hWnd);
	ImGui_ImplDX11_Init(mGraphicsManager->GetRenderer()->GetDevice(), mGraphicsManager->GetRenderer()->GetDeviceContext());
	ImGui::GetIO().IniFilename = "Config/imgui.ini";


	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->AddFontFromFileTTF("Assets/Fonts/malgun.ttf", 16.0f, NULL, io.Fonts->GetGlyphRangesKorean());

	/* Console Window */
	ConsoleWindow& console = ConsoleWindow::GetInstance();
	console.Init("Jungle Console Window", clientWidth);

	/* Resource Registration */
	mDefaultFontResource = std::make_unique<FFontResource>();

	const bool jsonLoaded = mDefaultFontResource->LoadUnicodeAtlas(
		FString("Assets/Fonts/KoreanFullAtlas.json"));

	mGpuResourceManager->CreateUnicodeFontTexture("Assets/Fonts/KoreanFullAtlas.png");
	mGpuResourceManager->SetDistanceRange(mDefaultFontResource->GetDistanceRange());

	FObjectFactory::SetDefaultFont(*mDefaultFontResource);
	FObjectFactory::SetAssetManager(*mAssetManager);

	// 예시 텍스쳐 사용용
	const int columns = 4;
	const int rows = 4;
	const int faceCells[6] = { 6, 4, 13, 5, 1, 9 };

	// 24: 인덱스 방식 / 36: 기존 방식
	FVertexTextured atlasVertices[24];

	TArray<FVertexTextured> sphereIndexVertices;
	TArray<UINT> sphereIndices;

	mGpuResourceManager->CreateTextureFromDDS("Assets/Textures/CubeTextureSample.dds");
	mGpuResourceManager->CreateTextureFromDDS("Assets/Textures/EarthTexture.dds");
	mGpuResourceManager->CreateTextureFromDDS("Assets/Textures/Explosion_Alpha.dds");

	const FVector4 NearTint(1.0f, 0.65f, 0.15f, 0.85f); // 주황 = 가까운 쪽
	const FVector4 FarTint(0.25f, 0.55f, 1.0f, 0.85f); // 파랑 = 먼 쪽

	mSceneManager->NewScene();


	{
		const UStaticMesh& cubeMeshAsset = mAssetManager->FindStaticMeshAssetOrAdd(BuiltinAssets::CubeMesh);
		mSceneManager->GetCurrentWorld()
			->SpawnActorWithRootComponent<UStaticMeshComponent>(
				FName("CubeActor"), mDefaultFontResource.get(),
				FVector(2, 0, 0), FRotator(0, 0, 0), FVector(1, 1, 1), &cubeMeshAsset);
	}
	{
		const UStaticMesh& sphereMeshAsset = mAssetManager->FindStaticMeshAssetOrAdd(BuiltinAssets::SphereMesh);
		mSceneManager->GetCurrentWorld()
			->SpawnActorWithRootComponent<UStaticMeshComponent>(
				FName("SphereActor"), mDefaultFontResource.get(),
				FVector(-2, 0, 0), FRotator(0, 0, 0), FVector(1, 1, 1), &sphereMeshAsset);
	}
	{
		const UStaticMesh& earthMesh = mAssetManager->FindStaticMeshAssetOrAdd(BuiltinAssets::SphereMesh);
		mSceneManager->GetCurrentWorld()
			->SpawnActorWithRootComponent<USphereComponent>(
				FName("EarthActor"), mDefaultFontResource.get(),
				FVector(0, 0, 1), FRotator(0, 0, 0), FVector(1, 1, 1), &earthMesh,
				true);

	}

	mEditorUIManager = new FEditorUIManager(ImGui::GetIO());

	FEditorCommands startupCommands;
	mEditorUIManager->LoadSettings(*mEditorViewportManager, startupCommands);
	processEditorCommands(startupCommands);
}

void FEngineLoop::Tick(bool bPumpMessages)
{
	if (GInTick) return;
	GInTick = true;

	FrameTimer->StartFrame();
	float deltaTime = FrameTimer->GetDeltaTime();

	mStatManager.UpdateFrame(*FrameTimer);
	mStatManager.UpdateMemory(deltaTime,{*mSceneManager,*mAssetManager,*mGpuResourceManager});

	ConsoleWindow& console = ConsoleWindow::GetInstance();

	//Input Threads

	WindowApplication.ProcessDeferredEvents();

	FViewport* activeViewport = mEditorViewportManager->getActiveViewport();
	if (activeViewport != nullptr)
	{
		viewportClient = &activeViewport->getClient();
	}

	//ImGui Input
	{
		//mSceneManager->UpdateGUI({ *FrameTimer, mGraphicsManager, ViewportClient, mFileManager });
	}

		if (viewportClient != nullptr)
		{
			FEditorCommands editorCommands;
			mEditorUIManager->UpdateGui({
				*FrameTimer,
				*mSceneManager,
				*viewportClient,
				*mGraphicsManager,
				*mFileManager,
				*mAssetManager,
				*mEditorViewportManager,
				mStatManager,
				},viewportClient->getSharedSettings(), editorCommands);
			processEditorCommands(editorCommands);

	}

	// UI에서 활성 Viewport가 변경되었을 수 있으므로 다시 조회한다.
	activeViewport = mEditorViewportManager->getActiveViewport();

	if (activeViewport != nullptr)
	{
		viewportClient = &activeViewport->getClient();
	}
	else
	{
		viewportClient = nullptr;
	}

	//Viewports
	const uint8 viewportCount = mEditorViewportManager->getViewportCount();
	for (uint8 i = 0; i < viewportCount; i++)
	{
		FViewport* viewport = mEditorViewportManager->getViewportAt(i);
		if (viewport == nullptr)
		{
			continue;
		}
		viewport->getClient().updateProjectionTransition(deltaTime);
	}

	//활성 Viewport의 카메라 입력과 RayCast 처리
	if (activeViewport != nullptr)
	{
		FSceneView activeSceneView = activeViewport->buildSceneView();

		if (activeSceneView.isValid())
		{
			const FViewportWindowState& windowState = activeViewport->getWindowState();
			activeViewport->getClient().Update(deltaTime, activeSceneView.Rect, mSceneManager, windowState.bImageHovered, windowState.bFocused);
		}
	}


	//Physics Threads
	{

	}

	//Game Threads
	{
		// 레이캐스트보다 먼저 돌려야 한다.
		// 여기서 RenderInfos 가 갱신되고, RayCast 가 그걸 읽는다.
		mSceneManager->Update(deltaTime);
	}

	//Render Threads
	{
		if (WindowApplication.bPendingResize)
		{
			float viewportWidth = mEditorUIManager->GetPanelWidth();
			float viewportHeight = static_cast<float>(WindowApplication.PendingHeight) - FEditorUIManager::BOTTOM_BAR_HEIGHT;

			if (viewportHeight < 0.0f)
			{
				viewportHeight = 0.0f;
			}

			mGraphicsManager->GetRenderer()->OnResize(WindowApplication.PendingWidth, WindowApplication.PendingHeight, viewportWidth, viewportHeight);
			WindowApplication.bPendingResize = false;
		}

		mGraphicsManager->BeginFrame();
		AActor* selectedActor = mSceneManager->GetSelectedActor();

		// Viewport 수만큼 같은 Scene을 다른 Camera로 렌더링
		for (uint8 i = 0; i < viewportCount; i++)
		{
			FViewport* viewport = mEditorViewportManager->getViewportAt(i);

			if (viewport == nullptr)
			{
				continue;
			}

			FEditorViewportClient& client = viewport->getClient();
			const FSceneView sceneView = viewport->buildSceneView();

			if (!sceneView.isValid())
			{
				continue;
			}

			client.UpdateGizmoForView(selectedActor);

			mGraphicsManager->RenderSceneView(
				mSceneManager->GetRenderInfos(),
				mSceneManager->GetAxisRenderInfos(),
				sceneView,
				selectedActor);
		}

		// Scene의 Depth만 한 번 초기화
		mGraphicsManager->ClearDepth();

		// 각 Viewport의 Gizmo 렌더링
		for (uint8 i = 0; i < viewportCount; ++i)
		{
			FViewport* viewport = mEditorViewportManager->getViewportAt(i);

			if (viewport == nullptr)
			{
				continue;
			}

			FEditorViewportClient& client = viewport->getClient();

			const FSceneView sceneView = viewport->buildSceneView();

			if (!sceneView.isValid())
			{
				continue;
			}
			client.UpdateGizmoForView(selectedActor);

			const TArray<FRenderInfo> gizmoRenderInfos = client.GetGizmo().GetGizmoRenderInfo();

			mGraphicsManager->RenderGizmoView(gizmoRenderInfos, sceneView);
		}


		//ImGui
		{
			ImGui::Render();
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		}

		// 테스트용 쿼드 그리기
		//mGraphicsManager->GetRenderer()->RenderTestQuad();


		///
		mGraphicsManager->Display();
	}

	FrameTimer->EndFrame();

	GInTick = false;
}


void FEngineLoop::End()
{
	mEditorUIManager->saveSettings(*mEditorViewportManager);
	mSceneManager->DeleteScene();

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	delete mEditorUIManager;
	delete FrameTimer;
	delete mSceneManager;
	delete mEditorViewportManager;
	delete mFileManager;
	delete mGraphicsManager;
	mAssetManager.reset();
}

void FEngineLoop::processEditorCommands(const FEditorCommands& commands)
{
	// Process each command except for the delete command first
	for (const auto& command : commands)
	{
		if (std::holds_alternative<FDeleteActorCommand>(command))
		{
			continue; // Skip delete commands for now
		}

		std::visit(
			[this](const auto& cmd)
			{
				processEditorCommand(cmd);
			},
			command
		);
	}

	// Process delete commands last to avoid issues with dangling references
	for (const auto& command : commands)
	{
		if (const auto* deleteActorCommand =
			std::get_if<FDeleteActorCommand>(&command))
		{
			processEditorCommand(*deleteActorCommand);
		}
	}
}

void FEngineLoop::processEditorCommand(const FNewSceneCommand& command)
{
	mSceneManager->NewScene();
}

void FEngineLoop::processEditorCommand(const FSaveSceneCommand& command)
{
	FCamera* perspectiveCamera = mEditorViewportManager->getPerspectiveCamera();
	//mSceneManager->SaveScene(command.SceneName, *mFileManager);
	FEditorFileUtils::SaveScene(
		mSceneManager->GetCurrentWorld(), perspectiveCamera
	);
}

void FEngineLoop::processEditorCommand(const FSaveSceneAsCommand& command)
{
	FCamera* perspectiveCamera = mEditorViewportManager->getPerspectiveCamera();
	//mSceneManager->SaveScene(command.SceneName, *mFileManager);
	FEditorFileUtils::SaveSceneAs(
		mSceneManager->GetCurrentWorld(), perspectiveCamera
	);
}

void FEngineLoop::processEditorCommand(const FLoadSceneCommand& command)
{
	//mSceneManager->LoadScene(command.SceneName, *mFileManager);
	FLoadedScene loaded = FEditorFileUtils::LoadScene();

	if (loaded.World == nullptr)
	{
		return;
	}

	mSceneManager->ReplaceWorld(loaded.World);

	if (loaded.PerspectiveCamera.has_value())
	{
		mEditorViewportManager->applyPerspectiveCamera(
			*loaded.PerspectiveCamera);
	}
	else
	{
		mEditorViewportManager->resetPerspectiveCamera();
	}

	for (TObjectIterator<UStaticMeshComponent> it; it; ++it)
	{
		UStaticMeshComponent* staticMeshComponent = *it;
		const FName& assetKey = staticMeshComponent->GetStaticMeshAssetKey();
		const TArray<FName> materialKeys = staticMeshComponent->GetMaterialAssetKeys();

		/* Load Static Mesh */
		if (assetKey == FName())
		{
			continue;
		}

		try
		{
			const UStaticMesh& staticMesh =
				mAssetManager->FindStaticMeshAssetOrAdd(assetKey);
			staticMeshComponent->SetStaticMesh(staticMesh);
		}
		catch (const std::exception& exception)
		{
			UE_LOG(Error, Editor,
				"Failed to restore static mesh: %s (%s)",
				assetKey.ToString().CStr(), exception.what());
		}

		/* Load Materials */
		{
			for (uint32 i = 0; i < materialKeys.Num(); ++i)
			{
				if (materialKeys[i] == FName())
				{
					continue;
				}
				const UMaterial* material =
					mAssetManager->FindMaterialAssetOrNull(materialKeys[i]);

				if (!material)
				{
					UE_LOG_F(Error, Editor,
						"Failed to restore material: {}",
						materialKeys[i].ToString().CStr());
					continue;
				}

				staticMeshComponent->SetMaterial(i, *material);
			}
		}
	}
}

void FEngineLoop::processEditorCommand(const FLoadObjCommand& command)
{
	try
	{
		const UStaticMesh& importedMesh = mAssetManager->FindStaticMeshAssetOrAdd(FName(command.ObjFilePath));

		UE_LOG(Log, Editor, "OBJ imported: %s", importedMesh.GetAssetPathFileName().ToString().CStr());
	}
	catch (const std::exception& exception)
	{
		UE_LOG(Error, Editor, "Failed to import OBJ: %s (%s)", command.ObjFilePath.CStr(), exception.what());
	}
}

void FEngineLoop::processEditorCommand(const FSpawnActorCommand& command)
{
	//for (int32 i = 0; i < command.SpawnCount; ++i)
	//{
	//	AActor* newActor = FObjectFactory::SpawnPrimitiveActor(
	//		command.PrimitiveType,
	//		FVector(0, 0, 0), FRotator(0, 0, 0), FVector(1, 1, 1)
	//	);
	//	mSceneManager->GetCurrentWorld()->AddActor(newActor);
	//}
}

void FEngineLoop::processEditorCommand(const FSpawnStaticMeshActorCommand& command)
{
	for (int32 i = 0; i < command.SpawnCount; ++i)
	{
		const UStaticMesh& staticMesh = mAssetManager->FindStaticMeshAssetOrAdd(command.StaticMeshKey);
		mSceneManager->GetCurrentWorld()
			->SpawnActorWithRootComponent<UStaticMeshComponent>(
				FName("StaticMeshActor"), mDefaultFontResource.get(),
				FVector(0, 0, 0), FRotator(0, 0, 0), FVector(1, 1, 1),
				&staticMesh);
	}
}

void FEngineLoop::processEditorCommand(const FDeleteActorCommand& command)
{
	AActor* actor = UObject::GetObjectByInternalIndex<AActor>(command.ObjectID.InternalIndex);
	if (actor)
	{
		mSceneManager->RemoveActor(actor);
	}
}

void FEngineLoop::processEditorCommand(const FSpawnParticleCommand& command)
{
	mSceneManager->GetCurrentWorld()
		->SpawnActorWithRootComponent<UParticleSubUVComponent>(
			FName("ParticleActor"), mDefaultFontResource.get(),
			FVector(0, 0, 0), FRotator(0, 0, 0), FVector(1, 1, 1),
			6, 6
		);
}

void FEngineLoop::processEditorCommand(const FSetActorLocationCommand& command)
{
	AActor* actor = UObject::GetObjectByInternalIndex<AActor>(command.ObjectID.InternalIndex);
	if (actor)
	{
		actor->SetLocation(command.Location);
	}
}

void FEngineLoop::processEditorCommand(const FSetActorRotationCommand& command)
{
	AActor* actor = UObject::GetObjectByInternalIndex<AActor>(command.ObjectID.InternalIndex);
	if (actor)
	{
		actor->SetRotation(command.Rotation);
	}
}

void FEngineLoop::processEditorCommand(const FSetActorScaleCommand& command)
{
	AActor* actor = UObject::GetObjectByInternalIndex<AActor>(command.ObjectID.InternalIndex);
	if (actor)
	{
		actor->SetScale(command.Scale);
	}
}

void FEngineLoop::processEditorCommand(const FSetActorNameCommand& command)
{
	AActor* actor = UObject::GetObjectByInternalIndex<AActor>(command.ObjectID.InternalIndex);
	if (actor)
	{
		actor->SetName(command.NewName);
	}
}

void FEngineLoop::processEditorCommand(const FSetSelectedActorCommand& command)
{
	AActor* actor = UObject::GetObjectByInternalIndex<AActor>(command.ObjectID.InternalIndex);
	if (actor)
	{
		mSceneManager->SetSelectedActor(actor);
	}
	else
	{
		mSceneManager->ResetSelectedActor();
	}
}

void FEngineLoop::processEditorCommand(const FSetStaticMeshComponentStaticMeshCommand& command)
{
	UStaticMeshComponent* staticMeshComponent = UObject::GetObjectByInternalIndex<UStaticMeshComponent>(command.ObjectID.InternalIndex);
	if (!staticMeshComponent) return;

	const UStaticMesh* staticMesh = mAssetManager->FindStaticMeshAssetOrNull(command.StaticMeshAssetKey);
	if (!staticMesh) return;

	staticMeshComponent->SetStaticMesh(*staticMesh);
}

void FEngineLoop::processEditorCommand(const FSetStaticMeshComponentMaterialCommand& command)
{
	UStaticMeshComponent* staticMeshComponent = UObject::GetObjectByInternalIndex<UStaticMeshComponent>(command.ObjectID.InternalIndex);
	if (!staticMeshComponent) return;
	const UMaterial* material = mAssetManager->FindMaterialAssetOrNull(command.MaterialAssetKey);
	if (!material) return;
	staticMeshComponent->SetMaterial(command.MaterialSlotIndex, *material);
}

void FEngineLoop::processEditorCommand(const FSetStaticMeshComponentSubUVCommand& command)
{
	UStaticMeshComponent* staticMeshComponent = UObject::GetObjectByInternalIndex<UStaticMeshComponent>(command.ObjectID.InternalIndex);
	if (!staticMeshComponent) return;

	staticMeshComponent->SetSubUVMesh(command.UVOffset, command.UVScale);
}

void FEngineLoop::processEditorCommand(const FSetComponentUseTextureCommand& command)
{
	UPrimitiveComponent* component = UObject::GetObjectByInternalIndex<UPrimitiveComponent>(command.ObjectID.InternalIndex);
	if (component)
	{
		component->SetUseTexture(command.bUseTexture);
	}
	else
	{
		UE_LOG_F(Warning, Editor, "Component with ObjectID {} is not a UPrimitiveComponent.", command.ObjectID.InternalIndex);
	}
}

void FEngineLoop::processEditorCommand(const FSetComponentColorCommand& command)
{
	UPrimitiveComponent* component = UObject::GetObjectByInternalIndex<UPrimitiveComponent>(command.ObjectID.InternalIndex);
	if (component)
	{
		component->SetColor(command.Color);
	}
	else
	{
		UE_LOG_F(Warning, Editor, "Component with ObjectID {} is not a UPrimitiveComponent.", command.ObjectID.InternalIndex);
	}
}

void FEngineLoop::processEditorCommand(const FSetSphereComponentSpinCommand& command)
{
	USphereComponent* sphereComponent = UObject::GetObjectByInternalIndex<USphereComponent>(command.ObjectID.InternalIndex);
	if (sphereComponent)
	{
		sphereComponent->SetSpin(command.bSpin);
	}
	else
	{
		UE_LOG_F(Warning, Editor, "Component with ObjectID {} is not a USphereComponent.", command.ObjectID.InternalIndex);
	}
}

void FEngineLoop::processEditorCommand(const FSetSphereComponentSpinSpeedCommand& command)
{
	USphereComponent* sphereComponent = UObject::GetObjectByInternalIndex<USphereComponent>(command.ObjectID.InternalIndex);
	if (sphereComponent)
	{
		sphereComponent->SetSpinSpeed(command.SpinSpeed);
	}
	else
	{
		UE_LOG_F(Warning, Editor, "Component with ObjectID {} is not a USphereComponent.", command.ObjectID.InternalIndex);
	}
}

void FEngineLoop::processEditorCommand(const FSetParticleSubUVComponentLoopingCommand& command)
{
	UParticleSubUVComponent* particleComponent = UObject::GetObjectByInternalIndex<UParticleSubUVComponent>(command.ObjectID.InternalIndex);
	if (particleComponent)
	{
		particleComponent->SetLooping(command.bLooping);
	}
	else
	{
		UE_LOG_F(Warning, Editor, "Component with ObjectID {} is not a UParticleSubUVComponent.", command.ObjectID.InternalIndex);
	}
}

void FEngineLoop::processEditorCommand(const FSetParticleSubUVComponentPlayRateCommand& command)
{
	UParticleSubUVComponent* particleComponent = UObject::GetObjectByInternalIndex<UParticleSubUVComponent>(command.ObjectID.InternalIndex);
	if (particleComponent)
	{
		particleComponent->SetPlayRate(command.PlayRate);
	}
	else
	{
		UE_LOG_F(Warning, Editor, "Component with ObjectID {} is not a UParticleSubUVComponent.", command.ObjectID.InternalIndex);
	}
}

void FEngineLoop::processEditorCommand(const FSetParticleSubUVComponentBlendStateTypeCommand& command)
{
	UParticleSubUVComponent* particleComponent = UObject::GetObjectByInternalIndex<UParticleSubUVComponent>(command.ObjectID.InternalIndex);
	if (particleComponent)
	{
		particleComponent->SetBlendStateType(command.BlendStateType);
	}
	else
	{
		UE_LOG_F(Warning, Editor, "Component with ObjectID {} is not a UParticleSubUVComponent.", command.ObjectID.InternalIndex);
	}
}

void FEngineLoop::processEditorCommand(const FSetPropertyCommand& command)
{
	UObject* object = UObject::GetObjectByInternalIndex<UObject>(command.ObjectID.InternalIndex);
	if (object)
	{
		object->SetPropertyValue(command.PropertyName, command.NewValue);
	}
	else
	{
		UE_LOG_F(Warning, Editor, "Object with ObjectID {} not found.", command.ObjectID.InternalIndex);
	}
}

void FEngineLoop::processEditorCommand(const FSetViewModeCommand& command)
{
	mGraphicsManager->SetViewMode(command.ViewMode);
}

void FEngineLoop::processEditorCommand(const FSetShowFlagCommand& command)
{
	mGraphicsManager->SetShowFlags(command.ShowFlags);
}

void FEngineLoop::processEditorCommand(const FSetCameraSensitivityCommand& command)
{
	viewportClient->GetCamera().SetCameraSensitivity(command.Sensitivity);
}

void FEngineLoop::processEditorCommand(const FSetCameraFovCommand& command)
{
	viewportClient->GetCamera().mFovDegree = command.Fov;
}

void FEngineLoop::processEditorCommand(const FSetCameraLocationCommand& command)
{
	viewportClient->GetCamera().Location = command.Location;
}

void FEngineLoop::processEditorCommand(const FSetCameraRotationCommand& command)
{
	viewportClient->GetCamera().Rotation = command.Rotation;
}

void FEngineLoop::processEditorCommand(const FSetGizmoModeCommand& command)
{
	viewportClient->mGizmo.SetGizmoType(command.GizmoMode);
}

void FEngineLoop::processEditorCommand(const FCycleGizmoModeCommand& command)
{
	viewportClient->mGizmo.CycleGizmoType();
}

void FEngineLoop::processEditorCommand(const FSetGridWidthCommand& command)
{
	mGraphicsManager->SetGridWidth(command.GridWidth);
}

void FEngineLoop::processEditorCommand(const FStartProjectionTransitionCommand& command)
{
	FViewport* activeViewport = mEditorViewportManager->getActiveViewport();
	AActor* selectedActor = mSceneManager->GetSelectedActor();
	if (selectedActor && command.bOrthographic && activeViewport->getClient().getProjectionRatio() == 1.0f)
	{
		const FVector offset = selectedActor->GetTransform().Location - activeViewport->getClient().GetCamera().Location;
		const float depth = FVector::dot(offset, activeViewport->getClient().GetCamera().GetForwardVector());
		activeViewport->getClient().GetCamera().mOrthoDistance = FMath::Max(depth, 0.1f);
	}
	activeViewport->getClient().startProjectionTransition(command.bOrthographic);
}

void FEngineLoop::processEditorCommand(const FSetViewportTypeCommand& command)
{
	FViewport* viewport =
		mEditorViewportManager->findViewport(command.viewportId);

	if (viewport == nullptr)
	{
		return;
	}

	const FCamera& camera = viewport->getClient().GetCamera();

	// 선택 액터가 없으면 현재 시선 앞쪽을 중심으로 사용한다.
	FVector pivot =
		camera.Location +
		camera.GetForwardVector() *
		FMath::Max(camera.mOrthoDistance, 0.1f);

	if (AActor* selectedActor = mSceneManager->GetSelectedActor())
	{
		pivot = selectedActor->GetTransform().Location;
	}

	viewport->transitionToType(command.Type, pivot);

	mEditorUIManager->saveSettings(*mEditorViewportManager);
}

void FEngineLoop::processEditorCommand(const FSetViewportViewModeCommand& command)
{
	FViewport* viewport = mEditorViewportManager->findViewport(command.viewportId);
	if (viewport == nullptr) { return; }
	viewport->getRenderSettings().ViewMode = command.ViewMode;
	mEditorUIManager->saveSettings(*mEditorViewportManager);
}

void FEngineLoop::processEditorCommand(const FSetViewportShowFlagCommand& command)
{
	FViewport* viewport = mEditorViewportManager->findViewport(command.viewportId);
	if (viewport == nullptr) { return; }
	viewport->getRenderSettings().SetShowFlag(command.Flag, command.bEnabled);
	mEditorUIManager->saveSettings(*mEditorViewportManager);
}

void FEngineLoop::processEditorCommand(const FSetSharedCameraSpeedCommand& command)
{
	mEditorViewportManager->setCameraSpeed(FMath::Clamp(command.Speed, 0.1f, 100.0f));
}

void FEngineLoop::processEditorCommand(const FSetSharedSnapPresetCommand& command)
{
	mEditorViewportManager->setSnapPreset(command.PresetIndex);

	const float gridSpacing = mEditorViewportManager->getSharedSettings().getSnapSize();

	mGraphicsManager->SetGridWidth(gridSpacing);
}

void FEngineLoop::processEditorCommand(const FSetViewportFovCommand& command)
{
	FViewport* viewport = mEditorViewportManager->findViewport(command.ViewportId);
	if (viewport != nullptr)
	{
		viewport->getClient().GetCamera().mFovDegree = FMath::Clamp(command.Fov, 5.0f, 175.0f);
	}
}

void FEngineLoop::processEditorCommand(const FToggleStatCommand& command)
{
	const bool enabled = mStatManager.Toggle(command.Group);
	const char* groupName = "Unknown";

	switch (command.Group)
	{
	case EStatGroup::FPS:
		groupName = "FPS";
		break;

	case EStatGroup::Memory:
		groupName = "Memory";
		break;

	default:
		break;
	}

	UE_LOG_F(
		Log,
		Editor,
		"Stat {} {}",
		groupName,
		enabled ? "enabled" : "disabled");
}

void FEngineLoop::processEditorCommand(const FDisableAllStatsCommand&)
{
	mStatManager.DisableAll();

	UE_LOG(
		Log,
		Editor,
		"All stats disabled");
}
