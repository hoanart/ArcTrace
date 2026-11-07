#pragma once

#include "CoreMinimal.h"

#include "Replay/ArcReplayTypes.h"

// 시각 -> 기록 인덱스 조회. 보간하지 않는다: 시각 t의 상태는 "TimeUs <= t 인 마지막 샘플".
class ARCTRACE_API FArcReplaySampler
{
public:
	// 시각 TimeUs에 유효한 샘플 인덱스. 첫 샘플 이전이면 INDEX_NONE.
	static int32 FindSampleIndexAt(const TArray<FArcTrackSample>& Samples, const int64 TimeUs);

	// TimeUs보다 늦은 첫 이벤트의 인덱스 (모든 이벤트가 TimeUs 이하이면 Events.Num()).
	static int32 FindFirstEventIndexAfter(const TArray<FArcReplayEvent>& Events, const int64 TimeUs);
};
