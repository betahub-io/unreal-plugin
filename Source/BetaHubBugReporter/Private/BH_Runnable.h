#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include <string>
#include <mutex>

class FBH_Runnable : public FRunnable
{
public:
    FBH_Runnable(const FString& Command, const FString& Params = TEXT(""), const FString& WorkingDirectory = FPaths::ProjectDir());
    virtual ~FBH_Runnable();

    virtual uint32 Run() override;
    void WriteToPipe(const TArray<uint8>& Data);
    FString GetBufferedOutput();
    void Stop(bool bCloseStdin = false);
    bool IsProcessRunning(int32* ExitCode = nullptr);
    void WaitForExit();

    // static method to run a command and return the output
    static FString RunCommand(const FString& Command, const FString& Params, const FString& WorkingDirectory, int32 &ExitCode);
    static FString RunCommand(const FString& Command, const FString& Params = TEXT(""), const FString& WorkingDirectory = FPaths::ProjectDir());

private:
    FString Command;
    FString Params;
    FString WorkingDirectory;
    FProcHandle ProcessHandle;

    void* StdInReadPipe;
    void* StdInWritePipe;
    void* StdOutReadPipe;
    void* StdOutWritePipe;
    FRunnableThread* Thread;
    FThreadSafeCounter StopTaskCounter;
    FThreadSafeBool bTerminateByStdinFlag;
    FString OutputBuffer;
    std::mutex BufferMutex;

    void TerminateProcess();
};