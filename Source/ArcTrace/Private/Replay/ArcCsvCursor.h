#pragma once

#include "CoreMinimal.h"

// 쉼표 구분 ASCII 텍스트를 앞에서부터 한 필드씩 읽는다. 문자열 복사 없이 버퍼 위에서 동작한다.
class FArcCsvCursor
{
public:
	FArcCsvCursor(const ANSICHAR* const InBegin, const ANSICHAR* const InEnd);

	bool IsAtEnd() const;
	bool IsLineEqualTo(const ANSICHAR* const ExpectedLine) const;
	void SkipLine();

	bool ReadInt64(int64* const OutValue);
	bool ReadDouble(double* const OutValue);

	// 마지막 필드를 읽은 뒤 호출: 줄 끝(또는 파일 끝)이어야 한다.
	bool FinishLine();

	int32 GetLineNumber() const;

private:
	// 현재 필드를 Buffer에 널 종료 문자열로 복사하고 구분자 앞까지 전진한다.
	bool ReadField(ANSICHAR* const OutBuffer, const int32 BufferSize);

private:
	const ANSICHAR* Current;
	const ANSICHAR* const End;
	int32 LineNumber;
};
