#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tasks/Task.h"

#include "Replay/ArcLoadRecordTask.h"
#include "Replay/ArcReplayTypes.h"

#include "ArcReplaySubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnArcReplayRecordLoaded, bool /*bSucceeded*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnArcReplayTimeChanged, int64 /*TimeUs*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnArcReplaySeeked, int64 /*TimeUs*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnArcReplayEvent, const FArcReplayEvent& /*Event*/);

// 기록 재연의 시계와 트랙 상태.
// 시각 t의 트랙 상태 = TimeUs <= t 인 마지막 샘플 (보간 없음). 시간은 정수 마이크로초.
// 재생 중 전진은 이전 인덱스부터 증분 갱신하고, 탐색은 트랙별 이진 탐색으로 다시 구한다.
UCLASS()
class ARCTRACE_API UArcReplaySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UArcReplaySubsystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	// <프로젝트>/Data/Records/<RecordName> 을 비동기로 읽는다. 읽는 중이면 false.
	bool StartLoadRecord(const FString& RecordName);

	void Play();
	void Pause();
	void SeekToTime(const int64 TimeUs);
	void SetPlaybackRate(const double Rate);

	bool IsLoading() const;
	bool HasRecord() const;
	bool IsPlaying() const;
	double GetPlaybackRate() const;
	int64 GetCurrentTimeUs() const;
	int64 GetStartTimeUs() const;
	int64 GetEndTimeUs() const;
	int32 GetTrackCount() const;
	int32 GetFlyingTrackCount() const;
	const FArcReplayRecord& GetRecord() const;

	// 현재 시각의 샘플 인덱스. 아직 발사 전이면 INDEX_NONE.
	int32 GetCurrentSampleIndex(const int32 TrackIndex) const;

	// 현재 시각의 샘플. 아직 발사 전이면 nullptr.
	const FArcTrackSample* FindCurrentSample(const int32 TrackIndex) const;

public:
	FOnArcReplayRecordLoaded OnRecordLoaded;
	FOnArcReplayTimeChanged OnTimeChanged;
	FOnArcReplaySeeked OnSeeked;
	FOnArcReplayEvent OnReplayEvent;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void FinishLoadRecord();
	void AdvanceTo(const int64 TimeUs);
	void ResolveAt(const int64 TimeUs);

private:
	FArcReplayRecord Record;
	TArray<int32> CurrentSampleIndices;
	TSharedPtr<FArcLoadRecordResult, ESPMode::ThreadSafe> PendingLoad;
	UE::Tasks::FTask LoadTask;
	int32 NextEventIndex;
	int64 CurrentTimeUs;
	double PendingMicroseconds;
	double PlaybackRate;
	bool bHasRecord;
	bool bIsPlaying;
};
