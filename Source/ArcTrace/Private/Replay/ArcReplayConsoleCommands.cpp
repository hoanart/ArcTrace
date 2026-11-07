#include "CoreMinimal.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

#include "ArcTrace.h"
#include "Replay/ArcReplaySubsystem.h"

// 시각화 전 단계에서 재연 코어를 콘솔로 확인하기 위한 명령.
namespace
{
	UArcReplaySubsystem* FindReplaySubsystem(UWorld* const World)
	{
		UArcReplaySubsystem* Subsystem = nullptr;
		if (World != nullptr)
		{
			Subsystem = World->GetSubsystem<UArcReplaySubsystem>();
		}
		if (Subsystem == nullptr)
		{
			UE_LOG(LogArcTrace, Warning, TEXT("[Replay] no replay subsystem in this world (run in PIE or game)"));
		}
		return Subsystem;
	}

	void ExecuteLoad(const TArray<FString>& Arguments, UWorld* World)
	{
		UArcReplaySubsystem* const Subsystem = FindReplaySubsystem(World);
		if (Subsystem == nullptr)
		{
			return;
		}
		const FString RecordName = Arguments.IsEmpty() ? FString(TEXT("sample_salvo")) : Arguments[0];
		Subsystem->StartLoadRecord(RecordName);
	}

	void ExecutePlay(const TArray<FString>& Arguments, UWorld* World)
	{
		UArcReplaySubsystem* const Subsystem = FindReplaySubsystem(World);
		if (Subsystem != nullptr)
		{
			Subsystem->Play();
		}
	}

	void ExecutePause(const TArray<FString>& Arguments, UWorld* World)
	{
		UArcReplaySubsystem* const Subsystem = FindReplaySubsystem(World);
		if (Subsystem != nullptr)
		{
			Subsystem->Pause();
		}
	}

	void ExecuteSeek(const TArray<FString>& Arguments, UWorld* World)
	{
		UArcReplaySubsystem* const Subsystem = FindReplaySubsystem(World);
		if (Subsystem == nullptr || Arguments.IsEmpty())
		{
			return;
		}
		const double Seconds = FCString::Atod(*Arguments[0]);
		Subsystem->SeekToTime(static_cast<int64>(FMath::RoundToDouble(Seconds * ARC_MICROSECONDS_PER_SECOND)));
	}

	void ExecuteRate(const TArray<FString>& Arguments, UWorld* World)
	{
		UArcReplaySubsystem* const Subsystem = FindReplaySubsystem(World);
		if (Subsystem != nullptr && !Arguments.IsEmpty())
		{
			Subsystem->SetPlaybackRate(FCString::Atod(*Arguments[0]));
		}
	}

	void ExecuteStatus(const TArray<FString>& Arguments, UWorld* World)
	{
		UArcReplaySubsystem* const Subsystem = FindReplaySubsystem(World);
		if (Subsystem == nullptr)
		{
			return;
		}
		if (!Subsystem->HasRecord())
		{
			UE_LOG(LogArcTrace, Display, TEXT("[Replay] no record (loading: %s)"), Subsystem->IsLoading() ? TEXT("yes") : TEXT("no"));
			return;
		}
		UE_LOG(LogArcTrace, Display, TEXT("[Replay] %s  t=%.3fs / %.3fs  rate x%.2f  flying %d / %d tracks"),
			Subsystem->IsPlaying() ? TEXT("playing") : TEXT("paused"),
			Subsystem->GetCurrentTimeUs() / ARC_MICROSECONDS_PER_SECOND,
			Subsystem->GetEndTimeUs() / ARC_MICROSECONDS_PER_SECOND,
			Subsystem->GetPlaybackRate(),
			Subsystem->GetFlyingTrackCount(),
			Subsystem->GetTrackCount());
	}

	FAutoConsoleCommandWithWorldAndArgs LoadCommand(TEXT("ArcTrace.Load"),
		TEXT("ArcTrace.Load <RecordName> : Data/Records/<RecordName> 비동기 로드 (기본 sample_salvo)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ExecuteLoad));
	FAutoConsoleCommandWithWorldAndArgs PlayCommand(TEXT("ArcTrace.Play"), TEXT("재생"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ExecutePlay));
	FAutoConsoleCommandWithWorldAndArgs PauseCommand(TEXT("ArcTrace.Pause"), TEXT("일시정지"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ExecutePause));
	FAutoConsoleCommandWithWorldAndArgs SeekCommand(TEXT("ArcTrace.Seek"), TEXT("ArcTrace.Seek <초> : 해당 시각으로 이동"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ExecuteSeek));
	FAutoConsoleCommandWithWorldAndArgs RateCommand(TEXT("ArcTrace.Rate"), TEXT("ArcTrace.Rate <배속> : 0~64"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ExecuteRate));
	FAutoConsoleCommandWithWorldAndArgs StatusCommand(TEXT("ArcTrace.Status"), TEXT("재연 상태 출력"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ExecuteStatus));
}
