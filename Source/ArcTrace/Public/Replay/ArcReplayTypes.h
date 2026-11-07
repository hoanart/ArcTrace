#pragma once

#include "CoreMinimal.h"

// 기록 포맷(Tools/ArcSim/FORMAT.md) v1의 메모리 표현. 데이터 전용 구조체라 함수를 두지 않는다.

inline constexpr double ARC_MICROSECONDS_PER_SECOND = 1000000.0;

// 50만 개 이상 샘플에 들어가므로 1바이트로 고정한다.
enum class EArcProjectilePhase : uint8
{
	Boost,
	Ascent,
	Descent,
	Impact
};

enum class EArcReplayEventType : uint8
{
	Launch,
	Impact
};

struct FArcTrackSample
{
	int64 TimeUs = 0;
	FVector PositionEcef = FVector::ZeroVector;
	FVector VelocityEcef = FVector::ZeroVector;
	double Latitude = 0.0;
	double Longitude = 0.0;
	double Altitude = 0.0;
	float Heading = 0.0f;
	float Pitch = 0.0f;
	float Speed = 0.0f;
	EArcProjectilePhase Phase = EArcProjectilePhase::Boost;
};

struct FArcReplayTrack
{
	int32 TrackId = INDEX_NONE;
	int32 KindIndex = INDEX_NONE;
	TArray<FArcTrackSample> Samples;
};

struct FArcReplayEvent
{
	int64 TimeUs = 0;
	int32 TrackId = INDEX_NONE;
	EArcReplayEventType Type = EArcReplayEventType::Launch;
	double Latitude = 0.0;
	double Longitude = 0.0;
	double Altitude = 0.0;
};

struct FArcReplayRecord
{
	FString ScenarioName;
	int32 FormatVersion = 0;
	int64 RecordIntervalUs = 0;
	int64 StartTimeUs = 0;
	int64 EndTimeUs = 0;
	TArray<FString> KindNames;
	TArray<FArcReplayTrack> Tracks;
	TArray<FArcReplayEvent> Events;
};
