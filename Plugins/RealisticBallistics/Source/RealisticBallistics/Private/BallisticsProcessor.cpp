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
	auto ballistics_sys = GetWorld()->GetSubsystem<UBallisticsSubsystem>();		

	projectile_simulation_step.ForEachEntityChunk(entity_manager, context, [world = GetWorld(), drag_curve_table = drag_table, &ballistics_settings, &ballistics_sys](FMassExecutionContext& context)
	{
		auto projectile_transforms = context.GetMutableFragmentView<FProjectileTransform>();
		auto projectile_physdatas = context.GetMutableFragmentView<FProjectilePhysicsData>();
		auto projectile_hitdatas = context.GetMutableFragmentView<FProjectileHitData>();
		auto projectile_datas = context.GetFragmentView<FProjectileProperties>();

		int32 num_projectiles = context.GetNumEntities();

		for (int32 i = 0; i < num_projectiles; i++)
		{
			auto& projectile_transform = projectile_transforms[i];
			auto& projectile_physdata = projectile_physdatas[i];
			auto& projectile_hitdata = projectile_hitdatas[i];
			const auto& projectile_properties = projectile_datas[i];
			
			FRealCurve* cd_curve;
			switch(projectile_properties.drag_model)
			{
			case FProjectileProperties::G1:
			{
				cd_curve = drag_curve_table->FindCurveUnchecked(FName("DragTable.G1"));
				break;
			}
			case FProjectileProperties::G2:
			{
				cd_curve = drag_curve_table->FindCurveUnchecked(FName("DragTable.G2"));
				break;
			}
			case FProjectileProperties::G5:
			{
				cd_curve = drag_curve_table->FindCurveUnchecked(FName("DragTable.G5"));
				break;
			}
			case FProjectileProperties::G6:
			{
				cd_curve = drag_curve_table->FindCurveUnchecked(FName("DragTable.G6"));
				break;
			}
			case FProjectileProperties::G7:
			{
				cd_curve = drag_curve_table->FindCurveUnchecked(FName("DragTable.G7"));
				break;
			}
			case FProjectileProperties::G8:
			{
				cd_curve = drag_curve_table->FindCurveUnchecked(FName("DragTable.G8"));
				break;
			}
			case FProjectileProperties::SPHERE:
			{
				cd_curve = drag_curve_table->FindCurveUnchecked(FName("DragTable.Sphere"));
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
 			float cross_section_area = UE_PI*(projectile_properties.diameter*0.5f)*(projectile_properties.diameter*0.5f);

			// SD = m / A
			float cross_section_density = projectile_properties.mass / cross_section_area;

			// assume input BC is imperial 
			float si_bc = projectile_properties.ballistic_coefficient * BC_TO_SI;

			// i = SD / C
			float form_factor = cross_section_density / si_bc;

			FVector3f wind_velocity = ballistics_sys->GetWindVector();
			FVector3f relative_velocity = projectile_physdata.velocity - wind_velocity;

			// a = p pi r^2 SD/C Cstd / 2 m (can be simplified, but later)
			FVector3f drag_accel = - ballistics_sys->GetAirDensity() * cross_section_area * (form_factor * standard_cd) / (2.f * projectile_properties.mass) * 
				relative_velocity.Length() * relative_velocity;

			float dt = context.GetDeltaTimeSeconds();
			const ECollisionChannel channel = ballistics_settings->projectile_trace_channel;

			FHitResult hit_result;
			FVector start = static_cast<FVector>(projectile_transform.previous_position);
			FVector end = static_cast<FVector>(projectile_transform.position);
			FCollisionQueryParams query_params(SCENE_QUERY_STAT_NAME_ONLY(ProjectileTrace));
			query_params.bReturnPhysicalMaterial = true;
			world->LineTraceSingleByChannel(hit_result, start, end, channel, query_params);

			bool is_penetrating = hit_result.bStartPenetrating || projectile_hitdata.inside_nonvolume;
			//penetration of projectile this tick
			float penetrated_depth = 0.f;


			if(hit_result.bBlockingHit && !is_penetrating)
			{
				bool height_field_collision = hit_result.Component.IsValid() && hit_result.Component->IsA<ULandscapeHeightfieldCollisionComponent>();
				if(height_field_collision) 
				{
					context.Defer().DestroyEntity(context.GetEntity(i));
					DrawDebugSphere(world, hit_result.ImpactPoint, 100.f, 12, FColor::Green, false, 10.f);
				}

				FHitResult reverse_hit;
				world->LineTraceSingleByChannel(reverse_hit, end, start, channel, query_params);

				if(reverse_hit.bStartPenetrating)
				{
					//projectile still in material this tick
					penetrated_depth = FVector::Dist(hit_result.ImpactPoint, end);
					
					DrawDebugLine(world, hit_result.ImpactPoint, end, FColor::Orange, false, 10.f, SDPG_Foreground);
				}
				else
				{
					//projectile left material in same tick
					penetrated_depth = FVector::Dist(hit_result.ImpactPoint, reverse_hit.ImpactPoint);

					DrawDebugLine(world, hit_result.ImpactPoint, reverse_hit.ImpactPoint, FColor::Orange, false, 10.f, SDPG_Foreground);
				}
			}
			else if(is_penetrating)
			{
				penetrated_depth = FVector::Dist(start, end);

				FHitResult reverse_hit;
				world->LineTraceSingleByChannel(reverse_hit, end, start, channel, query_params);
				
				FVector p_end = end;

				if(reverse_hit.bBlockingHit && !reverse_hit.bStartPenetrating)
				{
					penetrated_depth = FVector::Dist(start, reverse_hit.ImpactPoint);
					p_end = reverse_hit.ImpactPoint;
				}

				DrawDebugLine(world, start, p_end, FColor::Orange, false, 10.f, SDPG_Foreground);
			}

			projectile_hitdata.total_penetration += penetrated_depth;

			DrawDebugLine(world, start, end, FColor::Red, false, 10.f, 0);

			FVector3f gravity_accel = ballistics_sys->GetGravity();

			projectile_physdata.acceleration = drag_accel + gravity_accel; //+ wind_accel;

			projectile_transform.previous_position = projectile_transform.position;
			//semi-implicit euler
			projectile_physdata.velocity += projectile_physdata.acceleration * dt;
			projectile_transform.position += (projectile_physdata.velocity * TO_UE_UNITS) * dt;
		}
	});
}
