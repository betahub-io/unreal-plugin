#pragma once

#include "CoreMinimal.h"
#include "Misc/OutputDevice.h"

class UBH_LogCapture : public FOutputDevice
{
public:
    virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category) override;

    FString GetCapturedLogs() const;

private:
    FString CapturedLogs;
};