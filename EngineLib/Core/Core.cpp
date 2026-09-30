#include "Core.h"

FString::FString()
	: mData()
{
}

FString::FString(std::string_view str)
	: mData(str)

{
}

FString::FString(const char* str)
	: mData(str)
{
}

FString::FString(const FString& other)
	: mData(other.mData)
{
}

FString& FString::operator=(const FString& other)
{
	if (this != &other)
	{
		mData = other.mData;
	}
	return *this;
}

FString& FString::operator=(std::string_view str)
{
	mData = str;
	return *this;
}

FString::FString(FString&& other) noexcept
	: mData(std::move(other.mData))
{
}

FString& FString::operator=(FString&& other) noexcept
{
	if (this != &other)
	{
		mData = std::move(other.mData);
	}
	return *this;
}

FString& FString::Append(std::string_view str)
{
	mData.append(str);
	return *this;
}

FString& FString::Append(const FString& str)
{
	mData.append(str.mData);
	return *this;
}

FString& FString::AppendChar(char c)
{
	mData.push_back(c);
	return *this;
}

void FString::AppendInt(int32 num)
{
	mData.append(std::to_string(num));
}

int FString::Compare(const FString& other) const
{
	return mData.compare(other.mData);
}

bool FString::Contains(std::string_view subStr) const
{
	return mData.find(subStr) != std::string::npos;
}

bool FString::Contains(const FString& subStr) const
{
	return mData.find(subStr.mData) != std::string::npos;
}

const char* FString::CStr() const
{
	return mData.c_str();
}

bool FString::EndsWith(std::string_view suffix) const
{
	if (suffix.size() > mData.size())
		return false;
	return std::equal(suffix.rbegin(), suffix.rend(), mData.rbegin());
}

bool FString::EndsWith(const FString& suffix) const
{
	return EndsWith(suffix.mData);
}

bool FString::Equals(std::string_view other) const
{
	return mData == other;
}

bool FString::Equals(const FString& other) const
{
	return mData == other.mData;
}

int32 FString::Find(std::string_view subStr, int32 startIndex) const
{
	if (startIndex < 0 || startIndex >= static_cast<int32>(mData.size()))
		return -1;
	size_t pos = mData.find(subStr, static_cast<size_t>(startIndex));
	if (pos == std::string::npos)
		return -1;
	return static_cast<int32>(pos);
}

int32 FString::Find(const FString& subStr, int32 startIndex) const
{
	return Find(subStr.mData, startIndex);
}

void FString::InsertAt(int32 index, std::string_view str)
{
	if (index < 0 || index > static_cast<int32>(mData.size()))
		return;
	mData.insert(static_cast<size_t>(index), str);
}

void FString::InsertAt(int32 index, const FString& str)
{
	InsertAt(index,	str.mData);
}

bool FString::IsNumeric() const
{
	if (mData.empty())
		return false;
	size_t start = 0;
	if (mData[0] == '-' || mData[0] == '+')
		start = 1;
	for (size_t i = start; i < mData.size(); ++i)
	{
		if (!std::isdigit(static_cast<unsigned char>(mData[i])))
			return false;
	}
	return true;
}

FString FString::Left(int32 count) const
{
	if (count < 0)
		count = 0;
	if (count > static_cast<int32>(mData.size()))
		count = static_cast<int32>(mData.size());
	return FString(mData.substr(0, static_cast<size_t>(count)));
}

FString FString::LeftChop(int32 count) const
{
	if (count < 0)
		count = 0;
	if (count > static_cast<int32>(mData.size()))
		count = static_cast<int32>(mData.size());
	return FString(mData.substr(0, mData.size() - static_cast<size_t>(count)));
}

int32 FString::Len() const
{
	return static_cast<int32>(mData.size());
}

FString FString::Mid(int32 start, int32 count) const
{
	if (start < 0)
		start = 0;
	if (count < 0)
		count = 0;
	if (start >= static_cast<int32>(mData.size()))
		return FString();
	return FString(mData.substr(static_cast<size_t>(start), static_cast<size_t>(count)));
}

void FString::RemoveAt(int32 index, int32 count)
{
	if (index < 0 || index >= static_cast<int32>(mData.size()) || count <= 0)
		return;
	mData.erase(static_cast<size_t>(index), static_cast<size_t>(count));
}

bool FString::RemoveFromEnd(std::string_view suffix)
{
	if (EndsWith(suffix))
	{
		mData.erase(mData.size() - suffix.size());
		return true;
	}
	return false;
}

bool FString::RemoveFromEnd(const FString& suffix)
{
	return RemoveFromEnd(suffix.mData);
}

bool FString::RemoveFromStart(std::string_view prefix)
{
	if (StartsWith(prefix))
	{
		mData.erase(0, prefix.size());
		return true;
	}
	return false;
}

bool FString::RemoveFromStart(const FString& prefix)
{
	return RemoveFromStart(prefix.mData);
}

FString FString::Replace(std::string_view from, std::string_view to) const
{
	FString result(mData);
	size_t pos = 0;
	while ((pos = result.mData.find(from, pos)) != std::string::npos)
	{
		result.mData.replace(pos, from.size(), to);
		pos += to.size();
	}
	return result;
}

FString FString::Replace(const FString& from, const FString& to) const
{
	return Replace(from.mData, to.mData);
}

void FString::Reserve(int32 characterCount)
{
	if (characterCount > 0)
		mData.reserve(static_cast<size_t>(characterCount));
}

void FString::Reset(int32 newReservedSize)
{
	mData.clear();
	if (newReservedSize > 0)
		mData.reserve(static_cast<size_t>(newReservedSize));
}

FString FString::Reverse() const
{
	FString result(mData);
	std::reverse(result.mData.begin(), result.mData.end());
	return result;
}

void FString::ReverseString()
{
	std::reverse(mData.begin(), mData.end());
}

FString FString::Right(int32 count) const
{
	if (count < 0)
		count = 0;
	if (count > static_cast<int32>(mData.size()))
		count = static_cast<int32>(mData.size());
	return FString(mData.substr(mData.size() - static_cast<size_t>(count)));
}

FString FString::RightChop(int32 count) const
{
	if (count < 0)
		count = 0;
	if (count > static_cast<int32>(mData.size()))
		count = static_cast<int32>(mData.size());
	return FString(mData.substr(static_cast<size_t>(count)));
}

bool FString::StartsWith(std::string_view prefix) const
{
	if (prefix.size() > mData.size())
		return false;
	return std::equal(prefix.begin(), prefix.end(), mData.begin());
}

bool FString::StartsWith(const FString& prefix) const
{
	return StartsWith(prefix.mData);
}

bool FString::ToBool() const
{
	if (mData == "True" || mData == "Yes")
		return true;
	if (mData == "False" || mData == "No")
		return false;
	return std::stoi(mData) != 0;
}

// Core.h의 FString 내부에 추가
float FString::ToFloat() const
{
	return std::stof(mData);
}

int32 FString::ToInt() const
{
	return std::stoi(mData);
}

FString FString::ToLower() const
{
	FString result(mData);
	for (char& c : result.mData)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

	return result;
}

FString FString::ToUpper() const
{
	FString result(mData);
	for (char& c : result.mData)
		c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

	return result;
}

FString& FString::operator+=(std::string_view str)
{
	mData.append(str);
	return *this;
}

FString& FString::operator+=(const FString& str)
{
	mData.append(str.mData);
	return *this;
}

bool FString::operator== (const FString& str) const
{
	return Equals(str);
}
