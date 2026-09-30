#pragma once
#include "Core/Math/Vector.h"

#include "ViewportTypes.h"
#include "../Rendering/SceneView.h"
#include "Engine/World.h"
#include "Engine/EngineStatics.h"
#include "Rendering/Camera.h"
#include "Rendering/RenderInfo.h"
#include "Gizmo.h"
#include "Core/Math/FBoundingBox.h"
#include "Core/AssetManager.h"

#include "Rendering/FHiZBuffer.h"

class AActor;
class FSceneManager;
class FOctree;
class FWorldBVH;
struct FViewportSharedSettings;

struct FPickCandidate {
	float TMin;
	uint32 ObjectIndex;

	bool operator<(const FPickCandidate& other) const {
		return this->TMin < other.TMin;
	}
};

struct FEditorViewportClient
{
public:

	explicit FEditorViewportClient(FViewportSharedSettings& sharedSettings, EViewportType viewporttype) :
		mSharedSettings(sharedSettings)
	{
		configureCamera(viewporttype);
	}

	void Initialize(FAssetManager& assetManagerRef);
	void RayCast(const FViewRect& viewrect, const TArray<const FRenderInfo*>& renderInfos, bool bCheckObject, const FOctree& octree, const FWorldBVH& worldBVH);
	float GetFov() const { return mCamera.mFovDegree; }
	void Update(float deltaTime, const FViewRect& viewrect, FSceneManager* sceneManager, bool bViewportHoverd, bool bViewportFocused);
	bool IsMouseHit() const { return bMouseHit; }
	FViewportSharedSettings& getSharedSettings() const { return mSharedSettings; };

	void Reset();

	void startProjectionTransition(bool orthographic);
	void startViewportTransition(EViewportType viewportType, const FVector& pivot);
	void updateProjectionTransition(float deltaTime);

	float getProjectionRatio() const { return mProjectionRatio; }
	bool isOrthographicTarget() const { return mProjectionTargetRatio == 0.0f;  }

	FCamera& GetCamera() { return mCamera; }
	const FCamera& GetCamera() const { return mCamera; }

	void UpdateGizmoForView(const AActor* selectedActor);

	FGizmo& GetGizmo() { return mGizmo; }
	const FGizmo& GetGizmo() const{ return mGizmo; }
	void setViewportSettings(EViewportType type) {
		configureCamera(type); mProjectionElapsed = 0.0f; bProjectionTransitioning = false;
		bCameraTransitioning = false;
	}

	FCamera mCamera;
	FGizmo mGizmo;

private:
	/* Reference */
	FAssetManager* mAssetManagerRef = nullptr;

	//마우스 밑 무언가의
	FRenderInfo mHoveredRenderInfo;

	static constexpr uint32 InvalidObjectIndex = UINT32_MAX;
	uint32 mHoveredObjectIndex = InvalidObjectIndex;
	int32 mLastPickCandidateCount = -1;   

	// 선택된 액터의 RenderInfo는 캐시하지 않는다. 필요할 때 ClickedActor->GetRenderInfos()로 그때그때 뽑는다.
	//마우스 밑 무언가가 Actor이면 저장. RayCast 에서 채워야 함 (아직 미구현)
	// INFO: mClickedActor moved to FSceneManager::mSelectedActor.
	//AActor* mClickedActor = nullptr;

	void DeprojectScreenToWorld(int32 MouseX, int32 MouseY,
		float ScreenW, float ScreenH, float NearZ, float FarZ,
		FVector& OutNearPoint, FVector& OutFarPoint);

	void DeprojectScreenToWorldForOrtho(int32 MouseX, int32 MouseY,
		float ScreenW, float ScreenH, float NearZ, float FarZ,
		FVector& OutNearPoint, FVector& OutFarPoint);

	void DeprojectScreenToWorldForUnified(int32 MouseX, int32 MouseY,
		float ScreenW, float ScreenH, float NearZ, float FarZ,
		float orthoDistance, float perspectiveRatio,
		FVector& OutNearPoint, FVector& OutFarPoint
	);

	void configureCamera(EViewportType viewportType);

	bool bMouseHit = false;

	/* Projection Transitioning */
	float mProjectionRatio = 1.0f;
	float mProjectionStartRatio = 1.0f;
	float mProjectionTargetRatio = 1.0f;
	float mProjectionElapsed = 0.0f;
	float mProjectionDuration = 1.0f;
	bool bProjectionTransitioning = false;

	FVector mTransitionPivot = FVector(0.0f);
	float mTransitionDistance = 5.0f;

	FQuat mStartOrbitRotation;
	FQuat mStartViewRotation;
	FQuat mTargetRotation;
	FVector mTransitionStartLocation = FVector(0.0f);
	float mCenteringFraction = 0.0f;
	bool bOrbitThroughFront = false;

	FQuat mSavedPerspectiveOrbitRotation;
	bool bHasSavedPerspectiveOrbitRotation = false;

	bool bCameraTransitioning = false;
	
	// RayCast가 이번 프레임에 쏜 광선. 기즈모 드래그가 같은 광선을 다시 쓴다
	FVector mRayNear;
	FVector mRayFar;

	// 복사용 클립보드
	TMap<int32, int32> UUIDChangeMap;
	json::JSON mActorClipBoard;
	json::JSON copyObject;

	FViewportSharedSettings& mSharedSettings;

// HiZ Buffer
private:
	FHiZBuffer mHiZBuffer;
	bool mbEnableHiZ = true;

public:
	FHiZBuffer* GetHiZBuffer() { return &mHiZBuffer; }
	void SetEnableHiZ(bool bEnable);
	bool IsHiZEnabled() { return mbEnableHiZ; }
};
