#include "BallisticsSubsystem.h"
#include "MassEntitySubsystem.h"
#include "Fragments/ProjectileFragments.h"
#include "MassSimulationSubsystem.h"

FMassEntityHandle UBallisticsSubsystem::Projectile(const FVector3f& proj_pos, const FVector3f& dir, float pressure, const FProjectileProperties& properties )
{
	float radius = properties.diameter * 0.5f;
	float base_area = PI * (radius*radius);

	// v = sqrt( (2 * L * A * P) / m )
	float muzzle_velocity = FMath::Sqrt((2.f * current_barrel.length * base_area * pressure) / properties.mass);

	return Projectile(proj_pos, dir, properties, muzzle_velocity);
}

FMassEntityHandle UBallisticsSubsystem::Projectile(const FVector3f& proj_pos, const FVector3f& dir, const FProjectileProperties& properties, float muzzle_vel)
{
	auto& entity_manager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();

	FProjectileTransform projectile_transform;
	projectile_transform.position = proj_pos;
	projectile_transform.previous_position = proj_pos;
	projectile_transform.yaw = FMath::DegreesToRadians(CalculateInitialYawDegrees(properties));
	FVector3f angled_dir = FRotator3f(projectile_transform.yaw, 0.f, 0.f).Quaternion() * dir;
	projectile_transform.symmetry_axis = angled_dir.RotateAngleAxisRad(FMath::FRandRange(0.f, TWO_PI), dir);

	DrawDebugLine(GetWorld(), static_cast<FVector>(proj_pos), static_cast<FVector>(proj_pos + projectile_transform.symmetry_axis * 100.f), FColor::White, false, 10.f);

	FProjectilePhysicsData projectile_physdata;
	projectile_physdata.velocity = dir * muzzle_vel;
	projectile_physdata.external_force = FVector3f(0.f);
	projectile_physdata.angular_spin = 2.f * UE_PI * (muzzle_vel / current_barrel.twist_rate);
	projectile_physdata.ref_fired_speed = muzzle_vel;

	FProjectileHitData projectile_hitdata;
	projectile_hitdata.started_penetration = false;
	projectile_hitdata.total_penetration = 0.f;
	

	const FMassEntityHandle entity = entity_manager.ReserveEntity();
	entity_manager.Defer().PushCommand<
		FMassCommandBuildEntity<
		FProjectileTransform,
		FProjectilePhysicsData,
		FProjectileHitData,
		FProjectileProperties
		>
	>(
		entity,
		MoveTemp(projectile_transform),
		MoveTemp(projectile_physdata),
		MoveTemp(projectile_hitdata),
		FProjectileProperties(properties)
	);

	return entity;
}

FProjectileTransform* UBallisticsSubsystem::GetProjectileTransform(FMassEntityHandle projectile)
{
	auto mass_sys = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	return mass_sys->GetEntityManager().GetFragmentDataPtr<FProjectileTransform>(projectile);
}

bool UBallisticsSubsystem::IsProjectileAlive(FMassEntityHandle projectile)
{
	auto mass_sys = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	return mass_sys->GetEntityManager().IsEntityValid(projectile);
}

void UBallisticsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UMassEntitySubsystem>();

	UWorld* World = GetWorld();
	UMassSimulationSubsystem* Sim = World ? World->GetSubsystem<UMassSimulationSubsystem>() : nullptr;

	UE_LOG(LogTemp, Warning, TEXT("World=%s IsGameWorld=%d Sim=%p Paused=%d"),
		*GetNameSafe(World),
		World ? World->IsGameWorld() : 0,
		Sim,
		Sim ? Sim->IsSimulationPaused() : -1);

	Super::Initialize(Collection);
}

void UBallisticsSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

float UBallisticsSubsystem::CalculateInitialYawDegrees(const FProjectileProperties& properties)
{
	float quality_scale = properties.quality;

	std::normal_distribution distribution_p{current_barrel.horizontal_mean_bias, current_barrel.yaw_stddev * quality_scale};
	float angle_of_attack = distribution_p(rand_gen);

	std::normal_distribution distribution_y{ current_barrel.horizontal_mean_bias, current_barrel.yaw_stddev * quality_scale};
	float angle_of_sideslip = distribution_y(rand_gen);

	float eff_yaw = FMath::Sqrt((angle_of_attack*angle_of_attack) + (angle_of_sideslip*angle_of_sideslip));
	return eff_yaw;
}

UBallisticsProjectSettings::UBallisticsProjectSettings(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	CategoryName = "Plugins";
	SectionName = "Realistic Ballistics";
}
