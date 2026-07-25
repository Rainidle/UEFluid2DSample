#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "Fluid2DSubsystem.generated.h"

class FFluid2DViewExtension;

/**
 * Game-side entry point for the fluid post process. Owns the scene view extension that
 * hooks the renderer, and exposes the Blueprint API used to inject interaction.
 *
 * This is an engine subsystem rather than a world subsystem because the view extension is
 * global: it applies to every view and its lifetime follows the engine, not a world.
 */
UCLASS()
class UEFLUID2DSAMPLE_API UFluid2DSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Inject force and dye along the segment a point travelled since the last frame.
	 * Typical use is to divide the mouse position by the viewport size on tick, cache the
	 * previous value, and call this once per frame while the button is held.
	 */
	UFUNCTION(BlueprintCallable, Category = "Fluid2D")
	void AddImpulse(FVector2D PrevUV, FVector2D CurrentUV, float Radius = 0.015f, FLinearColor DyeColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), float DyeAmount = 0.5f);

	/**
	 * World-space form of AddImpulse, for moving objects that should leave a wake on screen.
	 * Positions are projected to viewport UV on the render thread using the view matrices
	 * of the frame being rendered; objects behind the camera or far outside the viewport
	 * are ignored.
	 *
	 * For the common case, attach a UFluid2DVelocityComponent instead and let it call this.
	 */
	UFUNCTION(BlueprintCallable, Category = "Fluid2D")
	void AddWorldImpulse(FVector PrevWorldPos, FVector CurrentWorldPos, float Radius = 0.02f, FLinearColor DyeColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), float DyeAmount = 0.1f);

	/**
	 * Inject a procedural radial burst at a point on screen. The pattern is generated from
	 * a radial profile modulated by noise, so no texture assets are involved and every
	 * blast looks different.
	 */
	UFUNCTION(BlueprintCallable, Category = "Fluid2D")
	void AddExplosion(FVector2D CenterUV, float Radius = 0.15f, float Strength = 300.0f, float Irregularity = 0.6f, FLinearColor DyeColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), float DyeAmount = 0.5f);

	/**
	 * World-space form of AddExplosion, suited to hooking up impact or detonation events.
	 * The centre is projected on the render thread and ignored when off screen.
	 */
	UFUNCTION(BlueprintCallable, Category = "Fluid2D")
	void AddWorldExplosion(FVector WorldPos, float Radius = 0.15f, float Strength = 300.0f, float Irregularity = 0.6f, FLinearColor DyeColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), float DyeAmount = 0.5f);

	/**
	 * Mouse position as viewport UV (0-1, origin top-left), ready to pass to AddImpulse or
	 * AddExplosion.
	 *
	 * 
	 */
	UFUNCTION(BlueprintPure, Category = "Fluid2D", meta = (WorldContext = "WorldContextObject"))
	static bool GetMouseViewportUV(UObject* WorldContextObject, FVector2D& OutUV);

private:
	void CreateViewExtension();

	TSharedPtr<FFluid2DViewExtension, ESPMode::ThreadSafe> ViewExtension;
};
