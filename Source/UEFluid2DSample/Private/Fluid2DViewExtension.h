#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "RendererInterface.h"

// A single force and dye injection along the path a point travelled during one frame.
// UVs are viewport space, 0-1, origin top-left.
struct FFluid2DImpulse
{
	FVector2f PrevUV = FVector2f::ZeroVector;
	FVector2f CurrUV = FVector2f::ZeroVector;
	float Radius = 0.015f;                        // falloff radius as a fraction of viewport height
	FLinearColor Dye = FLinearColor::Transparent; // rgb = colour, a = amount (0 injects force only)
};

// World-space impulse. The game thread only records positions; projection to viewport UV
// happens on the render thread, where the view matrices actually being rendered are known.
struct FFluid2DWorldImpulse
{
	FVector PrevPos = FVector::ZeroVector;
	FVector CurrPos = FVector::ZeroVector;
	float Radius = 0.02f;
	FLinearColor Dye = FLinearColor::Transparent;
};

// A procedural radial burst.
struct FFluid2DExplosion
{
	FVector2f CenterUV = FVector2f(0.5f, 0.5f);
	float Radius = 0.15f;      // as a fraction of viewport height
	float Strength = 300.0f;   // peak speed in simulation cells per second
	float Irregularity = 0.6f; // angular break-up, 0-1
	float Seed = 0.0f;
	FLinearColor Dye = FLinearColor::Transparent;
};

// World-space explosion; the centre is projected on the render thread.
struct FFluid2DWorldExplosion
{
	FVector Pos = FVector::ZeroVector;
	float Radius = 0.15f;
	float Strength = 300.0f;
	float Irregularity = 0.6f;
	float Seed = 0.0f;
	FLinearColor Dye = FLinearColor::Transparent;
};

// Render-side entry point. The engine calls PrePostProcessPass_RenderThread once per view
// per frame, and this is where the simulation and composite passes are added to the graph.
class FFluid2DViewExtension : public FSceneViewExtensionBase
{
public:
	FFluid2DViewExtension(const FAutoRegister& AutoRegister);

	//~ ISceneViewExtension interface
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs) override;
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;

	// Callable from any thread; consumed by the next simulation step.
	void EnqueueImpulse(const FFluid2DImpulse& Impulse);
	void EnqueueWorldImpulse(const FFluid2DWorldImpulse& Impulse);
	void EnqueueExplosion(const FFluid2DExplosion& Explosion);
	void EnqueueWorldExplosion(const FFluid2DWorldExplosion& Explosion);

private:
	// Builds one frame of simulation.
	void StepSimulation(FRDGBuilder& GraphBuilder, const FSceneView& View, FRDGTextureRef SceneColorCopy,
		FRDGTextureRef SceneDepth, FRDGTextureRef CustomDepth, FRDGTextureRef SceneVelocity, const FIntRect& ViewRect);

	FCriticalSection ImpulseLock;
	TArray<FFluid2DImpulse> PendingImpulses;
	TArray<FFluid2DWorldImpulse> PendingWorldImpulses;
	TArray<FFluid2DExplosion> PendingExplosions;
	TArray<FFluid2DWorldExplosion> PendingWorldExplosions;

	// ping-pong
	TRefCountPtr<IPooledRenderTarget> VelocityRT[2];
	TRefCountPtr<IPooledRenderTarget> PressureRT[2];
	TRefCountPtr<IPooledRenderTarget> DyeRT[2];
	int32 VelocityIdx = 0;
	int32 PressureIdx = 0;
	int32 DyeIdx = 0;

	FIntPoint SimSize = FIntPoint::ZeroValue;

	// Split screen and multiple editor viewports call the hook several times per frame.
	// The frame counter keeps the simulation to one step; the other views only composite.
	uint64 LastSimFrame = MAX_uint64;
};
