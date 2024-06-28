#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "BH_SceneCaptureActor.generated.h"

UCLASS()
class BETAHUBBUGREPORTER_API ABH_SceneCaptureActor : public AActor
{
    GENERATED_BODY()
    
public:    
    ABH_SceneCaptureActor();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Capture")
    UTextureRenderTarget2D* RenderTarget;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Capture")
    USceneCaptureComponent2D* SceneCaptureComponent;

protected:
    virtual void BeginPlay() override;
};