#include "EditorUIManager.h"

#include "ThirdParty/ImGui/imgui.h"
#include "ThirdParty/ImGui/imgui_impl_dx11.h"
#include "ThirdParty/ImGui/imgui_impl_win32.h"

#include "Core/FrameTimer.h"
#include "Core/IO/FileManager.h"
#include "Core/AssetManager.h"
#include "Core/Name.h"
#include "Core/Object/ObjectIterator.h"

#include "Rendering/GraphicsManager.h"
#include "Engine/EngineStatics.h"
#include "Engine/SceneManager.h"
#include "Engine/Components/ActorComponent.h"
#include "Engine/Components/PrimitiveComponent.h"
#include "Engine/Components/SphereComponent.h"
#include "Engine/Components/ParticleSubUVComponent.h"
#include "Engine/Components/StaticMeshComponent.h"
#include "Engine/Stats/StatManager.h"

/* Editor */
#include "FEditorViewportClient.h"
#include "AssetPicker.h"
#include "EditorViewportManager.h"
#include "Viewport.h"
#include "Console.h"
#include "Editor/StatOverlay.h"

#include <algorithm>

namespace
{
	struct FViewportTypeOption
	{
		const char* Label;
		EViewportType Type;
	};

	struct FViewModeOption
	{
		const char* Label;
		EViewModeIndex Mode;
	};

	struct FShowFlagOption
	{
		const char* Label;
		EEngineShowFlags Flag;
	};

	constexpr FViewportTypeOption viewportTypeOptions[] =
	{
		{ "Perspective", EViewportType::Perspective },
		{ "Top", EViewportType::Top },
		{ "Bottom", EViewportType::Bottom },
		{ "Front", EViewportType::Front },
		{ "Back", EViewportType::Back },
		{ "Left", EViewportType::Left },
		{ "Right", EViewportType::Right }
	};

	constexpr FViewModeOption viewModeOptions[] =
	{
		{ "Lit", EViewModeIndex::VMI_Lit },
		{ "Unlit", EViewModeIndex::VMI_Unlit },
		{ "Wireframe", EViewModeIndex::VMI_Wireframe }
	};

	constexpr FShowFlagOption showFlagOptions[] =
	{
		{ "Primitives", EEngineShowFlags::SF_Primitives },
		{ "Billboard Text", EEngineShowFlags::SF_BillboardText },
		{ "World Axis", EEngineShowFlags::SF_WorldAxis },
		{ "Bounding Box", EEngineShowFlags::SF_BoundingBox },
		{ "Grid", EEngineShowFlags::SF_Grid }
	};

	constexpr ImU32 viewportToolbarColors[] =
	{
		IM_COL32(2, 2, 2, 255),
		IM_COL32(2, 2, 2, 255),
		IM_COL32(2, 2, 2, 255),
		IM_COL32(2, 2, 2, 255)
	};

	const char* getViewportTypeName(EViewportType type)
	{
		for (const FViewportTypeOption& option : viewportTypeOptions)
		{
			if (option.Type == type)
			{
				return option.Label;
			}
		}

		return "Unknown";
	}

	const char* getViewModeName(EViewModeIndex mode)
	{
		for (const FViewModeOption& option : viewModeOptions)
		{
			if (option.Mode == mode)
			{
				return option.Label;
			}
		}

		return "Unknown";
	}

	bool drawViewportSharedControls(FEditorViewportManager& viewportManager, FViewportSharedSettings& sharedSettings, FEditorCommands& outCommands)
	{

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 1.0f)); // 기본
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.5f, 0.5f, 1.0f)); // Hover
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.7f, 0.7f, 1.0f)); // 클릭

		const uint8 viewportCount = viewportManager.getViewportCount();

		bool layoutChanged = false;

		ImGui::BeginDisabled(viewportCount >= maxViewportCount);
		if (ImGui::Button(" + "))
		{
			layoutChanged = viewportManager.addViewport() != invalidViewportId;
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		ImGui::BeginDisabled(viewportCount <= 1);
		if (ImGui::Button(" - "))
		{
			const FViewport* lastViewport =
				viewportManager.getViewportAt(viewportCount - 1);

			if (lastViewport != nullptr)
			{
				layoutChanged = viewportManager.removeViewport(lastViewport->getId());
			}
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		ImGui::Text(
			"%d / %d",
			viewportManager.getViewportCount(),
			static_cast<int32>(maxViewportCount));

		ImGui::SameLine();

		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.50f, 0.50f, 0.50f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));

		float speed = sharedSettings.cameraSpeed;
		ImGui::SetNextItemWidth(80.0f);
		if (ImGui::DragFloat("Camera Speed", &speed, 0.1f, 0.1f, 100.0f))
		{
			outCommands.Emplace(FSetSharedCameraSpeedCommand{ speed });
		}

		int presetIndex = sharedSettings.snapPresetIndex;
		const char* presets[] = { "0.1", "0.5", "1.0", "2.0", "3.0", "4.0", "5.0" };

		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.50f, 0.50f, 0.50f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));

		ImGui::SameLine();
		ImGui::SetNextItemWidth(100.0f);
		if (ImGui::Combo(
			"Grid Snap", &presetIndex,
			presets, IM_ARRAYSIZE(presets)))
		{
			outCommands.Emplace(FSetSharedSnapPresetCommand{ static_cast<uint8>(presetIndex) });
		}

		ImGui::Separator();
		ImGui::PopStyleColor(9);

		return layoutChanged;
	}

	bool updateSplitterInteraction(
		FEditorViewportManager& viewportManager,
		const FRect& hostRect,
		const FPoint& mousePoint)
	{
		if (hostRect.contains(mousePoint) &&
			ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			viewportManager.beginSplitterDrag(mousePoint);
		}

		if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			viewportManager.updateSplitterDrag(mousePoint);
		}

		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			return viewportManager.endSplitterDrag();
		}
		return false;
	}

	void updateViewportInteraction(
		FEditorViewportManager& viewportManager,
		const FPoint& mousePoint)
	{
		const bool bViewportWindowHovered =
			ImGui::IsWindowHovered();

		const bool bAssetDragging =
			ImGui::GetDragDropPayload() != nullptr;

		const bool bCanInteractWithViewport =
			bViewportWindowHovered && !bAssetDragging;

		const bool bActivationClick =
			bCanInteractWithViewport && (
				ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
				ImGui::IsMouseClicked(ImGuiMouseButton_Right));

		FViewport* clickedViewport = nullptr;

		for (uint8 index = 0;
			index < viewportManager.getViewportCount();
			++index)
		{
			FViewport* viewport = viewportManager.getViewportAt(index);
			if (viewport == nullptr)
			{
				continue;
			}

			FViewportWindowState& windowState = viewport->getWindowState();
			windowState.bImageHovered =
				bCanInteractWithViewport &&
				windowState.imageRect.contains(mousePoint);

			if (bActivationClick)
			{
				windowState.bFocused = false;

				if (windowState.bImageHovered)
				{
					clickedViewport = viewport;
				}
			}
		}

		if (clickedViewport != nullptr)
		{
			viewportManager.setActiveViewport(clickedViewport->getId());
			clickedViewport->getWindowState().bFocused = true;
		}
	}

	void drawViewportTypeSelector(
		const FViewport& viewport,
		float buttonWidth,
		float buttonHeight,
		FEditorCommands& outCommands)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 1.0f)); // 기본
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.5f, 0.5f, 1.0f)); // Hover
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.7f, 0.7f, 1.0f)); // 클릭
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.5f, 0.5f));

		if (ImGui::Button(
			getViewportTypeName(viewport.getType()),
			ImVec2(buttonWidth, buttonHeight)))
		{
			ImGui::OpenPopup("CameraMenu");
		}
		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar();
		if (!ImGui::BeginPopup("CameraMenu"))
		{
			return;
		}

		for (int32 index = 0; index < IM_ARRAYSIZE(viewportTypeOptions); ++index)
		{
			if (index == 1)
			{
				ImGui::Separator();
			}

			const FViewportTypeOption& option = viewportTypeOptions[index];
			const bool bSelected = viewport.getType() == option.Type;

			if (ImGui::MenuItem(option.Label, nullptr, bSelected))
			{
				outCommands.Emplace(FSetViewportTypeCommand{
					viewport.getId(),
					option.Type
					});
			}
		}

		ImGui::EndPopup();
	}

	void drawViewportViewModeSelector(
		const FViewport& viewport,
		float buttonWidth,
		float buttonHeight,
		FEditorCommands& outCommands)
	{
		const FViewportRenderSettings& settings = viewport.getRenderSettings();
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 1.0f)); // 기본
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.5f, 0.5f, 1.0f)); // Hover
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.7f, 0.7f, 1.0f)); // 클릭
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.5f, 0.5f));
		if (ImGui::Button(
			getViewModeName(settings.ViewMode),
			ImVec2(buttonWidth, buttonHeight)))
		{
			ImGui::OpenPopup("ViewModeMenu");
		}
		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar();
		if (!ImGui::BeginPopup("ViewModeMenu"))
		{
			return;
		}

		for (const FViewModeOption& option : viewModeOptions)
		{
			const bool bSelected = settings.ViewMode == option.Mode;

			if (ImGui::MenuItem(option.Label, nullptr, bSelected))
			{
				outCommands.Emplace(FSetViewportViewModeCommand{
					viewport.getId(),
					option.Mode
					});
			}
		}

		ImGui::EndPopup();
	}

	void drawViewportShowFlagSelector(
		const FViewport& viewport,
		float buttonWidth,
		float buttonHeight,
		FEditorCommands& outCommands)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 1.0f)); // 기본
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.5f, 0.5f, 1.0f)); // Hover
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.7f, 0.7f, 1.0f)); // 클릭
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.5f, 0.5f));

		if (ImGui::Button("Show", ImVec2(buttonWidth, buttonHeight)))
		{
			ImGui::OpenPopup("ShowFlagsMenu");
		}
		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar();
		if (!ImGui::BeginPopup("ShowFlagsMenu"))
		{
			return;
		}

		const FViewportRenderSettings& settings = viewport.getRenderSettings();

		for (const FShowFlagOption& option : showFlagOptions)
		{
			const bool bEnabled = settings.HasShowFlag(option.Flag);

			if (ImGui::MenuItem(option.Label, nullptr, bEnabled))
			{
				outCommands.Emplace(FSetViewportShowFlagCommand{
					viewport.getId(),
					option.Flag,
					!bEnabled
					});
			}
		}

		ImGui::EndPopup();
	}

	void drawViewportFovSelector(
		const FViewport& viewport,
		float buttonWidth,
		float buttonHeight,
		FEditorCommands& outCommands)
	{
		if (viewport.getType() != EViewportType::Perspective)
		{
			return;
		}

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.5f, 0.5f));

		if (ImGui::Button("FOV", ImVec2(buttonWidth, buttonHeight)))
		{
			ImGui::OpenPopup("FovMenu");
		}

		ImGui::PopStyleVar();
		ImGui::PopStyleColor(3);

		if (ImGui::BeginPopup("FovMenu"))
		{
			float fov = viewport.getClient().GetFov();
			ImGui::SetNextItemWidth(180.0f);

			if (ImGui::SliderFloat("##FOV", &fov, 5.0f, 175.0f, "%.0f"))
			{
				outCommands.Emplace(FSetViewportFovCommand{ viewport.getId(), fov });
			}

			ImGui::SameLine();
			ImGui::TextUnformatted("FOV");
			ImGui::EndPopup();
		}
	}

	void drawCompactViewportMenu(
		const FViewport& viewport,
		float buttonWidth,
		float buttonHeight,
		FEditorCommands& outCommands)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.5f, 0.5f));

		if (ImGui::Button("Options", ImVec2(buttonWidth, buttonHeight)))
		{
			ImGui::OpenPopup("CompactViewportMenu");
		}

		ImGui::PopStyleVar();
		ImGui::PopStyleColor(3);

		if (!ImGui::BeginPopup("CompactViewportMenu"))
		{
			return;
		}

		if (ImGui::BeginMenu("View"))
		{
			for (int32 index = 0; index < IM_ARRAYSIZE(viewportTypeOptions); ++index)
			{
				if (index == 1)
				{
					ImGui::Separator();
				}

				const FViewportTypeOption& option = viewportTypeOptions[index];
				const bool bSelected = viewport.getType() == option.Type;

				if (ImGui::MenuItem(option.Label, nullptr, bSelected))
				{
					outCommands.Emplace(FSetViewportTypeCommand{
						viewport.getId(),
						option.Type
						});
				}
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("View Mode"))
		{
			const FViewportRenderSettings& settings = viewport.getRenderSettings();

			for (const FViewModeOption& option : viewModeOptions)
			{
				const bool bSelected = settings.ViewMode == option.Mode;

				if (ImGui::MenuItem(option.Label, nullptr, bSelected))
				{
					outCommands.Emplace(FSetViewportViewModeCommand{
						viewport.getId(),
						option.Mode
						});
				}
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Show"))
		{
			const FViewportRenderSettings& settings = viewport.getRenderSettings();

			for (const FShowFlagOption& option : showFlagOptions)
			{
				const bool bEnabled = settings.HasShowFlag(option.Flag);

				if (ImGui::MenuItem(option.Label, nullptr, bEnabled))
				{
					outCommands.Emplace(FSetViewportShowFlagCommand{
						viewport.getId(),
						option.Flag,
						!bEnabled
						});
				}
			}

			ImGui::EndMenu();
		}

		if (viewport.getType() == EViewportType::Perspective)
		{
			ImGui::Separator();
			float fov = viewport.getClient().GetFov();
			ImGui::SetNextItemWidth(180.0f);

			if (ImGui::SliderFloat("FOV", &fov, 5.0f, 175.0f, "%.0f"))
			{
				outCommands.Emplace(FSetViewportFovCommand{ viewport.getId(), fov });
			}
		}

		ImGui::EndPopup();
	}

	void drawViewportOverlay(
		const FViewport& viewport,
		uint8 viewportIndex,
		bool bActive,
		const FStatManager& statManager,
		ImDrawList& drawList,
		FEditorCommands& outCommands)
	{
		const FViewportWindowState& windowState = viewport.getWindowState();
		const FRect& panelRect = windowState.panelRect;
		const FRect& imageRect = windowState.imageRect;

		const ImU32 toolbarColor =
			viewportToolbarColors[viewportIndex % IM_ARRAYSIZE(viewportToolbarColors)];

		drawList.AddRectFilled(
			ImVec2(panelRect.Left, panelRect.Top),
			ImVec2(panelRect.Right, imageRect.Top),
			toolbarColor);

		const ImU32 borderColor = bActive
			? IM_COL32(255, 210, 70, 255)
			: IM_COL32(150, 150, 160, 255);

		drawList.AddRect(
			ImVec2(panelRect.Left, panelRect.Top),
			ImVec2(panelRect.Right, panelRect.Bottom),
			borderColor,
			0.0f,
			0,
			bActive ? 3.0f : 1.0f);

		constexpr float sidePadding = 4.0f;
		constexpr float buttonHeight = 23.0f;
		constexpr float minButtonWidth = 82.0f;

		const float toolbarWidth = panelRect.getWidth();
		const float contentWidth = (std::max)(toolbarWidth - sidePadding * 2.0f, 1.0f);
		const bool bPerspective = viewport.getType() == EViewportType::Perspective;
		const int32 itemCount = bPerspective ? 4 : 3;
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const float requiredWidth =
			minButtonWidth * static_cast<float>(itemCount) +
			spacing * static_cast<float>(itemCount - 1);
		const bool bCompact = contentWidth < requiredWidth;

		const ImVec2 savedCursor = ImGui::GetCursorScreenPos();
		const ImVec2 toolbarMin(panelRect.Left, panelRect.Top);
		const ImVec2 toolbarMax(panelRect.Right, imageRect.Top);

		ImGui::PushClipRect(toolbarMin, toolbarMax, true);
		ImGui::SetCursorScreenPos(
			ImVec2(panelRect.Left + sidePadding, panelRect.Top + 3.0f));
		ImGui::PushID(static_cast<int>(viewport.getId()));

		if (bCompact)
		{
			drawCompactViewportMenu(
				viewport,
				contentWidth,
				buttonHeight,
				outCommands);
		}
		else
		{
			const float buttonWidth =
				(contentWidth - spacing * static_cast<float>(itemCount - 1)) /
				static_cast<float>(itemCount);

			ImGui::SetWindowFontScale(1.3f);

			drawViewportTypeSelector(viewport, buttonWidth, buttonHeight, outCommands);
			ImGui::SameLine();
			drawViewportViewModeSelector(viewport, buttonWidth, buttonHeight, outCommands);
			ImGui::SameLine();
			drawViewportShowFlagSelector(viewport, buttonWidth, buttonHeight, outCommands);

			ImGui::SetWindowFontScale(1.0f);

			if (bPerspective)
			{
				ImGui::SameLine();
				drawViewportFovSelector(viewport, buttonWidth, buttonHeight, outCommands);
			}
		}

		ImGui::PopID();
		ImGui::SetCursorScreenPos(savedCursor);
		ImGui::PopClipRect();

		if (bActive)
		{
			StatOverlay::Draw(
				imageRect,
				statManager,
				drawList);
		}
	}
}

bool drawPropertyValue(
	const char* label,
	const FPropertyValue& originalValue,
	FPropertyValue& outValue)
{
	return std::visit(
		[label, &outValue](auto& typedValue) -> bool
		{
			using T = std::decay_t<decltype(typedValue)>;

			if constexpr (std::is_same_v<T, bool>)
			{
				bool value = typedValue;
				if (ImGui::Checkbox(label, &value))
				{
					outValue = value;
					return true;
				}
				return false;
			}
			else if constexpr (std::is_same_v<T, int32>)
			{
				int32 value = typedValue;
				if (ImGui::InputInt(label, &value))
				{
					outValue = value;
					return true;
				}
				return false;
			}
			else if constexpr (std::is_same_v<T, uint32>)
			{
				uint32 value = typedValue;
				if (ImGui::InputScalar(label, ImGuiDataType_U32, &value))
				{
					outValue = value;
					return true;
				}
				return false;
			}
			else if constexpr (std::is_same_v<T, float>)
			{
				float value = typedValue;
				if (ImGui::DragFloat(label, &value, 0.1f))
				{
					outValue = value;
					return true;
				}
				return false;
			}
			else if constexpr (std::is_same_v<T, FString>)
			{
				char buffer[256];
				std::strncpy(buffer, typedValue.CStr(), sizeof(buffer));
				if (ImGui::InputText(label, buffer, sizeof(buffer)))
				{
					outValue = FString(buffer);
					return true;
				}
				return false;
			}
			else if constexpr (std::is_same_v<T, FName>)
			{
				char buffer[256];
				std::strncpy(buffer, typedValue.ToString().CStr(), sizeof(buffer));
				if (ImGui::InputText(label, buffer, sizeof(buffer)))
				{
					outValue = FName(buffer);
					return true;
				}
				return false;
			}
			else if constexpr (std::is_same_v<T, FVector2>)
			{
				FVector2 value = typedValue;
				if (ImGui::DragFloat2(label, &value.x, 0.1f))
				{
					outValue = value;
					return true;
				}
				return false;
			}
			else if constexpr (std::is_same_v<T, FVector3>)
			{
				FVector3 value = typedValue;
				if (ImGui::DragFloat3(label, &value.x, 0.1f))
				{
					outValue = value;
					return true;
				}
				return false;
			}
			else if constexpr (std::is_same_v<T, FVector4>)
			{
				FVector4 value = typedValue;
				if (ImGui::DragFloat4(label, &value.x, 0.1f))
				{
					outValue = value;
					return true;
				}
				return false;
			}
			else if constexpr (std::is_same_v<T, FRotator>)
			{
				// Rotator is stored as Pitch, Yaw, Roll,
				// but we want to display it as Roll, Pitch, Yaw in the UI.
				float value[3] = { typedValue.Roll, typedValue.Pitch, typedValue.Yaw };
				if (ImGui::DragFloat3(label, value, 0.1f))
				{
					outValue = FRotator{ value[1], value[2], value[0] };
					return true;
				}
				return false;
			}
			else if constexpr (std::is_same_v<T, FLinearColor>)
			{
				FLinearColor value = typedValue;
				if (ImGui::ColorEdit4(label, &value.R))
				{
					outValue = value;
					return true;
				}
				return false;
			}
			else
			{
				ImGui::TextDisabled("%s: Unsupported property type", label);
				return false;
			}
		},
		originalValue
	);
}

FEditorUIManager::FEditorUIManager(const ImGuiIO& io)
	: mGuiInputField()
	, mEditorSetting()
	, mImGuiIO(io)
{
	mPanelWidth = io.DisplaySize.x * MIN_WIDTH_RATIO;
}

void FEditorUIManager::LoadSettings(FEditorViewportManager& viewportManager, FEditorCommands& outCommands)
{
	mEditorSetting.Load();
	viewportManager.applyLayoutSetting(mEditorSetting);
	// Load settings into commands
	outCommands.Emplace(FSetCameraSensitivityCommand{ mEditorSetting.CameraSensitivity });
	outCommands.Emplace(FSetGridWidthCommand{ mEditorSetting.GridSpacing });
}

void FEditorUIManager::UpdateGui(const FGuiReference& guiReference, FViewportSharedSettings& sharedsettings, FEditorCommands& outCommands)
{
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	drawMainMenuBar(outCommands);
	updateControlPanelGUI(guiReference, outCommands);
	updatePropertyWindowGUI(guiReference, outCommands);
	updateObjectListPanelGUI(guiReference, outCommands);
	updateViewportLayoutPanelGUI(guiReference.ViewportManager, sharedsettings, guiReference.StatManager, outCommands);

	updateBottomBarGUI(guiReference, outCommands);
}

FString saveSceneFileDialog();
FString openSceneFileDialog();
FString openObjFileDialog();
void FEditorUIManager::drawMainMenuBar(FEditorCommands& outCommands)
{
	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New Scene"))
			{
				outCommands.Emplace(FNewSceneCommand{});
			}
			if (ImGui::MenuItem("Open Scene..."))
			{
				outCommands.Emplace(FLoadSceneCommand{});
			}
			if (ImGui::MenuItem("Save"))
			{
				outCommands.Emplace(FSaveSceneCommand{ FString(mGuiInputField.SceneName) });
			}
			if (ImGui::MenuItem("Save As..."))
			{
				outCommands.Emplace(FSaveSceneAsCommand{ FString(mGuiInputField.SceneName) });
			}


			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}
}

void FEditorUIManager::updateControlPanelGUI(const FGuiReference& guiReference, FEditorCommands& outCommands)
{
	const ImGuiViewport* mainViewport = ImGui::GetMainViewport();

	const float workPosY = mainViewport->WorkPos.y;
	const float workHeight = mainViewport->WorkSize.y;
	const float panelHeight = workHeight * CONTROL_PANEL_HEIGHT_RATIO;


	//float panelHeight = mImGuiIO.DisplaySize.y * CONTROL_PANEL_HEIGHT_RATIO;

	const ImGuiViewport* viewport = ImGui::GetMainViewport();

	ImGui::SetNextWindowPos(
		ImVec2(0.0f, viewport->WorkPos.y),
		ImGuiCond_Always
	);

	ImGui::SetNextWindowSizeConstraints(
		ImVec2(mImGuiIO.DisplaySize.x * MIN_WIDTH_RATIO, panelHeight),
		ImVec2(mImGuiIO.DisplaySize.x * MAX_WIDTH_RATIO, panelHeight)
	);
	ImGui::SetNextWindowSize(ImVec2(mPanelWidth, panelHeight), ImGuiCond_Always);

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;

	/* Begin ImGui Window */
	ImGui::Begin("PODO", nullptr, flags);
	mPanelWidth = ImGui::GetWindowWidth();

	//ImGui::Text("FPS: %.1f  dt: %.4f", guiReference.FrameTimer.GetFPS(), guiReference.FrameTimer.GetDeltaTime());

	if (ImGui::Button("Import Obj"))
	{
		const FString selectedPath = openObjFileDialog();

		if (selectedPath.Len() > 0)
		{
			outCommands.Emplace(FLoadObjCommand{ selectedPath });
		}
	}

	/* Spawn Actor */
	// NOTE: This name array must be edited when adding new primitive types to EPrimitive enum.
	ImGui::SeparatorText("Spawn Actor");

	//const char* primitiveTypeNames[] = { "Sphere", "Cube", "Triangle" };
	//int32 primitiveTypeIndex = static_cast<int32>(mGuiInputField.PrimitiveType);

	const char* meshNames[] = { "Cube", "Sphere" };
	const FName meshKeys[] = {
		BuiltinAssets::CubeMesh,
		BuiltinAssets::SphereMesh
	};

	int32 spawnCount = mGuiInputField.SpawnCount;

	if (ImGui::Combo("Static Mesh", &mGuiInputField.SelectedMeshIndex, meshNames, IM_ARRAYSIZE(meshNames)))
	{
		//mGuiInputField.StaticMeshKey = meshKeys[mGuiInputField.SelectedMeshIndex];
	}
	if (ImGui::Button("Spawn"))
	{
		//outCommands.Emplace(FSpawnActorCommand{ mGuiInputField.PrimitiveType, mGuiInputField.SpawnCount });
		outCommands.Emplace(FSpawnStaticMeshActorCommand{ meshKeys[mGuiInputField.SelectedMeshIndex], mGuiInputField.SpawnCount });
	}
	ImGui::SameLine();
	if (ImGui::InputInt("Number of spawn", &spawnCount))
	{
		if (spawnCount < 1)
		{
			spawnCount = 1;
		}
		mGuiInputField.SpawnCount = spawnCount;
	}
	if (ImGui::Button("Spawn Particle"))
	{
		outCommands.Emplace(FSpawnParticleCommand{});
	}

	/* Scene Control 삭제예정 */
	//ImGui::SeparatorText("Scene Control");

	//ImGui::InputText("Scene Name", mGuiInputField.SceneName, IM_ARRAYSIZE(mGuiInputField.SceneName), ImGuiInputTextFlags_ReadOnly);
	//if (ImGui::Button("New scene"))
	//{
	//	// TODO: add clear depth buffer function in renderer
	//	//guiReference.ViewportClient->Reset();
	//	//NewScene();
	//	outCommands.Emplace(FNewSceneCommand{});
	//	strcpy_s(mGuiInputField.SceneName, sizeof(mGuiInputField.SceneName), "Default");
	//}
	//ImGui::SameLine();
	//if (ImGui::Button("Save scene"))
	//{
	//	const FString selectedFile = saveSceneFileDialog();

	//	if (selectedFile.Len() > 0)
	//	{
	//		const std::filesystem::path selectedPath(selectedFile.CStr());
	//		const FString sceneName(selectedPath.stem().string());

	//		outCommands.Emplace(FSaveSceneCommand{ sceneName });
	//		strcpy_s(
	//			mGuiInputField.SceneName,
	//			sizeof(mGuiInputField.SceneName),
	//			sceneName.CStr());
	//	}
	//}
	//ImGui::SameLine();
	//if (ImGui::Button("Load scene"))
	//{
	//	const FString selectedFile = openSceneFileDialog();

	//	if (selectedFile.Len() > 0)
	//	{
	//		//LoadScene(selectedFile, *guiReference.FileManager);
	//		//std::filesystem::path p(selectedFile.CStr());
	//		//std::string LoadScenename = p.stem().string();
	//		//strcpy_s(mGuiInputField.SceneName, sizeof(mGuiInputField.SceneName), LoadScenename.c_str());
	//		//guiReference.ViewportClient->Reset();
	//		outCommands.Emplace(FLoadSceneCommand{ selectedFile });
	//	}
	//}

	//const FCamera& camera = guiReference.ViewportClient.GetCamera();

	/* Camera Control */
	ImGui::SeparatorText("Camera Control");

	const FCamera& camera = guiReference.ViewportClient.GetCamera();
	float cameraLocation[3] = { camera.Location.x, camera.Location.y, camera.Location.z };
	float cameraRotation[3] = { camera.Rotation.Roll, camera.Rotation.Pitch, camera.Rotation.Yaw };
	float cameraSensitivity = camera.Sensitivity;
	bool bCameraLocationChanged = false;
	bool bCameraRotationChanged = false;

	// 1) 라벨 텍스트를 먼저 그리고 같은 줄로
	ImGui::Text("Location ");
	ImGui::SameLine();

	// 2) 텍스트를 그린 "뒤"의 남은 폭을 기준으로 계산
	const float spacing = ImGui::GetStyle().ItemSpacing.x;
	const float itemWidth = (ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f;

	ImGui::SetNextItemWidth(itemWidth);
	if (ImGui::DragFloat("##CamLocX", &cameraLocation[0], 0.1f, 10.0f))
	{
		bCameraLocationChanged = true;
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(itemWidth);
	if (ImGui::DragFloat("##CamLocY", &cameraLocation[1], 0.1f, 10.0f))
	{
		bCameraLocationChanged = true;
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(itemWidth);
	if (ImGui::DragFloat("##CamLocZ", &cameraLocation[2], 0.1f, 10.0f))
	{
		bCameraLocationChanged = true;
	}

	ImGui::Text("Rotation ");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(itemWidth);
	if (ImGui::DragFloat("##CamRotX", &cameraRotation[0], 0.1f, 180.0f))
	{
		bCameraRotationChanged = true;
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(itemWidth);
	if (ImGui::DragFloat("##CamRotY", &cameraRotation[1], 0.1f, 180.0f))
	{
		bCameraRotationChanged = true;
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(itemWidth);
	if (ImGui::DragFloat("##CamRotZ", &cameraRotation[2], 0.1f, 180.0f))
	{
		bCameraRotationChanged = true;
	}

	if (bCameraLocationChanged)
	{
		outCommands.Emplace(FSetCameraLocationCommand{ FVector{ cameraLocation[0], cameraLocation[1], cameraLocation[2] } });
	}

	if (bCameraRotationChanged)
	{
		outCommands.Emplace(FSetCameraRotationCommand{ FRotator{ cameraRotation[1], cameraRotation[2], cameraRotation[0] } });
	}

	/*
	ImGui::SeparatorText("Memory Info");

	ImGui::Text("Total allocated memory count: %d", UEngineStatics::sTotalAllocationCount);
	ImGui::Text("Total allocated memory size: %d bytes", UEngineStatics::sTotalAllocationBytes);
	*/

	/* Gizmo Control */
	ImGui::SeparatorText("Gizmo Control");

	// Display the current gizmo mode dropdown
	const char* gizmoModeNames[] = { "Translate", "Rotate", "Scale" };
	int32 gizmoModeIndex = static_cast<int32>(guiReference.ViewportClient.mGizmo.eType);
	if (ImGui::Combo("Gizmo Mode", &gizmoModeIndex, gizmoModeNames, IM_ARRAYSIZE(gizmoModeNames)))
	{
		//guiReference.ViewportClient->mGizmo.SetGizmoType(static_cast<EGIZMO_TYPE>(gizmoModeIndex));
		outCommands.Emplace(FSetGizmoModeCommand{ static_cast<EGIZMO_TYPE>(gizmoModeIndex) });
	}
	if (ImGui::Button("Next Gizmo Mode"))
	{
		//guiReference.ViewportClient->mGizmo.CycleGizmoType();
		outCommands.Emplace(FCycleGizmoModeCommand{});

	}


	ImGui::End();
}

FString openSceneFileDialog()
{
	char fileName[MAX_PATH] = {};
	OPENFILENAMEA openFileName = {};

	openFileName.lStructSize = sizeof(OPENFILENAMEA);
	openFileName.hwndOwner = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);  // main window

	openFileName.lpstrFilter = "Scene Files (*.Scene)\0*.Scene\0All Files (*.*)\0*.*\0";
	openFileName.lpstrFile = fileName;
	openFileName.nMaxFile = MAX_PATH;

	openFileName.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
	openFileName.lpstrDefExt = "Scene";

	std::filesystem::path initialDirectory = std::filesystem::absolute(std::filesystem::path("Assets") / "SceneData");

	if (!std::filesystem::exists(initialDirectory))
	{
		std::filesystem::create_directories(initialDirectory);
	}

	const std::string initialDirectoryString = initialDirectory.string();

	openFileName.lpstrInitialDir = initialDirectoryString.c_str();

	if (GetOpenFileNameA(&openFileName))
	{
		return FString(fileName);
	}

	return FString("");
}

FString saveSceneFileDialog()
{
	char fileName[MAX_PATH] = {};
	OPENFILENAMEA openFileName = {};

	openFileName.lStructSize = sizeof(OPENFILENAMEA);
	openFileName.hwndOwner = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);  // main window

	openFileName.lpstrFilter = "Scene Files (*.Scene)\0*.Scene\0All Files (*.*)\0*.*\0";
	openFileName.lpstrFile = fileName;
	openFileName.nMaxFile = MAX_PATH;

	openFileName.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
	openFileName.lpstrDefExt = "Scene";

	std::filesystem::path initialDirectory = std::filesystem::absolute(std::filesystem::path("Assets") / "SceneData");

	if (!std::filesystem::exists(initialDirectory))
	{
		std::filesystem::create_directories(initialDirectory);
	}

	const std::string initialDirectoryString = initialDirectory.string();

	openFileName.lpstrInitialDir = initialDirectoryString.c_str();

	if (GetSaveFileNameA(&openFileName))
	{
		return FString(fileName);
	}

	return FString("");
}

FString openObjFileDialog()
{
	char fileName[MAX_PATH] = {};

	OPENFILENAMEA dialog = {};
	dialog.lStructSize = sizeof(OPENFILENAMEA);

	dialog.hwndOwner = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);  // main window

	dialog.lpstrFilter =
		"OBJ Files (*.obj)\0*.obj\0"
		"All Files (*.*)\0*.*\0";

	dialog.lpstrFile = fileName;
	dialog.nMaxFile = MAX_PATH;
	dialog.lpstrDefExt = "obj";

	dialog.Flags =
		OFN_EXPLORER |
		OFN_FILEMUSTEXIST |
		OFN_HIDEREADONLY |
		OFN_NOCHANGEDIR;

	const std::filesystem::path assetsDirectory =
		std::filesystem::absolute("Assets");

	const std::string assetsDirectoryString =
		assetsDirectory.string();

	dialog.lpstrInitialDir =
		assetsDirectoryString.c_str();

	if (!GetOpenFileNameA(&dialog))
	{
		return FString();
	}

	return FString(fileName);

}


void FEditorUIManager::updatePropertyWindowGUI(const FGuiReference& guiReference, FEditorCommands& outCommands)
{

	const ImGuiViewport* mainViewport = ImGui::GetMainViewport();

	const float workPosY = mainViewport->WorkPos.y;
	const float workHeight = mainViewport->WorkSize.y;

	const float controlPanelHeight = workHeight * CONTROL_PANEL_HEIGHT_RATIO;
	const float propertyHeight = workHeight * WINDOW_PROPERTY_HEIGHT_RATIO;

	//const float controlPanelHeight = mImGuiIO.DisplaySize.y * CONTROL_PANEL_HEIGHT_RATIO;
	//const float propertyHeight = mImGuiIO.DisplaySize.y * WINDOW_PROPERTY_HEIGHT_RATIO;


	ImGui::SetNextWindowPos(ImVec2(0.0f, controlPanelHeight), ImGuiCond_Always);

	ImGui::SetNextWindowSizeConstraints(
		ImVec2(mImGuiIO.DisplaySize.x * MIN_WIDTH_RATIO, propertyHeight),
		ImVec2(mImGuiIO.DisplaySize.x * MAX_WIDTH_RATIO, propertyHeight)
	);
	ImGui::SetNextWindowSize(ImVec2(mPanelWidth, propertyHeight), ImGuiCond_Always);

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;

	ImGui::Begin("Jungle Property Window", nullptr, flags);

	mPanelWidth = ImGui::GetWindowWidth();

	/* Actor Transform */
	ImGui::SeparatorText("Actor Transform");
	const AActor* selectedActor = guiReference.SceneManager.GetSelectedActor();
	if (selectedActor)
	{
		// Temporary variables to hold the values for ImGui input fields
		FTransform originalTransform = selectedActor->GetTransform();

		// Get the current transform of the clicked actor
		FVector translationInput = originalTransform.Location;
		FRotator originalRotator = selectedActor->GetRotator();
		float rotationInput[3] = {
			originalRotator.Roll,
			originalRotator.Pitch,
			originalRotator.Yaw
		};
		FVector scaleInput = originalTransform.Scale;

		// Display and edit the transform properties using ImGui input fields
		if (ImGui::DragFloat3("Translation", &translationInput.x, 0.1f))
		{
			//mSelectedActor->SetLocation(translationInput);
			outCommands.Emplace(FSetActorLocationCommand{ selectedActor->GetObjectID(), translationInput });
		}
		if (ImGui::DragFloat3("Rotation", &rotationInput[0], 0.1f))
		{
			//mSelectedActor->SetRotation(FRotator{
			//	rotationInput[1], // Pitch
			//	rotationInput[2], // Yaw
			//	rotationInput[0]  // Roll
			//	});

			outCommands.Emplace(FSetActorRotationCommand{ selectedActor->GetObjectID(), FRotator{
				rotationInput[1], // Pitch
				rotationInput[2], // Yaw
				rotationInput[0]  // Roll
				} });
		}
		if (ImGui::DragFloat3("Scale", &scaleInput.x, 0.1f, MIN_SCALE, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp))
		{
			//mSelectedActor->SetScale(scaleInput);
			outCommands.Emplace(FSetActorScaleCommand{ selectedActor->GetObjectID(), scaleInput });
		}

		/* Components */
		ImGui::SeparatorText("Components");
		if (ImGui::BeginChild("Components", ImVec2(0, 0), ImGuiChildFlags_Borders))
		{
			const TArray<UActorComponent*>& components = selectedActor->GetComponents();
			for (const UActorComponent* component : components)
			{
				ImGui::PushID(component->UUID);

				const FString componentName = component->GetName().ToString();

				const FString headerLabel = FString(std::format(
					"{} ({})###ComponentHeader",
					componentName.CStr(),
					component->GetRuntimeClass()->Name.CStr()));

				const bool bExpanded = ImGui::CollapsingHeader(
					headerLabel.CStr(),
					ImGuiTreeNodeFlags_DefaultOpen
				);

				if (bExpanded)
				{
					if (ImGui::BeginChild("ComponentFrame", ImVec2(0, 0), ImGuiChildFlags_FrameStyle | ImGuiChildFlags_AutoResizeY))
					{
						ImGui::Text("Class: %s", component->GetRuntimeClass()->Name.CStr());
						ImGui::Text("UUID: %d", component->UUID);

						FString componentName = component->GetName().ToString();
						ImGui::Text("Name: %s | DisplayIndex: %d | ComparisonIndex: %d | Number: %d",
							componentName.CStr(),
							component->GetName().GetDisplayId().ToUnstableInt(),
							component->GetName().GetComparisonId().ToUnstableInt(),
							component->GetName().GetNumber());

						/* Property Reflection UI Drawing */
						component->ForEachProperty(
							[&](const FPropertyInfo& property)
							{
								// Only show serializable properties in the UI
								if ((property.PropertyFlags & EPropertyFlags::Serializable)
									== EPropertyFlags::None)
									return;

								// Skip properties that don't have a JsonKey, GetValue, or SetValue function
								if (!property.JsonKey || !property.GetValue || !property.SetValue)
									return;

								// Disable editing for properties that are not marked as editable
								const bool bEditable = (property.PropertyFlags & EPropertyFlags::Editable) != EPropertyFlags::None;

								const FPropertyValue originalValue = property.GetValue(component);
								FPropertyValue editedValue = originalValue;

								ImGui::PushID(static_cast<const void*>(&property));
								ImGui::BeginDisabled(!bEditable);

								if (drawPropertyValue(
									property.JsonKey,
									originalValue,
									editedValue
								))
								{
									outCommands.Emplace(FSetPropertyCommand{
										component->GetObjectID(),
										property.JsonKey,
										editedValue
										});
								}

								ImGui::EndDisabled();
								ImGui::PopID();
							}
						);

						// StaticMesh DropList
						if (const UStaticMeshComponent* staticMeshComponent = component->Cast<UStaticMeshComponent>())
						{
							FName currentStaticMeshKey = staticMeshComponent->GetStaticMeshAssetKey();
							if (FAssetPicker::DrawStaticMeshPicker(guiReference.AssetManager, currentStaticMeshKey))
							{
								outCommands.Emplace(FSetStaticMeshComponentStaticMeshCommand{
									staticMeshComponent->GetObjectID(),
									currentStaticMeshKey
									});
							}

							for (int i = 0; i < staticMeshComponent->GetMaterialSlotCount(); ++i)
							{
								FName selectedMaterialKey = staticMeshComponent->GetMaterialAssetKey(i);
								if (FAssetPicker::DrawMaterialPicker(guiReference.AssetManager, selectedMaterialKey, i))
								{
									outCommands.Emplace(FSetStaticMeshComponentMaterialCommand{
										staticMeshComponent->GetObjectID(),
										i,
										selectedMaterialKey
										});
								}
							}

							// Sub uv
							FSubUVMesh subUVMesh = staticMeshComponent->GetSubUVMesh();
							bool bSubUVChanged = false;
							if (ImGui::DragFloat2("SubUV Offset", &subUVMesh.UVOffset.x, 0.01f))
							{
								bSubUVChanged = true;
							}
							if (ImGui::DragFloat2("SubUV Scale", &subUVMesh.UVScale.x, 0.01f))
							{
								bSubUVChanged = true;
							}
							if (bSubUVChanged)
							{
								outCommands.Emplace(FSetStaticMeshComponentSubUVCommand{
									staticMeshComponent->GetObjectID(),
									subUVMesh.UVOffset,
									subUVMesh.UVScale
									});
							}

						}
					}
					ImGui::EndChild();
				}
				ImGui::PopID();
			}
		}
		ImGui::EndChild();
	}



	static FName selectedStaticMeshKey;


	ImGui::End();
}

void FEditorUIManager::updateObjectListPanelGUI(const FGuiReference& guiReference, FEditorCommands& outCommands)
{
	const ImGuiViewport* mainViewport = ImGui::GetMainViewport();

	const float workPosY = mainViewport->WorkPos.y;
	const float workHeight = mainViewport->WorkSize.y;

	const float offsetHeight = workHeight * (CONTROL_PANEL_HEIGHT_RATIO + WINDOW_PROPERTY_HEIGHT_RATIO);

	const float objectListPanelHeight = workHeight - offsetHeight;;

	//float offsetHeight = mImGuiIO.DisplaySize.y * (CONTROL_PANEL_HEIGHT_RATIO + WINDOW_PROPERTY_HEIGHT_RATIO);
	//float objectListPanelHeight = mImGuiIO.DisplaySize.y - offsetHeight;

	ImGui::SetNextWindowPos(ImVec2(0.0f, offsetHeight), ImGuiCond_Always);

	ImGui::SetNextWindowSizeConstraints(
		ImVec2(mImGuiIO.DisplaySize.x * MIN_WIDTH_RATIO, objectListPanelHeight),
		ImVec2(mImGuiIO.DisplaySize.x * MAX_WIDTH_RATIO, objectListPanelHeight)
	);
	ImGui::SetNextWindowSize(ImVec2(mPanelWidth, objectListPanelHeight), ImGuiCond_Always);

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;

	const AActor* selectedActor = guiReference.SceneManager.GetSelectedActor();
	ImGui::Begin("Object List Panel", nullptr, flags);
	{
		/* Object Lists */
		if (ImGui::CollapsingHeader("Object List"))
		{
			if (ImGui::BeginChild("ObjectList", ImVec2(0, 0),
				ImGuiChildFlags_Borders))
			{
				// Update and sort the object list only if there has been a change in the global object revision
				if (mGuiInputField.LastGUObjectRevision != UObject::GetGObjectRevision())
				{
					mGuiInputField.SortedObjectLists = UObject::GetGObjectArray().ToTArray();
					mGuiInputField.LastGUObjectRevision = UObject::GetGObjectRevision();

					// Sort the objects by UUID
					std::sort(mGuiInputField.SortedObjectLists.begin(), mGuiInputField.SortedObjectLists.end(),
						[](UObject* a, UObject* b) { return a->UUID < b->UUID; });
				}

				int32 selectedActorUUID = selectedActor
					? selectedActor->UUID
					: -1;

				//UObject* bDeleteActorOrNull = nullptr;

				static char NameBuffer[384] = {};
				static int32 CachedSelectedUUID = -1;

				//for (unsigned int objectsIndex = 0; objectsIndex < mGuiInputField.SortedObjectLists.Num(); ++objectsIndex)
				//{
				//	UObject* object = mGuiInputField.SortedObjectLists[objectsIndex];
				for (const UObject* object : mGuiInputField.SortedObjectLists)
				{
					if (!object->IsA<AActor>())
					{
						continue;
					}

					bool bSelected = false;
					ImGui::PushID(object->UUID); // Ensure unique ID for each child

					if (ImGui::BeginChild("ObjectFrame", ImVec2(0, 0),
						ImGuiChildFlags_FrameStyle | ImGuiChildFlags_AutoResizeY))
					{
						ImGui::Text("Class: %s", object->GetRuntimeClass()->Name.CStr());
						ImGui::Text("UUID: %d", object->UUID);
						FString ObjectName = object->GetName().ToString();
						ImGui::Text("Name: %s | DisplayIndex: %d | ComparisonIndex: %d | Number: %d",
							ObjectName.CStr(),
							object->GetName().GetDisplayId().ToUnstableInt(),
							object->GetName().GetComparisonId().ToUnstableInt(),
							object->GetName().GetNumber()
						);

						// TODO: Move implement delete to where?
						if (object->IsA<AActor>())
						{
							const AActor* actor = object->Cast<AActor>();

							if (ImGui::Button("Select"))
							{
								//SetSelectedActor(actor);
								outCommands.Emplace(FSetSelectedActorCommand{ actor->GetObjectID() });
							}
							else
							{
								ImGui::SameLine();
								if (ImGui::Button("Delete"))
								{
									//bDeleteActorOrNull = object;
									outCommands.Emplace(FDeleteActorCommand{ actor->GetObjectID() });
								}
							}
						}
					}

					// Highlight the frame if this object is the clicked actor
					if (object->UUID == selectedActorUUID)
					{
						bSelected = true;
						ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(255, 255, 0, 50)); // Light yellow background

						if (CachedSelectedUUID != object->UUID)
						{
							CachedSelectedUUID = object->UUID;

							FString CurrentName = object->GetName().ToString();

							strcpy_s(NameBuffer, sizeof(NameBuffer), CurrentName.CStr());
						}

						ImGui::Text("Edit Name");
						ImGui::SameLine();
						bool bEnterPressed = ImGui::InputText("##Edit Name", NameBuffer, sizeof(NameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
						ImGui::SameLine();
						bool bApplyPressed = ImGui::Button("Apply");

						if (bEnterPressed || bApplyPressed)
						{
							//object->SetName(FName(NameBuffer));
							outCommands.Emplace(FSetActorNameCommand{ object->GetObjectID(), FName(NameBuffer) });
						}
					}

					if (bSelected)
					{
						ImGui::PopStyleColor(); // Pop the border color if it was pushed
					}

					ImGui::EndChild();

					ImGui::PopID();
				}

				//if (bDeleteActorOrNull != nullptr)
				//{
				//	AActor* deleteActor = bDeleteActorOrNull->Cast<AActor>();

				//	if (selectedActor != nullptr && selectedActor->UUID == deleteActor->UUID)
				//	{
				//		selectedActor = nullptr;
				//	}

				//	assert(mCurrentWorld != nullptr);
				//	mCurrentWorld->RemoveActor(deleteActor->UUID);

				//	delete deleteActor;
				//}

			}
			ImGui::EndChild();
		}

	}
	ImGui::End();
}

void FEditorUIManager::updateViewportLayoutPanelGUI(FEditorViewportManager& viewportManager, FViewportSharedSettings& sharedsettings, const FStatManager& statManager, FEditorCommands& outCommands)
{

	const float hostWidth =
		(std::max)(mImGuiIO.DisplaySize.x - mPanelWidth, 0.0f);
	const float hostHeight =
		(std::max)(mImGuiIO.DisplaySize.y - BOTTOM_BAR_HEIGHT, 0.0f);

	if (hostWidth <= 0.0f || hostHeight <= 0.0f)
	{
		return;
	}

	ImGui::SetNextWindowPos(
		ImVec2(mPanelWidth, 0.0f),
		ImGuiCond_Always);
	ImGui::SetNextWindowSize(
		ImVec2(hostWidth, hostHeight),
		ImGuiCond_Always);

	const ImGuiWindowFlags windowFlags =
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse |
		ImGuiWindowFlags_NoBackground;

	if (!ImGui::Begin("Viewport", nullptr, windowFlags))
	{
		ImGui::End();
		return;
	}

	const bool viewportCountChanged = drawViewportSharedControls(viewportManager, sharedsettings, outCommands);

	const ImVec2 hostMin = ImGui::GetCursorScreenPos();
	const ImVec2 availableSize = ImGui::GetContentRegionAvail();
	const ImVec2 hostMax = {
		hostMin.x + availableSize.x,
		hostMin.y + availableSize.y
	};

	const FRect hostRect = {
		hostMin.x,
		hostMin.y,
		hostMax.x,
		hostMax.y
	};

	// 이 호출이 각 Viewport의 panelRect와 imageRect를 갱신한다.
	viewportManager.arrangeLayout(hostRect);

	const ImVec2 mousePosition = ImGui::GetIO().MousePos;
	const FPoint mousePoint = {
		mousePosition.x,
		mousePosition.y
	};

	const bool splitterChanged = updateSplitterInteraction(viewportManager, hostRect, mousePoint);
	updateViewportInteraction(viewportManager, mousePoint);

	ImDrawList& drawList = *ImGui::GetWindowDrawList();
	drawList.PushClipRect(hostMin, hostMax, true);

	const FViewport* activeViewport = viewportManager.getActiveViewport();

	for (uint8 index = 0;
		index < viewportManager.getViewportCount();
		++index)
	{
		const FViewport* viewport = viewportManager.getViewportAt(index);
		if (viewport == nullptr)
		{
			continue;
		}

		drawViewportOverlay(
			*viewport,
			index,
			activeViewport == viewport,
			statManager,
			drawList,
			outCommands);
	}

	drawList.PopClipRect();

	if (viewportCountChanged || splitterChanged)
	{
		viewportManager.captureLayoutSetting(mEditorSetting);
		mEditorSetting.Save();
	}


	// ImGui에게 해당 영역이 실제 콘텐츠로 사용됐음을 알려준다.
	ImGui::Dummy(availableSize);
	ImGui::End();
}

void FEditorUIManager::updateBottomBarGUI(const FGuiReference& guiReference, FEditorCommands& outCommands)
{
	const float displayWidth = mImGuiIO.DisplaySize.x;
	const float displayHeight = mImGuiIO.DisplaySize.y;

	const float barWidth =
		(std::max)(displayWidth - mPanelWidth, 0.0f);

	if (barWidth <= 0.0f)
	{
		return;
	}

	ImGui::SetNextWindowPos(
		ImVec2(mPanelWidth, displayHeight - BOTTOM_BAR_HEIGHT),
		ImGuiCond_Always);

	ImGui::SetNextWindowSize(
		ImVec2(barWidth, BOTTOM_BAR_HEIGHT),
		ImGuiCond_Always);

	const ImGuiWindowFlags barFlags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoSavedSettings;

	ImGui::PushStyleVar(
		ImGuiStyleVar_WindowPadding,
		ImVec2(6.0f, 3.0f));

	if (ImGui::Begin("##EditorBottomBar", nullptr, barFlags))
	{
		/* Console */
		constexpr const char* consolePopupId =
			"ConsoleDrawer";

		const bool bConsoleOpen =
			ImGui::IsPopupOpen(consolePopupId);

		if (ImGui::Button(
			bConsoleOpen ? "Console *" : "Console"))
		{
			// 열려 있을 때 버튼을 누르면 그 클릭은
			// 팝업 외부 클릭으로 처리되어 닫힌다.
			if (!bConsoleOpen)
			{
				ImGui::OpenPopup(consolePopupId);
			}
		}

		const float popupHeight =
			displayHeight * CONSOLE_POPUP_HEIGHT_RATIO;

		// 팝업의 왼쪽 아래를 하단 바의 왼쪽 위에 고정한다.
		ImGui::SetNextWindowPos(
			ImVec2(
				mPanelWidth,
				displayHeight - BOTTOM_BAR_HEIGHT),
			ImGuiCond_Always,
			ImVec2(0.0f, 1.0f));

		ImGui::SetNextWindowSize(
			ImVec2(barWidth, popupHeight),
			ImGuiCond_Always);

		const ImGuiWindowFlags popupFlags =
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoCollapse;

		// OpenPopup과 BeginPopup은 반드시 같은 ImGui 창/ID
		// 스코프 안에 있어야 한다.
		if (ImGui::BeginPopup(
			consolePopupId,
			popupFlags))
		{
			// Console should be updated before drawing its contents
			ConsoleWindow::GetInstance().Update();
			ConsoleWindow::GetInstance().DrawContents(outCommands);
			ImGui::EndPopup();
		}

		/* Asset Browser */
		ImGui::SameLine();

		constexpr const char* assetPopupId = "AssetBrowserDrawer";
		const bool bAssetBrowserOpen =
			ImGui::IsPopupOpen(assetPopupId);

		if (ImGui::Button(
			bAssetBrowserOpen
			? "Asset Browser *###AssetBrowserButton"
			: "Asset Browser###AssetBrowserButton"))
		{
			if (!bAssetBrowserOpen)
			{
				ImGui::OpenPopup(assetPopupId);
			}
		}

		ImGui::SetNextWindowPos(
			ImVec2(
				mPanelWidth,
				displayHeight - BOTTOM_BAR_HEIGHT),
			ImGuiCond_Always,
			ImVec2(0.0f, 1.0f));

		ImGui::SetNextWindowSize(
			ImVec2(barWidth, popupHeight),
			ImGuiCond_Always);

		if (ImGui::BeginPopup(assetPopupId, popupFlags))
		{
			drawAssetBrowserContents(guiReference.AssetManager);
			ImGui::EndPopup();
		}

	}

	ImGui::End();
	ImGui::PopStyleVar();
}

void FEditorUIManager::drawAssetBrowserContents(
	const FAssetManager& assetManager)
{
	ImGui::TextUnformatted("Asset Browser");
	ImGui::Separator();

	if (ImGui::BeginTabBar("AssetBrowserTabs"))
	{
		if (ImGui::BeginTabItem("Static Meshes"))
		{
			const TArray<FName> assetKeys =
				assetManager.GetAllStaticMeshAssetKeys();

			ImGui::PushID("StaticMeshes");

			//for (const FName& assetKey : assetKeys)
			//{
			//	const FString assetName = assetKey.ToString();

			//	ImGui::PushID(assetKey.ComparisonIndex);

			//	{ /* Dragable */
			//		ImGui::Selectable(
			//			assetName.CStr(),
			//			false,
			//			ImGuiSelectableFlags_NoAutoClosePopups);

			//		if (ImGui::BeginDragDropSource())
			//		{
			//			ImGui::SetDragDropPayload(
			//				"ASSET_STATIC_MESH",
			//				&assetKey,
			//				sizeof(assetKey));

			//			// 마우스를 따라다니는 미리보기
			//			ImGui::Text("Static Mesh: %s", assetName.CStr());

			//			ImGui::EndDragDropSource();
			//		}
			//	}
			for (TObjectIterator<UStaticMesh> It; It; ++It)
			{
				UStaticMesh* staticMesh = *It;

				const FString assetName = staticMesh->GetName().ToString();

				ImGui::PushID(staticMesh->UUID);

				{ /* Dragable */
					ImGui::Selectable(
						assetName.CStr(),
						false,
						ImGuiSelectableFlags_NoAutoClosePopups);
					if (ImGui::BeginDragDropSource())
					{
						FName assetKey = staticMesh->GetAssetPathFileName();
						ImGui::SetDragDropPayload(
							"ASSET_STATIC_MESH",
							&assetKey,
							sizeof(assetKey));
						ImGui::Text("Static Mesh: %s", assetName.CStr());
						ImGui::EndDragDropSource();
					}
				}

				ImGui::PopID();
			}

			ImGui::PopID();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Materials"))
		{
			const TArray<FName> assetKeys =
				assetManager.GetAllMaterialAssetKeys();

			ImGui::PushID("Materials");

			//for (const FName& assetKey : assetKeys)
			//{
			//	const FString assetName = assetKey.ToString();

			//	ImGui::PushID(assetKey.ComparisonIndex);

			//	{ /* Dragable */
			//		ImGui::Selectable(
			//			assetName.CStr(),
			//			false,
			//			ImGuiSelectableFlags_NoAutoClosePopups);

			//		if (ImGui::BeginDragDropSource())
			//		{
			//			ImGui::SetDragDropPayload(
			//				"ASSET_MATERIAL",
			//				&assetKey,
			//				sizeof(assetKey));

			//			ImGui::Text("Material: %s", assetName.CStr());

			//			ImGui::EndDragDropSource();
			//		}
			//	}

			for (TObjectIterator<UMaterial> It; It; ++It)
			{
				UMaterial* material = *It;
				const FString assetName = material->GetName().ToString();
				ImGui::PushID(material->UUID);

				{ /* Dragable */
					ImGui::Selectable(
						assetName.CStr(),
						false,
						ImGuiSelectableFlags_NoAutoClosePopups);
					if (ImGui::BeginDragDropSource())
					{
						FName assetKey = material->GetMaterialName();
						ImGui::SetDragDropPayload(
							"ASSET_MATERIAL",
							&assetKey,
							sizeof(assetKey));
						ImGui::Text("Material: %s", assetName.CStr());
						ImGui::EndDragDropSource();
					}
				}

				ImGui::PopID();
			}

			ImGui::PopID();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}
}

void FEditorUIManager::saveSettings(const FEditorViewportManager& viewportManager)
{
	viewportManager.captureLayoutSetting(mEditorSetting);
	mEditorSetting.Save();
}
