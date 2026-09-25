
#include "PrimitiveComponent.h"

#include <format>

#include "Rendering/RenderInfo.h"
#include "Core/enum.h"
#include "Core/IO/JsonUtil.h"
#include "Editor/Console.h"
#include "Engine/Actor.h"

#include "Rendering/Primitives/Cube.h"
#include "Rendering/Primitives/Sphere.h"
#include "Rendering/Primitives/Triangle.h"
#include "Rendering/Primitives/GizmoArrow.h"
#include "Rendering/Primitives/Circle.h"
#include "Rendering/Primitives/Primitives.h"

static FBoundingBox CalculateBounds(const FVertexSimple* vertices, uint32 count);
static const FBoundingBox& GetPrimitiveLocalBounds(EPrimitive primitive);

IMPLEMENT_CLASS_WITH_PROPERTIES(UPrimitiveComponent, USceneComponent);
IMPLEMENT_SERIALIZATION(UPrimitiveComponent, USceneComponent,
	{ mLocalBounds = GetPrimitiveLocalBounds(mePrimitive); })

UPrimitiveComponent::UPrimitiveComponent()
{
}

/*
void UPrimitiveComponent::Initialize(GraphicsManager* graphicsManager, EPrimitive ePrimitive, FVector location, FRotator rotation, FVector scale3D)
{
	USceneComponent::Initialize(location, rotation, scale3D);

	mGraphicsManager = graphicsManager;
	mePrimitive = ePrimitive;
}
*/

void UPrimitiveComponent::Initialize(EPrimitive ePrimitive)
{
	Initialize(ePrimitive, FVector(0.f, 0.f, 0.f), FRotator(0.f, 0.f, 0.f), FVector(0.f, 0.f, 0.f));
}

void UPrimitiveComponent::Initialize(EPrimitive ePrimitive, FVector location, FRotator rotation, FVector scale3D)
{
	USceneComponent::Initialize(location, rotation, scale3D);

	mePrimitive = ePrimitive;
	mLocalBounds = GetPrimitiveLocalBounds(ePrimitive);
	mColor = FLinearColor(1.f, 1.f, 1.f, 0.f);
}

void UPrimitiveComponent::Initialize(EPrimitive ePrimitive, FVector location, FRotator rotation, FVector scale3D, bool bUseTexture)
{
	USceneComponent::Initialize(location, rotation, scale3D);
	mePrimitive = ePrimitive;
	mLocalBounds = GetPrimitiveLocalBounds(ePrimitive);
	mbUseTexture = bUseTexture;
	mColor = bUseTexture
		? FLinearColor(1.f, 1.f, 1.f, 1.f)
		: FLinearColor(1.f, 1.f, 1.f, 0.f);
}

UPrimitiveComponent::~UPrimitiveComponent()
{
}

void UPrimitiveComponent::Update(float deltaTime, TArray<FRenderInfo>* outRenderInfos)
{
	// Todo: Update coordinates here
	{
		//UE_LOG("Primitive selected");
	}

	GetRenderInfos(outRenderInfos);
}

void UPrimitiveComponent::GetRenderInfos(TArray<FRenderInfo>* outRenderInfos) const
{
	assert(outRenderInfos);

	outRenderInfos->Add(makeRenderInfo());
}

FRenderInfo UPrimitiveComponent::makeRenderInfo() const
{
	ERenderFlags renderFlags =
		ERenderFlags::RF_Raycastable |
		ERenderFlags::RF_Primitive;

	if (mbUseTexture)
	{
		renderFlags = renderFlags | ERenderFlags::RF_Texture;
	}

	if (mbShowBoundingBox)
	{
		renderFlags = renderFlags | ERenderFlags::RF_BoundingBox;
	}

	FRenderInfo renderInfo{};
	//renderInfo.ePrimitive = mePrimitive;
	renderInfo.WorldTransformMatrix = GetTransformMatrix();
	renderInfo.ObejctID = { mOwner->UUID, mOwner->InternalIndex };
	renderInfo.Color = mColor;
	renderInfo.eRenderFlags = renderFlags;
	renderInfo.Textmesh = nullptr;

	renderInfo.LocalBounds = mLocalBounds;
	renderInfo.WorldBounds = TransformBoundingBox(mLocalBounds, renderInfo.WorldTransformMatrix);

	return renderInfo;
}

/*
void UPrimitiveComponent::Render(FStruct)
{
	// Todo: Fix renderer
	mGraphicsManager->Render(GetTransformMatrix(), mePrimitive);
}
*/

static FBoundingBox CalculateBounds(
	const FVertexSimple* vertices,
	uint32 count)
{
	FBoundingBox result{};
	result.min = vertices[0].GetPosition();
	result.max = result.min;

	for (uint32 i = 1; i < count; ++i)
	{
		const FVector position = vertices[i].GetPosition();

		result.min.x = std::min(result.min.x, position.x);
		result.min.y = std::min(result.min.y, position.y);
		result.min.z = std::min(result.min.z, position.z);

		result.max.x = std::max(result.max.x, position.x);
		result.max.y = std::max(result.max.y, position.y);
		result.max.z = std::max(result.max.z, position.z);
	}

	return result;
}

static const FBoundingBox& GetPrimitiveLocalBounds(EPrimitive primitive)
{
	switch (primitive)
	{
	case EPrimitive::EP_Cube:
	{
		static const FBoundingBox bounds =
			CalculateBounds(Cube_vertices, _countof(Cube_vertices));
		return bounds;
	}
	case EPrimitive::EP_Sphere:
	{
		static const FBoundingBox bounds =
			CalculateBounds(Sphere_vertices, _countof(Sphere_vertices));
		return bounds;
	}
	case EPrimitive::EP_Triangle:
	{
		static const FBoundingBox bounds =
			CalculateBounds(Triangle_vertices, _countof(Triangle_vertices));
		return bounds;
	}
	case EPrimitive::EP_GizmoArrow:
	{
		static const FBoundingBox bounds =
			CalculateBounds(GizmoArrow_vertices, _countof(GizmoArrow_vertices));
		return bounds;
	}
	case EPrimitive::EP_Circle:
	{
		static const FBoundingBox bounds =
			CalculateBounds(Circle_vertices, _countof(Circle_vertices));
		return bounds;
	}
	case EPrimitive::EP_BillboardQuad:
	{
		static const FBoundingBox bounds =
			CalculateBounds(Quad_vertices, _countof(Quad_vertices));
		return bounds;
	}
	}

	static const FBoundingBox emptyBounds{};
	return emptyBounds;
}

FBoundingBox UPrimitiveComponent::GetWorldBounds() const
{
	return TransformBoundingBox(mLocalBounds, GetTransformMatrix());
}


std::span<const FPropertyInfo>
UPrimitiveComponent::GetDeclaredProperties()
{
	static const FPropertyInfo Properties[] =
	{
		REFLECT_PROPERTY(
			UPrimitiveComponent,
			mePrimitive),

		REFLECT_PROPERTY(
			UPrimitiveComponent,
			mbUseTexture,
			EPropertyFlags::Serializable | EPropertyFlags::Editable
		),

		REFLECT_PROPERTY(
			UPrimitiveComponent,
			mbShowBoundingBox,
			EPropertyFlags::Serializable | EPropertyFlags::Editable
		),

		REFLECT_PROPERTY(
			UPrimitiveComponent,
			mColor,
			EPropertyFlags::Serializable | EPropertyFlags::Editable
		),
	};

	return Properties;
}
