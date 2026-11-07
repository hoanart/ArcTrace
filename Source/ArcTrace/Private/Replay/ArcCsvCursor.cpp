#include "ArcCsvCursor.h"

namespace
{
	constexpr int32 FIELD_BUFFER_SIZE = 64;
}

FArcCsvCursor::FArcCsvCursor(const ANSICHAR* const InBegin, const ANSICHAR* const InEnd)
	: Current(InBegin)
	, End(InEnd)
	, LineNumber(1)
{
}

bool FArcCsvCursor::IsAtEnd() const
{
	return Current >= End;
}

bool FArcCsvCursor::IsLineEqualTo(const ANSICHAR* const ExpectedLine) const
{
	const int32 ExpectedLength = FCStringAnsi::Strlen(ExpectedLine);
	const ANSICHAR* LineEnd = Current;
	while (LineEnd < End && *LineEnd != '\n' && *LineEnd != '\r')
	{
		++LineEnd;
	}
	const int32 LineLength = static_cast<int32>(LineEnd - Current);
	return LineLength == ExpectedLength && FMemory::Memcmp(Current, ExpectedLine, ExpectedLength) == 0;
}

void FArcCsvCursor::SkipLine()
{
	while (Current < End && *Current != '\n')
	{
		++Current;
	}
	if (Current < End)
	{
		++Current;
		++LineNumber;
	}
}

bool FArcCsvCursor::ReadField(ANSICHAR* const OutBuffer, const int32 BufferSize)
{
	check(OutBuffer);

	const ANSICHAR* const FieldBegin = Current;
	while (Current < End && *Current != ',' && *Current != '\n' && *Current != '\r')
	{
		++Current;
	}
	const int32 FieldLength = static_cast<int32>(Current - FieldBegin);
	if (FieldLength == 0 || FieldLength >= BufferSize)
	{
		return false;
	}
	FMemory::Memcpy(OutBuffer, FieldBegin, FieldLength);
	OutBuffer[FieldLength] = '\0';

	if (Current < End && *Current == ',')
	{
		++Current;
	}
	return true;
}

bool FArcCsvCursor::ReadInt64(int64* const OutValue)
{
	check(OutValue);

	ANSICHAR Buffer[FIELD_BUFFER_SIZE];
	if (!ReadField(Buffer, FIELD_BUFFER_SIZE))
	{
		return false;
	}
	ANSICHAR* ParseEnd = nullptr;
	*OutValue = FCStringAnsi::Strtoi64(Buffer, &ParseEnd, 10);
	return ParseEnd != Buffer && *ParseEnd == '\0';
}

bool FArcCsvCursor::ReadDouble(double* const OutValue)
{
	check(OutValue);

	ANSICHAR Buffer[FIELD_BUFFER_SIZE];
	if (!ReadField(Buffer, FIELD_BUFFER_SIZE))
	{
		return false;
	}
	*OutValue = FCStringAnsi::Atod(Buffer);
	return true;
}

bool FArcCsvCursor::FinishLine()
{
	if (Current < End && *Current == '\r')
	{
		++Current;
	}
	if (Current >= End)
	{
		return true;
	}
	if (*Current != '\n')
	{
		return false;
	}
	++Current;
	++LineNumber;
	return true;
}

int32 FArcCsvCursor::GetLineNumber() const
{
	return LineNumber;
}
