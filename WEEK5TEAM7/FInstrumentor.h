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
#include <unordered_map>

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

struct FRealtimeStats
{
	float LastDurationMs = 0.0f;
	uint32 CallCount = 0;
	float TotalDurationMs = 0.0f;
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

    // 설정 적용 완료 시점을 기존 프로파일과 같은 시계로 기록합니다.
    void WriteLODMarker(int32 ForcedLOD, uint32 MeshCount, uint32 Failures, bool Regenerated)
    {
        if (!CurrentSession) return;
        const char* Name = ForcedLOD == 0 ? "LOD0 Applied" :
            ForcedLOD == 1 ? "LOD1 Applied" : ForcedLOD == 2 ? "LOD2 Applied" : "LOD Automatic Applied";
        const auto Timestamp = FloatingPointMicroseconds{ std::chrono::steady_clock::now().time_since_epoch() };
        OutputStream << std::fixed << std::setprecision(3)
            << ",{\"cat\":\"LOD\",\"name\":\"" << Name
            << "\",\"ph\":\"i\",\"s\":\"t\",\"pid\":0,\"tid\":" << std::this_thread::get_id()
            << ",\"ts\":" << Timestamp.count()
            << ",\"args\":{\"forced_lod\":" << ForcedLOD
            << ",\"mesh_assets\":" << MeshCount << ",\"generation_failures\":" << Failures
            << ",\"regenerated\":" << (Regenerated ? "true" : "false") << "}}";
        OutputStream.flush();
    }
    // 렌더 업로드 경로 전환 시점을 trace에 남겨 같은 장면의 전후 구간을 구분합니다.
    void WriteRenderBatchMarker(bool bEnabled)
    {
        WriteRenderOptionMarker("Render CB Batch", bEnabled);
    }

    void WriteRenderOptionMarker(const char* Name, bool bEnabled)
    {
        if (!CurrentSession) return;
        const auto Timestamp = FloatingPointMicroseconds{ std::chrono::steady_clock::now().time_since_epoch() };
        OutputStream << std::fixed << std::setprecision(3)
            << ",{\"cat\":\"Render\",\"name\":\"" << Name << " " << (bEnabled ? "ON" : "OFF")
            << "\",\"ph\":\"i\",\"s\":\"t\",\"pid\":0,\"tid\":" << std::this_thread::get_id()
            << ",\"ts\":" << Timestamp.count() << "}";
        OutputStream.flush();
    }
	static FInstrumentor& Get()
	{
		static FInstrumentor instance;
		return instance;
	}

	void UpdateRealtimeStats(const std::string& name, float durationMs)
	{
		auto& stat = RealtimeStatsMap[name];
		stat.LastDurationMs = durationMs;
		stat.CallCount++;
		stat.TotalDurationMs += durationMs;
	}

	FRealtimeStats GetRealtimeStats(const std::string& name)
	{
		return RealtimeStatsMap[name];
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
	std::unordered_map<std::string, FRealtimeStats> RealtimeStatsMap;
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

		float elapsedMs = elapsedTime.count() * 0.001f;
		FInstrumentor::Get().UpdateRealtimeStats(Name, elapsedMs);

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
