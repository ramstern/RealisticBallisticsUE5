#pragma once
#include "CoreMinimal.h"
#include "MassExternalSubsystemTraits.h"
#include "MassEntityHandle.h"
#include "Engine/DeveloperSettings.h"
#include "BallisticsSubsystem.generated.h"

struct FProjectileTransform;

USTRUCT()
struct FProjectileBarrel
{
	GENERATED_BODY()

	float length;
	float twist_rate;
};

//https://github.com/getnamo/MassCommunitySample/tree/main?tab=readme-ov-file#44-subsystems
UCLASS()
class REALISTICBALLISTICS_API UBallisticsSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void SetGravity(FVector3f _gravity) {gravity = _gravity;};
	void SetAirDensity(float density) {air_density = density;};
	void SetWindVector(FVector3f _wind_vec) {wind_vector = _wind_vec;};
	void SetBarrel(const FProjectileBarrel& barrel) {current_barrel = barrel;};

	FVector3f GetGravity() const {return gravity;};
	float GetAirDensity() const {return air_density;};
	FVector3f GetWindVector() const {return wind_vector;};

	FMassEntityHandle Projectile(const FVector3f& proj_pos, const FVector3f& dir, float pressure, const struct FProjectileProperties& properties);
	FMassEntityHandle Projectile(const FVector3f& proj_pos, const FVector3f& dir, const struct FProjectileProperties& properties, float muzzle_vel);

	FProjectileTransform* GetProjectileTransform(FMassEntityHandle projectile);
	bool IsProjectileAlive(FMassEntityHandle projectile);

protected:
	// UWorldSubsystem begin interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// UWorldSubsystem end interface

private:
	UE_MT_DECLARE_RW_ACCESS_DETECTOR(AccessDetector);

	FProjectileBarrel current_barrel = {.length = 0.56f, .twist_rate = 0.254f};

	FVector3f gravity = FVector3f(0.f, 0, -9.81f);
	FVector3f wind_vector = FVector3f(0.f, 10.6f, 0.f);
	//sea level, 15 deg cel
	float air_density = 1.2250f;
};

template<>
struct TMassExternalSubsystemTraits<UBallisticsSubsystem> final
{
	enum
	{
		ThreadSafeRead = true,
		ThreadSafeWrite = false,
	};
};

//https://unreal-garden.com/tutorials/developer-settings/
UCLASS(Config = Game, DefaultConfig)
class REALISTICBALLISTICS_API UBallisticsProjectSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	UBallisticsProjectSettings(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, Config, Category = "Ballistics|Collision")
	TEnumAsByte<ECollisionChannel> projectile_trace_channel = ECC_Visibility;

	UPROPERTY(EditAnywhere, Config, Category ="Ballistics|Simulation")
	float sim_fixed_step = 1.f / 60.f;
	UPROPERTY(EditAnywhere, Config, Category = "Ballistics|Simulation")
	int32 penetration_substep_factor = 3;

	UPROPERTY(EditAnywhere, Config, Category = "Ballistics|Simulation")
	float destroy_projectile_below = -1000.f;
};