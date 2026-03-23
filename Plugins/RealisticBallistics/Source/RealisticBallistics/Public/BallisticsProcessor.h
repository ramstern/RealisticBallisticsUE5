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

	struct ProjectileCollisionStepResult
	{
		float time_alpha;
		float penetrated_depth;
		bool is_penetrating;
		FVector3f entry_point;
	};

	float step_accumulator = 0.f;
	const int32 MAX_SUBSTEPS = 32;

	void TickBallistics(FMassExecutionContext& context, float dt);

	struct State
	{
		FVector3f pos;
		FVector3f vel;

		State operator+(const State& other) const
		{
			return State{
				pos + other.pos,
				vel + other.vel
			};
		}
		State operator*(float scalar) const
		{
			return State{
				pos * scalar,
				vel * scalar
			};
		}
	};

	State ComputeDerivative(const State& state, FVector3f acceleration);

	void ProjectileIntegrateStep(float dt, const FProjectileProperties& projectile_properties, FProjectileTransform& projectile_transform, FProjectilePhysicsData& projectile_physdata,
								 const UBallisticsSubsystem* ballistics_sys);
	
	ProjectileCollisionStepResult ProjectileCollisionStep(const int proj_ent, const FProjectileTransform& projectile_transform, FMassExecutionContext& context, ECollisionChannel channel);

	void ApplyPenetrationResistance(const FProjectileProperties& projectile_properties, FProjectilePhysicsData& projectile_physdata, const ProjectileCollisionStepResult& collision_data);

	void KillProjectile(FMassExecutionContext& context, int i);

	FMassEntityQuery projectile_simulation_step;
	TObjectPtr<UCurveTable> drag_table;
};
