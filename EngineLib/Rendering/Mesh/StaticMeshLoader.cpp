#include "StaticMeshLoader.h"

#include <filesystem>
#include <utility>

#include "Archive.h"
#include "Core/IO/FileManager.h"
#include "Core/IO/WindowsBinReader.h"
#include "Core/IO/WindowsBinWriter.h"
#include "Core/Name.h"
#include "Rendering/Mesh/ObjImporter.h"
#include "Rendering/Mesh/StaticMesh.h"

namespace
{
	bool AreMaterialLibrariesUsable(
		const std::filesystem::path& sourcePath,
		const TArray<FString>& materialLibraryPaths,
		const std::filesystem::file_time_type& binaryWriteTime);

	constexpr uint32 StaticMeshMagic =
		static_cast<uint32>('P') |
		(static_cast<uint32>('M') << 8) |
		(static_cast<uint32>('S') << 16) |
		(static_cast<uint32>('H') << 24);

	constexpr uint32 StaticMeshBinaryVersion = 3;

	constexpr uint32 MaxVertices = 10'000'000;
	constexpr uint32 MaxIndices = 30'000'000;
	constexpr uint32 MaxSections = 1'000'000;
	constexpr uint32 MaxGroupNames = 1'000'000;
	constexpr uint32 MaxMaterialSlots = 65'536;

	std::filesystem::path ResolvePath(
		const FString& pathString)
	{
		std::filesystem::path path( pathString.CStr());

		if (path.is_relative())
		{
			path = std::filesystem::path{kDefaultRootPath} / path;
		}

		std::error_code error;

		auto normalized = std::filesystem::weakly_canonical( path, error);

		if (!error)
		{
			return normalized;
		}

		error.clear();

		normalized = std::filesystem::absolute( path, error);

		return error ? path.lexically_normal() : normalized.lexically_normal();
	}

	std::filesystem::path MakeBinaryPath( const std::filesystem::path& sourcePath)
	{
		std::filesystem::path binaryPath = sourcePath;

		binaryPath.replace_extension(".bin");
		return binaryPath;
	}

	bool IsBinaryUsable( const std::filesystem::path& sourcePath, const std::filesystem::path& binaryPath)
	{
		std::error_code error;

		const bool binaryExists = std::filesystem::is_regular_file( binaryPath, error);

		if (error || !binaryExists)
		{
			return false;
		}

		error.clear();

		const bool sourceExists = std::filesystem::is_regular_file( sourcePath, error);

		if (error)
		{
			return false;
		}

		// 배포 환경처럼 OBJ가 없고 바이너리만 있는 경우
		if (!sourceExists)
		{
			return true;
		}

		const auto sourceWriteTime = std::filesystem::last_write_time( sourcePath, error);

		if (error)
		{
			return false;
		}

		const auto binaryWriteTime = std::filesystem::last_write_time( binaryPath, error);

		if (error)
		{
			return false;
		}

		if (binaryWriteTime < sourceWriteTime)
		{
			return false;
		}

		return true;
	}


	// 텍스처 경로를 바이너리 파일에 저장할 때는 상대 경로로 변환하고, 바이너리에서 읽을 때는 다시 절대 경로로 복원하는 함수.
	// 다른 PC나 다른 폴더로 .bin과 텍스처를 함께 옮겨도 원래 PC의 절대 경로를 참조해서 텍스처 로딩이 실패할 수 있는 상황을 방지.
	void SerializeTexturePath(FArchive& archive, FName& texture, const std::filesystem::path& binaryDirectory)
	{
		uint8 hasValue =
			archive.IsSaving() && texture.IsValid() ? 1 : 0;

		archive << hasValue;

		if (archive.HasError() || hasValue > 1)
		{
			archive.SetError();
			return;
		}

		if (!hasValue)
		{
			if (archive.IsLoading())
			{
				texture = FName();
			}
			return;
		}

		FString storedPath;

		if (archive.IsSaving())
		{
			// 현재 importer가 넣어 주는 텍스처 경로는 절대 경로.
			const std::filesystem::path absolutePath(
				texture.ToString().CStr());

			const auto relativePath =
				absolutePath.lexically_relative(binaryDirectory);

			// 다른 드라이브 등에 있어 상대 경로를 만들 수 없는 경우.
			if (relativePath.empty() || relativePath.is_absolute())
			{
				archive.SetError();
				return;
			}

			storedPath = FString(relativePath.generic_string().c_str());
		}

		archive << storedPath;

		if (archive.IsLoading() && !archive.HasError())
		{
			const std::filesystem::path relativePath(storedPath.CStr());

			if (relativePath.empty() || relativePath.is_absolute())
			{
				archive.SetError();
				return;
			}

			const auto absolutePath =
				(binaryDirectory / relativePath).lexically_normal();

			texture = FName(FString(absolutePath.string().c_str()));
		}
	}

	void SerializeName( FArchive& archive, FName& name)
	{
		uint8 hasValue = archive.IsSaving() && name.IsValid() ? 1 : 0;

		archive << hasValue;

		if (archive.HasError() ||
			hasValue > 1)
		{
			archive.SetError();
			return;
		}

		FString stringValue;

		if (archive.IsSaving() && hasValue)
		{
			stringValue = name.ToString();
		}

		if (hasValue)
		{
			archive << stringValue;
		}

		if (archive.IsLoading() && !archive.HasError())
		{
			name = hasValue ? FName(stringValue) : FName();
		}
	}

	void SerializeVector( FArchive& archive, FVector& vector)
	{
		archive << vector.x;
		archive << vector.y;
		archive << vector.z;
	}

	void SerializeVector2(FArchive& archive, FVector2& vector)
	{
		archive << vector.x;
		archive << vector.y;
	}

	void SerializeColor(FArchive& archive, FLinearColor& color)
	{
		archive << color.R;
		archive << color.G;
		archive << color.B;
		archive << color.A;
	}

	void SerializeVertex(FArchive& archive, FNormalVertex& vertex)
	{
		SerializeVector(archive, vertex.pos);

		SerializeVector(archive, vertex.normal);

		SerializeColor(archive, vertex.color);

		SerializeVector2(archive, vertex.tex);
	}

	void SerializeSection(FArchive& archive, FStaticMeshSection& section)
	{
		archive << section.Name;
		archive << section.MaterialSlotIndex;
		archive << section.StartIndex;
		archive << section.IndexCount;
		archive << section.GroupIndex;
	}

	void SerializeMaterial(FArchive& archive, FMaterial& material,const std::filesystem::path& binaryDirectory)
	{
		SerializeVector(archive, material.AmbientColor);

		SerializeVector(archive, material.DiffuseColor);

		SerializeVector(archive, material.SpecularColor);

		archive << material.SpecularExponent;
		archive << material.Opacity;

		SerializeTexturePath(archive, material.DiffuseTexture,binaryDirectory);

		SerializeTexturePath(archive,material.NormalTexture,binaryDirectory);

		SerializeTexturePath(archive, material.SpecularTexture,binaryDirectory);
	}

	void SerializeMaterialSlot(FArchive& archive, FMaterialSlot& slot,const std::filesystem::path& binaryDirectory)
	{
		archive << slot.Name;

		SerializeMaterial(archive, slot.DefaultMaterial,binaryDirectory);
	}

	template<typename T, typename Serializer>
	bool SerializeArray(FArchive& archive, TArray<T>& values, uint32 maxCount, uint64 minElementBytes, Serializer serializeElement)
	{
		uint32 count = archive.IsSaving() ?
			static_cast<uint32>(values.Num())
			: 0;

		archive << count;

		if (archive.HasError() || count > maxCount)
		{
			archive.SetError();
			return false;
		}

		if (archive.IsLoading())
		{

			// 반드시 메모리 할당 전에 검사.
			// 곱셈 대신 나눗셈을 사용해 오버플로를 방지.
			// minElementByte는 파일에 기록되는 원소 하나의 최소크기

			if (minElementBytes == 0 || count > archive.RemainingBytes() / minElementBytes)
			{
				archive.SetError();
				return false;
			}
			values.Reset(static_cast<int32>(count));

			for (uint32 i = 0; i < count; ++i)
			{
				values.Add(T{});
			}
		}

		for (uint32 i = 0;i < count; ++i)
		{
			serializeElement(archive, values[i]);

			if (archive.HasError())
			{
				return false;
			}
		}

		return true;
	}

	template<typename T>
	bool SerializeMeshBuffer(FArchive& archive, TArray<T>& values, uint32 maxCount)
	{
		static_assert(
			std::is_same_v<T, FNormalVertex> ||
			std::is_same_v<T, uint32>);

		if (archive.HasError())
		{
			return false;
		}

		uint32 count = archive.IsSaving()? static_cast<uint32>(values.Num()): 0;

		// 기존 형식 유지: 원소 개수 다음에 배열 데이터
		archive << count;

		if (archive.HasError())
		{
			return false;
		}

		if (count > maxCount ||
			count > static_cast<uint32>((std::numeric_limits<int32>::max)()) ||
			count > (std::numeric_limits<size_t>::max)() / sizeof(T))
		{
			archive.SetError();
			return false;
		}

		const size_t byteCount = static_cast<size_t>(count) * sizeof(T);

		if (archive.IsLoading())
		{
			// 메모리 할당 전에 파일에 충분한 데이터가 있는지 확인
			if (byteCount > archive.RemainingBytes())
			{
				archive.SetError();
				return false;
			}

			// 현재 TArray::Reset()은 공간만 확보하므로,
			// Add()로 실제 원소도 생성해야 합니다.
			values.Reset(static_cast<int32>(count));

			for (uint32 i = 0; i < count; ++i)
			{
				values.Add(T{});
			}
		}

		// 원소별로 읽거나 쓰지 않고 배열 전체를 한 번에 처리
		if (byteCount > 0)
		{
			archive.Serialize(values.GetData(), byteCount);
		}

		return !archive.HasError();
	}

	bool SerializeMaterialLibraryPaths(FArchive& archive, TArray<FString>& materialLibraryPaths)
	{
		constexpr uint64 StringMinBytes = sizeof(uint32);
		return SerializeArray(archive, materialLibraryPaths, MaxMaterialSlots, StringMinBytes,
			[](FArchive& ar, FString& path)
			{
				ar << path;
			});
	}

	bool SerializeBakedData(FArchive& archive, FStaticMesh& mesh, TArray<FMaterialSlot>& materialSlots,const std::filesystem::path& binaryDirectory)
	{

		constexpr uint64 VertexBytes = sizeof(float) * 12;
		constexpr uint64 IndexBytes = sizeof(uint32);

		constexpr uint64 StringMinBytes = sizeof(uint32);

		constexpr uint64 SectionMinBytes =
			StringMinBytes + sizeof(int32) * 4;

		// Ambient/Diffuse/Specular: float 9개
		// SpecularExponent/Opacity: float 2개
		// 텍스처 3개: 각각 hasValue(uint8)

		constexpr uint64 MaterialSlotMinBytes = StringMinBytes + sizeof(float) * 11 + sizeof(uint8) * 3;

		if (!SerializeMeshBuffer( archive, mesh.Vertices, MaxVertices))
		{
			return false;
		}

		if (!SerializeMeshBuffer(archive, mesh.Indices, MaxIndices))
		{
			return false;
		}

		if (!SerializeArray(archive, mesh.Sections, MaxSections, SectionMinBytes, SerializeSection))
		{
			return false;
		}

		if (!SerializeArray(archive, mesh.GroupNames, MaxGroupNames, StringMinBytes,
			[](FArchive& ar, FString& name)
			{
				ar << name;
			}))
		{
			return false;
		}

		if (!SerializeArray(archive,materialSlots,MaxMaterialSlots, MaterialSlotMinBytes,
			[&binaryDirectory](FArchive& ar, FMaterialSlot& slot)
			{
				SerializeMaterialSlot(ar, slot, binaryDirectory);
			}))
		{
			return false;
		}

		return !archive.HasError();
	}

	bool ValidateBakedData(const FStaticMesh& mesh, const TArray<FMaterialSlot>& materialSlots)
	{
		if (mesh.Vertices.IsEmpty() ||
			mesh.Indices.IsEmpty() ||
			mesh.Indices.Num() % 3 != 0)
		{
			return false;
		}

		for (uint32 index : mesh.Indices)
		{
			if (index >= static_cast<uint32>(mesh.Vertices.Num()))
			{
				return false;
			}
		}

		for (const FStaticMeshSection& section :
			mesh.Sections)
		{
			if (section.StartIndex < 0 ||
				section.IndexCount <= 0 ||
				section.IndexCount % 3 != 0)
			{
				return false;
			}

			const uint64 sectionEnd =
				static_cast<uint64>(section.StartIndex) + static_cast<uint64>(section.IndexCount);

			if (sectionEnd > static_cast<uint64>(mesh.Indices.Num()))
			{
				return false;
			}

			if (section.MaterialSlotIndex < -1 ||
				section.MaterialSlotIndex >= static_cast<int32>(materialSlots.Num()))
			{
				return false;
			}

			if (section.GroupIndex < -1 ||
				section.GroupIndex >= static_cast<int32>(mesh.GroupNames.Num()))
			{
				return false;
			}
		}

		return true;
	}

	// OBJ 파일에서 참조하는 mtl 파일이 바이너리보다 최신이면, 바이너리를 다시 생성해야 함.
	bool AreMaterialLibrariesUsable(const std::filesystem::path& sourcePath, const TArray<FString>& materialLibraryPaths, const std::filesystem::file_time_type& binaryWriteTime)
	{
		std::error_code error;

		const bool sourceExists = std::filesystem::is_regular_file(sourcePath, error);

		if (error) return false;

		if(!sourceExists)
		{
			return false;
		}

		for (const FString& storedPath :
			materialLibraryPaths)
		{
			std::filesystem::path materialPath(
				storedPath.CStr());

			if (materialPath.empty())
			{
				return false;
			}

			if (materialPath.is_relative())
			{
				materialPath = sourcePath.parent_path() / materialPath;
			}

			materialPath = materialPath.lexically_normal();

			error.clear();

			const bool materialExists = std::filesystem::is_regular_file( materialPath, error);

			if (error || !materialExists)
			{
				return false;
			}

			error.clear();

			const auto materialWriteTime = std::filesystem::last_write_time( materialPath, error);

			if (error || materialWriteTime > binaryWriteTime)
			{
				return false;
			}
		}

		return true;	
	}

	bool TryLoadBinary( const std::filesystem::path& sourcePath, FStaticMeshCookedData& outResult)
	{

		const std::filesystem::path binaryPath = MakeBinaryPath(sourcePath);

		if (!IsBinaryUsable( sourcePath, binaryPath))
		{
			return false;
		}

		FWindowsBinReader reader(binaryPath);

		if (reader.HasError())
		{
			return false;
		}

		uint32 magic = 0;
		uint32 version = 0;

		reader << magic;
		reader << version;

		if (reader.HasError() ||
			magic != StaticMeshMagic ||
			version != StaticMeshBinaryVersion)
		{
			return false;
		}


		TArray<FString> materialLibraryPaths;

		if (!SerializeMaterialLibraryPaths(reader,
			materialLibraryPaths))
		{
			return false;
		}

		std::error_code error;

		const auto binaryWriteTime = std::filesystem::last_write_time( binaryPath, error);

		if (error)
		{
			return false;
		}

		if (!AreMaterialLibrariesUsable( sourcePath, materialLibraryPaths, binaryWriteTime))
		{
			return false;
		}

		auto mesh = std::make_unique<FStaticMesh>();

		TArray<FMaterialSlot> materialSlots;

		if (!SerializeBakedData(reader, *mesh, materialSlots, binaryPath.parent_path()))
		{
			return false;
		}

		if (!ValidateBakedData( *mesh, materialSlots))
		{
			return false;
		}

		outResult.meshData =std::move(mesh);

		outResult.materialSlots = std::move(materialSlots);

		return true;
	}

	bool SaveBinary(const std::filesystem::path& sourcePath, FStaticMesh& mesh, TArray<FMaterialSlot>& materialSlots, TArray<FString>& materialLibraryPaths)
	{
		const std::filesystem::path binaryPath = MakeBinaryPath(sourcePath);

		FWindowsBinWriter writer(binaryPath);

		if (writer.HasError())
		{
			return false;
		}

		uint32 magic = StaticMeshMagic;
		uint32 version = StaticMeshBinaryVersion;

		writer << magic;
		writer << version;

		if (!SerializeMaterialLibraryPaths(writer, materialLibraryPaths))
		{
			return false;
		}

		if (!SerializeBakedData(writer,mesh, materialSlots, binaryPath.parent_path()))
		{
			return false;
		}

		writer.Flush();
		return !writer.HasError();
	}
}

bool StaticMeshLoader::Load( const FString& sourcePath, FStaticMeshCookedData& outResult)
{
	outResult.meshData.reset();
	outResult.materialSlots.Reset(0);

	const std::filesystem::path resolvedPath =  ResolvePath(sourcePath);

	if (TryLoadBinary(resolvedPath, outResult))
	{
		outResult.meshData->PathFileName = FName(sourcePath);

		return true;
	}

	FObjImportResult objImportResult;

	if (!FObjImporter::ParseAndConvert(sourcePath,objImportResult) || !objImportResult.meshData)
	{
		return false;
	}

	// Bake 저장 실패가 현재 메시 로딩 실패를 뜻하지는 않는다.
	SaveBinary(resolvedPath, *objImportResult.meshData, objImportResult.materialSlots, objImportResult.materialLibraryPaths);

	objImportResult.meshData->PathFileName =
		FName(sourcePath);

	outResult.meshData =
		std::move(objImportResult.meshData);

	outResult.materialSlots =
		std::move(objImportResult.materialSlots);

	return true;
}
