#include "BH_LogCapture.h"

void UBH_LogCapture::Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category)
{
    // Capture the log message
    CapturedLogs.Append(V);
    CapturedLogs.Append(TEXT("\n"));
}

FString UBH_LogCapture::GetCapturedLogs() const
{
    return CapturedLogs;
}