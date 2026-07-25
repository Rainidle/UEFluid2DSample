#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Fluid2DVelocityComponent.generated.h"

/**
 * Attach to an actor to stir the screen-space fluid as it moves. Each tick the component
 * feeds its own displacement to UFluid2DSubsystem::AddWorldImpulse, so the projection to
 * viewport UV happens on the render thread with the frame's real view matrices.
 *
 * This suits objects whose centroid actually travels: projectiles, vehicles, a running
 * character's root. It cannot express skinned motion, because a handstand or a spin leaves
 * the centroid where it was and therefore produces no impulse. For those, enable Render
 * CustomDepth on the mesh and use the motion vector force (r.Fluid2DSample.MotionForce),
 * which drives the fluid per pixel.
 */
UCLASS(ClassGroup = (Rendering), meta = (BlueprintSpawnableComponent))
class UEFLUID2DSAMPLE_API UFluid2DVelocityComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UFluid2DVelocityComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Falloff radius as a fraction of viewport height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid2D")
	float Radius = 0.02f;

	/** Colour of the dye injected while moving. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid2D")
	FLinearColor DyeColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	/** Dye amount; 0 stirs the fluid without colouring it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid2D")
	float DyeAmount = 0.1f;

	/** Movement below this distance in centimetres is ignored, so a stationary object
	 *  does not pin the fluid in place. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid2D")
	float MinMoveDistance = 1.0f;

private:
	FVector PrevLocation = FVector::ZeroVector;
};
