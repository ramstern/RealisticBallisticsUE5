// Fill out your copyright notice in the Description page of Project Settings.


#include "BallisticsProcessor.h"
#include "BallisticsSubsystem.h"
#include "Fragments/ProjectileFragments.h"
#include "MassExecutionContext.h"
#include "MassCommonTypes.h"
#include "LandscapeHeightfieldCollisionComponent.h"

UBallisticsProcessor::UBallisticsProcessor()
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);
	ProcessingPhase = EMassProcessingPhase::PrePhysics;
	bRequiresGameThreadExecution = true;
	QueryBasedPruning = EMassQueryBasedPruning::Never;

	drag_table = ConstructorHelpers::FObjectFinder<UCurveTable>(TEXT("CurveTable'/RealisticBallistics/drag_tables.drag_tables'")).Object;
}

void UBallisticsProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& entity_manager)
{
	projectile_simulation_step.Initialize(entity_manager);

	projectile_simulation_step.AddRequirement<FProjectileProperties>(EMassFragmentAccess::ReadOnly);
	projectile_simulation_step.AddRequirement<FProjectileTransform>(EMassFragmentAccess::ReadWrite);
	projectile_simulation_step.AddRequirement<FProjectilePhysicsData>(EMassFragmentAccess::ReadWrite);
	projectile_simulation_step.AddRequirement<FProjectileHitData>(EMassFragmentAccess::ReadWrite);

	projectile_simulation_step.RegisterWithProcessor(*this);
}

void UBallisticsProcessor::Execute(FMassEntityManager& entity_manager, FMassExecutionContext& context)
{
	const auto ballistics_settings = GetDefault<UBallisticsProjectSettings>();
	float sim_dt = ballistics_settings->sim_fixed_step;

	int32 substeps = 0;
	step_accumulator += context.GetDeltaTimeSeconds();
	while(step_accumulator >= sim_dt && substeps < MAX_SUBSTEPS)
	{
		step_accumulator -= sim_dt;
		TickBallistics(context, sim_dt);
		++substeps;
	}
}

void UBallisticsProcessor::TickBallistics(FMassExecutionContext& context, float dt)
{
	auto ballistics_settings = GetDefault<UBallisticsProjectSettings>();
	auto ballistics_sys = GetWorld()->GetSubsystem<UBallisticsSubsystem>();
	projectile_simulation_step.ForEachEntityChunk(context,
		[this, &ballistics_settings, &ballistics_sys, &dt]
		(FMassExecutionContext& context)
		{
			auto projectile_transforms = context.GetMutableFragmentView<FProjectileTransform>();
			auto projectile_physdatas = context.GetMutableFragmentView<FProjectilePhysicsData>();
			auto projectile_hitdatas = context.GetMutableFragmentView<FProjectileHitData>();
			auto projectile_datas = context.GetFragmentView<FProjectileProperties>();

			const int32 num_projectiles = context.GetNumEntities();
			const auto world = GetWorld();
			const float penetration_dt = dt * (1.f / ballistics_settings->penetration_substep_factor);
			const ECollisionChannel channel = ballistics_settings->projectile_trace_channel;

			for (int32 i = 0; i < num_projectiles; i++)
			{
				auto& projectile_transform = projectile_transforms[i];
				auto& projectile_physdata = projectile_physdatas[i];
				auto& projectile_hitdata = projectile_hitdatas[i];
				const auto& projectile_properties = projectile_datas[i];

				if (projectile_hitdata.started_penetration)
				{
					for (int32 substep = 0; substep < ballistics_settings->penetration_substep_factor; substep++)
					{
						ProjectileIntegrateStep(penetration_dt, projectile_properties, projectile_transform, projectile_physdata, ballistics_sys);
						auto collision_data = ProjectileCollisionStep(i, projectile_transform, context, channel);

						projectile_hitdata.total_penetration += collision_data.penetrated_depth;

						if (!collision_data.is_penetrating)
						{
							projectile_hitdata.started_penetration = false;
							break;
						}
						else 
						{
							// do something with the projectile 
							projectile_physdata.external_force += static_cast<FVector3f>(FMath::VRand()) * 100.f;
							projectile_physdata.external_force += -0.5f * (projectile_physdata.velocity);
						}
					}
				}
				else
				{
					ProjectileIntegrateStep(dt, projectile_properties, projectile_transform, projectile_physdata, ballistics_sys);
					auto collision_data = ProjectileCollisionStep(i, projectile_transform, context, channel);

					projectile_hitdata.total_penetration += collision_data.penetrated_depth;

					if (!projectile_hitdata.started_penetration && collision_data.penetrated_depth > FLT_EPSILON)
					{
						projectile_hitdata.started_penetration = true;
						projectile_transform.position = collision_data.entry_point;

						projectile_hitdata.total_penetration -= collision_data.penetrated_depth;

						float vel_mag = projectile_physdata.velocity.Length();
						int catchup_steps = FMath::Max(collision_data.penetrated_depth / (vel_mag * penetration_dt), 1);


						for (int step = 0; step < catchup_steps; step++)
						{
							ProjectileIntegrateStep(penetration_dt, projectile_properties, projectile_transform, projectile_physdata, ballistics_sys);
							auto substep_collision_data = ProjectileCollisionStep(i, projectile_transform, context, channel);

							projectile_hitdata.total_penetration += substep_collision_data.penetrated_depth;
							if(!substep_collision_data.is_penetrating)
							{
								projectile_hitdata.started_penetration = false;
								break;
							}
							else 
							{
								projectile_physdata.external_force += static_cast<FVector3f>(FMath::VRand()) * 100.f;
								projectile_physdata.external_force += - 0.5f * (projectile_physdata.velocity);
							}

						}
						continue;
					}
				}
			}
		});
}

void UBallisticsProcessor::ProjectileIntegrateStep(float dt,
	const FProjectileProperties& projectile_properties, FProjectileTransform& projectile_transform, FProjectilePhysicsData& projectile_physdata,
	const UBallisticsSubsystem* ballistics_sys)
{
	FRealCurve* cd_curve;
	switch (projectile_properties.drag_model)
	{
	case FProjectileProperties::G1:
	{
		cd_curve = drag_table->FindCurveUnchecked(FName("DragTable.G1"));
		break;
	}
	case FProjectileProperties::G2:
	{
		cd_curve = drag_table->FindCurveUnchecked(FName("DragTable.G2"));
		break;
	}
	case FProjectileProperties::G5:
	{
		cd_curve = drag_table->FindCurveUnchecked(FName("DragTable.G5"));
		break;
	}
	case FProjectileProperties::G6:
	{
		cd_curve = drag_table->FindCurveUnchecked(FName("DragTable.G6"));
		break;
	}
	case FProjectileProperties::G7:
	{
		cd_curve = drag_table->FindCurveUnchecked(FName("DragTable.G7"));
		break;
	}
	case FProjectileProperties::G8:
	{
		cd_curve = drag_table->FindCurveUnchecked(FName("DragTable.G8"));
		break;
	}
	case FProjectileProperties::SPHERE:
	{
		cd_curve = drag_table->FindCurveUnchecked(FName("DragTable.Sphere"));
		break;
	}
	default:
		cd_curve = nullptr;
	}

	// M
	float mach_number = projectile_physdata.velocity.Length() / SOUND_SPEED;

	// Cd standardized
	float standard_cd = cd_curve->Eval(mach_number);

	// A = pi r^2
	float cross_section_area = UE_PI * (projectile_properties.diameter * 0.5f) * (projectile_properties.diameter * 0.5f);

	// SD = m / A
	float cross_section_density = projectile_properties.mass / cross_section_area;

	// assume input BC is imperial 
	float si_bc = projectile_properties.ballistic_coefficient * BC_TO_SI;

	// i = SD / C
	float form_factor = cross_section_density / si_bc;

	FVector3f wind_velocity = ballistics_sys->GetWindVector();
	FVector3f relative_velocity = projectile_physdata.velocity - wind_velocity;

	// a = p pi r^2 SD/C Cstd / 2 m (can be simplified, but later)
	FVector3f drag_accel = -ballistics_sys->GetAirDensity() * cross_section_area * (form_factor * standard_cd) / (2.f * projectile_properties.mass) *
		relative_velocity.Length() * relative_velocity;

	projectile_transform.previous_position = projectile_transform.position;

	//DrawDebugPoint(GetWorld(), static_cast<FVector>(projectile_transform.position), 3.f, FColor::Red, false, 10.f, SDPG_Foreground);

	FVector3f gravity_accel = ballistics_sys->GetGravity();
	FVector3f force_accel = projectile_physdata.external_force / projectile_properties.mass;
	projectile_physdata.acceleration = drag_accel + gravity_accel + force_accel;
	//semi-implicit euler
	projectile_physdata.velocity += projectile_physdata.acceleration * dt;
	projectile_transform.position += (projectile_physdata.velocity * TO_UE_UNITS) * dt;

	projectile_physdata.external_force = FVector3f(0.f);
}

UBallisticsProcessor::ProjectileCollisionStepResult UBallisticsProcessor::ProjectileCollisionStep(const int i, const FProjectileTransform& projectile_transform, FMassExecutionContext& context, ECollisionChannel channel)
{
	auto world = GetWorld();

	FHitResult hit_result;
	FVector start = static_cast<FVector>(projectile_transform.previous_position);
	FVector end = static_cast<FVector>(projectile_transform.position);
	FCollisionQueryParams query_params(SCENE_QUERY_STAT_NAME_ONLY(ProjectileTrace));
	query_params.bReturnPhysicalMaterial = true;
	world->LineTraceSingleByChannel(hit_result, start, end, channel, query_params);

	DrawDebugLine(world, start, end, FColor::Red, false, 10.f, 0);

	//penetration of projectile this tick
	ProjectileCollisionStepResult return_data{0.f, false};

	if (hit_result.bBlockingHit)
	{
		FVector height_field_pos = hit_result.ImpactPoint;

		bool height_field_collision = hit_result.Component.IsValid() && hit_result.Component->IsA<ULandscapeHeightfieldCollisionComponent>();

		if (!height_field_collision)
		{
			FHitResult reverse_hit;
			world->LineTraceSingleByChannel(reverse_hit, end, start, channel, query_params);

			bool is_height_field = reverse_hit.Component.IsValid() && reverse_hit.Component->IsA<ULandscapeHeightfieldCollisionComponent>();
			if (is_height_field)
			{
				//we might have hit the underside of heightfield after a penetration of a normal object
				query_params.AddIgnoredComponent(reverse_hit.Component);
				world->LineTraceSingleByChannel(reverse_hit, end, start, channel, query_params);
				query_params.ClearIgnoredComponents();
			}

			if (hit_result.bStartPenetrating && reverse_hit.bStartPenetrating)
			{
				//projectile still in material this tick
				return_data.penetrated_depth = FVector::Dist(start, end);
				return_data.is_penetrating = true;
				return_data.entry_point = static_cast<FVector3f>(hit_result.ImpactPoint);

				DrawDebugLine(world, start, end, FColor::Orange, false, 10.f, SDPG_Foreground);
			}
			else if (!hit_result.bStartPenetrating && reverse_hit.bStartPenetrating)
			{
				//projectile entered material this tick
				return_data.penetrated_depth = FVector::Dist(hit_result.ImpactPoint, end);
				return_data.is_penetrating = true;
				return_data.entry_point = static_cast<FVector3f>(hit_result.ImpactPoint);

				DrawDebugLine(world, hit_result.ImpactPoint, end, FColor::Orange, false, 10.f, SDPG_Foreground);
			}
			else if (reverse_hit.bBlockingHit)
			{
				//projectile left material in this tick
				return_data.penetrated_depth = FVector::Dist(hit_result.ImpactPoint, reverse_hit.ImpactPoint);
				return_data.is_penetrating = false;
				return_data.entry_point = static_cast<FVector3f>(hit_result.ImpactPoint);

				DrawDebugLine(world, hit_result.ImpactPoint, reverse_hit.ImpactPoint, FColor::Orange, false, 10.f, SDPG_Foreground);

				FVector start_p = reverse_hit.ImpactPoint + (end - start).GetUnsafeNormal() * 0.01f;
				FHitResult rest_hit;
				world->LineTraceSingleByChannel(rest_hit, start_p, end, channel, query_params);

				height_field_collision |= rest_hit.Component.IsValid() && rest_hit.Component->IsA<ULandscapeHeightfieldCollisionComponent>();
				height_field_pos = rest_hit.ImpactPoint;

				if (height_field_collision)
				{
					context.Defer().DestroyEntity(context.GetEntity(i));
					DrawDebugSphere(world, height_field_pos, 100.f, 12, FColor::Green, false, 10.f);
				}

			}
		}
		else
		{
			context.Defer().DestroyEntity(context.GetEntity(i));
			DrawDebugSphere(world, height_field_pos, 100.f, 12, FColor::Green, false, 10.f);
		}
	}
	return_data.penetrated_depth *= UE_TO_METRIC_UNITS;
	return return_data;
}
