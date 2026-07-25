#include "Fluid2DVelocityComponent.h"
#include "Fluid2DSubsystem.h"
#include "Engine/Engine.h"

UFluid2DVelocityComponent::UFluid2DVelocityComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UFluid2DVelocityComponent::BeginPlay()
{
	Super::BeginPlay();
	PrevLocation = GetComponentLocation();
}

void UFluid2DVelocityComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const FVector Current = GetComponentLocation();

	if (FVector::DistSquared(Current, PrevLocation) >= FMath::Square(MinMoveDistance))
	{
		if (UFluid2DSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UFluid2DSubsystem>() : nullptr)
		{
			Subsystem->AddWorldImpulse(PrevLocation, Current, Radius, DyeColor, DyeAmount);
		}
	}

	PrevLocation = Current;
}
