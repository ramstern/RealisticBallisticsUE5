#include "BallisticsSubsystem.h"
#include "MassEntitySubsystem.h"
#include "Fragments/ProjectileFragments.h"
#include "MassSimulationSubsystem.h"

FMassEntityHandle UBallisticsSubsystem::Projectile(const FVector3f& proj_pos, const FVector3f& dir, float barrel_length, float pressure, const FProjectileProperties& properties )
{
	float radius = properties.diameter * 0.5f;
	float base_area = PI * (radius*radius);

	// v = sqrt( (2 * L * A * P) / m )
	float muzzle_velocity = FMath::Sqrt((2.f * barrel_length * base_area * pressure) / properties.mass);

	return Projectile(proj_pos, dir, barrel_length, properties, muzzle_velocity);
}

FMassEntityHandle UBallisticsSubsystem::Projectile(const FVector3f& proj_pos, const FVector3f& dir, float barrel_length, const FProjectileProperties& properties, float muzzle_vel)
{
	auto& entity_manager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
	//const FMassEntityHandle ent_handle = entity_manager.ReserveEntity();
	TArray<FInstancedStruct, TInlineAllocator<3>> fragments;

	FInstancedStruct projectile_transform = FInstancedStruct::Make<FProjectileTransform>();
	auto& proj_transform = projectile_transform.GetMutable<FProjectileTransform>();
	proj_transform.position = proj_pos;
	proj_transform.previous_position = proj_pos;
	fragments.Add(MoveTemp(projectile_transform));

	FInstancedStruct projectile_physdata = FInstancedStruct::Make<FProjectilePhysicsData>();
	auto& physics_data = projectile_physdata.GetMutable<FProjectilePhysicsData>();
	physics_data.velocity = dir * muzzle_vel;
	fragments.Add(MoveTemp(projectile_physdata));

	FInstancedStruct projectile_hitdata = FInstancedStruct::Make<FProjectileHitData>();
	auto& hit_data = projectile_hitdata.GetMutable<FProjectileHitData>();
	hit_data.entered_nonvolume = false;
	hit_data.inside_nonvolume = false;
	hit_data.total_penetration = 0.f;
	fragments.Add(MoveTemp(projectile_hitdata));

	fragments.Add(FInstancedStruct::Make(properties));

	return entity_manager.CreateEntity(fragments);
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

UBallisticsProjectSettings::UBallisticsProjectSettings(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	CategoryName = "Plugins";
	SectionName = "Realistic Ballistics";
}
