#include "StaticMesh.h"
#include "ThirdParty/Meshoptimizer/meshoptimizer.h"

IMPLEMENT_CLASS_WITH_PROPERTIES(UStaticMesh, UObject);
IMPLEMENT_SERIALIZATION(UStaticMesh, UObject,
	{
		// TODO: Get handle from assest manager?
		mStaticMeshAsset = nullptr;
	}
)

void UStaticMesh::Initialize(FStaticMesh* inStaticMesh)
{
	UObject::Initialize();
	SetStaticMeshAsset(inStaticMesh);
}

void UStaticMesh::Initialize(std::unique_ptr<FStaticMesh> inStaticMesh)
{
	UObject::Initialize();
	SetStaticMeshAsset(std::move(inStaticMesh));
}

void UStaticMesh::Initialize(std::unique_ptr<FStaticMesh> inStaticMesh, TArray<const UMaterial*>&& inDefaultMaterialRefs)
{
	UObject::Initialize();
	SetStaticMeshAsset(std::move(inStaticMesh));
	mDefaultMaterialRefs = std::move(inDefaultMaterialRefs);
}

const FName& UStaticMesh::GetAssetPathFileName() const
{
	return mStaticMeshAsset->PathFileName;
}

void UStaticMesh::SetStaticMeshAsset(FStaticMesh* inStaticMesh)
{
	mStaticMeshAsset.reset(inStaticMesh);
	mStaticMeshAsset->LODs.Reserve(MAX_LOD_COUNT);
	if (mStaticMeshAsset && mStaticMeshAsset->LODs.Num() > 0 && mStaticMeshAsset->LODs[0].BufferKey.IsNone())
	{
		mStaticMeshAsset->LODs[0].BufferKey = mStaticMeshAsset->PathFileName;
	}
	mBVH.Build(mStaticMeshAsset->LODs[0].Vertices, mStaticMeshAsset->LODs[0].Indices);
}

void UStaticMesh::SetStaticMeshAsset(std::unique_ptr<FStaticMesh> inStaticMesh)
{
	mStaticMeshAsset = std::move(inStaticMesh);
	mStaticMeshAsset->LODs.Reserve(MAX_LOD_COUNT);
	if (mStaticMeshAsset && mStaticMeshAsset->LODs.Num() > 0 && mStaticMeshAsset->LODs[0].BufferKey.IsNone())
	{
		mStaticMeshAsset->LODs[0].BufferKey = mStaticMeshAsset->PathFileName;
	}
	mBVH.Build(mStaticMeshAsset->LODs[0].Vertices, mStaticMeshAsset->LODs[0].Indices);
}

const FStaticMesh* UStaticMesh::GetStaticMeshAsset() const
{
	return mStaticMeshAsset.get();
}

const UMaterial* UStaticMesh::GetDefaultMaterialOrNull(int32 slotIndex) const
{
	if (slotIndex < 0 || slotIndex >= mDefaultMaterialRefs.Num())
	{
		return nullptr;
	}
	const UMaterial* materialAsset = mDefaultMaterialRefs[slotIndex];
	if (!materialAsset)
	{
		return nullptr;
	}
	return materialAsset;
}

const TArray<const UMaterial*>& UStaticMesh::GetDefaultMaterials() const
{
	return mDefaultMaterialRefs;
}

bool UStaticMesh::GenerateLOD(float reductionRatio, float screenSize, float targetError)
{
	if (!mStaticMeshAsset || mStaticMeshAsset->LODs.IsEmpty() ||
		mStaticMeshAsset->LODs.Num() >= MAX_LOD_COUNT) // Limit to MAX_LOD_COUNT LODs for now
	{
		return false;
	}

	const FStaticMeshLOD& baseLOD = mStaticMeshAsset->LODs[0];
	uint32 currentIndexOffset = 0;

	// Set new LOD
	FStaticMeshLOD newLOD;
	newLOD.Vertices = baseLOD.Vertices;
	newLOD.Indices.Reset(0);
	newLOD.Sections.Reset(0);
	newLOD.ScreenSize = screenSize;

	for (const FStaticMeshSection& baseSection : baseLOD.Sections)
	{
		if (baseSection.IndexCount == 0)
		{
			continue;
		}

		const uint32* sectionIndices = &baseLOD.Indices[baseSection.StartIndex];
		size_t targetIndexCount = static_cast<size_t>(baseSection.IndexCount * reductionRatio);

		// Make simplified LOD
		std::vector<uint32_t> simplifiedIndices(static_cast<uint32>(baseSection.IndexCount));			
		size_t newIndexCount = meshopt_simplify(
			simplifiedIndices.data(),
			sectionIndices,
			static_cast<size_t>(baseSection.IndexCount),
			&baseLOD.Vertices[0].pos.x,
			baseLOD.Vertices.Num(),
			sizeof(FNormalVertex),
			targetIndexCount,
			targetError
		);
		simplifiedIndices.resize(newIndexCount);

		// New section data
		FStaticMeshSection newSection = baseSection;
		newSection.StartIndex = static_cast<int32>(currentIndexOffset);
		newSection.IndexCount = static_cast<int32>(newIndexCount);
		newLOD.Sections.Add(newSection);

		for (uint32_t idx : simplifiedIndices)
		{
			newLOD.Indices.Add(idx);
		}

		currentIndexOffset += static_cast<uint32>(newIndexCount);
	}

	FName lodKey = FName(std::format("{}_LOD{}", mStaticMeshAsset->PathFileName.ToString(), mStaticMeshAsset->LODs.Num()));
	newLOD.BufferKey = lodKey;
	mStaticMeshAsset->LODs.Add(std::move(newLOD));

	return true;
}

bool UStaticMesh::RemoveLOD(int32 targetLODIndex)
{
	return false;
}
