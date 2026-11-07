#include "Replay/ArcRecordLoader.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "ArcCsvCursor.h"

namespace
{
	constexpr int32 SUPPORTED_FORMAT_VERSION = 1;
	constexpr int32 MAX_PHASE_VALUE = static_cast<int32>(EArcProjectilePhase::Impact);
	constexpr int32 MAX_EVENT_TYPE_VALUE = static_cast<int32>(EArcReplayEventType::Impact);
	// ecef xyz, vel xyz, lat, lon, alt, heading, pitch, speed
	constexpr int32 SAMPLE_VALUE_COUNT = 12;

	const ANSICHAR* const SAMPLE_HEADER =
		"time_us,track_id,kind,phase,ecef_x,ecef_y,ecef_z,vel_x,vel_y,vel_z,lat,lon,alt,heading,pitch,speed";
	const ANSICHAR* const EVENT_HEADER = "time_us,track_id,type,lat,lon,alt";

	// samples.bin (Tools/ArcSim/FORMAT.md): 리틀 엔디언 고정 길이 레코드
	static_assert(PLATFORM_LITTLE_ENDIAN, "samples.bin is little-endian");
	const ANSICHAR BINARY_MAGIC[4] = { 'A', 'R', 'C', 'S' };
	constexpr uint32 BINARY_VERSION = 1;
	constexpr int64 BINARY_HEADER_SIZE = 32;
	constexpr int64 BINARY_RECORD_SIZE = 84;

	template<typename ValueType>
	ValueType ReadBinaryValue(const uint8* const Record, const int32 Offset)
	{
		ValueType Value;
		FMemory::Memcpy(&Value, Record + Offset, sizeof(ValueType));
		return Value;
	}

	bool LoadFileWithTerminator(const FString& FilePath, TArray<uint8>* const OutBytes)
	{
		check(OutBytes);

		if (!FFileHelper::LoadFileToArray(*OutBytes, *FilePath))
		{
			return false;
		}
		OutBytes->Add(0);
		return true;
	}

	FString MakeLineError(const FString& FilePath, const int32 LineNumber, const TCHAR* const Reason)
	{
		return FString::Printf(TEXT("%s:%d %s"), *FPaths::GetCleanFilename(FilePath), LineNumber, Reason);
	}
}

bool FArcRecordLoader::LoadRecord(const FString& DirectoryPath, FArcReplayRecord* const OutRecord, FString* const OutErrorMessage)
{
	check(OutRecord);
	check(OutErrorMessage);

	*OutRecord = FArcReplayRecord();
	int32 TrackCount = 0;
	int64 SampleCount = 0;

	bool bSucceeded = LoadMeta(FPaths::Combine(DirectoryPath, TEXT("meta.json")), OutRecord, &TrackCount, &SampleCount, OutErrorMessage);
	if (bSucceeded)
	{
		const FString BinaryPath = FPaths::Combine(DirectoryPath, TEXT("samples.bin"));
		bSucceeded = FPaths::FileExists(BinaryPath)
			? LoadBinarySamples(BinaryPath, TrackCount, SampleCount, OutRecord, OutErrorMessage)
			: LoadSamples(FPaths::Combine(DirectoryPath, TEXT("samples.csv")), TrackCount, SampleCount, OutRecord, OutErrorMessage);
	}
	if (bSucceeded)
	{
		bSucceeded = LoadEvents(FPaths::Combine(DirectoryPath, TEXT("events.csv")), OutRecord, OutErrorMessage);
	}
	if (bSucceeded)
	{
		bSucceeded = ValidateTracks(*OutRecord, OutErrorMessage);
	}
	if (!bSucceeded)
	{
		*OutRecord = FArcReplayRecord();
	}
	return bSucceeded;
}

FString FArcRecordLoader::GetRecordDirectory(const FString& RecordName)
{
	return FPaths::Combine(FPaths::ProjectDir(), TEXT("Data"), TEXT("Records"), RecordName);
}

bool FArcRecordLoader::LoadMeta(const FString& FilePath, FArcReplayRecord* const OutRecord, int32* const OutTrackCount,
	int64* const OutSampleCount, FString* const OutErrorMessage)
{
	check(OutRecord);
	check(OutTrackCount);
	check(OutSampleCount);
	check(OutErrorMessage);

	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *FilePath))
	{
		*OutErrorMessage = FString::Printf(TEXT("cannot read %s"), *FilePath);
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		*OutErrorMessage = TEXT("meta.json is not valid JSON");
		return false;
	}

	int32 FormatVersion = 0;
	int32 TrackCount = 0;
	int64 SampleCount = 0;
	int64 RecordIntervalUs = 0;
	int64 StartTimeUs = 0;
	int64 EndTimeUs = 0;
	FString ScenarioName;
	const TArray<TSharedPtr<FJsonValue>>* KindValues = nullptr;

	const bool bHasAllFields = Root->TryGetNumberField(TEXT("format_version"), FormatVersion)
		&& Root->TryGetNumberField(TEXT("track_count"), TrackCount)
		&& Root->TryGetNumberField(TEXT("sample_count"), SampleCount)
		&& Root->TryGetNumberField(TEXT("record_interval_us"), RecordIntervalUs)
		&& Root->TryGetNumberField(TEXT("start_time_us"), StartTimeUs)
		&& Root->TryGetNumberField(TEXT("end_time_us"), EndTimeUs)
		&& Root->TryGetStringField(TEXT("scenario"), ScenarioName)
		&& Root->TryGetArrayField(TEXT("kinds"), KindValues);
	if (!bHasAllFields)
	{
		*OutErrorMessage = TEXT("meta.json is missing required fields");
		return false;
	}
	if (FormatVersion != SUPPORTED_FORMAT_VERSION)
	{
		*OutErrorMessage = FString::Printf(TEXT("unsupported format_version %d"), FormatVersion);
		return false;
	}
	if (TrackCount <= 0 || SampleCount <= 0 || EndTimeUs < StartTimeUs)
	{
		*OutErrorMessage = TEXT("meta.json has invalid counts or time range");
		return false;
	}

	OutRecord->FormatVersion = FormatVersion;
	OutRecord->ScenarioName = ScenarioName;
	OutRecord->RecordIntervalUs = RecordIntervalUs;
	OutRecord->StartTimeUs = StartTimeUs;
	OutRecord->EndTimeUs = EndTimeUs;
	OutRecord->KindNames.Reserve(KindValues->Num());
	for (const TSharedPtr<FJsonValue>& KindValue : *KindValues)
	{
		OutRecord->KindNames.Add(KindValue->AsString());
	}
	*OutTrackCount = TrackCount;
	*OutSampleCount = SampleCount;
	return true;
}

bool FArcRecordLoader::LoadSamples(const FString& FilePath, const int32 TrackCount, const int64 SampleCount,
	FArcReplayRecord* const OutRecord, FString* const OutErrorMessage)
{
	check(OutRecord);
	check(OutErrorMessage);

	TArray<uint8> Bytes;
	if (!LoadFileWithTerminator(FilePath, &Bytes))
	{
		*OutErrorMessage = FString::Printf(TEXT("cannot read %s"), *FilePath);
		return false;
	}

	const ANSICHAR* const Begin = reinterpret_cast<const ANSICHAR*>(Bytes.GetData());
	FArcCsvCursor Cursor(Begin, Begin + Bytes.Num() - 1);
	if (!Cursor.IsLineEqualTo(SAMPLE_HEADER))
	{
		*OutErrorMessage = MakeLineError(FilePath, 1, TEXT("unexpected header"));
		return false;
	}
	Cursor.SkipLine();

	TArray<FArcReplayTrack>& Tracks = OutRecord->Tracks;
	Tracks.Reserve(TrackCount);
	const int32 ExpectedSamplesPerTrack = static_cast<int32>(SampleCount / TrackCount) + 1;
	int64 LoadedSampleCount = 0;

	while (!Cursor.IsAtEnd())
	{
		int64 TimeUs = 0;
		int64 TrackId = 0;
		int64 KindIndex = 0;
		int64 PhaseValue = 0;
		double Values[SAMPLE_VALUE_COUNT];

		bool bLineValid = Cursor.ReadInt64(&TimeUs)
			&& Cursor.ReadInt64(&TrackId)
			&& Cursor.ReadInt64(&KindIndex)
			&& Cursor.ReadInt64(&PhaseValue);
		for (int32 ValueIndex = 0; bLineValid && ValueIndex < SAMPLE_VALUE_COUNT; ++ValueIndex)
		{
			bLineValid = Cursor.ReadDouble(&Values[ValueIndex]);
		}
		const int32 LineNumber = Cursor.GetLineNumber();
		if (!bLineValid || !Cursor.FinishLine())
		{
			*OutErrorMessage = MakeLineError(FilePath, LineNumber, TEXT("malformed sample row"));
			return false;
		}
		if (PhaseValue < 0 || PhaseValue > MAX_PHASE_VALUE || KindIndex < 0 || KindIndex >= OutRecord->KindNames.Num())
		{
			*OutErrorMessage = MakeLineError(FilePath, LineNumber, TEXT("phase or kind out of range"));
			return false;
		}

		const bool bStartsNewTrack = Tracks.IsEmpty() || Tracks.Last().TrackId != TrackId;
		if (bStartsNewTrack)
		{
			// 트랙 우선 정렬이고 ID가 0부터 연속이어야 배열 인덱스 = 트랙 ID가 성립한다.
			if (TrackId != Tracks.Num())
			{
				*OutErrorMessage = MakeLineError(FilePath, LineNumber, TEXT("track ids must be contiguous and track-major"));
				return false;
			}
			FArcReplayTrack& NewTrack = Tracks.AddDefaulted_GetRef();
			NewTrack.TrackId = static_cast<int32>(TrackId);
			NewTrack.KindIndex = static_cast<int32>(KindIndex);
			NewTrack.Samples.Reserve(ExpectedSamplesPerTrack);
		}

		FArcTrackSample& Sample = Tracks.Last().Samples.AddDefaulted_GetRef();
		Sample.TimeUs = TimeUs;
		Sample.Phase = static_cast<EArcProjectilePhase>(PhaseValue);
		Sample.PositionEcef = FVector(Values[0], Values[1], Values[2]);
		Sample.VelocityEcef = FVector(Values[3], Values[4], Values[5]);
		Sample.Latitude = Values[6];
		Sample.Longitude = Values[7];
		Sample.Altitude = Values[8];
		Sample.Heading = static_cast<float>(Values[9]);
		Sample.Pitch = static_cast<float>(Values[10]);
		Sample.Speed = static_cast<float>(Values[11]);
		++LoadedSampleCount;
	}

	if (Tracks.Num() != TrackCount || LoadedSampleCount != SampleCount)
	{
		*OutErrorMessage = FString::Printf(TEXT("samples.csv has %d tracks / %lld samples, meta.json says %d / %lld"),
			Tracks.Num(), LoadedSampleCount, TrackCount, SampleCount);
		return false;
	}
	return true;
}

bool FArcRecordLoader::LoadBinarySamples(const FString& FilePath, const int32 TrackCount, const int64 SampleCount,
	FArcReplayRecord* const OutRecord, FString* const OutErrorMessage)
{
	check(OutRecord);
	check(OutErrorMessage);

	TArray64<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *FilePath))
	{
		*OutErrorMessage = FString::Printf(TEXT("cannot read %s"), *FilePath);
		return false;
	}
	const uint8* const Data = Bytes.GetData();
	const bool bHeaderValid = Bytes.Num() >= BINARY_HEADER_SIZE
		&& FMemory::Memcmp(Data, BINARY_MAGIC, sizeof(BINARY_MAGIC)) == 0
		&& ReadBinaryValue<uint32>(Data, 4) == BINARY_VERSION
		&& ReadBinaryValue<uint32>(Data, 16) == BINARY_RECORD_SIZE;
	if (!bHeaderValid)
	{
		*OutErrorMessage = TEXT("samples.bin has an invalid header");
		return false;
	}
	const int64 FileSampleCount = static_cast<int64>(ReadBinaryValue<uint64>(Data, 8));
	if (FileSampleCount != SampleCount || Bytes.Num() != BINARY_HEADER_SIZE + FileSampleCount * BINARY_RECORD_SIZE)
	{
		*OutErrorMessage = FString::Printf(TEXT("samples.bin has %lld samples (%lld bytes), meta.json says %lld"),
			FileSampleCount, Bytes.Num(), SampleCount);
		return false;
	}

	TArray<FArcReplayTrack>& Tracks = OutRecord->Tracks;
	Tracks.Reserve(TrackCount);
	const int32 ExpectedSamplesPerTrack = static_cast<int32>(SampleCount / TrackCount) + 1;

	for (int64 SampleIndex = 0; SampleIndex < FileSampleCount; ++SampleIndex)
	{
		const uint8* const Record = Data + BINARY_HEADER_SIZE + SampleIndex * BINARY_RECORD_SIZE;
		const int32 TrackId = ReadBinaryValue<int32>(Record, 8);
		const int32 KindIndex = Record[12];
		const int32 PhaseValue = Record[13];
		if (PhaseValue > MAX_PHASE_VALUE || KindIndex >= OutRecord->KindNames.Num())
		{
			*OutErrorMessage = FString::Printf(TEXT("samples.bin record %lld: phase or kind out of range"), SampleIndex);
			return false;
		}

		const bool bStartsNewTrack = Tracks.IsEmpty() || Tracks.Last().TrackId != TrackId;
		if (bStartsNewTrack)
		{
			// CSV와 같은 규칙: 트랙 우선 정렬, ID는 0부터 연속 (배열 인덱스 = 트랙 ID).
			if (TrackId != Tracks.Num())
			{
				*OutErrorMessage = FString::Printf(TEXT("samples.bin record %lld: track ids must be contiguous and track-major"), SampleIndex);
				return false;
			}
			FArcReplayTrack& NewTrack = Tracks.AddDefaulted_GetRef();
			NewTrack.TrackId = TrackId;
			NewTrack.KindIndex = KindIndex;
			NewTrack.Samples.Reserve(ExpectedSamplesPerTrack);
		}

		FArcTrackSample& Sample = Tracks.Last().Samples.AddDefaulted_GetRef();
		Sample.TimeUs = ReadBinaryValue<int64>(Record, 0);
		Sample.Phase = static_cast<EArcProjectilePhase>(PhaseValue);
		Sample.PositionEcef = FVector(ReadBinaryValue<double>(Record, 16), ReadBinaryValue<double>(Record, 24), ReadBinaryValue<double>(Record, 32));
		Sample.VelocityEcef = FVector(ReadBinaryValue<float>(Record, 40), ReadBinaryValue<float>(Record, 44), ReadBinaryValue<float>(Record, 48));
		Sample.Latitude = ReadBinaryValue<double>(Record, 52);
		Sample.Longitude = ReadBinaryValue<double>(Record, 60);
		Sample.Altitude = ReadBinaryValue<float>(Record, 68);
		Sample.Heading = ReadBinaryValue<float>(Record, 72);
		Sample.Pitch = ReadBinaryValue<float>(Record, 76);
		Sample.Speed = ReadBinaryValue<float>(Record, 80);
	}

	if (Tracks.Num() != TrackCount)
	{
		*OutErrorMessage = FString::Printf(TEXT("samples.bin has %d tracks, meta.json says %d"), Tracks.Num(), TrackCount);
		return false;
	}
	return true;
}

bool FArcRecordLoader::LoadEvents(const FString& FilePath, FArcReplayRecord* const OutRecord, FString* const OutErrorMessage)
{
	check(OutRecord);
	check(OutErrorMessage);

	TArray<uint8> Bytes;
	if (!LoadFileWithTerminator(FilePath, &Bytes))
	{
		*OutErrorMessage = FString::Printf(TEXT("cannot read %s"), *FilePath);
		return false;
	}

	const ANSICHAR* const Begin = reinterpret_cast<const ANSICHAR*>(Bytes.GetData());
	FArcCsvCursor Cursor(Begin, Begin + Bytes.Num() - 1);
	if (!Cursor.IsLineEqualTo(EVENT_HEADER))
	{
		*OutErrorMessage = MakeLineError(FilePath, 1, TEXT("unexpected header"));
		return false;
	}
	Cursor.SkipLine();

	TArray<FArcReplayEvent>& Events = OutRecord->Events;
	Events.Reserve(OutRecord->Tracks.Num() * 2);

	while (!Cursor.IsAtEnd())
	{
		int64 TimeUs = 0;
		int64 TrackId = 0;
		int64 TypeValue = 0;
		FArcReplayEvent Event;

		const bool bLineValid = Cursor.ReadInt64(&TimeUs)
			&& Cursor.ReadInt64(&TrackId)
			&& Cursor.ReadInt64(&TypeValue)
			&& Cursor.ReadDouble(&Event.Latitude)
			&& Cursor.ReadDouble(&Event.Longitude)
			&& Cursor.ReadDouble(&Event.Altitude);
		const int32 LineNumber = Cursor.GetLineNumber();
		if (!bLineValid || !Cursor.FinishLine())
		{
			*OutErrorMessage = MakeLineError(FilePath, LineNumber, TEXT("malformed event row"));
			return false;
		}
		if (TrackId < 0 || TrackId >= OutRecord->Tracks.Num() || TypeValue < 0 || TypeValue > MAX_EVENT_TYPE_VALUE)
		{
			*OutErrorMessage = MakeLineError(FilePath, LineNumber, TEXT("track id or event type out of range"));
			return false;
		}
		if (!Events.IsEmpty() && TimeUs < Events.Last().TimeUs)
		{
			*OutErrorMessage = MakeLineError(FilePath, LineNumber, TEXT("events must be sorted by time"));
			return false;
		}

		Event.TimeUs = TimeUs;
		Event.TrackId = static_cast<int32>(TrackId);
		Event.Type = static_cast<EArcReplayEventType>(TypeValue);
		Events.Add(Event);
	}
	return true;
}

bool FArcRecordLoader::ValidateTracks(const FArcReplayRecord& Record, FString* const OutErrorMessage)
{
	check(OutErrorMessage);

	for (const FArcReplayTrack& Track : Record.Tracks)
	{
		const TArray<FArcTrackSample>& Samples = Track.Samples;
		for (int32 SampleIndex = 1; SampleIndex < Samples.Num(); ++SampleIndex)
		{
			if (Samples[SampleIndex].TimeUs <= Samples[SampleIndex - 1].TimeUs)
			{
				*OutErrorMessage = FString::Printf(TEXT("track %d: time must strictly increase"), Track.TrackId);
				return false;
			}
		}
		if (Samples.Last().Phase != EArcProjectilePhase::Impact)
		{
			*OutErrorMessage = FString::Printf(TEXT("track %d: last sample must be Impact"), Track.TrackId);
			return false;
		}
	}
	return true;
}
