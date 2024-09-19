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

    void HideScreenAreaFromReport(FVector4 AreaToHide);
    void HideScreenAreaFromReportArray(TArray<FVector4> AreasToHide);
    void SetHiddenAreaColor(FColor NewColor);

private:
    UPROPERTY()
    UBH_FrameBuffer* FrameBuffer;

    UPROPERTY()
    ABH_SceneCaptureActor* SceneCaptureActor;

    TArray<FVector4> HiddenAreas;
    FColor HiddenAreaColor;

    TSharedPtr<BH_VideoEncoder> VideoEncoder;
    int32 TargetFPS;
    FTimespan RecordingDuration;

    bool bIsRecording;

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
    FDateTime LastCaptureTime;

    BH_AsyncQueue<BH_RawFrameBuffer<uint8>> RawFrameBufferQueue;
    BH_AsyncPool<BH_RawFrameBuffer<uint8>> RawFrameBufferPool;

    void CaptureFrame();
    void CaptureRenderTargetFrame();
    void ReadPixels();

    void SetFrameData(int32 Width, int32 Height, const TArray<FColor>& Data);
    void ResizeImageToFrame(const TArray<FColor>& ImageData, uint32 ImageWidth, uint32 ImageHeight, uint32 FrameWidth, uint32 FrameHeight, TArray<FColor>& ResizedData);

    TArray<int32> GetPixelIndicesFromViewportRectangle(const FVector2D& TopLeftViewportCoords, const FVector2D& BottomRightViewportCoords, int32 TextureWidth, int32 TextureHeight);
};
