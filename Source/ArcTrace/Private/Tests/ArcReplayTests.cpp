#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#include "Replay/ArcRecordLoader.h"
#include "Replay/ArcReplaySampler.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	TArray<FArcTrackSample> MakeSamples(const TArray<int64>& Times)
	{
		TArray<FArcTrackSample> Samples;
		Samples.Reserve(Times.Num());
		for (const int64 TimeUs : Times)
		{
			FArcTrackSample& Sample = Samples.AddDefaulted_GetRef();
			Sample.TimeUs = TimeUs;
		}
		return Samples;
	}

	int32 FindSampleIndexLinear(const TArray<FArcTrackSample>& Samples, const int64 TimeUs)
	{
		int32 FoundIndex = INDEX_NONE;
		for (int32 SampleIndex = 0; SampleIndex < Samples.Num(); ++SampleIndex)
		{
			if (Samples[SampleIndex].TimeUs <= TimeUs)
			{
				FoundIndex = SampleIndex;
			}
		}
		return FoundIndex;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArcReplaySamplerBoundaryTest, "ArcTrace.Replay.SamplerBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FArcReplaySamplerBoundaryTest::RunTest(const FString& Parameters)
{
	const TArray<FArcTrackSample> Samples = MakeSamples({ 1000, 100000, 200000, 250037 });

	TestEqual(TEXT("before first sample"), FArcReplaySampler::FindSampleIndexAt(Samples, 999), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("exactly first sample"), FArcReplaySampler::FindSampleIndexAt(Samples, 1000), 0);
	TestEqual(TEXT("between samples keeps previous"), FArcReplaySampler::FindSampleIndexAt(Samples, 99999), 0);
	TestEqual(TEXT("exactly on a sample selects it"), FArcReplaySampler::FindSampleIndexAt(Samples, 100000), 1);
	TestEqual(TEXT("off-grid impact sample"), FArcReplaySampler::FindSampleIndexAt(Samples, 250037), 3);
	TestEqual(TEXT("after last sample"), FArcReplaySampler::FindSampleIndexAt(Samples, 9999999), 3);
	TestEqual(TEXT("empty track"), FArcReplaySampler::FindSampleIndexAt(TArray<FArcTrackSample>(), 0), static_cast<int32>(INDEX_NONE));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArcRecordLoadSampleTest, "ArcTrace.Replay.LoadSampleRecord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FArcRecordLoadSampleTest::RunTest(const FString& Parameters)
{
	const FString Directory = FArcRecordLoader::GetRecordDirectory(TEXT("sample_salvo"));
	if (!FPaths::FileExists(FPaths::Combine(Directory, TEXT("meta.json"))))
	{
		AddWarning(TEXT("Data/Records/sample_salvo not found - run Tools/ArcSim/run_sim.py first"));
		return true;
	}

	FArcReplayRecord Record;
	FString ErrorMessage;
	const bool bLoaded = FArcRecordLoader::LoadRecord(Directory, &Record, &ErrorMessage);
	TestTrue(FString::Printf(TEXT("load (%s)"), *ErrorMessage), bLoaded);
	if (!bLoaded)
	{
		return false;
	}
	TestTrue(TEXT("has tracks"), Record.Tracks.Num() > 0);
	TestEqual(TEXT("launch + impact event per track"), Record.Events.Num(), Record.Tracks.Num() * 2);

	// 임의 시각에서 이진 탐색 결과가 선형 탐색(정의 그대로)과 같아야 한다.
	FRandomStream Random(20261029);
	for (int32 QueryIndex = 0; QueryIndex < 2000; ++QueryIndex)
	{
		const int64 TimeUs = Record.StartTimeUs - 1000000
			+ static_cast<int64>(Random.FRand() * static_cast<float>(Record.EndTimeUs - Record.StartTimeUs + 2000000));
		const FArcReplayTrack& Track = Record.Tracks[Random.RandRange(0, Record.Tracks.Num() - 1)];
		const int32 Expected = FindSampleIndexLinear(Track.Samples, TimeUs);
		const int32 Actual = FArcReplaySampler::FindSampleIndexAt(Track.Samples, TimeUs);
		if (Expected != Actual)
		{
			AddError(FString::Printf(TEXT("track %d t=%lld: expected %d, got %d"), Track.TrackId, TimeUs, Expected, Actual));
			return false;
		}
	}
	return true;
}

#endif
