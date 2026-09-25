#pragma once

#include <functional>

#include "Core/Core.h"
#include <string_view>

// Max size of name, including the null terminator
enum { NAME_SIZE = 1024 };

enum class ENameCase : uint8
{
	CaseSensitive,
	IgnoreCase,
};

struct FNameEntryId
{
	constexpr FNameEntryId() : Value(0) {}

	constexpr uint32 ToUnstableInt() const { return Value; }
	static FNameEntryId FromUnstableInt(uint32 UnstableInt)
	{
		FNameEntryId Id;
		Id.Value = UnstableInt;
		return Id;
	}

	// operator
	bool operator==(const FNameEntryId& Rhs) const
	{
		return this->Value == Rhs.Value;
	}

	bool operator!=(const FNameEntryId& Rhs) const
	{
		return this->Value != Rhs.Value;
	}

private:
	uint32 Value;
};

struct FNameEntryHeader
{
	uint16 bIsWide : 1;
	uint16 Len : 15;
};

struct FNameEntry
{
private:
	FNameEntryHeader Header;
	FNameEntryId ComparisonId;
	uint8 NameData[0];

public:
	FNameEntry(const FNameEntry&) = delete;
	FNameEntry(FNameEntry&&) = delete;
	FNameEntry& operator=(const FNameEntry&) = delete;
	FNameEntry& operator=(FNameEntry&&) = delete;

	bool IsWide() const { return Header.bIsWide; }
	int32 GetNameLength() const { return Header.Len; }
	FNameEntryId GetComparisonId() const { return ComparisonId; }
	void SetComparisonId(FNameEntryId NewId) { ComparisonId = NewId; }

	const char* GetName() const { return (char*)NameData; }
};

class FName
{
public:
	constexpr FName() = default;
	FName(std::string_view str);
	FName(const char* pStr);
	FName(FString str);
	FName(std::string_view BaseName, int32 InNumber);

	int32 Compare(const FName& Rhs) const;
	bool operator==(const FName& Rhs) const;
	bool operator<(const FName& Rhs) const;

	FString ToString() const;

	FNameEntryId GetDisplayId() const { return DisplayId; }
	FNameEntryId GetComparisonId() const { return ComparisonId; }
	int32 GetNumber() const { return Number; }

	bool IsNone() const { return ComparisonId.ToUnstableInt() == 0; }
	bool IsValid() const { return !IsNone(); }	

private:
	FNameEntryId DisplayId;
	FNameEntryId ComparisonId;
	int32 Number = 0;
};

// helper
inline void SplitNameAndNumber(std::string_view InString, std::string_view& OutString, int32& OutNumber)
{
	if (InString.empty())
	{
		return;
	}

	OutString = InString;
	OutNumber = 0;

	const size_t Sep = InString.rfind('_');
	if (Sep == std::string_view::npos || Sep == 0 || Sep + 1 == InString.length())
	{
		return;
	}

	int32 Num = 0;
	for (size_t i = Sep + 1; i < InString.length(); ++i)
	{
		if (!std::isdigit(static_cast<unsigned char>(InString[i])))
		{
			return;
		}

		Num = Num * 10 + (InString[i] - '0');
	}

	OutString = InString.substr(0, Sep);
	OutNumber = Num + 1;
}


template<>
struct std::hash<FName>
{
	std::size_t operator()(const FName& name) const noexcept
	{
		uint64 combined = (static_cast<uint64>(name.GetComparisonId().ToUnstableInt()) << 32)
			| static_cast<uint32>(name.GetNumber());
		return std::hash<uint64>{}(combined);
	}
};
