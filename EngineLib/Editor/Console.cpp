#include <format>

#include "ThirdParty/ImGui/imgui.h"
#include "Console.h"

#include <cassert>
#include <chrono>
#include <ctime>
#include <sstream>
#include <algorithm>
#include <cctype>


namespace
{
	FString GetCurrentTimeString()
	{
		auto Now = std::chrono::system_clock::now();
		std::time_t NowTime = std::chrono::system_clock::to_time_t(Now);

		std::tm LocalTime{};
		localtime_s(&LocalTime, &NowTime);

		std::ostringstream Stream;
		Stream.imbue(std::locale(""));
		Stream << std::put_time(&LocalTime, "%H:%M:%S");

		return FString{ Stream.str() };
	}

	const char* LogCategoryToString(ELogCategory Category)
	{
		switch (Category)
		{
		case ELogCategory::Core:    return "[Core]";
		case ELogCategory::Render:  return "[Render]";
		case ELogCategory::Physics: return "[Physics]";
		case ELogCategory::Etc:     return "[Etc]";
		default:                    return "[Unknown]";
		}
	}

	const char* LogLevelToString(ELogLevel Level)
	{
		switch (Level)
		{
		case ELogLevel::Log:     return "[Log]";
		case ELogLevel::Warning: return "[Warning]";
		case ELogLevel::Error:   return "[Error]";
		case ELogLevel::Fatal:   return "[Fatal]";
		default:                 return "[Unknown]";
		}
	}

	ImVec4 GetLogLevelColor(ELogLevel Level)
	{
		switch (Level)
		{
		case ELogLevel::Warning:
			return ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
		case ELogLevel::Error:
			return ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
		case ELogLevel::Fatal:
			return ImVec4(1.0f, 0.0f, 1.0f, 1.0f);
		default:
			return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		}
	}

	void DrawConsoleMessage(const FConsoleMessage& Message)
	{
		ImGui::TextUnformatted(Message.Time.CStr());
		ImGui::SameLine();

		const ImVec4 LevelColor = GetLogLevelColor(Message.Level);
		ImGui::PushStyleColor(ImGuiCol_Text, LevelColor);
		ImGui::TextUnformatted(LogLevelToString(Message.Level));
		ImGui::PopStyleColor();
		ImGui::SameLine();

		ImGui::TextUnformatted(LogCategoryToString(Message.Category));
		ImGui::SameLine();

		ImGui::TextUnformatted(Message.Text.CStr());
	}
}

ConsoleWindow::ConsoleWindow()
	: mTitle("Console Window")
{
}

ConsoleWindow& ConsoleWindow::GetInstance()
{
	static ConsoleWindow instance;
	return instance;
}

void ConsoleWindow::Init(std::string_view title, int capacity)
{
	mTitle = title;
	mCapacity = capacity;

	Clear();

	mMessages.Reserve(mCapacity);

	mPendingBuffers[0].Reserve(64);
	mPendingBuffers[1].Reserve(64);

	ImGuiIO& io = ImGui::GetIO();
	mFont = io.Fonts->AddFontFromFileTTF("Assets/Fonts/consola.ttf", 16.0f);
}

void ConsoleWindow::Update()
{
	FlushPending();
}

void ConsoleWindow::DrawContents(FEditorCommands& outCommands)
{
	if (mFont)
	{
		ImGui::PushFont(mFont);
	}

	// BeginPopup은 제목 표시줄이 없으므로 직접 표시
	ImGui::TextUnformatted(mTitle.CStr());
	ImGui::Separator();

	const float footerHeight =
		ImGui::GetFrameHeightWithSpacing() * 2.0f;

	if (ImGui::BeginChild(
		"ConsoleMessage",
		ImVec2(0.0f, -footerHeight),
		true))
	{
		const bool bWasAtBottom =
			ImGui::GetScrollY() >= ImGui::GetScrollMaxY();

		for (uint32 index = 0; index < mCount; ++index)
		{
			DrawConsoleMessage(GetMessage(index));
		}

		if (mbAutoScroll && bWasAtBottom)
		{
			ImGui::SetScrollHereY(1.0f);
		}
	}
	ImGui::EndChild();

	ImGui::Separator();

	if (ImGui::InputText(
		"##ConsoleInput",
		mInputBuffer,
		sizeof(mInputBuffer),
		ImGuiInputTextFlags_EnterReturnsTrue))
	{
		if (mInputBuffer[0] != '\0')
		{
			ExecuteCommand(mInputBuffer, outCommands);
			mInputBuffer[0] = '\0';
			ImGui::SetKeyboardFocusHere(-1);
		}
	}

	ImGui::TextDisabled(
		"Type 'help' and press ENTER for available commands.");

	if (mFont)
	{
		ImGui::PopFont();
	}
}

void ConsoleWindow::AddLog(ELogLevel Level, ELogCategory Category, std::string_view Text)
{
	FConsoleMessage Message;

	Message.Time = GetCurrentTimeString();
	Message.Level = Level;
	Message.Category = Category;
	Message.Text = Text;

	{
		std::lock_guard<std::mutex> Lock(mPendingMutex);
		mPendingBuffers[mWriteBufferIndex].Add(Message);
	}
}

void ConsoleWindow::PushHistory(FConsoleMessage Message)
{
	if (mCount < mCapacity)
	{
		mMessages.Add(Message);
		++mCount;
		return;
	}

	mMessages[mFront] = Message;
	mFront = (mFront + 1) % mCapacity;
}

void ConsoleWindow::FlushPending()
{
	{
		std::lock_guard<std::mutex> Lock(mPendingMutex);
		std::swap(mWriteBufferIndex, mReadBufferIndex);
	}

	TArray<FConsoleMessage>& ReadBuffer = mPendingBuffers[mReadBufferIndex];
	for (const FConsoleMessage& Message : ReadBuffer)
	{
		PushHistory(Message);
	}

	ReadBuffer.Reset(0);
}

const FConsoleMessage& ConsoleWindow::GetMessage(size_t Index) const
{
	assert(Index < mCount);
	assert(mCount > 0);

	// mFront는 현재 가장 오래된 메시지 위치
	const uint32 CircularIndex = (mFront + Index) % mCount;

	return mMessages[CircularIndex];
}

void ConsoleWindow::Clear()
{
	{
		std::lock_guard<std::mutex> Lock(
			mPendingMutex
		);

		mPendingBuffers[0].Reset(0);
		mPendingBuffers[1].Reset(0);
	}

	mMessages.Reset(0);

	mFront = 0;
	mCount = 0;
}

void ConsoleWindow::ExecuteCommand(const char* Input, FEditorCommands& outCommands)
{
	std::istringstream Stream(Input);

	std::string Command;
	Stream >> Command;

	if (Command == "stat")
	{
		std::string argument;
		Stream >> argument;

		std::transform(argument.begin(), argument.end(), argument.begin(),
			[](unsigned char character) {return static_cast<char>(std::tolower(character));});

		if (argument == "fps")
		{
			outCommands.Emplace(FToggleStatCommand{ EStatGroup::FPS });
		}
		else if (argument == "memory")
		{
			outCommands.Emplace(FToggleStatCommand{ EStatGroup::Memory });
		}
		else if (argument == "picking")
		{
			outCommands.Emplace(FToggleStatCommand{ EStatGroup::Picking });
		}
		else if (argument == "all")
		{
			outCommands.Emplace(FToggleStatCommand{ EStatGroup::All });
		}
		else if (argument == "clear")
		{
			outCommands.Emplace(FClearPickingDataCommand{});
		}
		else if (argument == "none")
		{
			outCommands.Emplace(FDisableAllStatsCommand{});
		}
		else
		{
			AddLog(
				ELogLevel::Warning,
				ELogCategory::Core,
				"Usage: stat <fps|memory|picking|clear|none>");
		}

		return;
	}

	if (Command == "clear")
	{
		Clear();
	}
	else if (Command == "help")
	{
		AddLog(
			ELogLevel::Log,
			ELogCategory::Etc,
			"Commands: clear, echo, stat fps, stat memory, stat picking, stat clear, stat none");
	}
	else if (Command == "echo")
	{
		std::string Text;
		std::getline(Stream >> std::ws, Text);

		AddLog(
			ELogLevel::Log,
			ELogCategory::Core,
			Text);
	}
	else if (Command == "PODO")
	{
		AddLog(
			ELogLevel::Fatal,
			ELogCategory::Etc,
			"***** PODO ENGINE *****\n"
			"\n"
			"******* WEEK  2 *******\n"
			"*** LKH LSU KDH KSH ***\n"
			"******* WEEK  3 *******\n"
			"*** KSH KHW CHS LJY ***\n"
			"******* WEEK  4 *******\n"
			"*** HDY KHJ LGH KSH ***\n"
			"******* WEEK  5 *******\n"
			"*** YHJ KSH HSM JYH ***\n"
			"***********************\n");

	}
	else
	{
		AddLog(
			ELogLevel::Warning,
			ELogCategory::Core,
			"Unknown command");
	}
}
