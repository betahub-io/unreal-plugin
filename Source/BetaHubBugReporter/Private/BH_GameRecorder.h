#pragma once

#include "CoreMinimal.h"
#include "Tickable.h"
#include "HAL/CriticalSection.h"
#include "BH_VideoEncoder.h"
#include "BH_FrameBuffer.h"
#include "UObject/NoExportTypes.h"
#include "BH_SceneCaptureActor.h"
#include "BH_Async.h"
#include "BH_RawFrameBuffer.h"
#include "BH_GameRecorder.generated.h"

UCLASS()
class BETAHUBBUGREPORTER_API UBH_GameRecorder : public UObject, public FTickableGameObject
{
    GENERATED_BODY()

public:
    UBH_GameRecorder(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable, Category="Recording")
    void StartRecording(int32 targetFPS, int32 RecordingDuration);

    UFUNCTION(BlueprintCallable, Category="Recording")
    void PauseRecording();

    UFUNCTION(BlueprintCallable, Category="Recording")
    void StopRecording();

    UFUNCTION(BlueprintCallable, Category="Recording")
    FString SaveRecording();

    UFUNCTION(BlueprintCallable, Category="Recording")
    FString CaptureScreenshotToJPG(const FString& Filename = "");

    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override;
    virtual TStatId GetStatId() const override;

    // Sets the maximum video dimensions while maintaining aspect ratio
    void SetMaxVideoDimensions(int32 InMaxWidth, int32 InMaxHeight);

private:
    UPROPERTY()
    TObjectPtr<UBH_FrameBuffer> FrameBuffer;

    UPROPERTY()
    TObjectPtr<ABH_SceneCaptureActor> SceneCaptureActor;

    TSharedPtr<BH_VideoEncoder> VideoEncoder;
    int32 TargetFPS;
    FTimespan RecordingDuration;

    bool bIsRecording;

    bool bCopyTextureStarted;
    FRenderCommandFence CopyTextureFence;

    TArray<FLinearColor> PendingLinearPixels;
    TArray<FColor> PendingPixels;
    TArray<FColor> ResizedPixels;

    FTextureRHIRef StagingTexture;
    EPixelFormat StagingTextureFormat;

    int32 ViewportWidth;
    int32 ViewportHeight;
    int32 FrameWidth;
    int32 FrameHeight;
    FDateTime LastCaptureTime;

    BH_AsyncQueue<BH_RawFrameBuffer<uint8>> RawFrameBufferQueue;
    BH_AsyncPool<BH_RawFrameBuffer<uint8>> RawFrameBufferPool;

    SWindow* MainEditorWindow;
    FVector2D LargestSize;

    // Maximum video dimensions
    int32 MaxVideoWidth;
    int32 MaxVideoHeight;

    void ReadPixels(const FTextureRHIRef& BackBuffer);

    void SetFrameData(int32 Width, int32 Height, const TArray<FColor>& Data);
    void ResizeImageToFrame(const TArray<FColor>& ImageData, uint32 ImageWidth, uint32 ImageHeight, uint32 FrameWidth, uint32 FrameHeight, TArray<FColor>& ResizedData);

    void OnBackBufferReady(SWindow& Window, const FTextureRHIRef& BackBuffer);

    void OnBackBufferResized(const FTextureRHIRef& BackBuffer);

    //Hack TODO
    TSet<FString> CreatedWindows;
};
