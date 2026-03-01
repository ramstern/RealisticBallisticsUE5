#pragma once
#include "CoreMinimal.h"
#include "MassEntityElementTypes.h"
#include "ProjectileFragments.generated.h"

constexpr float TO_UE_UNITS = 100.f;
constexpr float UE_TO_METRIC_UNITS = 0.01f;
constexpr float SOUND_SPEED = 343.f;
constexpr float BC_TO_SI = 703.06957829636f;

USTRUCT()
struct REALISTICBALLISTICS_API FProjectileProperties : public FMassFragment
{
	GENERATED_BODY()

	float mass;
	float diameter;
	float ballistic_coefficient;
	float length;

	enum DragModel
	{
		G1,
		G2,
		G5,
		G6,
		G7,
		G8,
		SPHERE
	} drag_model;
};

USTRUCT()
struct REALISTICBALLISTICS_API FProjectileTransform : public FMassFragment
{
	GENERATED_BODY()

	FVector3f position;
	FVector3f previous_position;
	float yaw;
	float pitch;
	float roll;
};

USTRUCT()
struct REALISTICBALLISTICS_API FProjectilePhysicsData : public FMassFragment
{
	GENERATED_BODY()

	FVector3f velocity;
	FVector3f acceleration;
};

USTRUCT()
struct REALISTICBALLISTICS_API FProjectileHitData : public FMassFragment
{
	GENERATED_BODY()

	float total_penetration;
	float starting_penetration;
	bool started_penetration;
};