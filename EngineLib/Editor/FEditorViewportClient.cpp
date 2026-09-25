#include "FEditorViewportClient.h"

#include "Console.h"
#include "Core/Math/MathUtility.h"
#include "Core/Math/Quat.h"
#include "Engine/SceneManager.h"
#include "Platform/WindowApplication.h"
#include "Rendering/GraphicsManager.h"
#include "ThirdParty/ImGui/imgui.h"

// Primitive vertices definitions
#include "Rendering/Primitives/Circle.h"
#include "Rendering/Primitives/Cube.h"
#include "Rendering/Primitives/GizmoArrow.h"
#include "Rendering/Primitives/Primitives.h"
#include "Rendering/Primitives/Sphere.h"
#include "Rendering/Primitives/Triangle.h"


// 정점 배열이 보이는 스코프라 sizeof 로 개수가 나온다.
// 포인터로 받으면 배열 크기 정보가 사라지므로 여기서 개수를 같이 넘긴다.
static bool GetPrimitiveMesh(EPrimitive ePrimitive, const FVertexSimple*& OutVertices, uint32& OutCount)
{
	switch (ePrimitive)
	{
	case EPrimitive::EP_Cube:
		OutVertices = Cube_vertices;
		OutCount = static_cast<uint32>(sizeof(Cube_vertices) / sizeof(FVertexSimple));
		return true;
	case EPrimitive::EP_Sphere:
		OutVertices = Sphere_vertices;
		OutCount = static_cast<uint32>(sizeof(Sphere_vertices) / sizeof(FVertexSimple));
		return true;
	case EPrimitive::EP_Triangle:
		OutVertices = Triangle_vertices;
		OutCount = static_cast<uint32>(sizeof(Triangle_vertices) / sizeof(FVertexSimple));
		return true;
	case EPrimitive::EP_GizmoArrow:
		OutVertices = GizmoArrow_vertices;
		OutCount = static_cast<uint32>(sizeof(GizmoArrow_vertices) / sizeof(FVertexSimple));
		return true;
	case EPrimitive::EP_Circle:
		OutVertices = Circle_vertices;
		OutCount = static_cast<uint32>(sizeof(Circle_vertices) / sizeof(FVertexSimple));
		return true;
	case EPrimitive::EP_BillboardQuad:
		OutVertices = Quad_vertices;
		OutCount = static_cast<uint32>(sizeof(Quad_vertices) / sizeof(FVertexSimple));
		return true;
	}

	return false;
}

void FEditorViewportClient::Initialize(FAssetManager& assetManagerRef)
{
	mAssetManagerRef = &assetManagerRef;
	mGizmo.Reset();
}

bool FEditorViewportClient::RaycastBounds(
	const FVector& rayStart,
	const FVector& rayEnd,
	const FBoundingBox& bounds)
{
	const FVector direction = rayEnd - rayStart;

	float tMin = 0.0f;
	float tMax = 1.0f;

	for (int axis = 0; axis < 3; ++axis)
	{
		const float origin = rayStart[axis];
		const float dir = direction[axis];
		const float minValue = bounds.min[axis];
		const float maxValue = bounds.max[axis];

		if (fabsf(dir) < 1e-6f)
		{
			if (origin < minValue || origin > maxValue)
			{
				return false;
			}
			continue;
		}

		float t1 = (minValue - origin) / dir;
		float t2 = (maxValue - origin) / dir;

		if (t1 > t2)
		{
			std::swap(t1, t2);
		}

		tMin = max(tMin, t1);
		tMax = min(tMax, t2);

		if (tMin > tMax)
		{
			return false;
		}
	}

	return true;
}

void FEditorViewportClient::RayCast(const FViewRect& viewrect, const TArray<FRenderInfo>& renderInfos, bool bCheckObject)
{
	assert(mAssetManagerRef != nullptr);

	bMouseHit = false;

	// 투영 방식에 따라 광선을 만드는 법만 다르다. 두 점을 구하고 나면 이후 판정은 완전히 같다
	FVector NearPoint, FarPoint;
	//if (bPerspectiveProjection)
	//{
	//	DeprojectScreenToWorld(WindowApplication.Input.CursorX - ViewportInfo.TopLeftX, WindowApplication.Input.CursorY - ViewportInfo.TopLeftY,
	//		ViewportInfo.Width, ViewportInfo.Height, 0.1f, 100.f, NearPoint, FarPoint);
	//}
	//else
	//{
	//	DeprojectScreenToWorldForOrtho(WindowApplication.Input.CursorX - ViewportInfo.TopLeftX, WindowApplication.Input.CursorY - ViewportInfo.TopLeftY,
	//		ViewportInfo.Width, ViewportInfo.Height, 0.1f, 100.f, NearPoint, FarPoint);
	//}
	DeprojectScreenToWorldForUnified(WindowApplication.Input.CursorX - viewrect.X, WindowApplication.Input.CursorY - viewrect.Y,
		viewrect.Width, viewrect.Height, 0.1f, 250.f, mCamera.mOrthoDistance, mProjectionRatio, NearPoint, FarPoint);

	mRayNear = NearPoint;
	mRayFar = FarPoint;

	float NearlistT = FLT_MAX;

	// 드래그 중에는 히트 판정을 하지 않는다.
	// 빠르게 끌면 커서가 축 캡슐을 벗어나는데, 그때 eAxis가 NONE이 되면 드래그가 끊긴다.
	if (mGizmo.mDraggingAxis != EGIZMO_AXIS::NONE)
	{
		bMouseHit = true;
		mGizmo.mbHovered = true;
		mGizmo.eAxis = mGizmo.mDraggingAxis;   // 끌고 있는 축의 강조를 유지한다
		return;
	}

	// Gizmo 탐색
	if (mGizmo.IsRayInGizmo(NearPoint, FarPoint))
	{
		bMouseHit = true;
		mGizmo.mbHovered = true;
		// gizmo highlight
		return;
	}

	// 클릭하지 않았다면 Object는 검사하지 않음
	if (!bCheckObject)
	{
		return;
	}

	// Object 탐색
	for (const FRenderInfo& RI : renderInfos)
	{
		if (!HasAllRenderFlags(RI.eRenderFlags, ERenderFlags::RF_Raycastable))
		{
			continue;
		}

		const FMatrix effectiveWorld = RI.GetTransformMatrix(mCamera.Rotation);

		const FBoundingBox worldBounds =
			RI.MeshName == BuiltinAssets::BillboardQuadTextured
			? TransformBoundingBox(RI.LocalBounds, effectiveWorld)
			: RI.WorldBounds;

		// 월드 AABB 검사
		if (!RaycastBounds(NearPoint, FarPoint, worldBounds))
		{
			continue;
		}

		TArray<FVector> vertexArray;
		TArray<uint32> indexArray;
		const FVertexSimple* vertices = nullptr;
		uint32 length = 0;

		if (RI.StaticMesh)
		{
			for (const auto& vertex : RI.StaticMesh->Vertices)
			{
				vertexArray.Add(vertex.pos);
			}
			for (const auto& index : RI.StaticMesh->Indices)
			{
				indexArray.Add(index);
			}
		}
		else if (HasAllRenderFlags(RI.eRenderFlags, ERenderFlags::RF_Billboard))
		{
			for (const auto& vertex : Quad_textured_indexed_vertices)
			{
				vertexArray.Add(vertex.GetPosition());
			}
			for (uint32 i = 0; i < sizeof(Quad_indices) / sizeof(uint32); i++)
			{
				indexArray.Add(Quad_indices[i]);
			}
		}
		else
		{
			continue;
		}

		const FMatrix WorldToLocal = effectiveWorld.Inverse();

		//역행렬이 존재하지 않으면(스케일이 작아 det이 0에 가까운 경우) Racast 대상에서 제외
		if (WorldToLocal == FMatrix::Zero) continue;

		const FVector LocalNear = WorldToLocal.TransformPosition(NearPoint);
		const FVector LocalFar = WorldToLocal.TransformPosition(FarPoint);

		if (!RaycastBounds(LocalNear, LocalFar, RI.LocalBounds))
		{
			continue;
		}

		// 삼각형 리스트라 정점 3개씩 묶인다
		for (uint32 i = 0; i + 2 < indexArray.Num(); i += 3)
		{
			const FVector V0 = vertexArray[indexArray[i]];
			const FVector V1 = vertexArray[indexArray[i + 1]];
			const FVector V2 = vertexArray[indexArray[i + 2]];

			float OutT, OutU, OutV;
			if (RayIntersectsTriangle(LocalNear, LocalFar, V0, V1, V2, OutT, OutU, OutV)
				&& OutT < NearlistT)
			{
				// 같은 메시 안에서도 더 가까운 삼각형이 뒤에 나올 수 있으므로 break 하지 않는다
				NearlistT = OutT;
				bMouseHit = true;
				mHoveredRenderInfo = RI;
			}
		}
	}
}

void FEditorViewportClient::Update(float deltaTime, const FViewRect& viewRect, FSceneManager* sceneManager, bool bViewportHovered, bool bViewportFocused)
{
	const FInputState& Input = WindowApplication.Input;
	ImGuiIO& io = ImGui::GetIO();

	// 마우스 입력은 실제 3D 이미지 위에 있을 때 허용한다.
    // RMB 드래그 중에는 화면 밖으로 조금 벗어나도 계속 회전하게 한다.
	const bool bCanUseMouse = bViewportHovered ||(bViewportFocused && Input.IsDown(VK_RBUTTON));

	// 키보드는 Focus된 Viewport만 사용한다.
	// 텍스트 필드에 입력 중일 때는 카메라를 움직이지 않는다.
	const bool bCanUseKeyboard =bViewportFocused && !io.WantTextInput;
	const bool bOrthographic = isOrthographicTarget();

	// Camera Transition 중에는 카메라를 직접 움직이지 못하게 한다.
	if (!bCameraTransitioning)
	{

		// Camera Rotate
		// 회전을 이동보다 먼저, 이번 프레임에 돌린 방향으로 바로 움직이게
		if (bCanUseMouse && !bOrthographic && Input.IsDown(VK_RBUTTON))
		{
			mCamera.Rotate(Input.MouseDX, Input.MouseDY);
		}

		// Camera Velocity
		FVector MoveDir(0.f, 0.f, 0.f);
		if (bCanUseKeyboard)
		{
			const FMatrix R = FMatrix::Rotate(mCamera.Rotation);
			const FVector Forward = R.GetUnitAxis(EAxis::X);
			const FVector Upward = R.GetUnitAxis(EAxis::Z);
			const FVector Right = R.GetUnitAxis(EAxis::Y);

			if (bOrthographic)
			{
				if (Input.IsDown('W')) MoveDir += Upward;
				if (Input.IsDown('S')) MoveDir -= Upward;
				if (Input.IsDown('D')) MoveDir += Right;
				if (Input.IsDown('A')) MoveDir -= Right;
				if (Input.IsDown('E')) MoveDir += FVector(0.f, 0.f, 1.f);
				if (Input.IsDown('Q')) MoveDir -= FVector(0.f, 0.f, 1.f);
			}
			else
			{
				if (Input.IsDown('W')) MoveDir += Forward;
				if (Input.IsDown('S')) MoveDir -= Forward;
				if (Input.IsDown('D')) MoveDir += Right;
				if (Input.IsDown('A')) MoveDir -= Right;
				if (Input.IsDown('E')) MoveDir += FVector(0.f, 0.f, 1.f);
				if (Input.IsDown('Q')) MoveDir -= FVector(0.f, 0.f, 1.f);
			}
		}

		const bool bMoveKeyDown = !MoveDir.IsNearlyZero();
		if (bMoveKeyDown)
		{
			MoveDir.Normalize();
		}


		//Camera Translate
		if (bCanUseMouse && Input.MouseWheelDelta != 0.0f)
		{
			//키 입력이 없으면 마우스 휠은 줌인/줌아웃
			if (!bMoveKeyDown)
			{
				if (mProjectionRatio < 1.0f)
				{
					mCamera.mOrthoDistance *= FMath::Pow(1.2f, -Input.MouseWheelDelta);
					mCamera.mOrthoDistance = FMath::Clamp(mCamera.mOrthoDistance, 0.1f, 100.0f);
				}
				else
				{
					mCamera.Location += mCamera.GetForwardVector() * 1.0f * Input.MouseWheelDelta;
				}
			}
			//입력이 있으면 마우스 휠은 카메라 이동속도 조절

			else
			{
				if (mProjectionRatio < 1.0f)
				{
					mCamera.mOrthoDistance *= FMath::Pow(1.2f, -Input.MouseWheelDelta);
					mCamera.mOrthoDistance = FMath::Clamp(mCamera.mOrthoDistance, 0.1f, 100.0f);
				}
				//mCamera.Speed *= FMath::Pow(1.2f, Input.MouseWheelDelta);
				//mCamera.Speed = FMath::Clamp(mCamera.Speed, 0.1f, 100.0f);
			}

		}
		if (bOrthographic)
		{

		}

		mCamera.Speed = mSharedSettings.cameraSpeed;
		const FVector TargetVelocity = MoveDir * mCamera.Speed;

		// 지수 감쇠만큼 카메라 속도가 서서히 줄어듬
		const float Alpha = FMath::Exp(-mCamera.Damping * deltaTime);
		mCamera.Velocity = TargetVelocity + (mCamera.Velocity - TargetVelocity) * Alpha;
		if (mCamera.Velocity.IsNearlyZero())
		{
			mCamera.Velocity = FVector(0.f);
		}

		mCamera.Location += mCamera.Velocity * deltaTime;

	}

	if (bCanUseKeyboard && Input.WasPressed(VK_SPACE))
	{
		mGizmo.CycleGizmoType();
	}

	const bool bLeftClicked = bViewportHovered && Input.WasPressed(VK_LBUTTON);

	RayCast(viewRect, sceneManager->GetRenderInfos(), bLeftClicked);
	
	////Editor Click 처리
	//if (mClickedActor)
	//{
	//	mClickedActor->BeginFrame();
	//}
	if (sceneManager->GetSelectedActor())
	{
		if (Input.IsDown(VK_CONTROL) && Input.WasPressed('C'))
		{
			sceneManager->GetSelectedActor()->SerializeClass(mActorClipBoard);
		}
	}

	if (!mActorClipBoard.IsNull() && Input.IsDown(VK_CONTROL) && Input.WasPressed('V'))
	{
		copyObject = mActorClipBoard;
		UUIDChangeMap.Reset(); // UUID Map 리셋
		UUIDChangeMap.Reserve(copyObject["Properties"]["mComponents"].length()); // Component 개수만큼 Map 미리 Reserve
		copyObject["Properties"]["UUID"] = UEngineStatics::GenerateUUID(); // Actor UUID 발급

		for (int i = 0;i < copyObject["Properties"]["mComponents"].length();i++)
		{
			int32 oldUUID = copyObject["Properties"]["mComponents"][i]["Properties"]["UUID"].ToInt();
			int newUUID = UEngineStatics::GenerateUUID(); // 연결된 Component마다 UUID 발급
			UUIDChangeMap[oldUUID] = newUUID; // 예전 UUID와 새 UUID를 연결할수 있도록 UUIDChangeMap에 매핑
			copyObject["Properties"]["mComponents"][i]["Properties"]["UUID"] = newUUID;
		}
		int32 oldRoot = copyObject["Properties"]["mRootComponentUUID"].ToInt();
		copyObject["Properties"]["mRootComponentUUID"] = UUIDChangeMap[oldRoot]; // 위에서 Mapping 해놨기 때문에 Mapping 값 맞춰서 Root가 업데이트 됨
		for (int i = 1;i < copyObject["Properties"]["mComponents"].length();i++)
		{
			int32 oldParent = copyObject["Properties"]["mComponents"][i]["ParentUUID"].ToInt();
			copyObject["Properties"]["mComponents"][i]["ParentUUID"] = UUIDChangeMap[oldParent]; // 위에서 Mapping 해놨기 때문에 Mapping 값 맞춰서 Parent가 업데이트 됨
		}
		FString className(copyObject["ClassName"].ToString());
		const FClassInfo* classinfo = FObjectFactory::GetClassInfoByName(className); // Actor Class 이름을 읽어서 classinfo 가져옴
		if (classinfo == nullptr)
		{
			return;
		}
		UObject* LoadActor = FObjectFactory::LoadObject(classinfo,copyObject); // classinfo 바탕으로 object 생성
		if (LoadActor == nullptr)
		{
			return;
		}
		AActor* NewActor = LoadActor->Cast<AActor>(); // AActor로 캐스팅 => 실제 하는 작업은 다 AActor를 이용하는 작업
		if (NewActor == nullptr)
		{
			LoadActor->Destroy();
			return;
		}
		UWorld * CurrentWorld = sceneManager->GetCurrentWorld();
		CurrentWorld->AddActor(std::unique_ptr<AActor>(NewActor));
		NewActor->SetName(NewActor->GetName()); // UUID 바뀌었기 때문에 이름 다시 설정
		NewActor->SetLocation(NewActor->GetTransform().Location + FVector(1.0f, 1.0f, 0.0f)); // 겹치지 않게 위치 변경
		sceneManager->SetSelectedActor(NewActor); // Select 변경
	}

	// 누른 순간에만 선택을 갱신한다. 떼는 것으로는 선택이 풀리지 않는다.
	if (bLeftClicked)
	{
		AActor* Hit = nullptr;

		if (IsMouseHit())
		{
			//Gizmo라면 드래그 기준값을 저장
			if (mGizmo.eAxis != EGIZMO_AXIS::NONE &&
				sceneManager->IsActorSelected() &&
				mGizmo.mDraggingAxis == EGIZMO_AXIS::NONE)
			{
				mGizmo.BeginDrag(mRayNear, mRayFar, sceneManager->GetSelectedActor()->GetTransform());
			}

			//Actor라면 액터를 저장
			else
			{
				uint32 clickedObjectIndex = mHoveredRenderInfo.ObejctID.InternalIndex;
				UObject* ClickedObject = UObject::GetObjectByInternalIndex(clickedObjectIndex);
				if (ClickedObject && ClickedObject->IsA(AActor::GetClass()))
				{
					Hit = static_cast<AActor*>(ClickedObject);
				}
			}
		}

		//// 다른 것을 눌렀으면 이전 선택 해제. 같은 것이면 유지.
		//if (mClickedActor && mClickedActor != Hit && !mGizmo.mbHovered)
		//{
		//
		//	mClickedActor->UnPressed();
		//}

		//Gizmo를 제외한 다른 것을 눌렀을 때, ClickedActor로 갱신
		if (!mGizmo.mbHovered)
		{
			if (Hit != nullptr)
			{
				sceneManager->SetSelectedActor(Hit);
			}
			else
			{
				sceneManager->ResetSelectedActor();
			}
		}

		//if (Hit)
		//{
		//	Hit->Pressed();      // 선택 유지
		//	Hit->ClickStart();   // 이번 프레임에 시작했음을 표시
		//}
	}

	//Gizmo 축을 클릭한 상태로 마우스 이동이 있으면 해당 축 방향으로 ClickedActor을 변형한다.
	if (mGizmo.mDraggingAxis != EGIZMO_AXIS::NONE && sceneManager->IsActorSelected())
	{
		if (mGizmo.eType == EGIZMO_TYPE::TRANSLATE)
		{
			// 절대 좌표가 아니라 시작 시점 대비 변위. 축 직선도 시작 시점에 고정돼 있다
			FVector newLocation;
			if (mGizmo.GetDragLocation(mRayNear, mRayFar, newLocation, mSharedSettings.getSnapSize()))
			{
				sceneManager->GetSelectedActor()->SetLocation(newLocation);
			}
		}
		if (mGizmo.eType == EGIZMO_TYPE::ROTATE)
		{
			// 링 평면 위에서 잰 각도. 시작 회전에 누적각을 한 번만 얹는다
			FQuat newRotation;
			if (mGizmo.GetDragRotation(mRayNear, mRayFar, newRotation))
			{
				//ClickedActor->SetRotation(newRotation);
				mGizmo.UpdateRotation = newRotation;
				sceneManager->GetSelectedActor()->SetRotation(newRotation);
			}
		}
		if (mGizmo.eType == EGIZMO_TYPE::SCALE)
		{
			FVector newScale;
			if (mGizmo.GetDragScale(mRayNear, mRayFar, newScale))
			{
				sceneManager->GetSelectedActor()->SetScale(newScale);
			}
		}
	}

	if (Input.WasReleased(VK_LBUTTON))
	{
		mGizmo.mDraggingAxis = EGIZMO_AXIS::NONE;
	}

	//변형된 Actor를 바탕으로 Gizmo를 위치시킨다.
	UpdateGizmoForView(sceneManager->GetSelectedActor());
}

bool FEditorViewportClient::RayIntersectsTriangle(const FVector& Origin, const FVector& Dir, const FVector& V0, const FVector& V1, const FVector& V2, float& OutT, float& OutU, float& OutV)
{
	static const float EPSILON = 1e-6f;

	//삼각형판정 => O +tD = V0+ uE1+vE2
	// -tD + uE1 + vE2 = O - V0
	//E2=v2-v0. E1=v1-v0

	FVector D = Dir - Origin;
	FVector T = Origin - V0;
	FVector E2 = V2 - V0;
	FVector E1 = V1 - V0;
	FVector P = FVector::cross(D, E2);
	float Det = FVector::dot(E1, P);

	if (fabsf(Det) < EPSILON) return false;   // 평면과 평행

	float InvDet = 1.0f / Det;

	OutU = FVector::dot(T, P) * InvDet;
	if (OutU < 0.0f || OutU > 1.0f) return false;

	FVector Q = FVector::cross(T, E1);
	OutV = FVector::dot(D, Q) * InvDet;
	if (OutV < 0.0f || OutU + OutV > 1.0f) return false;

	OutT = FVector::dot(E2, Q) * InvDet;

	return (OutT > EPSILON);                  // 광선 앞쪽만

	// OutT : 맞은물체가 얼마나 가까이있나(float)
	// OutU, OutV 정확환 클릭지점을 확인하려면 필요
}

void FEditorViewportClient::DeprojectScreenToWorld(int32 MouseX, int32 MouseY, float ScreenW, float ScreenH, float NearZ, float FarZ, FVector& OutNearPoint, FVector& OutFarPoint)
{
	// 1) 픽셀 -> NDC. 화면 Y 는 아래로 +, NDC Y 는 위로 + 라서 뒤집는다
	const float ndcX = (2.0f * (MouseX + 0.5f) / ScreenW) - 1.0f;
	const float ndcY = 1.0f - (2.0f * (MouseY + 0.5f) / ScreenH);

	// 2) 투영 스케일 항 — GetProjectionMatrix 와 반드시 같은 식이어야 한다
	const float Aspect = ScreenW / ScreenH;
	const float yScale = 1.0f / tanf(mCamera.mFovDegree * 0.5f * PI / 180.f);
	const float xScale = yScale / Aspect;

	// 3) 카메라 기저로 월드 방향 합성. 전방 성분이 1 이므로 정규화하면 안 된다
	const FMatrix R = FMatrix::Rotate(mCamera.Rotation);
	FVector V = R.GetUnitAxis(EAxis::X);                    // 전방 (성분 1)
	V += R.GetUnitAxis(EAxis::Y) * (ndcX / xScale);         // 우측
	V += R.GetUnitAxis(EAxis::Z) * (ndcY / yScale);         // 상방

	// 4) 곱하면 그대로 각 평면 위의 점
	OutNearPoint = mCamera.Location + V * NearZ;
	OutFarPoint = mCamera.Location + V * FarZ;
}

void FEditorViewportClient::DeprojectScreenToWorldForOrtho(int32 MouseX, int32 MouseY, float ScreenW, float ScreenH, float NearZ, float FarZ, FVector& OutNearPoint, FVector& OutFarPoint)
{
	// 1) 픽셀 -> NDC. 화면 Y 는 아래로 +, NDC Y 는 위로 + 라서 뒤집는다
	const float ndcX = (2.0f * (MouseX + 0.5f) / ScreenW) - 1.0f;
	const float ndcY = 1.0f - (2.0f * (MouseY + 0.5f) / ScreenH);

	// 2) 화면이 담는 월드 크기 — GetOrthographicMatrix 에 넘기는 값과 반드시 같아야 한다.
	//    직교 행렬은 2/width, 2/height 로 나누므로 되돌리려면 절반을 곱한다
	const float Aspect = ScreenW / ScreenH;
	const float orthoHeight = mCamera.mOrthoHeight;
	const float orthoWidth = orthoHeight * Aspect;

	const FMatrix R = FMatrix::Rotate(mCamera.Rotation);
	const FVector Forward = R.GetUnitAxis(EAxis::X);
	const FVector Right = R.GetUnitAxis(EAxis::Y);
	const FVector Up = R.GetUnitAxis(EAxis::Z);

	// 3) 원근과 결정적으로 다른 점: 방향이 아니라 시작점이 픽셀마다 달라진다.
	//    모든 광선이 전방과 나란하고, 카메라 평면 위에서 평행이동한 자리에서 출발한다
	const FVector RayOrigin = mCamera.Location
		+ Right * (ndcX * orthoWidth * 0.5f)
		+ Up * (ndcY * orthoHeight * 0.5f);

	OutNearPoint = RayOrigin + Forward * NearZ;
	OutFarPoint = RayOrigin + Forward * FarZ;
}

void FEditorViewportClient::DeprojectScreenToWorldForUnified(
	int32 MouseX, int32 MouseY,
	float ScreenW, float ScreenH, float NearZ, float FarZ,
	float orthoDistance, float perspectiveRatio,
	FVector& OutNearPoint, FVector& OutFarPoint
)
{
	const float ndcX = (2.0f * (MouseX + 0.5f) / ScreenW) - 1.0f;
	const float ndcY = 1.0f - (2.0f * (MouseY + 0.5f) / ScreenH);

	const FMatrix invProjection = mCamera.GetInverseUnifiedProjectionMatrix(
		ScreenW / ScreenH, mCamera.mFovDegree, orthoDistance, NearZ, FarZ, perspectiveRatio
	);

	const FMatrix invViewProj = invProjection * mCamera.GetViewMatrix().Inverse();

	const auto Unproject = [&](float ndcZ) -> FVector
		{
			const FVector xyz = invViewProj.TransformPosition(FVector(ndcX, ndcY, ndcZ));

			const float w =
				ndcX * invViewProj.M[0][3] +
				ndcY * invViewProj.M[1][3] +
				ndcZ * invViewProj.M[2][3] +
				invViewProj.M[3][3];

			return xyz * (1.0f / w);
		};

	OutNearPoint = Unproject(0.0f);
	OutFarPoint = Unproject(1.0f);
}

void FEditorViewportClient::Reset()
{
	mHoveredRenderInfo = FRenderInfo();
	bMouseHit = false;
	mGizmo.Reset();
}

void FEditorViewportClient::configureCamera(EViewportType viewporttype)
{
	switch (viewporttype)
	{
	case EViewportType::Perspective:
		mCamera.Location = FVector(-5.0f, 5.0f, 5.0f);
		mCamera.LookAt(FVector(0.0f, 0.0f, 0.0f));
		mProjectionRatio = 1.0f;
		break;
	case EViewportType::Top:
		mCamera.Location = FVector(0.0f, 0.0f, 10.0f);
		mCamera.LookAt(FVector(0.0f, 0.0f, 0.0f));
		mCamera.mOrthoDistance = 5.0f;
		mProjectionRatio = 0.0f;
		break;
	case EViewportType::Bottom:
		mCamera.Location = FVector(0.0f, 0.0f, -10.0f);
		mCamera.LookAt(FVector(0.0f, 0.0f, 0.0f));
		mCamera.mOrthoDistance = 5.0f;
		mProjectionRatio = 0.0f;
		break;
	case EViewportType::Right:
		mCamera.Location = FVector(0.0f, 10.0f, 0.0f);
		mCamera.LookAt(FVector(0.0f, 0.0f, 0.0f));
		mCamera.mOrthoDistance = 5.0f;
		mProjectionRatio = 0.0f;
		break;
	case EViewportType::Left:
		mCamera.Location = FVector(0.0f, -10.0f, 0.0f);
		mCamera.LookAt(FVector(0.0f, 0.0f, 0.0f));
		mCamera.mOrthoDistance = 5.0f;
		mProjectionRatio = 0.0f;
		break;
	case EViewportType::Front:
		mCamera.Location = FVector(-10.0f, 0.0f, 0.0f);
		mCamera.LookAt(FVector(0.0f, 0.0f, 0.0f));
		mCamera.mOrthoDistance = 5.0f;
		mProjectionRatio = 0.0f;
		break;
	case EViewportType::Back:
		mCamera.Location = FVector(10.0f, 0.0f, 0.0f);
		mCamera.LookAt(FVector(0.0f, 0.0f, 0.0f));
		mCamera.mOrthoDistance = 5.0f;
		mProjectionRatio = 0.0f;
		break;
	default:
		mCamera.Location = FVector(-5.0f, 5.0f, 5.0f);
		mCamera.LookAt(FVector(0.0f, 0.0f, 0.0f));
		mProjectionRatio = 1.0f;
		break;
	}
	mProjectionStartRatio = mProjectionRatio;
	mProjectionTargetRatio = mProjectionRatio;
}

void FEditorViewportClient::startProjectionTransition(bool orthographic)
{
	bCameraTransitioning = false;
	mProjectionStartRatio = mProjectionRatio;
	mProjectionTargetRatio = orthographic ? 0.0f : 1.0f;
	mProjectionElapsed = 0.0f;
	bProjectionTransitioning = mProjectionStartRatio != mProjectionTargetRatio;
}

namespace
{
	FRotator GetOrthographicRotation(EViewportType type)
	{
		switch (type)
		{
		case EViewportType::Top:
			return FRotator(-90.0f, 0.0f, 0.0f);

		case EViewportType::Bottom:
			return FRotator(90.0f, 0.0f, 0.0f);

		case EViewportType::Front:
			return FRotator(0.0f, 0.0f, 0.0f);

		case EViewportType::Back:
			return FRotator(0.0f, 180.0f, 0.0f);

		case EViewportType::Left:
			return FRotator(0.0f, 90.0f, 0.0f);

		case EViewportType::Right:
			return FRotator(0.0f, -90.0f, 0.0f);

		default:
			return FRotator(0.0f, 0.0f, 0.0f);
		}
	}
}

void FEditorViewportClient::startViewportTransition(
	EViewportType targetType,
	const FVector& pivot)
{
	mTransitionPivot = pivot;
	mTransitionDistance = (mCamera.Location - pivot).Length();

	// 너무 가까우면 카메라가 Pivot을 뚫고 들어가서 회전이 이상해진다. 최소 거리를 강제한다
	if (mTransitionDistance < 0.1f)
	{
		mTransitionDistance = 0.1f;
		mTransitionPivot =
			mCamera.Location +
			mCamera.GetForwardVector() * mTransitionDistance;
	}

	mStartViewRotation = mCamera.Rotation.Quaternion();
	mTransitionStartLocation = mCamera.Location;

	const FVector toPivot =
		(mTransitionPivot - mCamera.Location).GetNormalized();

	const FVector cameraForward = mCamera.GetForwardVector();

	const float alignment = FVector::dot(cameraForward, toPivot);

	if (alignment >= 1.0f - 1.e-6f)
	{
		// 이미 중심을 보고 있다면 현재 회전의 Yaw/Roll까지 유지한다.
		mStartOrbitRotation = mStartViewRotation;
	}
	else
	{
		// Rotate the current frame toward the pivot without rebuilding its yaw at a pole.
		FQuat correction;
		if (alignment < -1.0f + 1.e-6f)
		{
			const FVector axis = mCamera.GetUpVector().GetNormalized();
			correction = FQuat(axis.x, axis.y, axis.z, 0.0f);
		}
		else
		{
			const FVector axis = FVector::cross(cameraForward, toPivot);
			correction = FQuat(axis.x, axis.y, axis.z, 1.0f + alignment).GetNormalized();
		}
		mStartOrbitRotation = (correction * mStartViewRotation).GetNormalized();
	}
	mCenteringFraction = alignment >= 1.0f - 1.e-6f ? 0.0f : 0.25f;
	bOrbitThroughFront =
		(targetType == EViewportType::Bottom && cameraForward.z < -0.9999f) ||
		(targetType == EViewportType::Top && cameraForward.z > 0.9999f);

	const bool targetPerspective =
		targetType == EViewportType::Perspective;

	// Perspective에서 Orthographic으로 전환할 때, 현재 Orbit 회전을 저장해 두었다가 다시 Perspective로 돌아올 때 사용한다
	if (!targetPerspective &&
		!bProjectionTransitioning &&
		mProjectionRatio >= 1.0f - KINDA_SMALL_NUMBER)
	{
		mSavedPerspectiveOrbitRotation = mStartOrbitRotation;
		bHasSavedPerspectiveOrbitRotation = true;
	}

	if (targetPerspective)
	{
		mTargetRotation = bHasSavedPerspectiveOrbitRotation
			? mSavedPerspectiveOrbitRotation
			: FRotator::LookAt(
				FVector(-5.0f, 5.0f, 5.0f),
				FVector(0.0f)).Quaternion();
	}
	else
	{
		mTargetRotation =
			GetOrthographicRotation(targetType).Quaternion();
	}

	// 전환 중에는 카메라가 Pivot을 향하도록 강제한다
	if (mProjectionRatio >= 1.0f)
	{
		mCamera.mOrthoDistance = mTransitionDistance;
	}

	mCamera.Velocity = FVector(0.0f);

	mProjectionStartRatio = mProjectionRatio;
	mProjectionTargetRatio = targetPerspective ? 1.0f : 0.0f;
	mProjectionElapsed = 0.0f;

	// 전환 중에는 카메라가 Pivot을 향하도록 강제한다
	bProjectionTransitioning = true;
	bCameraTransitioning = !targetPerspective;
}

void FEditorViewportClient::updateProjectionTransition(float deltaTime)
{
	if (!bProjectionTransitioning)
	{
		return;
	}

	mProjectionElapsed += deltaTime;

	const float u = mProjectionDuration > 0.0f
		? FMath::Clamp(
			mProjectionElapsed / mProjectionDuration,
			0.0f,
			1.0f)
		: 1.0f;

	const float orbitU = bCameraTransitioning
		? FMath::Clamp((u - mCenteringFraction) / (1.0f - mCenteringFraction), 0.0f, 1.0f)
		: u;
	const float alpha = orbitU * orbitU * (3.0f - 2.0f * orbitU);

	mProjectionRatio =
		mProjectionStartRatio +
		(mProjectionTargetRatio - mProjectionStartRatio) * alpha;

	if (bCameraTransitioning)
	{
		if (u < mCenteringFraction)
		{
			const float centerU = u / mCenteringFraction;
			const float centerAlpha = centerU * centerU * (3.0f - 2.0f * centerU);
			mCamera.Location = mTransitionStartLocation;
			mCamera.Rotation = FQuat::Slerp(
				mStartViewRotation, mStartOrbitRotation, centerAlpha).Rotator();
		}
		else
		{
			FQuat rotation;
			if (bOrbitThroughFront)
			{
				const FQuat front = GetOrthographicRotation(EViewportType::Front).Quaternion();
				// Apply easing to the whole orbit, not separately at the waypoint.
				rotation = alpha < 0.5f
					? FQuat::Slerp(mStartOrbitRotation, front, alpha * 2.0f)
					: FQuat::Slerp(front, mTargetRotation, alpha * 2.0f - 1.0f);
			}
			else
			{
				rotation = FQuat::Slerp(mStartOrbitRotation, mTargetRotation, alpha);
			}
			mCamera.Rotation = rotation.Rotator();
			mCamera.Location = mTransitionPivot -
				mCamera.GetForwardVector() * mTransitionDistance;
		}
	}

	if (u >= 1.0f)
	{
		mProjectionRatio = mProjectionTargetRatio;
		bProjectionTransitioning = false;
		bCameraTransitioning = false;
	}
}

void FEditorViewportClient::UpdateGizmoForView(const AActor* selectedActor)
{
	mGizmo.Update(
		selectedActor,
		mCamera.Location,
		mCamera.GetForwardVector(),
		mCamera.mFovDegree,
		mProjectionRatio,
		mCamera.mOrthoDistance);
}
