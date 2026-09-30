#pragma once
#include <windows.h>

class FFrameTimer
{
public:
	FFrameTimer(int TargetFPS) : targetFrameTime(1000.0 / TargetFPS), elapsedTime(1000.0 / TargetFPS)
	{
		QueryPerformanceFrequency(&Frequency);
	}

	void StartFrame()
	{
		deltaTime = (float)(elapsedTime * 0.001);
		if (deltaTime > 0.1f) deltaTime = 0.1f;

		QueryPerformanceCounter(&StartTime);
	}
	void EndFrame()
	{	
		QueryPerformanceCounter(&EndTime);
		elapsedTime = (EndTime.QuadPart - StartTime.QuadPart) * 1000.0 / Frequency.QuadPart;

		while (elapsedTime < targetFrameTime)
		{
			YieldProcessor();
			QueryPerformanceCounter(&EndTime);
			elapsedTime = (EndTime.QuadPart - StartTime.QuadPart) * 1000.0 / Frequency.QuadPart;
		}
	}

	float GetDeltaTime() const { return deltaTime; }
	float GetFPS() const { return elapsedTime > 0.0 ? (float)(1000.0 / elapsedTime) : 0.f; }
	float GetFrameTimeMilliseconds() const { return static_cast<float>(elapsedTime); }

private:
	double targetFrameTime;
	double elapsedTime;	
	float deltaTime = 0.f;
	LARGE_INTEGER Frequency, StartTime, EndTime;
};
