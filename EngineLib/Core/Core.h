#pragma once

#include <iosfwd>
#include <string>
#include <string_view>
#include <memory>

typedef char int8;
typedef unsigned char uint8;
typedef short int16;
typedef unsigned short uint16;
typedef int int32;
typedef unsigned int uint32;
typedef long long int64;
typedef unsigned long long uint64;

typedef float float32;
typedef double float64;

struct FString
{
public:
	FString();
	FString(std::string_view str);
	FString(const char* str);

	FString(const FString& other);
	FString& operator=(const FString& other);
	FString& operator=(std::string_view str);
	FString(FString&& other) noexcept;
	FString& operator=(FString&& other) noexcept;

	using iterator = std::string::iterator;
	using const_iterator = std::string::const_iterator;

	iterator begin() { return mData.begin(); }
	const_iterator begin() const { return mData.begin(); }

	iterator end() { return mData.end(); }
	const_iterator end() const { return mData.end(); }

	inline operator std::string() const { return mData; }
	inline operator std::string_view() const { return mData; }

	FString& Append(std::string_view str);
	FString& Append(const FString& str);
	FString& AppendChar(char c);

	template<typename... Args>
	FString& Appendf(std::string_view fmt, Args&&... args)
	{
		std::string formatted;
		snprintf(formatted.data(), formatted.size(), fmt.data(), std::forward<Args>(args)...);
		mData->append(formatted);
		return *this;
	}
	void AppendInt(int32 num);

	int32 Compare(const FString& other) const;

	bool Contains(std::string_view subStr) const;
	bool Contains(const FString& subStr) const;

	const char* CStr() const;

	bool EndsWith(std::string_view suffix) const;
	bool EndsWith(const FString& suffix) const;

	bool Equals(std::string_view other) const;
	bool Equals(const FString& other) const;

	int32 Find(std::string_view subStr, int32 startIndex = 0) const;
	int32 Find(const FString& subStr, int32 startIndex = 0) const;

	void InsertAt(int32 index, std::string_view str);
	void InsertAt(int32 index, const FString& str);

	bool IsNumeric() const;

	FString Left(int32 count) const;
	FString LeftChop(int32 count) const;

	int32 Len() const;

	FString Mid(int32 start, int32 count) const;

	void RemoveAt(int32 index, int32 count = 1);

	bool RemoveFromEnd(std::string_view suffix);
	bool RemoveFromEnd(const FString& suffix);

	bool RemoveFromStart(std::string_view prefix);
	bool RemoveFromStart(const FString& prefix);

	FString Replace(std::string_view from, std::string_view to) const;
	FString Replace(const FString& from, const FString& to) const;

	void Reserve(int32 characterCount);
	void Reset(int32 newReservedSize = 0);

	FString Reverse() const;
	void ReverseString();

	FString Right(int32 count) const;
	FString RightChop(int32 count) const;

	bool StartsWith(std::string_view prefix) const;
	bool StartsWith(const FString& prefix) const;

	bool ToBool() const;
	float ToFloat() const;
	int32 ToInt() const;

	FString ToLower() const;
	FString ToUpper() const;

	FString& operator+=(std::string_view str);
	FString& operator+=(const FString& str);

	bool operator== (const FString& str) const;

	bool IsEmpty() const { return mData.empty(); }


private:
	std::string mData;
};

template<>
struct std::hash<FString>
{
	std::size_t operator()(const FString& str) const noexcept
	{
		return std::hash<std::string_view>{}(static_cast<std::string_view>(str));
	}
};

#include <format>

template<>
struct std::formatter<FString, char> : std::formatter<std::string_view, char>
{
	template<typename FormatContext>
	auto format(const FString& str, FormatContext& ctx) const
	{
		return std::formatter<std::string_view, char>::format(static_cast<std::string_view>(str), ctx);
	}
};

#ifndef FORCEINLINE
	#if defined(_MSC_VER)
		#define FORCEINLINE __forceinline
	#else
		#define FORCEINLINE inline __attribute__((always_inline))
	#endif
#endif

