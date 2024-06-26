#include "BH_Runnable.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFilemanager.h"

FBH_Runnable::FBH_Runnable(const FString& Command, const FString& Params, const FString& WorkingDirectory)
    : Command(Command), Params(Params), WorkingDirectory(WorkingDirectory), ProcessHandle(nullptr),
      StdInReadPipe(nullptr), StdInWritePipe(nullptr), StdOutReadPipe(nullptr), StdOutWritePipe(nullptr), StopTaskCounter(0)
{
    FPlatformProcess::CreatePipe(StdOutReadPipe, StdOutWritePipe);
    FPlatformProcess::CreatePipe(StdInReadPipe, StdInWritePipe, true);
    Thread = FRunnableThread::Create(this, TEXT("FBH_RunnableThread"), 0, TPri_Normal);
    if (!Thread)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create runnable thread."));
    }
}

FBH_Runnable::~FBH_Runnable()
{
    Stop();
    if (Thread)
    {
        Thread->WaitForCompletion();
        delete Thread;
    }
    FPlatformProcess::ClosePipe(StdInReadPipe, StdInWritePipe);
    FPlatformProcess::ClosePipe(StdOutReadPipe, StdOutWritePipe);
}

uint32 FBH_Runnable::Run()
{
    UE_LOG(LogTemp, Log, TEXT("Starting process %s %s."), *Command, *Params);
    
    ProcessHandle = FPlatformProcess::CreateProc(*Command, *Params, true, false, false, nullptr, 0, *WorkingDirectory, StdOutWritePipe, StdInReadPipe);
    if (!ProcessHandle.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to start process."));
        return 1;
    }

    while (StopTaskCounter.GetValue() == 0)
    {
        FString Output = FPlatformProcess::ReadPipe(StdOutReadPipe);
        if (!Output.IsEmpty())
        {
            std::lock_guard<std::mutex> lock(BufferMutex);
            OutputBuffer += Output;
        }

        int exitCode;
        if (IsProcessRunning(&exitCode))
        {
            FPlatformProcess::Sleep(0.1f);
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("Process exited with code %d."), exitCode);
            StopTaskCounter.Increment();
        }
    }

    TerminateProcess();

    UE_LOG(LogTemp, Log, TEXT("Process stopped."));

    return 0;
}

void FBH_Runnable::TerminateProcess()
{
    if (ProcessHandle.IsValid())
    {
        FPlatformProcess::TerminateProc(ProcessHandle);
        FPlatformProcess::WaitForProc(ProcessHandle);
        FPlatformProcess::CloseProc(ProcessHandle);
        ProcessHandle.Reset();
    }
}

void FBH_Runnable::WriteToPipe(const TArray<uint8>& Data)
{
    if (ProcessHandle.IsValid())
    {
        int32 BytesWritten;
        FPlatformProcess::WritePipe(StdInWritePipe, Data.GetData(), Data.Num(), &BytesWritten);
        // UE_LOG(LogTemp, Log, TEXT("Written %d bytes to pipe."), BytesWritten); // Added log for debug purposes
    }
}

FString FBH_Runnable::GetBufferedOutput()
{
    std::lock_guard<std::mutex> lock(BufferMutex);
    FString BufferedOutput = OutputBuffer;
    OutputBuffer.Empty();
    return BufferedOutput;
}

void FBH_Runnable::Stop()
{
    UE_LOG(LogTemp, Log, TEXT("Stopping process."));
    StopTaskCounter.Increment();

    if (Thread)
    {
        Thread->WaitForCompletion();
    }
}

bool FBH_Runnable::IsProcessRunning(int32* ExitCode)
{
    if (FPlatformProcess::IsProcRunning(ProcessHandle))
    {
        return true;
    }
    else
    {
        if (ExitCode)
        {
            FPlatformProcess::GetProcReturnCode(ProcessHandle, ExitCode);
        }
        return false;
    }
}