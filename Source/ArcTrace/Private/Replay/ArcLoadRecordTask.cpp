#include "Replay/ArcLoadRecordTask.h"

#include "HAL/PlatformTime.h"

#include "Replay/ArcRecordLoader.h"

void FArcLoadRecordJob::operator()() const
{
	const double StartSeconds = FPlatformTime::Seconds();
	Result->bSucceeded = FArcRecordLoader::LoadRecord(Result->DirectoryPath, &Result->Record, &Result->ErrorMessage);
	Result->ElapsedSeconds = FPlatformTime::Seconds() - StartSeconds;
}
