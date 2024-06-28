#include "BH_SceneCaptureActor.h"

ABH_SceneCaptureActor::ABH_SceneCaptureActor()
{
    PrimaryActorTick.bCanEverTick = false;

    // Create render target
    RenderTarget = CreateDefaultSubobject<UTextureRenderTarget2D>(TEXT("RenderTarget"));
    RenderTarget->InitCustomFormat(1920, 1080, PF_B8G8R8A8, true);

    // Create scene capture component
    SceneCaptureComponent = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCaptureComponent"));
    SceneCaptureComponent->SetupAttachment(RootComponent);
    SceneCaptureComponent->SetRelativeLocation(FVector(0, 0, 0));
    SceneCaptureComponent->bCaptureEveryFrame = false;
    SceneCaptureComponent->bCaptureOnMovement = false;
    SceneCaptureComponent->TextureTarget = RenderTarget;
}

void ABH_SceneCaptureActor::BeginPlay()
{
    Super::BeginPlay();
}