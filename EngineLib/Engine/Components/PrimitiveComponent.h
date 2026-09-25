#pragma once

#include <span>

#include "Core/Math/Color.h"
#include "Rendering/RenderInfo.h"

#include "SceneComponent.h"

class UPrimitiveComponent : public USceneComponent
{
	DECLARE_OBJECT(UPrimitiveComponent, USceneComponent)
	DECLARE_SERIALIZATION()

public:
	UPrimitiveComponent();

	void Initialize(EPrimitive ePrimitive);
	void Initialize(EPrimitive ePrimitive, FVector location, FRotator rotation, FVector scale3D);
	void Initialize(EPrimitive ePrimitive, FVector location, FRotator rotation, FVector scale3D, bool bUseTexture);

	virtual ~UPrimitiveComponent();

	virtual FBoundingBox GetWorldBounds() const override;

	void Update(float deltaTime, TArray<FRenderInfo>* outRenderInfos) override;
	void GetRenderInfos(TArray<FRenderInfo>* outRenderInfos) const override final;
	void SetUseTexture(bool value) { mbUseTexture = value; }
	bool GetUseTexture() const { return mbUseTexture; }

	const FLinearColor& GetColor() const { return mColor; }
	void SetColor(const FLinearColor& color) { mColor = color; }

	static std::span<const FPropertyInfo> GetDeclaredProperties();

protected:
	virtual FRenderInfo makeRenderInfo() const;

	EPrimitive mePrimitive;
	FLinearColor mColor{ 1.f, 1.f, 1.f, 1.f };

	FBoundingBox mLocalBounds{};
	FBoundingBox mWocalBounds{};

	bool mbUseTexture = false;
	bool mbShowBoundingBox = true;
};


