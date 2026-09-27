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

// FNamePoolShardIBit
static constexpr uint32 FNamePoolShardBits = 8;
static constexpr uint32 FNamePoolShards = 1U << FNamePoolShardBits;
constexpr uint32 FNamePoolInitialSlotBits = 8;
constexpr uint32 FNamePoolInitialSlotsPerShard = 1 << FNamePoolInitialSlotBits;

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
	std::mutex Lock;

	uint32 CurrentBlock = 0;
	uint32 CurrentByteCursor = 0;
	uint8* Blocks[FNameMaxBlocks] = {};
};

// FNameHash
// Hash(uint32) and ProbeHash(uint32)
struct FNameHash
{
	uint32 ShardIndex;
	uint32 UnmaskedSlotIndex;
	uint32 SlotProbeHash;

	static uint32 GetShardIndexFromHiBits(uint32 HiBits)
	{
		return HiBits % FNamePoolShards;
	}

	static uint32 GetShardIndex(uint64 Hash)
	{
		return GetShardIndexFromHiBits(static_cast<uint32>(Hash >> 32));
	}

	static uint64 GenerateHash(const char* Str, size_t Len)
	{
		return CityHash64(Str, Len);
	}

	FNameHash(const char* Str, int32 Len)
		: FNameHash(GenerateHash(Str, Len), Len)
	{
	}

	FNameHash()
		: ShardIndex(0), UnmaskedSlotIndex(0), SlotProbeHash(0)
	{
	}

	FNameHash(uint64 InHash, int32 Len)
	{
		uint32 Hi = static_cast<uint32>(InHash >> 32);
		uint32 Lo = static_cast<uint32>(InHash & 0xFFFFFFFF);

		ShardIndex = GetShardIndexFromHiBits(Hi);
		UnmaskedSlotIndex = Lo;
		SlotProbeHash = (Hi & ProbeHashMask);
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

// FNamePoolShard
// Hash bucket sharding
class FNamePoolShardBase
{
public:
	void Initialize(FNameEntryAllocator& InEntries)
	{
		Entries = &InEntries;
		UsedSlots = 0;
		Slots = static_cast<FNameSlot*>(calloc(FNamePoolInitialSlotsPerShard, sizeof(FNameSlot)));	
		CapacityMask = FNamePoolInitialSlotsPerShard - 1;
	}

	~FNamePoolShardBase()
	{
		free(Slots);
		Slots = nullptr;
		UsedSlots = 0;
		CapacityMask = 0;		
	}

	uint32 Capacity() const { return CapacityMask + 1; }

protected:
	enum { LoadFactorQuotient = 9, LoadFactorDivisor = 10}; // Realloc slots when 90% full

	mutable std::shared_mutex Lock;
	uint32 UsedSlots = 0;
	uint32 CapacityMask = 0;
	FNameSlot* Slots = nullptr;
	FNameEntryAllocator* Entries = nullptr;
};

template<ENameCase Sensitivity>
class FNamePoolShard : public FNamePoolShardBase
{
public:
	FNameEntryId Find(const FNameValue& InValue) const
	{
		std::shared_lock<std::shared_mutex> ReadLock(Lock);

		if (CapacityMask == 0)
		{
			return FNameEntryId();
		}
		
		uint32 SlotIndex = InValue.Hash.UnmaskedSlotIndex & CapacityMask;

		// Linear probing
		while (Slots[SlotIndex].Used())
		{
			if (Slots[SlotIndex].GetProbeHash() == InValue.Hash.ProbeHash)
			{
				FNameEntryId ExistingId = Slots[SlotIndex].GetId();
				const FNameEntry& Entry = Entries->Resolve(ExistingId);

				const char* ExistingStr = Entry.GetName();
				if (Entry.GetNameLength() == InValue.Name.length())
				{
					bool bIsMatch = true;
					if constexpr (Sensitivity == ENameCase::CaseSensitive)
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

	FNameEntryId Insert(const FNameValue& InValue, bool& bCreatedNewEntry)
	{
		std::unique_lock<std::shared_mutex> WriteLock(Lock);
		
		if ((UsedSlots + 1) * LoadFactorDivisor >= (Capacity() * LoadFactorQuotient))
		{
			Grow();
		}

		uint32 SlotIndex = InValue.Hash.UnmaskedSlotIndex & CapacityMask;
		uint32 Probes = 0;		

		// Linear probing
		while (Slots[SlotIndex].Used())
		{			
			SlotIndex = (SlotIndex + 1) & CapacityMask;
		}

		Slots[SlotIndex] = FNameSlot(InEntryId, InValue.Hash.ProbeHash);

		bCreatedNewEntry = true;
		++UsedSlots;

		return InEntryId;
	}

private:
	void Grow()
	{
		Grow(Capacity() * 2);
	}

	// Rehashing
	void Grow(const uint32 NewCapacity)
	{
		uint32 OldCapcity = Capacity();
		uint32 NewCapacityMask = NewCapacity - 1;

		FNameSlot* NewSlots = static_cast<FNameSlot*>(std::calloc(NewCapacity, sizeof(FNameSlot)));

		for (uint32 i = 0; i < OldCapcity; ++i)
		{
			FNameSlot OldSlot = Slots[i];

			if (OldSlot.Used())
			{
				const FNameEntry& Entry = Entries->Resolve(OldSlot.GetId());
				const char* EntryName = Entry.GetName();
				size_t Len = std::min(static_cast<size_t>(Entry.GetNameLength()), size_t(NAME_SIZE - 1));
				FNameHash HashValue;

				if constexpr (Sensitivity == ENameCase::IgnoreCase)
				{
					// Stack buffer
					char LowerBuffer[NAME_SIZE];					

					for (size_t c = 0; c < Len; ++c)
					{
						LowerBuffer[c] = static_cast<char>(std::tolower(EntryName[c]));
					}

					HashValue = FNameHash(LowerBuffer, static_cast<int32>(Len));
				}
				else
				{
					HashValue = FNameHash(EntryName, Len);
				}

				uint32 SlotIndex = HashValue.UnmaskedSlotIndex & NewCapacityMask;

				// Linear probing
				while (NewSlots[SlotIndex].Used())
				{
					SlotIndex = (SlotIndex + 1) & NewCapacityMask;
				}
				NewSlots[SlotIndex] = OldSlot;
			}
		}
		
		free(Slots);
		Slots = NewSlots;
		CapacityMask = NewCapacityMask;
	}
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
		FNameEntryId Existing = DisplayShards[DisplayValue.Hash.ShardIndex].Find(DisplayValue);
		if (Existing.ToUnstableInt() != 0)
		{
			return Existing;
		}

		// Comparison
		FNameComparisonValue ComparisonValue(NameString);
		return ComparisonShards[ComparisonValue.Hash.ShardIndex].Find(ComparisonValue);
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
		FNameEntryId Existing = DisplayShards[DisplayValue.Hash.ShardIndex].Find(DisplayValue);
		if (Existing.ToUnstableInt() != 0)
		{
			return Existing;
		}

		bool bAdded = false;
		FNameComparisonValue ComparisonValue(NameString);
		FNameEntryId ComparisonId = ComparisonShards[ComparisonValue.Hash.ShardIndex].Insert(ComparisonValue, bAdded);

		return StoreDisplayValue(DisplayValue, ComparisonId, bAdded);
	}
	const FNameEntry& Resolve(FNameEntryId Id) const
	{
		return Entries.Resolve(Id);
	}

private:
	FNamePool()
	{
		// Shard
		for (FNamePoolShardBase& Shard : ComparisonShards)
		{
			Shard.Initialize(Entries);
		}
		for (FNamePoolShardBase& Shard : DisplayShards)
		{
			Shard.Initialize(Entries);
		}
	}

	FNameEntryId StoreDisplayValue(const FNameValue& InValue, FNameEntryId InComparisonId, bool bWasAdded)
	{
		if (bWasAdded)
		{			
			return DisplayShards[InValue.Hash.ShardIndex].Insert(InValue, bWasAdded);
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

		DisplayShards[InValue.Hash.ShardIndex].Insert(InValue, bWasAdded);
	
		return NewHandle;
	}

	FNameEntryAllocator Entries;
	FNamePoolShard<ENameCase::CaseSensitive> DisplayShards[FNamePoolShards];
	FNamePoolShard<ENameCase::IgnoreCase> ComparisonShards[FNamePoolShards];
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
