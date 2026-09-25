#include "ObjViewerApplication.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <string>
#include <cmath>

#include "Core/AssetManager.h"
#include "Core/Math/MathUtility.h"
#include "Core/Object/ObjectFactory.h"
#include "Core/enum.h"

#include "Engine/Actor.h"
#include "Engine/SceneManager.h"
#include "Engine/World.h"
#include "Engine/Components/StaticMeshComponent.h"

#include "Editor/AssetPicker.h"

#include "Rendering/Camera.h"
#include "Rendering/FontResource.h"
#include "Rendering/GpuResourceManager.h"
#include "Rendering/GraphicsManager.h"
#include "Rendering/RenderInfo.h"
#include "Rendering/SceneView.h"
#include "Rendering/Mesh/StaticMesh.h"

#include "ThirdParty/ImGui/imgui.h"
#include "ThirdParty/ImGui/imgui_impl_dx11.h"
#include "ThirdParty/ImGui/imgui_impl_win32.h"
#include "ThirdParty/ImGui/imgui_impl_win32.cpp"

namespace
{
	constexpr wchar_t WindowClassName[] = L"JungleObjViewerWindow";
	constexpr wchar_t WindowTitle[] = L"OBJ Viewer";
	constexpr float ObjListHeight = 56.0f;

	FQuat NormalizeQuaternion(const FQuat& quaternion)
	{
		const float sizeSquared =
			quaternion.x * quaternion.x +
			quaternion.y * quaternion.y +
			quaternion.z * quaternion.z +
			quaternion.w * quaternion.w;

		if (sizeSquared <= 1.0e-8f)
		{
			return FQuat::Identity();
		}

		const float inverseSize = 1.0f / std::sqrt(sizeSquared);

		return FQuat(
			quaternion.x * inverseSize,
			quaternion.y * inverseSize,
			quaternion.z * inverseSize,
			quaternion.w * inverseSize);
	}

	FVector MapCursorToArcball(
		int cursorX,
		int cursorY,
		float viewportWidth,
		float viewportHeight)
	{
		const float diameter = FMath::Max(FMath::Min(viewportWidth, viewportHeight), 1.0f);
		float screenX = (2.0f * static_cast<float>(cursorX) - viewportWidth) / diameter;
		float screenY = (viewportHeight - 2.0f * static_cast<float>(cursorY)) / diameter;
		const float screenLengthSquared = screenX * screenX + screenY * screenY;

		float sphereDepth = 0.0f;
		if (screenLengthSquared <= 1.0f)
		{
			sphereDepth = std::sqrt(1.0f - screenLengthSquared);
		}
		else
		{
			const float inverseLength = 1.0f / std::sqrt(screenLengthSquared);
			screenX *= inverseLength;
			screenY *= inverseLength;
		}

		return FVector(-sphereDepth, screenX, screenY);
	}

	FQuat MakeArcballDelta(const FVector& start, const FVector& current)
	{
		FVector axis = FVector::cross(current, start);
		const float axisLength = axis.Length();
		const float dot = FMath::Clamp(FVector::dot(start, current), -1.0f, 1.0f);

		if (axisLength <= 1.0e-6f)
		{
			if (dot > 0.0f)
			{
				return FQuat::Identity();
			}

			axis = FVector::cross(start, FVector::Up());
			if (axis.Length() <= 1.0e-6f)
			{
				axis = FVector::cross(start, FVector::Right());
			}
		}

		axis.Normalize();
		const float halfAngle = 0.5f * std::atan2(axisLength, dot);
		const float sine = std::sin(halfAngle);

		return NormalizeQuaternion(FQuat(
			axis.x * sine,
			axis.y * sine,
			axis.z * sine,
			std::cos(halfAngle)));
	}
}

FObjViewerApplication::FObjViewerApplication() = default;

FObjViewerApplication::~FObjViewerApplication()
{
	ShutdownRenderer();
}


// Command

void FObjViewerApplication::ProcessViewerCommands(const FEditorCommands& commands)
{
	for (const FEditorCommand& command : commands)
	{
		const auto* materialCommand = std::get_if<FSetStaticMeshComponentMaterialCommand>(&command);

		if (materialCommand != nullptr)
		{
			ProcessViewerCommand(*materialCommand);
		}
	}
}

void FObjViewerApplication::ProcessViewerCommand(const FSetStaticMeshComponentMaterialCommand& command)
{
	UStaticMeshComponent* staticMeshComponent =
		UObject::GetObjectByInternalIndex<UStaticMeshComponent>(command.ObjectID.InternalIndex);

	if (staticMeshComponent == nullptr)
	{
		return;
	}

	const UMaterial* material =
		mAssetManager->FindMaterialAssetOrNull(command.MaterialAssetKey);

	if (material == nullptr)
	{
		return;
	}

	staticMeshComponent->SetMaterial(command.MaterialSlotIndex, *material);
}



int FObjViewerApplication::Run(HINSTANCE instance, int showCommand)
{
	WNDCLASSW windowClass{};
	windowClass.hInstance = instance;
	windowClass.lpfnWndProc = WindowProc;
	windowClass.lpszClassName = WindowClassName;
	windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);

	if (RegisterClassW(&windowClass) == 0)
	{
		return -1;
	}

	mWindow = CreateWindowExW(
		0, WindowClassName, WindowTitle,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		1280, 720, nullptr, nullptr, instance, this);

	if (mWindow == nullptr)
	{
		return -1;
	}

	RAWINPUTDEVICE rawMouseDevice{};
	rawMouseDevice.usUsagePage = 0x01;
	rawMouseDevice.usUsage = 0x02;
	rawMouseDevice.dwFlags = 0;
	rawMouseDevice.hwndTarget = mWindow;
	if (!RegisterRawInputDevices(
		&rawMouseDevice,
		1,
		sizeof(rawMouseDevice)))
	{
		DestroyWindow(mWindow);
		mWindow = nullptr;
		return -1;
	}

	if (!InitializeRenderer())
	{
		DestroyWindow(mWindow);
		mWindow = nullptr;
		return -1;
	}

	ShowWindow(mWindow, showCommand);
	UpdateWindow(mWindow);

	MSG message{};
	bool isRunning = true;


	while (isRunning)
	{
		while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE))
		{
			if (message.message == WM_QUIT)
			{
				isRunning = false;
				break;
			}

			TranslateMessage(&message);
			DispatchMessageW(&message);
		}

		if (!isRunning)
		{
			break;
		}

		Tick();
	}

	ShutdownRenderer();
	mWindow = nullptr;

	return static_cast<int>(message.wParam);
}

// Init

bool FObjViewerApplication::InitializeRenderer()
{
	mAssetManager = std::make_unique<FAssetManager>();

	mGpuResourceManager = std::make_unique<FGpuResourceManager>();

	mGraphicsManager = std::make_unique<FGraphicsManager>();

	mGraphicsManager->Initialize(mWindow, *mGpuResourceManager, *mAssetManager);

	ID3D11Device* device = mGraphicsManager->GetRenderer()->GetDevice();

	if (device == nullptr)
	{
		ShutdownRenderer();
		return false;
	}

	mGpuResourceManager->Initialize(*mAssetManager, *device);
	mCamera = std::make_unique<FCamera>();
	mDefaultFontResource = std::make_unique<FFontResource>();

	FObjectFactory::SetDefaultFont(*mDefaultFontResource);
	FObjectFactory::SetAssetManager(*mAssetManager);

	mSceneManager = std::make_unique<FSceneManager>();
	mSceneManager->NewScene();

	if (!InitializeImGui())
	{
		ShutdownRenderer();
		return false;
	}

	// obj 파일 찾기
	ScanObjFiles();

	UpdateOrbitCamera();

	return true;
}

bool FObjViewerApplication::InitializeImGui()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	if (!ImGui_ImplWin32_Init(mWindow))
	{
		ImGui::DestroyContext();
		return false;
	}

	if (!ImGui_ImplDX11_Init(
		mGraphicsManager->GetRenderer()->GetDevice(),
		mGraphicsManager->GetRenderer()->GetDeviceContext()))
	{
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		return false;
	}

	ImGui::GetIO().IniFilename = nullptr;
	mbImGuiInitialized = true;
	return true;
}


// Camera

void FObjViewerApplication::ResetCameraPosition()
{
	mOrbitPivot = mInitialOrbitPivot;
	mOrbitDistance = mInitialOrbitDistance;
	mOrbitRotation = FRotator(-25.0f, -45.0f, 0.0f).Quaternion();
	ApplyOrbitCameraTransform();
}

void FObjViewerApplication::ApplyOrbitCameraTransform()
{
	if (mCamera == nullptr)
	{
		return;
	}

	const FMatrix orbitMatrix = FMatrix::Rotate(mOrbitRotation);
	const FVector forward = orbitMatrix.GetUnitAxis(EAxis::X);

	mCamera->Location = mOrbitPivot - forward * mOrbitDistance;
}

void FObjViewerApplication::UpdateOrbitCamera()
{
	if (mCamera == nullptr)
	{
		return;
	}

	constexpr float ZoomBase = 0.85f;
	const FInputState& input = mWindowApplication.Input;
	const ImGuiIO& io = ImGui::GetIO();
	const bool isCursorInViewport =
		input.CursorX >= 0 &&
		input.CursorX < static_cast<int>(mClientWidth) &&
		input.CursorY >= static_cast<int>(ObjListHeight) &&
		input.CursorY < static_cast<int>(mClientHeight);
	const bool canStartCameraDrag = isCursorInViewport && !io.WantCaptureMouse;

	if (input.WasPressed(VK_LBUTTON))
	{
		mbOrbitDragging = canStartCameraDrag;
		if (mbOrbitDragging)
		{
			mArcballStartVector = MapCursorToArcball(
				input.CursorX,
				input.CursorY - static_cast<int>(ObjListHeight),
				static_cast<float>(mClientWidth),
				FMath::Max(static_cast<float>(mClientHeight) - ObjListHeight, 1.0f));
			mArcballStartRotation = mOrbitRotation;
		}
	}
	if (input.WasPressed(VK_RBUTTON))
	{
		mbPanDragging = canStartCameraDrag;
	}

	if (input.WasReleased(VK_LBUTTON) || !input.IsDown(VK_LBUTTON))
	{
		mbOrbitDragging = false;
	}
	if (input.WasReleased(VK_RBUTTON) || !input.IsDown(VK_RBUTTON))
	{
		mbPanDragging = false;
	}

	if (mbOrbitDragging)
	{
		const FVector currentArcballVector = MapCursorToArcball(
			input.CursorX,
			input.CursorY - static_cast<int>(ObjListHeight),
			static_cast<float>(mClientWidth),
			FMath::Max(static_cast<float>(mClientHeight) - ObjListHeight, 1.0f));
		const FQuat arcballDelta = MakeArcballDelta(
			mArcballStartVector,
			currentArcballVector);

		mOrbitRotation = NormalizeQuaternion(
			mArcballStartRotation * arcballDelta);
	}

	if (isCursorInViewport && !io.WantCaptureMouse && input.MouseWheelDelta != 0.0f)
	{
		mOrbitDistance *= FMath::Pow(ZoomBase, input.MouseWheelDelta);
		mOrbitDistance = FMath::Clamp(mOrbitDistance, 0.25f, 500.0f);
	}

	ApplyOrbitCameraTransform();

	if (mbPanDragging)
	{
		const float panScale = mOrbitDistance * 0.0015f;

		const FMatrix orbitMatrix = FMatrix::Rotate(mOrbitRotation);
		const FVector right = orbitMatrix.GetUnitAxis(EAxis::Y);
		const FVector up = orbitMatrix.GetUnitAxis(EAxis::Z);

		mOrbitPivot += right * (-input.MouseDX * panScale);
		mOrbitPivot += up * (input.MouseDY * panScale);
	}

	ApplyOrbitCameraTransform();
}


// Obj

void FObjViewerApplication::ScanObjFiles()
{
	mObjFilePaths.clear();

	std::error_code error;
	const std::filesystem::path assetDirectory("Assets");
	if (!std::filesystem::exists(assetDirectory, error)) 
	{
		return;
	}

	std::filesystem::recursive_directory_iterator iterator(assetDirectory,
		std::filesystem::directory_options::skip_permission_denied, error);

	const std::filesystem::recursive_directory_iterator end;

	while (iterator != end)
	{
		if (!error && iterator->is_regular_file(error))
		{
			std::string extension = iterator->path().extension().string();
			std::transform(
				extension.begin(),
				extension.end(),
				extension.begin(),
				[](unsigned char character)
				{
					return static_cast<char>(std::tolower(character));
				});

			if (extension == ".obj")
			{
				mObjFilePaths.push_back(iterator->path());
			}
		}

		iterator.increment(error);
		if (error)
		{
			error.clear();
		}
	}

	std::sort(mObjFilePaths.begin(), mObjFilePaths.end());
}

bool FObjViewerApplication::LoadObjFile(const std::filesystem::path& objPath)
{
	try
	{
		const std::string pathString = objPath.generic_string();
		const UStaticMesh& meshAsset = mAssetManager->FindStaticMeshAssetOrAdd(FName(pathString.c_str()));
		const FStaticMesh* meshData = meshAsset.GetStaticMeshAsset();

		if (meshData == nullptr || meshData->Vertices.IsEmpty())
		{
			return false;
		}

		FVector boundsMin = meshData->Vertices[0].pos;
		FVector boundsMax = boundsMin;

		for (const FNormalVertex& vertex : meshData->Vertices)
		{
			boundsMin.x = FMath::Min(boundsMin.x, vertex.pos.x);
			boundsMin.y = FMath::Min(boundsMin.y, vertex.pos.y);
			boundsMin.z = FMath::Min(boundsMin.z, vertex.pos.z);
			boundsMax.x = FMath::Max(boundsMax.x, vertex.pos.x);
			boundsMax.y = FMath::Max(boundsMax.y, vertex.pos.y);
			boundsMax.z = FMath::Max(boundsMax.z, vertex.pos.z);
		}

		const FVector boundsCenter = (boundsMin + boundsMax) * 0.5f;
		const FVector boundsExtent = (boundsMax - boundsMin) * 0.5f;
		const FVector actorLocation(-boundsCenter.x, -boundsCenter.y, -boundsMin.z);

		mDisplayedActor = mSceneManager->GetCurrentWorld()
			->SpawnActorWithRootComponent<UStaticMeshComponent>(
				FName("DisplayedActor"), nullptr,
				actorLocation, FRotator(0, 0, 0), FVector(1.0f),
				&meshAsset, true);

		mInitialOrbitPivot = FVector(0.0f, 0.0f, boundsExtent.z);
		mInitialOrbitDistance = FMath::Max(boundsExtent.Length() * 2.5f, 0.5f);
		ResetCameraPosition();

		return true;
	}
	catch (const std::exception&)
	{
		return false;
	}
}

void FObjViewerApplication::RenderObjList(FEditorCommands& outCommands)
{
	ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
	ImGui::SetNextWindowSize(ImVec2(static_cast<float>(mClientWidth), ObjListHeight));

	constexpr ImGuiWindowFlags windowFlags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 10.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 8.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.060f, 0.070f, 1.0f));

	ImGui::Begin("OBJ Files", nullptr, windowFlags);
	ImGui::AlignTextToFramePadding();

	ImGui::TextUnformatted("OBJ Viewer");
	ImGui::SameLine(0.0f, 24.0f);

	// 카메라 위치 리셋
	if (ImGui::Button("Reset View"))
	{
		ResetCameraPosition();
	}
	ImGui::SameLine(0.0f, 18.0f);

	// Obj Material
	ImGui::TextDisabled("Object");
	ImGui::SameLine();



	if (mObjFilePaths.empty())
	{
		ImGui::TextDisabled("No OBJ files found in Assets");
	}
	else
	{
		std::string previewLabel = "None";
		if (mSelectedObjIndex >= 0 && static_cast<size_t>(mSelectedObjIndex) < mObjFilePaths.size())
		{
			previewLabel = mObjFilePaths[static_cast<size_t>(mSelectedObjIndex)].filename().string();
		}

		ImGui::SetNextItemWidth(280.0f);
		if (ImGui::BeginCombo("##ObjModel", previewLabel.c_str()))
		{
			const bool isNoneSelected = mSelectedObjIndex < 0;
			if (ImGui::Selectable("None", isNoneSelected))
			{
				if (mDisplayedActor != nullptr)
				{
					mSceneManager->RemoveActor(mDisplayedActor);
					mDisplayedActor = nullptr;
				}
				mSelectedObjIndex = -1;
			}

			if (isNoneSelected)
			{
				ImGui::SetItemDefaultFocus();
			}

			for (size_t index = 0; index < mObjFilePaths.size(); ++index)
			{
				const bool isSelected = static_cast<int32>(index) == mSelectedObjIndex;

				//Obj 식별 Key 생성
				const std::string label = mObjFilePaths[index].filename().string();
				const std::string selectableLabel = label + "##obj_" + std::to_string(index);

				if (ImGui::Selectable(selectableLabel.c_str(), isSelected))
				{
					if (LoadObjFile(mObjFilePaths[index]))
					{
						mSelectedObjIndex = static_cast<int32>(index);
					}
					else
					{
						MessageBoxW(mWindow, L"선택한 OBJ 파일을 불러오지 못했습니다.", L"OBJ Viewer", MB_OK | MB_ICONERROR);
					}
				}

				if (ImGui::IsItemHovered())
				{
					const std::string fullPath =
						mObjFilePaths[index].generic_string();
					ImGui::SetTooltip("%s", fullPath.c_str());
				}

				if (isSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
	}



	UStaticMeshComponent* staticMeshComponent = nullptr;
	if (mDisplayedActor != nullptr)
	{
		staticMeshComponent = mDisplayedActor->GetComponentByType<UStaticMeshComponent>();
	}

	const bool hasMaterialSlots =
		staticMeshComponent != nullptr &&
		staticMeshComponent->GetMaterialSlotCount() > 0;

	ImGui::SameLine(0.0f, 18.0f);

	ImGui::BeginDisabled(!hasMaterialSlots);

	// Material
	if (ImGui::Button("Materials"))
	{
		ImGui::OpenPopup("MaterialPickerPopup");
	}
	ImGui::EndDisabled();

	ImGui::SetNextWindowSizeConstraints(
		ImVec2(320.0f, 0.0f),
		ImVec2(420.0f, 420.0f));

	// Slots
	if (ImGui::BeginPopup("MaterialPickerPopup"))
	{
		ImGui::TextUnformatted("Material Slots");
		ImGui::Separator();

		const int32 materialSlotCount = static_cast<int32>(staticMeshComponent->GetMaterialSlotCount());

		for (int32 slotIndex = 0;
			slotIndex < materialSlotCount;
			++slotIndex)
		{
			FName materialKey =
				staticMeshComponent->GetMaterialAssetKey(slotIndex);

			ImGui::SetNextItemWidth(190.0f);
			if (FAssetPicker::DrawMaterialPicker(
				*mAssetManager,
				materialKey,
				static_cast<uint32>(slotIndex)))
			{
				outCommands.Emplace(
					FSetStaticMeshComponentMaterialCommand{
						staticMeshComponent->GetObjectID(),
						slotIndex,
						materialKey });
			}
		}

		ImGui::EndPopup();
	}

	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(5);
}


void FObjViewerApplication::Tick()
{
	if (mGraphicsManager == nullptr)
	{
		return;
	}

	mWindowApplication.ProcessDeferredEvents();

	if (mbPendingResize)
	{
		mGraphicsManager->GetRenderer()->OnResize(
			mClientWidth,
			mClientHeight,
			static_cast<float>(mClientWidth),
			static_cast<float>(mClientHeight));

		mbPendingResize = false;
	}

	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	UpdateOrbitCamera();

	FEditorCommands viewerCommands;
	RenderObjList(viewerCommands);
	ProcessViewerCommands(viewerCommands);

	mSceneManager->Update(0.0f);
	mGraphicsManager->BeginFrame();

	const FViewRect viewRect{
		0.0f, ObjListHeight, static_cast<float>(mClientWidth),
		FMath::Max( static_cast<float>(mClientHeight) - ObjListHeight, 0.0f)
	};

	{
		FSceneView sceneView = makeSceneView(*mCamera, viewRect, 1.0f);

		const FMatrix cameraRotation =
			FMatrix::Rotate(mOrbitRotation);

		sceneView.cameraLocation = mCamera->Location;
		sceneView.cameraForward = cameraRotation.GetUnitAxis(EAxis::X);
		sceneView.cameraRight = cameraRotation.GetUnitAxis(EAxis::Y);
		sceneView.cameraUp = cameraRotation.GetUnitAxis(EAxis::Z);


		sceneView.viewMatrix = FMatrix::Translation(FVector(
			-mCamera->Location.x, -mCamera->Location.y, -mCamera->Location.z))
			* cameraRotation.Transpose()
			* FMatrix::UEToDX;

		sceneView.viewProjectionMatrix = sceneView.viewMatrix * sceneView.projectionMatrix;

		sceneView.inverseViewprojectionMatrix = sceneView.viewProjectionMatrix.Inverse();

		sceneView.viewMode = EViewModeIndex::VMI_Lit;
		sceneView.showFlags = static_cast<uint32>(EEngineShowFlags::SF_Primitives);

		mGraphicsManager->RenderSceneView(
			mSceneManager->GetRenderInfos(),
			mSceneManager->GetAxisRenderInfos(),
			sceneView,
			nullptr);
	}

	ImGui::Render();
	mGraphicsManager->PrepareForUI();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	mGraphicsManager->Display();
}

void FObjViewerApplication::ShutdownRenderer()
{
	if (mbImGuiInitialized)
	{
		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		mbImGuiInitialized = false;
	}

	mDisplayedActor = nullptr;

	if (mSceneManager != nullptr)
	{
		mSceneManager->DeleteScene();
	}

	mSceneManager.reset();
	mDefaultFontResource.reset();
	mCamera.reset();

	mGraphicsManager.reset();
	mGpuResourceManager.reset();
	mAssetManager.reset();
}

LRESULT CALLBACK FObjViewerApplication::WindowProc(
	HWND window,
	UINT message,
	WPARAM wParam,
	LPARAM lParam)
{
	FObjViewerApplication* application =
		reinterpret_cast<FObjViewerApplication*>(
			GetWindowLongPtrW(window, GWLP_USERDATA));

	if (message == WM_NCCREATE)
	{
		CREATESTRUCTW* createInfo =
			reinterpret_cast<CREATESTRUCTW*>(lParam);

		application =
			static_cast<FObjViewerApplication*>(
				createInfo->lpCreateParams);

		SetWindowLongPtrW(
			window,
			GWLP_USERDATA,
			reinterpret_cast<LONG_PTR>(application));

		if (application != nullptr)
		{
			application->mWindow = window;
		}
	}

	const bool wasHandledByImGui = ImGui_ImplWin32_WndProcHandler(
		window,
		message,
		wParam,
		lParam) != 0;

	switch (message)
	{
	case WM_KEYDOWN:
	case WM_KEYUP:
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
		if (application != nullptr)
		{
			application->mWindowApplication.Defer(
				{ window, message, wParam, lParam });
			SetCapture(window);
		}
		return 0;

	case WM_LBUTTONUP:
	case WM_RBUTTONUP:
		if (application != nullptr)
		{
			application->mWindowApplication.Defer(
				{ window, message, wParam, lParam });
			if ((wParam & (MK_LBUTTON | MK_RBUTTON)) == 0 &&
				GetCapture() == window)
			{
				ReleaseCapture();
			}
		}
		return 0;

	case WM_MOUSEMOVE:
	case WM_MOUSEWHEEL:
		if (application != nullptr)
		{
			application->mWindowApplication.Defer(
				{ window, message, wParam, lParam });
		}
		return 0;

	case WM_INPUT:
		if (application != nullptr)
		{
			FDeferredMessage deferredMessage{
				window,
				message,
				wParam,
				lParam };

			BYTE rawInputBuffer[sizeof(RAWINPUT)];
			UINT rawInputSize = sizeof(rawInputBuffer);
			if (GetRawInputData(
				reinterpret_cast<HRAWINPUT>(lParam),
				RID_INPUT,
				rawInputBuffer,
				&rawInputSize,
				sizeof(RAWINPUTHEADER)) != static_cast<UINT>(-1))
			{
				const RAWINPUT* rawInput =
					reinterpret_cast<const RAWINPUT*>(rawInputBuffer);
				if (rawInput->header.dwType == RIM_TYPEMOUSE &&
					(rawInput->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0)
				{
					deferredMessage.RawMouseDX =
						rawInput->data.mouse.lLastX;
					deferredMessage.RawMouseDY =
						rawInput->data.mouse.lLastY;
				}
			}

			application->mWindowApplication.Defer(deferredMessage);
		}
		return DefWindowProcW(window, message, wParam, lParam);

	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
		if (application != nullptr)
		{
			application->mWindowApplication.Defer(
				{ window, message, wParam, lParam });
		}
		return DefWindowProcW(window, message, wParam, lParam);

	case WM_KILLFOCUS:
		if (application != nullptr)
		{
			application->mWindowApplication.Defer(
				{ window, message, wParam, lParam });
		}
		if (GetCapture() == window)
		{
			ReleaseCapture();
		}
		return 0;

	case WM_SIZE:
		if (application != nullptr &&
			wParam != SIZE_MINIMIZED)
		{
			application->mClientWidth = LOWORD(lParam);

			application->mClientHeight = HIWORD(lParam);

			application->mbPendingResize = true;
		}
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	default:
		if (wasHandledByImGui)
		{
			return 1;
		}
		return DefWindowProcW(
			window,
			message,
			wParam,
			lParam);
	}
}
