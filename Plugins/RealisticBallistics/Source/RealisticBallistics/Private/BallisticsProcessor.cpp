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
	while (step_accumulator >= sim_dt && substeps < MAX_SUBSTEPS)
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

				if(projectile_transform.position.Z < ballistics_settings->destroy_projectile_below)
				{
					KillProjectile(context, i);
					DrawDebugSphere(world, static_cast<FVector>(projectile_transform.position), 5.f, 12, FColor::Green, false, 10.f, SDPG_Foreground);
					continue;
				}

				if (!projectile_hitdata.started_penetration)
				{
					//free flight of the projectile

					ProjectileIntegrateStep(dt, projectile_properties, projectile_transform, projectile_physdata, ballistics_sys);
					auto collision_data = ProjectileStep(i, projectile_transform, context, channel);

					//projectile_hitdata.total_penetration += collision_data.penetrated_depth;`

					switch(collision_data.desired_response)
					{
					
					case ProjectileStepResult::PENETRATE:
						//need to rewind and catch up
					{
						projectile_hitdata.started_penetration = true;

						projectile_transform.position = collision_data.entry_point + (-projectile_physdata.velocity.GetUnsafeNormal());
						float remaining_time = (1.f - collision_data.time_alpha) * dt;

						// we need to set the position of the projectile to the entry point and catch up to its remaining time
						float catchup_steps = remaining_time / penetration_dt;
						int real_steps = FMath::Floor(catchup_steps);
						float decimal_step = catchup_steps - static_cast<float>(real_steps);

						bool lodged = false;
						for (int step = 0; step < real_steps; step++)
						{
							ProjectileIntegrateStep(penetration_dt, projectile_properties, projectile_transform, projectile_physdata, ballistics_sys);
							auto substep_collision_data = ProjectileCollisionStep(i, projectile_transform, projectile_physdata, projectile_hitdata, ballistics_settings, context, channel);

							projectile_hitdata.total_penetration += substep_collision_data.penetrated_depth;

							if (!substep_collision_data.is_penetrating)
							{
								projectile_hitdata.started_penetration = false;
							}
							else
							{
								projectile_hitdata.started_penetration = true;
								ApplyPenetrationResistance(projectile_properties, projectile_physdata, substep_collision_data, *ballistics_settings);
							}

							//check for lodged
							bool kill_rule = projectile_physdata.velocity.SquaredLength() < 1.f && projectile_hitdata.total_penetration > UE_KINDA_SMALL_NUMBER;
							if (kill_rule)
							{
								lodged = true;
								KillProjectile(context, i);
								//DrawDebugSphere(world, static_cast<FVector>(projectile_transform.previous_position), 5.f, 12, FColor::Green, false, 10.f, SDPG_Foreground);
								break;
							}
						}
						if (decimal_step > UE_KINDA_SMALL_NUMBER && !lodged)
						{
							ProjectileIntegrateStep(decimal_step * penetration_dt, projectile_properties, projectile_transform, projectile_physdata, ballistics_sys);
							auto substep_collision_data = ProjectileCollisionStep(i, projectile_transform, projectile_physdata, projectile_hitdata, ballistics_settings, context, channel);

							projectile_hitdata.total_penetration += substep_collision_data.penetrated_depth;
							if (!substep_collision_data.is_penetrating)
							{
								projectile_hitdata.started_penetration = false;
							}
							else
							{
								projectile_hitdata.started_penetration = true;
								ApplyPenetrationResistance(projectile_properties, projectile_physdata, substep_collision_data, *ballistics_settings);
							}

							//check for lodged
							bool kill_rule = projectile_physdata.velocity.SquaredLength() < 1.f && projectile_hitdata.total_penetration > UE_KINDA_SMALL_NUMBER;
							if (kill_rule)
							{
								KillProjectile(context, i);
								DrawDebugSphere(world, static_cast<FVector>(projectile_transform.previous_position), 5.f, 12, FColor::Green, false, 10.f, SDPG_Foreground);
								break;
							}
						}

						
					}
					case ProjectileStepResult::DEAD: case ProjectileStepResult::IDLE:
						continue;
					}
				}
				else
				{
					//actively penetrating
					for (int32 substep = 0; substep < ballistics_settings->penetration_substep_factor; substep++)
					{
						
						ProjectileIntegrateStep(penetration_dt, projectile_properties, projectile_transform, projectile_physdata, ballistics_sys);
						auto collision_data = ProjectileCollisionStep(i, projectile_transform, projectile_physdata, projectile_hitdata, ballistics_settings, context, channel);

						if (collision_data.is_lodged)
						{
							break;
						}
						projectile_hitdata.total_penetration += collision_data.penetrated_depth;

						if (!collision_data.is_penetrating)
						{
							projectile_hitdata.started_penetration = false;
						}
						else
						{
							projectile_hitdata.started_penetration = true;
							ApplyPenetrationResistance(projectile_properties, projectile_physdata, collision_data, *ballistics_settings);
						}

						//check for lodged
						bool kill_rule = projectile_physdata.velocity.SquaredLength() < 1.f && projectile_hitdata.total_penetration > UE_KINDA_SMALL_NUMBER;
						if (kill_rule)
						{
							KillProjectile(context, i);
							DrawDebugSphere(world, static_cast<FVector>(projectile_transform.previous_position), 5.f, 12, FColor::Green, false, 10.f, SDPG_Foreground);
							break;
						}
					}
				}
			}
		});
}

UBallisticsProcessor::State UBallisticsProcessor::ComputeDerivative(const State& state, FVector3f acceleration)
{
	return State{ .pos = state.vel, .vel = acceleration };
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

	float air_density = ballistics_sys->GetAirDensity();

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
	FVector3f drag_accel = -air_density * cross_section_area * (form_factor * standard_cd) / (2.f * projectile_properties.mass) *
		relative_velocity.Length() * relative_velocity;

	projectile_transform.previous_position = projectile_transform.position;                         

	FVector3f i = relative_velocity.GetUnsafeNormal();
	float eff_yaw = FMath::Acos(FVector3f::DotProduct(projectile_transform.symmetry_axis, i));

	//GEngine->AddOnScreenDebugMessage(2, 1.f, FColor::White, FString::Printf(TEXT("%f"), FMath::RadiansToDegrees(delta)));

	const float lift_force_coeff = 1.f;
	float nonlinear_lift_coeff = lift_force_coeff + (lift_force_coeff * lift_force_coeff * lift_force_coeff) * (eff_yaw * eff_yaw);

	projectile_physdata.external_force += 0.5f * air_density * cross_section_area * nonlinear_lift_coeff * (relative_velocity.Length() * relative_velocity.Length()) * i.Cross(projectile_transform.symmetry_axis.Cross(i));

	FVector3f gravity_accel = ballistics_sys->GetGravity();
	FVector3f force_accel = projectile_physdata.external_force / projectile_properties.mass;
	projectile_physdata.acceleration = (drag_accel + gravity_accel + force_accel) * TO_UE_UNITS;

	//fixed step Rk4
	State current_state{ .pos = projectile_transform.position, .vel = projectile_physdata.velocity * TO_UE_UNITS };

	State k1_deriv = ComputeDerivative(current_state, projectile_physdata.acceleration);
	State k2_deriv = ComputeDerivative(current_state + k1_deriv * (dt / 2.f), projectile_physdata.acceleration);
	State k3_deriv = ComputeDerivative(current_state + k2_deriv * (dt / 2.f), projectile_physdata.acceleration);
	State k4_deriv = ComputeDerivative(current_state + k3_deriv * dt, projectile_physdata.acceleration);

	State new_state = current_state + (k1_deriv + k2_deriv * 2.f + k3_deriv * 2.f + k4_deriv) * (dt / 6.f);

	projectile_physdata.velocity = new_state.vel * UE_TO_METRIC_UNITS;
	projectile_transform.position = new_state.pos;

	if(projectile_physdata.velocity.ContainsNaN() || projectile_transform.position.ContainsNaN())
	{
		check(false);
	}

	projectile_transform.yaw = eff_yaw;
	 
	projectile_physdata.external_force = FVector3f(0.f);
	//projectile_physdata.external_energy_loss = 0.f;
}

UBallisticsProcessor::ProjectileCollisionStepResult UBallisticsProcessor::ProjectileCollisionStep(const int i, const FProjectileTransform& projectile_transform, const FProjectilePhysicsData& projectile_physdata, const FProjectileHitData& projectile_hitdata, const UBallisticsProjectSettings* ballistics_settings, FMassExecutionContext& context, ECollisionChannel channel)
{
	//optimizable due to added ProjectileStep

	auto world = GetWorld();

	FHitResult hit_result;
	FVector start = static_cast<FVector>(projectile_transform.previous_position);
	FVector end = static_cast<FVector>(projectile_transform.position);
	FCollisionQueryParams query_params(SCENE_QUERY_STAT_NAME_ONLY(ProjectileTrace));
	query_params.bReturnPhysicalMaterial = true;
	world->LineTraceSingleByChannel(hit_result, start, end, channel, query_params);

	//penetration of projectile this tick
	ProjectileCollisionStepResult return_data{ 0.f,0.f, false, false};

	if (hit_result.bBlockingHit)
	{
		FVector height_field_pos = hit_result.ImpactPoint;

		bool height_field_collision = hit_result.Component.IsValid() && hit_result.Component->IsA<ULandscapeHeightfieldCollisionComponent>();
		return_data.phys_mat = hit_result.PhysMaterial;

		if (!height_field_collision)
		{
			FHitResult reverse_hit;
			world->LineTraceSingleByChannel(reverse_hit, end, start, channel, query_params);

			if (hit_result.bStartPenetrating && reverse_hit.bStartPenetrating)
			{
				//projectile still in material this tick
				return_data.penetrated_depth = FVector::Dist(start, end) * UE_TO_METRIC_UNITS;
				return_data.is_penetrating = true;
				return_data.entry_point = static_cast<FVector3f>(hit_result.ImpactPoint);
				return_data.time_alpha = 1.f;

				//DrawDebugLine(world, start, end, FColor::Orange, false, 10.f, SDPG_Foreground);

				return return_data;
			}
			else if (!hit_result.bStartPenetrating && reverse_hit.bStartPenetrating)
			{
				//projectile entered material this tick
				return_data.penetrated_depth = FVector::Dist(hit_result.ImpactPoint, end) * UE_TO_METRIC_UNITS;
				return_data.is_penetrating = true;
				return_data.entry_point = static_cast<FVector3f>(hit_result.ImpactPoint);
				return_data.time_alpha = hit_result.Distance / FVector::Dist(start, end);

				//DrawDebugLine(world, hit_result.ImpactPoint, end, FColor::Orange, false, 10.f, SDPG_Foreground);
				return return_data;
			}
			else if (reverse_hit.bBlockingHit)
			{
				//projectile left material in this tick
				return_data.penetrated_depth = FVector::Dist(hit_result.ImpactPoint, reverse_hit.ImpactPoint) * UE_TO_METRIC_UNITS;
				return_data.is_penetrating = true;
				return_data.entry_point = static_cast<FVector3f>(hit_result.ImpactPoint);
				float dist = FVector::Dist(start, end);
				return_data.time_alpha = hit_result.Distance / dist;

				//DrawDebugLine(world, hit_result.ImpactPoint, reverse_hit.ImpactPoint, FColor::Orange, false, 10.f, SDPG_Foreground);
				////DrawDebugLine(world, reverse_hit.ImpactPoint, end, FColor::Red, false, 10.f);

				return return_data;
			}
		}
		else
		{
			end = height_field_pos;
			KillProjectile(context, i);
			DrawDebugSphere(world, height_field_pos, 5.f, 5, FColor::Green, false, 10.f, SDPG_Foreground);

			return return_data;
		}
	}


	//DrawDebugLine(world, start, end, FColor::Red, false, 10.f);
	return_data.penetrated_depth *= UE_TO_METRIC_UNITS;
	return return_data;
}

UBallisticsProcessor::ProjectileStepResult UBallisticsProcessor::ProjectileStep(const int proj_ent, const FProjectileTransform& projectile_transform, FMassExecutionContext& context, ECollisionChannel channel)
{
	auto world = GetWorld();

	FHitResult hit_result;
	FVector start = static_cast<FVector>(projectile_transform.previous_position);
	FVector end = static_cast<FVector>(projectile_transform.position);
	FCollisionQueryParams query_params(SCENE_QUERY_STAT_NAME_ONLY(ProjectileTrace));
	query_params.bReturnPhysicalMaterial = true;
	world->LineTraceSingleByChannel(hit_result, start, end, channel, query_params);

	ProjectileStepResult step_result{.desired_response = ProjectileStepResult::IDLE};

	if (hit_result.bBlockingHit)
	{
		step_result.normal = static_cast<FVector3f>(hit_result.Normal);

		bool height_field_collision = hit_result.Component.IsValid() && hit_result.Component->IsA<ULandscapeHeightfieldCollisionComponent>();
		if(height_field_collision)
		{
			FVector height_field_pos = hit_result.ImpactPoint;

			step_result.desired_response = ProjectileStepResult::DEAD;
			step_result.entry_point = static_cast<FVector3f>(hit_result.ImpactPoint);

			KillProjectile(context, proj_ent);

			DrawDebugSphere(world, height_field_pos, 5.f,12, FColor::Green, false, 10.f, SDPG_Foreground);
			end = height_field_pos;
			//DrawDebugLine(world, start, end, FColor::Red, false, 10.f);

			return step_result;
		}

		//TODO: choose whether to deflect or penetrate
		step_result.desired_response = ProjectileStepResult::PENETRATE;
		step_result.entry_point = static_cast<FVector3f>(hit_result.ImpactPoint);

		step_result.time_alpha = hit_result.Distance / FVector::Dist(start, end);

		end = hit_result.ImpactPoint;
		//DrawDebugLine(world, start, end, FColor::Red, false, 10.f);
		return step_result;
	}

	//DrawDebugLine(world, start, end, FColor::Red, false, 10.f);
	return step_result;
	
}

void UBallisticsProcessor::ApplyPenetrationResistance(const FProjectileProperties& projectile_properties, FProjectilePhysicsData& projectile_physdata, const ProjectileCollisionStepResult& collision_data, const UBallisticsProjectSettings& ballistics_settings)
{
	auto* ballistics_data = ballistics_settings.ballistics_material_data.Find(MakeSoftObjectPtr(collision_data.phys_mat.Get()));

	float surface_area = UE_PI * (projectile_properties.diameter * 0.5f) * (projectile_properties.diameter * 0.5f);
	float y_eff = ballistics_data->resistance; //resistance coeff
	float k = ballistics_data->tuning_coeff; //coeff multiplier

	//kg/m3
	float density = collision_data.phys_mat->Density * 1000.f;

	float resist_force = surface_area * (y_eff + k * density * projectile_physdata.velocity.SquaredLength());
	float energy_loss = resist_force * collision_data.penetrated_depth;

	//projectile_physdata.external_energy_loss += energy_loss;

	float speed_sq = projectile_physdata.velocity.SquaredLength();
	float new_speed_sq = FMath::Max(0.f, speed_sq - 2.f * energy_loss / projectile_properties.mass);
		
	float new_speed = FMath::Sqrt(new_speed_sq);


	float max_deg_dev = ballistics_data->max_penetration_dev_deg;

	FVector3f vel_normal = projectile_physdata.velocity.GetUnsafeNormal();
	FVector3f new_vel_dir = static_cast<FVector3f>(FMath::VRandCone(static_cast<FVector>(vel_normal), (1.f - new_speed / projectile_physdata.ref_fired_speed) * FMath::DegreesToRadians(max_deg_dev)));

	FVector3f final_velocity = new_vel_dir * new_speed;

	projectile_physdata.velocity = final_velocity;
}

void UBallisticsProcessor::KillProjectile(FMassExecutionContext& context, int i)
{
	context.Defer().DestroyEntity(context.GetEntity(i));
}
