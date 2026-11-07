#pragma once

#include "CoreMinimal.h"

#include "Replay/ArcReplayTypes.h"

// 워커 스레드 로드 결과. 작업이 끝난 뒤에만 게임 스레드가 읽는다.
struct FArcLoadRecordResult
{
	FString DirectoryPath;
	FArcReplayRecord Record;
	FString ErrorMessage;
	double ElapsedSeconds = 0.0;
	bool bSucceeded = false;
};

// UE::Tasks에 넘기는 이름 있는 작업 함수 객체. UObject를 건드리지 않는다.
struct FArcLoadRecordJob
{
	TSharedRef<FArcLoadRecordResult, ESPMode::ThreadSafe> Result;

	void operator()() const;
};
