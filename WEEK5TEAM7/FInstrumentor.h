#pragma once

#include "Core.h"
#include "FLogManager.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <string>
#include <thread>
#include <sstream>

using FloatingPointMicroseconds = std::chrono::duration<double, std::micro>;

struct FProfileResult
{
	FString Name;

	FloatingPointMicroseconds Start;
	std::chrono::microseconds ElapsedTime;
	std::thread::id ThreadID;
};

struct FInstrumentationSession
{
	FString Name;
};

class FInstrumentor
{
public:
	FInstrumentor(const FInstrumentor&) = delete;
	FInstrumentor(FInstrumentor&&) = delete;

	void BeginSession(const FString& Name, const FString& FilePath = "results.json")
	{
		if (CurrentSession)
		{
			// If there is already a current session, then close it before beginning new one.
			// Subsequent profiling output meant for the original session will end up in the
			// newly opened session instead.  That's better than having badly formatted
			// profiling output.
			UE_DEBUG_LOG_ERROR("Instrumentor::BeginSession('{%s}') when session '{%s}' already open.", Name.c_str(), CurrentSession->Name.c_str());
			InternalEndSession();
		}

		OutputStream.open(FilePath);

		if (OutputStream.is_open())
		{
			CurrentSession = new FInstrumentationSession({ Name });
			WriteHeader();
		}
		else
		{
			UE_DEBUG_LOG_ERROR("Instrumentor could not open results file '{%s}'.", FilePath.c_str());
		}
	}

	void EndSession()
	{
		InternalEndSession();
	}

	void WriteProfile(const FProfileResult& Result)
	{
		std::stringstream json;

		json << std::setprecision(3) << std::fixed;
		json << ",{";
		json << "\"cat\":\"function\",";
		json << "\"dur\":" << (Result.ElapsedTime.count()) << ',';
		json << "\"name\":\"" << Result.Name.c_str() << "\",";
		json << "\"ph\":\"X\",";
		json << "\"pid\":0,";
		json << "\"tid\":" << Result.ThreadID << ",";
		json << "\"ts\":" << Result.Start.count();
		json << "}";

		if (CurrentSession)
		{
			OutputStream << json.str();
			OutputStream.flush();
		}
	}

	static FInstrumentor& Get()
	{
		static FInstrumentor instance;
		return instance;
	}

private:
	FInstrumentor()
		: CurrentSession(nullptr)
	{
	}

	~FInstrumentor()
	{
		EndSession();
	}

	void WriteHeader()
	{
		OutputStream << "{\"otherData\": {},\"traceEvents\":[{}";
		OutputStream.flush();
	}

	void WriteFooter()
	{
		OutputStream << "]}";
		OutputStream.flush();
	}

	void InternalEndSession()
	{
		if (CurrentSession)
		{
			WriteFooter();
			OutputStream.close();
			delete CurrentSession;
			CurrentSession = nullptr;
		}
	}

private:
	FInstrumentationSession* CurrentSession;
	std::ofstream OutputStream;
};

class FInstrumentationTimer
{
public:
	FInstrumentationTimer(const char* name)
		: Name(name)
		, Stopped(false)
	{
		StartTimepoint = std::chrono::steady_clock::now();
	}

	~FInstrumentationTimer()
	{
		if (!Stopped)
		{
			Stop();
		}
	}

	void Stop()
	{
		auto endTimepoint = std::chrono::steady_clock::now();
		auto highResStart = FloatingPointMicroseconds{ StartTimepoint.time_since_epoch() };
		auto elapsedTime = std::chrono::time_point_cast<std::chrono::microseconds>(endTimepoint).time_since_epoch() - std::chrono::time_point_cast<std::chrono::microseconds>(StartTimepoint).time_since_epoch();

		FInstrumentor::Get().WriteProfile({ Name, highResStart, elapsedTime, std::this_thread::get_id() });

		Stopped = true;
	}

private:
	const char* Name;
	std::chrono::time_point<std::chrono::steady_clock> StartTimepoint;
	bool Stopped;
};

namespace InstrumentorUtils {
	template <size_t N>
	struct ChangeResult
	{
		char Data[N];
	};

	template <size_t N, size_t K>
	constexpr auto CleanupOutputString(const char(&expr)[N], const char(&remove)[K])
	{
		ChangeResult<N> result = {};

		size_t srcIndex = 0;
		size_t dstIndex = 0;
		while (srcIndex < N)
		{
			size_t matchIndex = 0;
			while (matchIndex < K - 1 && srcIndex + matchIndex < N - 1 && expr[srcIndex + matchIndex] == remove[matchIndex])
			{
				matchIndex++;
			}

			if (matchIndex == K - 1)
			{
				srcIndex += matchIndex;
			}

			result.Data[dstIndex] = (expr[srcIndex] == '"') ? '\'' : expr[srcIndex];
			dstIndex++;
			srcIndex++;
		}

		return result;
	}
}

#define ENABLE_PROFILE 1

#if ENABLE_PROFILE
#define PROFILE_BEGIN_SESSION(name, filepath) FInstrumentor::Get().BeginSession(name, filepath)
#define PROFILE_END_SESSION() FInstrumentor::Get().EndSession()
#define PROFILE_SCOPE_LINE2(name, line) constexpr auto fixedName##line = InstrumentorUtils::CleanupOutputString(name, "__cdecl ");\
											   FInstrumentationTimer timer##line(fixedName##line.Data)
#define PROFILE_SCOPE_LINE(name, line) PROFILE_SCOPE_LINE2(name, line)
#define PROFILE_SCOPE(name) PROFILE_SCOPE_LINE(name, __LINE__)
#define PROFILE_FUNCTION() PROFILE_SCOPE(FUNC_SIG)
#else
#define PROFILE_BEGIN_SESSION(name, filepath)
#define PROFILE_END_SESSION()
#define PROFILE_SCOPE(name)
#define PROFILE_FUNCTION()
#endif