// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "Engine/CollisionProfile.h"

#include "BallisticsProcessor.generated.h"

/**
 * 
 */

struct FProjectileProperties;
struct FProjectileTransform;
struct FProjectilePhysicsData;
class UBallisticsSubsystem;

UCLASS()
class REALISTICBALLISTICS_API UBallisticsProcessor : public UMassProcessor
{
	GENERATED_BODY()
	
public:
	UBallisticsProcessor();
protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>&) override;
	
	virtual void Execute(FMassEntityManager& entity_manager, FMassExecutionContext& context) override;
private:

	void ProjectileIntegrateStep(float dt, const FProjectileProperties& projectile_properties, FProjectileTransform& projectile_transform, FProjectilePhysicsData& projectile_physdata,
								 const UBallisticsSubsystem* ballistics_sys);

	FMassEntityQuery projectile_simulation_step;
	TObjectPtr<UCurveTable> drag_table;
};
