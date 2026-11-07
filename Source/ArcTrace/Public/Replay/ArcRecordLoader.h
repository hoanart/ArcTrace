#pragma once

#include "CoreMinimal.h"

#include "Replay/ArcReplayTypes.h"

// 기록 폴더(meta.json, samples.bin 또는 samples.csv, events.csv)를 읽어 FArcReplayRecord로 만든다.
// samples.bin이 있으면 그것을 쓴다 (텍스트 파싱 없음).
// UObject를 건드리지 않으므로 워커 스레드에서 호출해도 된다.
class ARCTRACE_API FArcRecordLoader
{
public:
	static bool LoadRecord(const FString& DirectoryPath, FArcReplayRecord* const OutRecord, FString* const OutErrorMessage);

	// <프로젝트>/Data/Records/<RecordName>
	static FString GetRecordDirectory(const FString& RecordName);

private:
	static bool LoadMeta(const FString& FilePath, FArcReplayRecord* const OutRecord, int32* const OutTrackCount,
		int64* const OutSampleCount, FString* const OutErrorMessage);
	static bool LoadSamples(const FString& FilePath, const int32 TrackCount, const int64 SampleCount,
		FArcReplayRecord* const OutRecord, FString* const OutErrorMessage);
	static bool LoadBinarySamples(const FString& FilePath, const int32 TrackCount, const int64 SampleCount,
		FArcReplayRecord* const OutRecord, FString* const OutErrorMessage);
	static bool LoadEvents(const FString& FilePath, FArcReplayRecord* const OutRecord, FString* const OutErrorMessage);
	static bool ValidateTracks(const FArcReplayRecord& Record, FString* const OutErrorMessage);
};
