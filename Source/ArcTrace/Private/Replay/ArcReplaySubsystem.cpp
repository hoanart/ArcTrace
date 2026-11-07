#include "Replay/ArcReplaySubsystem.h"

#include "ArcTrace.h"
#include "Replay/ArcRecordLoader.h"
#include "Replay/ArcReplaySampler.h"

namespace
{
	constexpr double MIN_PLAYBACK_RATE = 0.0;
	constexpr double MAX_PLAYBACK_RATE = 64.0;
}

UArcReplaySubsystem::UArcReplaySubsystem()
	: NextEventIndex(0)
	, CurrentTimeUs(0)
	, PendingMicroseconds(0.0)
	, PlaybackRate(1.0)
	, bHasRecord(false)
	, bIsPlaying(false)
{
}

void UArcReplaySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UArcReplaySubsystem::Deinitialize()
{
	if (PendingLoad.IsValid())
	{
		LoadTask.Wait();
		PendingLoad.Reset();
	}
	Super::Deinitialize();
}

void UArcReplaySubsystem::Tick(const float DeltaTime)
{
	ARC_SCOPED_PROFILE(ReplayTick);
	Super::Tick(DeltaTime);

	if (PendingLoad.IsValid() && LoadTask.IsCompleted())
	{
		FinishLoadRecord();
	}
	if (!bHasRecord || !bIsPlaying)
	{
		return;
	}

	// 프레임 시간을 정수 µs로 바꿀 때 남는 소수부를 다음 프레임으로 넘겨 누적 오차를 없앤다.
	PendingMicroseconds += static_cast<double>(DeltaTime) * PlaybackRate * ARC_MICROSECONDS_PER_SECOND;
	const int64 StepUs = static_cast<int64>(FMath::FloorToDouble(PendingMicroseconds));
	PendingMicroseconds -= static_cast<double>(StepUs);
	if (StepUs <= 0)
	{
		return;
	}

	const int64 TargetTimeUs = FMath::Min(CurrentTimeUs + StepUs, Record.EndTimeUs);
	AdvanceTo(TargetTimeUs);
	if (CurrentTimeUs >= Record.EndTimeUs)
	{
		Pause();
	}
}

TStatId UArcReplaySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UArcReplaySubsystem, STATGROUP_Tickables);
}

bool UArcReplaySubsystem::StartLoadRecord(const FString& RecordName)
{
	if (IsLoading())
	{
		UE_LOG(LogArcTrace, Warning, TEXT("[Replay] already loading"));
		return false;
	}

	Pause();
	PendingLoad = MakeShared<FArcLoadRecordResult, ESPMode::ThreadSafe>();
	PendingLoad->DirectoryPath = FArcRecordLoader::GetRecordDirectory(RecordName);
	LoadTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, FArcLoadRecordJob{ PendingLoad.ToSharedRef() });
	UE_LOG(LogArcTrace, Log, TEXT("[Replay] loading %s"), *PendingLoad->DirectoryPath);
	return true;
}

void UArcReplaySubsystem::Play()
{
	if (!bHasRecord)
	{
		return;
	}
	if (CurrentTimeUs >= Record.EndTimeUs)
	{
		SeekToTime(Record.StartTimeUs);
	}
	bIsPlaying = true;
}

void UArcReplaySubsystem::Pause()
{
	bIsPlaying = false;
	PendingMicroseconds = 0.0;
}

void UArcReplaySubsystem::SeekToTime(const int64 TimeUs)
{
	if (!bHasRecord)
	{
		return;
	}
	ResolveAt(FMath::Clamp(TimeUs, Record.StartTimeUs, Record.EndTimeUs));
}

void UArcReplaySubsystem::SetPlaybackRate(const double Rate)
{
	PlaybackRate = FMath::Clamp(Rate, MIN_PLAYBACK_RATE, MAX_PLAYBACK_RATE);
}

bool UArcReplaySubsystem::IsLoading() const
{
	return PendingLoad.IsValid();
}

bool UArcReplaySubsystem::HasRecord() const
{
	return bHasRecord;
}

bool UArcReplaySubsystem::IsPlaying() const
{
	return bIsPlaying;
}

double UArcReplaySubsystem::GetPlaybackRate() const
{
	return PlaybackRate;
}

int64 UArcReplaySubsystem::GetCurrentTimeUs() const
{
	return CurrentTimeUs;
}

int64 UArcReplaySubsystem::GetStartTimeUs() const
{
	return Record.StartTimeUs;
}

int64 UArcReplaySubsystem::GetEndTimeUs() const
{
	return Record.EndTimeUs;
}

int32 UArcReplaySubsystem::GetTrackCount() const
{
	return Record.Tracks.Num();
}

int32 UArcReplaySubsystem::GetFlyingTrackCount() const
{
	int32 FlyingCount = 0;
	for (int32 TrackIndex = 0; TrackIndex < CurrentSampleIndices.Num(); ++TrackIndex)
	{
		const FArcTrackSample* const Sample = FindCurrentSample(TrackIndex);
		if (Sample != nullptr && Sample->Phase != EArcProjectilePhase::Impact)
		{
			++FlyingCount;
		}
	}
	return FlyingCount;
}

const FArcReplayRecord& UArcReplaySubsystem::GetRecord() const
{
	return Record;
}

int32 UArcReplaySubsystem::GetCurrentSampleIndex(const int32 TrackIndex) const
{
	check(CurrentSampleIndices.IsValidIndex(TrackIndex));
	return CurrentSampleIndices[TrackIndex];
}

const FArcTrackSample* UArcReplaySubsystem::FindCurrentSample(const int32 TrackIndex) const
{
	check(CurrentSampleIndices.IsValidIndex(TrackIndex));

	const int32 SampleIndex = CurrentSampleIndices[TrackIndex];
	const FArcTrackSample* Sample = nullptr;
	if (SampleIndex != INDEX_NONE)
	{
		Sample = &Record.Tracks[TrackIndex].Samples[SampleIndex];
	}
	return Sample;
}

bool UArcReplaySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UArcReplaySubsystem::FinishLoadRecord()
{
	FArcLoadRecordResult& Result = *PendingLoad;
	const bool bSucceeded = Result.bSucceeded;
	if (bSucceeded)
	{
		Record = MoveTemp(Result.Record);
		bHasRecord = true;
		CurrentSampleIndices.Init(INDEX_NONE, Record.Tracks.Num());
		UE_LOG(LogArcTrace, Log, TEXT("[Replay] loaded '%s': %d tracks, %d events, %.1f~%.1fs (%.2fs)"),
			*Record.ScenarioName, Record.Tracks.Num(), Record.Events.Num(),
			Record.StartTimeUs / ARC_MICROSECONDS_PER_SECOND, Record.EndTimeUs / ARC_MICROSECONDS_PER_SECOND,
			Result.ElapsedSeconds);
	}
	else
	{
		UE_LOG(LogArcTrace, Error, TEXT("[Replay] load failed: %s"), *Result.ErrorMessage);
	}
	PendingLoad.Reset();
	LoadTask = UE::Tasks::FTask();

	if (bSucceeded)
	{
		ResolveAt(Record.StartTimeUs);
	}
	OnRecordLoaded.Broadcast(bSucceeded);
}

void UArcReplaySubsystem::AdvanceTo(const int64 TimeUs)
{
	check(TimeUs >= CurrentTimeUs);

	for (int32 TrackIndex = 0; TrackIndex < Record.Tracks.Num(); ++TrackIndex)
	{
		const TArray<FArcTrackSample>& Samples = Record.Tracks[TrackIndex].Samples;
		int32& SampleIndex = CurrentSampleIndices[TrackIndex];
		while (SampleIndex + 1 < Samples.Num() && Samples[SampleIndex + 1].TimeUs <= TimeUs)
		{
			++SampleIndex;
		}
	}
	CurrentTimeUs = TimeUs;

	while (NextEventIndex < Record.Events.Num() && Record.Events[NextEventIndex].TimeUs <= TimeUs)
	{
		OnReplayEvent.Broadcast(Record.Events[NextEventIndex]);
		++NextEventIndex;
	}
	OnTimeChanged.Broadcast(CurrentTimeUs);
}

void UArcReplaySubsystem::ResolveAt(const int64 TimeUs)
{
	ARC_SCOPED_PROFILE(ReplaySeek);
	for (int32 TrackIndex = 0; TrackIndex < Record.Tracks.Num(); ++TrackIndex)
	{
		CurrentSampleIndices[TrackIndex] = FArcReplaySampler::FindSampleIndexAt(Record.Tracks[TrackIndex].Samples, TimeUs);
	}
	CurrentTimeUs = TimeUs;
	NextEventIndex = FArcReplaySampler::FindFirstEventIndexAfter(Record.Events, TimeUs);
	PendingMicroseconds = 0.0;

	// 탐색은 이벤트를 하나씩 발생시키지 않는다. 구독자는 OnSeeked에서 현재 상태 전체를 다시 구성한다.
	OnSeeked.Broadcast(CurrentTimeUs);
	OnTimeChanged.Broadcast(CurrentTimeUs);
}
