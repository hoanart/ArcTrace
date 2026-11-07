#include "Replay/ArcReplaySampler.h"

#include "Algo/BinarySearch.h"

int32 FArcReplaySampler::FindSampleIndexAt(const TArray<FArcTrackSample>& Samples, const int64 TimeUs)
{
	const int32 UpperIndex = Algo::UpperBoundBy(Samples, TimeUs, &FArcTrackSample::TimeUs);
	return UpperIndex - 1;
}

int32 FArcReplaySampler::FindFirstEventIndexAfter(const TArray<FArcReplayEvent>& Events, const int64 TimeUs)
{
	return Algo::UpperBoundBy(Events, TimeUs, &FArcReplayEvent::TimeUs);
}
