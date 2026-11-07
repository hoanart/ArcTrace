#pragma once

#include "CoreMinimal.h"

#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"

DECLARE_LOG_CATEGORY_EXTERN(LogArcTrace, Log, All);
CSV_DECLARE_CATEGORY_EXTERN(ArcTrace);

// csvprofile(ArcTrace/<Name>)와 Unreal Insights(ArcTrace_<Name>)에 같은 구간을 기록한다.
#define ARC_SCOPED_PROFILE(Name) \
	CSV_SCOPED_TIMING_STAT(ArcTrace, Name); \
	TRACE_CPUPROFILER_EVENT_SCOPE(ArcTrace_##Name)
