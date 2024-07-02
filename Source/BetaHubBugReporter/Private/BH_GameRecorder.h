#pragma once

#include "CoreMinimal.h"
#include "BH_VideoEncoder.h"
#include "BH_FrameBuffer.h"
#include "Tickable.h"
#include "UObject/NoExportTypes.h"
#include "BH_SceneCaptureActor.h"
#include "BH_GameRecorder.generated.h"

UCLASS()
class BETAHUBBUGREPORTER_API UBH_GameRecorder : public UObject, public FTickableGameObject
{
    GENERATED_BODY()

private:
    UPROPERTY()
    UBH_FrameBuffer* FrameBuffer;

    UPROPERTY()
    ABH_SceneCaptureActor* SceneCaptureActor;

    TSharedPtr<BH_VideoEncoder> VideoEncoder;
    bool bIsRecording;

public:
    UBH_GameRecorder(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable, Category="Recording")
    void StartRecording(int32 targetFPS, const FTimespan& RecordingDuration);

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

private:
    bool bCopyTextureStarted;
    FRenderCommandFence CopyTextureFence;

    TArray<FLinearColor> PendingLinearPixels;
    TArray<FColor> PendingPixels;
    TArray<FColor> ResizedPixels;

    FTexture2DRHIRef StagingTexture;
    EPixelFormat StagingTextureFormat;

    int32 ViewportWidth;
    int32 ViewportHeight;
    int32 FrameWidth;
    int32 FrameHeight;
    int32 TargetFPS;
    FDateTime LastCaptureTime;

    void *RawData;
    int32 RawDataWidth;
    int32 RawDataHeight;

    void CaptureFrame();
    void CaptureRenderTargetFrame();
    void ReadPixels();

    void SetFrameData(int32 Width, int32 Height, const TArray<FColor>& Data);
    void PadBitmap(TArray<FColor>& Bitmap, int32& Width, int32& Height);
    void ResizeImageToFrame(const TArray<FColor>& ImageData, uint32 ImageWidth, uint32 ImageHeight, uint32 FrameWidth, uint32 FrameHeight, TArray<FColor>& ResizedData);
};
