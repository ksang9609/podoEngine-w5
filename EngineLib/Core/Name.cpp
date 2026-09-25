#include "Core.h"
#include "Core/Container/TArray.h"
#include "ThirdParty/Hash/city.h"
#include "Name.h"

#include <vector>
#include <algorithm>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>

static constexpr uint32 FNameMaxBlockBits = 13;
static constexpr uint32 FNameBlockOffsetBits = 16;
static constexpr uint32 FNameMaxBlocks = 1 << FNameMaxBlockBits;
static constexpr uint32 FNameBlockOffsets = 1 << FNameBlockOffsetBits;

static constexpr uint32 EntryIdBits = FNameMaxBlockBits + FNameBlockOffsetBits;
static constexpr uint32 EntryIdMask = (1 << EntryIdBits) - 1;
static constexpr uint32 ProbeHashShift = EntryIdBits;
static constexpr uint32 ProbeHashMask = ~EntryIdMask;

// Unpacked FNameEntryId to Block and Offset
struct FNameEntryHandle
{
	uint32 Block = 0;
	uint32 Offset = 0;

	FNameEntryHandle(uint32 InBlock, uint32 InOffset)
		: Block(InBlock), Offset(InOffset) {
	}
	FNameEntryHandle(FNameEntryId Id)
		: Block(Id.ToUnstableInt() >> FNameBlockOffsetBits), Offset(Id.ToUnstableInt()& (FNameBlockOffsets - 1)) {
	}

	operator FNameEntryId() const
	{
		return FNameEntryId::FromUnstableInt(Block << FNameBlockOffsetBits | Offset);
	}

	explicit operator bool() const { return Block | Offset; }
};

// FNameSlot
// Hash and Id
struct FNameSlot
{
	FNameSlot() {}
	FNameSlot(FNameEntryId Value, uint32 ProbeHash)
		: IdAndHash(Value.ToUnstableInt() | ProbeHash) {
	}

	FNameEntryId GetId() const { return FNameEntryId::FromUnstableInt(IdAndHash & EntryIdMask); }
	uint32 GetProbeHash() const { return IdAndHash & ProbeHashMask; }

	bool Used() const { return IdAndHash != 0; }
private:
	uint32 IdAndHash = 0;
};

// FNameEntryAllocator
// Allocate memory to FNameEntry
class FNameEntryAllocator
{
public:
	enum { Stride = alignof(FNameEntry) };
	enum { BlockSizeBytes = Stride * FNameBlockOffsets };

	FNameEntryAllocator()
	{
		Blocks[0] = new uint8[BlockSizeBytes]();
		CurrentByteCursor = Stride;
	}

	~FNameEntryAllocator()
	{
		for (int32 Index = CurrentBlock; Index >= 0; --Index)
		{
			delete[] Blocks[Index];
		}
	}

	FNameEntryHandle Allocate(uint32 Bytes)
	{
		uint32 Step = (Bytes + Stride - 1) & ~(Stride - 1);

		if (CurrentByteCursor + Step > BlockSizeBytes)
		{
			AllocateNewBlock();
		}

		uint32 ByteOffset = CurrentByteCursor;
		CurrentByteCursor += Step;

		return FNameEntryHandle(CurrentBlock, ByteOffset / Stride);
	}

	FNameEntry& Resolve(FNameEntryHandle Handle) const
	{
		return *reinterpret_cast<FNameEntry*>(Blocks[Handle.Block] + Stride * Handle.Offset);
	}

	void AllocateNewBlock()
	{
		++CurrentBlock;
		CurrentByteCursor = 0;

		if (Blocks[CurrentBlock] == nullptr)
		{
			Blocks[CurrentBlock] = new uint8[BlockSizeBytes]();
		}
	}

private:
	uint32 CurrentBlock = 0;
	uint32 CurrentByteCursor = 0;
	uint8* Blocks[FNameMaxBlocks] = {};
};

// FNameHash
// Hash(uint32) and ProbeHash(uint32)
struct FNameHash
{
	uint32 Hash;
	uint32 ProbeHash;

	static uint64 GenerateHash(const char* Str, size_t Len)
	{
		return CityHash64(Str, Len);
	}

	FNameHash(const char* Str, int32 Len)
		: FNameHash(GenerateHash(Str, Len), Len)
	{
	}

	FNameHash()
		: Hash(0), ProbeHash(0)
	{
	}

	FNameHash(uint64 InHash, int32 Len)
	{
		uint32 Hi = static_cast<uint32>(InHash >> 32);
		uint32 Lo = static_cast<uint32>(InHash & 0xFFFFFFFF);

		Hash = Lo;
		ProbeHash = Hi & ProbeHashMask;
	}
};

// FNameValue
// Name(string_view), Hash(FNameHash)
struct FNameValue
{
	FNameValue(std::string_view InName)
		: Name(InName)
	{
	}

	FNameValue(std::string_view InName, FNameHash InHash)
		: Name(InName), Hash(InHash)
	{
	}

	std::string_view Name;
	FNameHash Hash;
};

// FNameComparisonValue
// Only for using lower(Not display)
struct FNameComparisonValue : public FNameValue
{
	FNameComparisonValue(std::string_view InName)
		: FNameValue(InName)
	{
		// Stack buffer
		char LowerBuffer[NAME_SIZE];

		size_t Len = std::min(InName.length(), size_t(NAME_SIZE - 1));

		for (size_t i = 0; i < Len; ++i)
		{
			LowerBuffer[i] = static_cast<char>(std::tolower(InName[i]));
		}

		Hash = FNameHash(LowerBuffer, static_cast<int32>(Len));
	}
};
// FNameDisplayValue
struct FNameDisplayValue : public FNameValue
{
	FNameDisplayValue(std::string_view InName)
		: FNameValue(InName)
	{
		Hash = FNameHash(InName.data(), static_cast<int32>(InName.length()));
	}
};

class FNamePoolShardBase
{
public:
	void Initialize(FNameEntryAllocator& InEntries)
	{
		Entries = &InEntries;
		UsedSlots = 0;
	}

	~FNamePoolShardBase()
	{
		UsedSlots = 0;
		CapacityMask = 0;
		Slots = nullptr;
	}

protected:
	enum { LoadFactorQuotient = 9, LoadFactorDivisor = 10}; // Realloc slots when 90% full

	mutable std::shared_mutex Lock;
	uint32 UsedSlots = 0;
	uint32 CapacityMask = 0;
	FNameSlot* Slots = nullptr;
	FNameEntryAllocator* Entries = nullptr;
};

// FNamePool
// HashBuckets, Entries
class FNamePool
{
public:
	static FNamePool& Get()
	{
		static FNamePool Instance;
		return Instance;
	}
	FNameEntryId Find(std::string_view NameString) const
	{
		if (NameString.empty())
		{
			return FNameEntryId();
		}

		if (NameString.length() >= NAME_SIZE)
		{
			assert(false && "FName string too long! FName is only meant for identifiers (<= 1023 chars).");

			NameString = NameString.substr(0, NAME_SIZE - 1);
		}

		// Display
		FNameDisplayValue DisplayValue(NameString);
		FNameEntryId Existing = FNamePool::FindValue(DisplayHashBuckets, DisplayValue, true);
		if (Existing.ToUnstableInt() != 0)
		{
			return Existing;
		}

		// Comparison
		FNameComparisonValue ComparisonValue(NameString);

		return FNamePool::FindValue(ComparisonHashBuckets, ComparisonValue, false);
	}
	FNameEntryId Store(std::string_view NameString)
	{
		if (NameString.empty())
		{
			return FNameEntryId();
		}

		if (NameString.length() >= NAME_SIZE)
		{
			assert(false && "FName string too long! FName is only meant for identifiers (<= 1023 chars).");

			NameString = NameString.substr(0, NAME_SIZE - 1);
		}

		FNameDisplayValue DisplayValue(NameString);
		FNameEntryId Existing = FNamePool::FindValue(DisplayHashBuckets, DisplayValue, true);
		if (Existing.ToUnstableInt() != 0)
		{
			return Existing;
		}

		bool bAdded = false;
		FNameComparisonValue ComparisonValue(NameString);
		FNameEntryId ComparisonId = StoreComparisonValue(ComparisonValue, bAdded);

		return StoreDisplayValue(DisplayValue, ComparisonId, bAdded);
	}
	const FNameEntry& Resolve(FNameEntryId Id) const
	{
		return Entries.Resolve(Id);
	}

private:
	FNamePool()
	{
		Initialize(1 << 20);
	}

	void Initialize(uint32 InitialCapacity)
	{
		ComparisonHashBuckets.Init(FNameSlot(), InitialCapacity);
		DisplayHashBuckets.Init(FNameSlot(), InitialCapacity);
	}

	FNameEntryId FindValue(const TArray<FNameSlot>& Buckets, const FNameValue& InValue, bool bIsCaseSensitive) const
	{
		uint32 CapacityMask = static_cast<uint32>(Buckets.Num() - 1);
		uint32 SlotIndex = InValue.Hash.Hash & CapacityMask;

		while (Buckets[SlotIndex].Used())
		{
			if (Buckets[SlotIndex].GetProbeHash() == InValue.Hash.ProbeHash)
			{
				FNameEntryId ExistingId = Buckets[SlotIndex].GetId();
				const FNameEntry& Entry = Resolve(ExistingId);

				const char* ExistingStr = Entry.GetName();
				if (Entry.GetNameLength() == InValue.Name.length())
				{
					bool bIsMatch = true;
					if (bIsCaseSensitive)
					{
						bIsMatch = (std::memcmp(ExistingStr, InValue.Name.data(), InValue.Name.length()) == 0);
					}
					else
					{
						bIsMatch = (_strnicmp(ExistingStr, InValue.Name.data(), InValue.Name.length()) == 0);
					}

					if (bIsMatch)
					{
						return ExistingId;
					}
				}
			}

			SlotIndex = (SlotIndex + 1) & CapacityMask;
		}


		return FNameEntryId();
	}

	FNameEntryId StoreValue(TArray<FNameSlot>& Buckets, const FNameValue& InValue, bool bIsCaseSensitive)
	{
		FNameEntryId ExistingId = FNamePool::FindValue(Buckets, InValue, bIsCaseSensitive);
		if (ExistingId.ToUnstableInt() != 0)
		{
			return ExistingId;
		}

		// Write Memory
		uint32 NeededByte = static_cast<uint32>(sizeof(FNameEntryHeader) + InValue.Name.length() + 1); // 1 : null terminator
		FNameEntryHandle NewHandle = Entries.Allocate(NeededByte);

		// Set header
		FNameEntry& NewEntry = Entries.Resolve(NewHandle);
		uint16* HeaderPtr = reinterpret_cast<uint16*>(&NewEntry);
		*HeaderPtr = static_cast<uint16>(InValue.Name.length()) << 1;
		// Set string
		char* DataPtr = const_cast<char*>(NewEntry.GetName());
		std::memcpy(DataPtr, InValue.Name.data(), InValue.Name.length());
		DataPtr[InValue.Name.length()] = '\0'; // null terminator

		uint32 CapacityMask = static_cast<uint32>(Buckets.Num() - 1);
		uint32 SlotIndex = InValue.Hash.Hash & CapacityMask;
		uint32 Probes = 0;
		const uint32 MaxProbes = static_cast<uint32>(Buckets.Num());

		while (Buckets[SlotIndex].Used())
		{
			if (++Probes >= MaxProbes)
			{
				assert(false && "FNamePool out of memory!");
				std::abort();
			}

			SlotIndex = (SlotIndex + 1) & CapacityMask;
		}

		Buckets[SlotIndex] = FNameSlot(NewHandle, InValue.Hash.ProbeHash);

		return NewHandle;
	}

	FNameEntryId StoreComparisonValue(const FNameValue& InValue, bool& bOutAdded)
	{
		FNameEntryId ExistingId = FNamePool::FindValue(ComparisonHashBuckets, InValue, false);
		if (ExistingId.ToUnstableInt() != 0)
		{
			return ExistingId;
		}

		bOutAdded = true;

		// Write Memory
		uint32 NeededByte = static_cast<uint32>(sizeof(FNameEntry) + InValue.Name.length() + 1); // 1 : null terminator
		FNameEntryHandle NewHandle = Entries.Allocate(NeededByte);

		// Set header
		FNameEntry& NewEntry = Entries.Resolve(NewHandle);
		uint16* HeaderPtr = reinterpret_cast<uint16*>(&NewEntry);
		*HeaderPtr = static_cast<uint16>(InValue.Name.length()) << 1;
		// Set string
		char* DataPtr = const_cast<char*>(NewEntry.GetName());
		std::memcpy(DataPtr, InValue.Name.data(), InValue.Name.length());
		DataPtr[InValue.Name.length()] = '\0'; // null terminator

		NewEntry.SetComparisonId(NewHandle);

		InsertSlot(ComparisonHashBuckets, InValue, NewHandle);

		return NewHandle;
	}

	FNameEntryId StoreDisplayValue(const FNameValue& InValue, FNameEntryId InComparisonId, bool bWasAdded)
	{
		if (bWasAdded)
		{
			InsertSlot(DisplayHashBuckets, InValue, InComparisonId);
			return InComparisonId;
		}

		// Write Memory
		uint32 NeededByte = static_cast<uint32>(sizeof(FNameEntry) + InValue.Name.length() + 1); // 1 : null terminator
		FNameEntryHandle NewHandle = Entries.Allocate(NeededByte);

		// Set header
		FNameEntry& NewEntry = Entries.Resolve(NewHandle);
		uint16* HeaderPtr = reinterpret_cast<uint16*>(&NewEntry);
		*HeaderPtr = static_cast<uint16>(InValue.Name.length()) << 1;
		// Set string
		char* DataPtr = const_cast<char*>(NewEntry.GetName());
		std::memcpy(DataPtr, InValue.Name.data(), InValue.Name.length());
		DataPtr[InValue.Name.length()] = '\0'; // null terminator

		NewEntry.SetComparisonId(InComparisonId);

		InsertSlot(DisplayHashBuckets, InValue, NewHandle);

		return NewHandle;
	}

	void InsertSlot(TArray<FNameSlot>& Buckets, const FNameValue& InValue, FNameEntryId InEntryId)
	{
		uint32 CapacityMask = static_cast<uint32>(Buckets.Num() - 1);
		uint32 SlotIndex = InValue.Hash.Hash & CapacityMask;
		uint32 Probes = 0;
		const uint32 MaxProbes = static_cast<uint32>(Buckets.Num());

		while (Buckets[SlotIndex].Used())
		{
			if (++Probes >= MaxProbes)
			{
				assert(false && "FNamePool out of memory!");
				std::abort();
			}

			SlotIndex = (SlotIndex + 1) & CapacityMask;
		}

		Buckets[SlotIndex] = FNameSlot(InEntryId, InValue.Hash.ProbeHash);
	}

	FNameEntryAllocator Entries;

	TArray<FNameSlot> ComparisonHashBuckets;
	TArray<FNameSlot> DisplayHashBuckets;
};

FName::FName(std::string_view str)
{
	if (str.length() > 0)
	{
		std::string_view BaseStr;
		SplitNameAndNumber(str, BaseStr, Number);

		DisplayId = FNamePool::Get().Store(BaseStr);
		ComparisonId = FNamePool::Get().Resolve(DisplayId).GetComparisonId();
	}
}

FName::FName(const char* pStr)
	: FName(pStr ? FName(std::string_view(pStr)) : FName())
{
}

FName::FName(FString str)
	: FName(std::string_view(str))
{
}

FName::FName(std::string_view BaseName, int32 InNumber)
{
	if (!BaseName.empty())
	{
		Number = (InNumber >= 0) ? (InNumber + 1) : 0;
		DisplayId = FNamePool::Get().Store(BaseName);
		ComparisonId = FNamePool::Get().Resolve(DisplayId).GetComparisonId();
	}
}

int32 FName::Compare(const FName& Rhs) const
{
	if (ComparisonId != Rhs.ComparisonId)
	{
		return (ComparisonId.ToUnstableInt() < Rhs.ComparisonId.ToUnstableInt()) ? -1 : 1;
	}
	return Number - Rhs.Number;
}

bool FName::operator==(const FName& Rhs) const
{
	return this->ComparisonId == Rhs.ComparisonId && this->Number == Rhs.Number;
}

bool FName::operator<(const FName& Rhs) const
{
	if (ComparisonId != Rhs.ComparisonId)
	{
		return ComparisonId.ToUnstableInt() < Rhs.ComparisonId.ToUnstableInt();
	}
	return Number < Rhs.Number;
}

FString FName::ToString() const
{
	if (ComparisonId.ToUnstableInt() == 0)
	{
		return FString("");
	}

	const FNameEntry& Entry = FNamePool::Get().Resolve(DisplayId);

	FString Result = FString(Entry.GetName());

	if (Number > 0)
	{
		Result += "_" + std::to_string(Number - 1);
	}

	return Result;
}
