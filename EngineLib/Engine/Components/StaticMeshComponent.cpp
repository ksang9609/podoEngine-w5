#include "StaticMeshComponent.h"

static FBoundingBox calculateBounds(const TArray<FNormalVertex> vertices);

IMPLEMENT_CLASS_WITH_PROPERTIES(UStaticMeshComponent, UMeshComponent);

void UStaticMeshComponent::SerializeClass(json::JSON& outJson) const
{
	UMeshComponent::SerializeClass(outJson);
	for (const FPropertyInfo& property : ClassInfo.DeclaredProperties)
	{
		property.Serialize(property, this, outJson["Properties"]);
	}
}

void UStaticMeshComponent::DeserializeClass(const json::JSON& inJson)
{
	UMeshComponent::DeserializeClass(inJson);
	const json::JSON& properties = inJson.at("Properties");

	for (const FPropertyInfo& property : ClassInfo.DeclaredProperties)
	{
		// Older scene files do not contain the static mesh asset key.
		if (!properties.hasKey(property.JsonKey))
		{
			continue;
		}

		property.Deserialize(property, this, properties);
	}
}

void UStaticMeshComponent::PostDeserialize()
{
	UMeshComponent::PostDeserialize();
	mStaticMeshRef = nullptr;
}

void UStaticMeshComponent::Initialize(
	FVector location,
	FRotator rotation,
	FVector scale3D,
	const UStaticMesh* staticMeshOrNull,
	bool bUseTexture
)
{
	UPrimitiveComponent::Initialize(EPrimitive::EP_StaticMesh, location, rotation, scale3D, bUseTexture);
	mStaticMeshRef = staticMeshOrNull;
	mStaticMeshAssetKey = staticMeshOrNull
		? staticMeshOrNull->GetAssetPathFileName()
		: FName();
	resetMaterialOverrides();

	mLocalBounds = FBoundingBox{};

	if (mStaticMeshRef)
	{
		mLocalBounds = calculateBounds(mStaticMeshRef->GetStaticMeshAsset()->Vertices);
	}
	else
	{
		mLocalBounds = FBoundingBox{};
	}
}

void UStaticMeshComponent::SetStaticMesh(const UStaticMesh& staticMeshRef)
{
	mStaticMeshRef = &staticMeshRef;
	resetMaterialOverrides();
	mStaticMeshAssetKey = staticMeshRef.GetAssetPathFileName();

	mLocalBounds = calculateBounds(mStaticMeshRef->GetStaticMeshAsset()->Vertices);

	updateComponentToWorld();
}

const FName& UStaticMeshComponent::GetMaterialAssetKey(int32 slotIndex) const
{
	if (slotIndex < 0 || slotIndex >= mStaticMeshRef->GetDefaultMaterials().Num())
	{
		static const FName InvalidMaterialKey;
		return InvalidMaterialKey;
	}

	// Return the overridden material key if it exists
	const FMaterialOverride& materialOverride = mMaterialOverrides[slotIndex];
	if (materialOverride.bIsSet && materialOverride.OverridedMaterialRef)
	{
		return materialOverride.OverridedMaterialRef->GetMaterialName();
	}

	// Return the default material key from the static mesh if no override exists
	const UMaterial* materialAsset = mStaticMeshRef->GetDefaultMaterials()[slotIndex];
	if (!materialAsset)
	{
		static const FName InvalidMaterialKey;
		return InvalidMaterialKey;
	}
	return materialAsset->GetMaterialName();
}

const UStaticMesh* UStaticMeshComponent::GetStaticMeshAsset() const
{
	assert(mStaticMeshRef && "Static mesh reference is null.");
	return mStaticMeshRef;
}

const UMaterial* UStaticMeshComponent::GetMaterialAsset(int32 slotIndex) const
{
	if (slotIndex < 0 || slotIndex >= mStaticMeshRef->GetDefaultMaterials().Num())
	{
		return nullptr;
	}

	if (slotIndex < mMaterialOverrides.Num())
	{
		const FMaterialOverride& materialOverride = mMaterialOverrides[slotIndex];

		if (materialOverride.bIsSet)
		{
			return materialOverride.OverridedMaterialRef;
		}
	}

	return mStaticMeshRef->GetDefaultMaterials()[slotIndex];
}

bool UStaticMeshComponent::SetMaterial(int32 slotIndex, const UMaterial& materialAsset)
{
	if (slotIndex < 0 || slotIndex >= mMaterialOverrides.Num())
	{
		return false;
	}

	FMaterialOverride& materialOverride = mMaterialOverrides[slotIndex];

	materialOverride.bIsSet = true;
	materialOverride.OverridedMaterialRef = &materialAsset;
	mMaterialAssetKeys[slotIndex] = materialAsset.GetMaterialName();

	return true;
}

bool UStaticMeshComponent::ClearMaterialOverride(int32 slotIndex)
{
	if (slotIndex < 0 || slotIndex >= mMaterialOverrides.Num())
	{
		return false;
	}

	mMaterialOverrides[slotIndex] = FMaterialOverride{};
	mMaterialAssetKeys[slotIndex] = FName("");
	return true;
}

FRenderInfo UStaticMeshComponent::makeRenderInfo() const
{
	FRenderInfo renderInfo = UMeshComponent::makeRenderInfo();

	renderInfo.MeshName = mStaticMeshRef ? mStaticMeshRef->GetAssetPathFileName() : FName();
	renderInfo.StaticMesh = mStaticMeshRef ? mStaticMeshRef->GetStaticMeshAsset() : nullptr;

	if (renderInfo.StaticMesh)
	{
		const int32 slotCount = mStaticMeshRef->GetDefaultMaterials().Num();

		renderInfo.Materials.Reserve(slotCount);

		for (int32 slotIndex = 0; slotIndex < slotCount; ++slotIndex)
		{
			const UMaterial* materialAsset = GetMaterialAsset(slotIndex);
			const FMaterial* material = materialAsset ? materialAsset->GetMaterial() : nullptr;

			renderInfo.Materials.Add(material ? *material : FMaterial{});
		}
	}

	renderInfo.SubUVMesh = &mSubUVMesh;

	return renderInfo;
}

void UStaticMeshComponent::resetMaterialOverrides()
{
	mMaterialOverrides.Reset(mStaticMeshRef->GetDefaultMaterials().Num());
	mMaterialAssetKeys.Reset(mStaticMeshRef->GetDefaultMaterials().Num());

	for (int32 i = 0; i < mStaticMeshRef->GetDefaultMaterials().Num(); ++i)
	{
		mMaterialOverrides.Add(FMaterialOverride{});
		mMaterialAssetKeys.Add(FName(""));
	}
}

static FBoundingBox calculateBounds(
	const TArray<FNormalVertex> vertices)
{
	FBoundingBox result{};
	result.min = vertices[0].pos;
	result.max = result.min;

	for (const auto& vertex : vertices)
	{
		const FVector position = vertex.pos;

		result.min.x = std::min(result.min.x, position.x);
		result.min.y = std::min(result.min.y, position.y);
		result.min.z = std::min(result.min.z, position.z);

		result.max.x = std::max(result.max.x, position.x);
		result.max.y = std::max(result.max.y, position.y);
		result.max.z = std::max(result.max.z, position.z);
	}

	return result;
}

std::span<const FPropertyInfo> UStaticMeshComponent::GetDeclaredProperties()
{
	static const FPropertyInfo Properties[] =
	{
		REFLECT_PROPERTY(
			UStaticMeshComponent,
			mStaticMeshAssetKey
		),
		REFLECT_PROPERTY(
			UStaticMeshComponent,
			mMaterialAssetKeys
		),
	};

	return Properties;
}
