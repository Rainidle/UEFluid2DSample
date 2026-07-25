#include "Fluid2DSubsystem.h"
#include "Fluid2DViewExtension.h"

#include "SceneViewExtension.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "UnrealClient.h"

void UFluid2DSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (!FApp::CanEverRender())
	{
		return;
	}

	if (GEngine && GEngine->IsInitialized())
	{
		CreateViewExtension();
	}
	else
	{
		FCoreDelegates::OnPostEngineInit.AddUObject(this, &UFluid2DSubsystem::CreateViewExtension);
	}
}

void UFluid2DSubsystem::Deinitialize()
{
	FCoreDelegates::OnPostEngineInit.RemoveAll(this);

	// release on render thread
	if (ViewExtension.IsValid())
	{
		ENQUEUE_RENDER_COMMAND(Fluid2DReleaseViewExtension)(
			[Extension = MoveTemp(ViewExtension)](FRHICommandListImmediate&) mutable
			{
				Extension.Reset();
			});
	}

	Super::Deinitialize();
}

void UFluid2DSubsystem::CreateViewExtension()
{
	ViewExtension = FSceneViewExtensions::NewExtension<FFluid2DViewExtension>();
}

void UFluid2DSubsystem::AddImpulse(FVector2D PrevUV, FVector2D CurrentUV, float Radius, FLinearColor DyeColor, float DyeAmount)
{
	if (!ViewExtension.IsValid())
	{
		return;
	}

	FFluid2DImpulse Impulse;
	Impulse.PrevUV = FVector2f(PrevUV.X, PrevUV.Y);
	Impulse.CurrUV = FVector2f(CurrentUV.X, CurrentUV.Y);
	Impulse.Radius = FMath::Max(Radius, 0.001f);
	Impulse.Dye = DyeColor;
	Impulse.Dye.A = DyeAmount;
	ViewExtension->EnqueueImpulse(Impulse);
}

void UFluid2DSubsystem::AddWorldImpulse(FVector PrevWorldPos, FVector CurrentWorldPos, float Radius, FLinearColor DyeColor, float DyeAmount)
{
	if (!ViewExtension.IsValid())
	{
		return;
	}

	FFluid2DWorldImpulse Impulse;
	Impulse.PrevPos = PrevWorldPos;
	Impulse.CurrPos = CurrentWorldPos;
	Impulse.Radius = FMath::Max(Radius, 0.001f);
	Impulse.Dye = DyeColor;
	Impulse.Dye.A = DyeAmount;
	ViewExtension->EnqueueWorldImpulse(Impulse);
}

void UFluid2DSubsystem::AddExplosion(FVector2D CenterUV, float Radius, float Strength, float Irregularity, FLinearColor DyeColor, float DyeAmount)
{
	if (!ViewExtension.IsValid())
	{
		return;
	}

	FFluid2DExplosion Explosion;
	Explosion.CenterUV = FVector2f(CenterUV.X, CenterUV.Y);
	Explosion.Radius = FMath::Max(Radius, 0.001f);
	Explosion.Strength = Strength;
	Explosion.Irregularity = Irregularity;
	Explosion.Seed = FMath::FRand() * 100.0f;
	Explosion.Dye = DyeColor;
	Explosion.Dye.A = DyeAmount;
	ViewExtension->EnqueueExplosion(Explosion);
}

void UFluid2DSubsystem::AddWorldExplosion(FVector WorldPos, float Radius, float Strength, float Irregularity, FLinearColor DyeColor, float DyeAmount)
{
	if (!ViewExtension.IsValid())
	{
		return;
	}

	FFluid2DWorldExplosion Explosion;
	Explosion.Pos = WorldPos;
	Explosion.Radius = FMath::Max(Radius, 0.001f);
	Explosion.Strength = Strength;
	Explosion.Irregularity = Irregularity;
	Explosion.Seed = FMath::FRand() * 100.0f;
	Explosion.Dye = DyeColor;
	Explosion.Dye.A = DyeAmount;
	ViewExtension->EnqueueWorldExplosion(Explosion);
}

bool UFluid2DSubsystem::GetMouseViewportUV(UObject* WorldContextObject, FVector2D& OutUV)
{
	OutUV = FVector2D::ZeroVector;

	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	UGameViewportClient* ViewportClient = World ? World->GetGameViewport() : nullptr;
	if (!ViewportClient || !ViewportClient->Viewport)
	{
		return false;
	}

	// Both of these are in viewport pixels, which is what makes the ratio a true UV.
	FVector2D MousePosition;
	const FIntPoint ViewportSize = ViewportClient->Viewport->GetSizeXY();
	if (!ViewportClient->GetMousePosition(MousePosition) || ViewportSize.X <= 0 || ViewportSize.Y <= 0)
	{
		return false;
	}

	OutUV = FVector2D(MousePosition.X / ViewportSize.X, MousePosition.Y / ViewportSize.Y);
	return true;
}

static FAutoConsoleCommand GFluid2DTestExplosionCmd(
	TEXT("Fluid2DSample.TestExplosion"),
	TEXT("Inject an explosion at the centre of the viewport. Optional arguments: radius (0.4) strength (500) irregularity (4.0) color (0.5)."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (UFluid2DSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UFluid2DSubsystem>() : nullptr)
			{
				const float Radius = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 0.4f;
				const float Strength = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 500.0f;
				const float Irregularity = Args.Num() > 2 ? FCString::Atof(*Args[2]) : 4.0f;
				const float ColorStrength = Args.Num() > 3 ? FCString::Atof(*Args[3]) : 0.5f;
				Subsystem->AddExplosion(FVector2D(0.5, 0.5), Radius, Strength, Irregularity, FLinearColor::MakeRandomColor(), ColorStrength);
			}
		}));
