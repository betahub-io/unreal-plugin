#pragma once

#include "CoreMinimal.h"
#include "BH_VideoEncoder.h"
#include "BH_FrameBuffer.h"
#include "Tickable.h"
#include "UObject/NoExportTypes.h"
#include "BH_GameRecorder.generated.h"

UCLASS()
class BETAHUBBUGREPORTER_API UBH_GameRecorder : public UObject, public FTickableGameObject
{
    GENERATED_BODY()

private:
    UPROPERTY()
    UBH_FrameBuffer* FrameBuffer;

    TSharedPtr<BH_VideoEncoder> VideoEncoder;
    bool bIsRecording;

public:
    UBH_GameRecorder(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable, Category="Recording")
    void StartRecording(int32 targetFPS);

    UFUNCTION(BlueprintCallable, Category="Recording")
    void PauseRecording();

    UFUNCTION(BlueprintCallable, Category="Recording")
    void StopRecording();

    UFUNCTION(BlueprintCallable, Category="Recording")
    FString SaveRecording();

    FString CaptureScreenshotToJPG(const FString& Filename = "");

    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override;
    virtual TStatId GetStatId() const override;

private:
    void CaptureFrame();
    void CaptureViewportFrame();
    void SetFrameData(int32 Width, int32 Height, const TArray<FColor>& Data);
    void PadBitmap(TArray<FColor>& Bitmap, int32& Width, int32& Height);
};