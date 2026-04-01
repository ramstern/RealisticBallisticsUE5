// Fill out your copyright notice in the Description page of Project Settings.


#include "BallisticsTestPawn.h"
#include "InputActionValue.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "MassEntitySubsystem.h"
#include "BallisticsSubsystem.h"
#include "Fragments/ProjectileFragments.h"
#include "Camera/CameraComponent.h"

ABallisticsTestPawn::ABallisticsTestPawn()
{
    camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    camera->SetupAttachment(RootComponent);
    camera->bUsePawnControlRotation = true;
}

void ABallisticsTestPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	
    UEnhancedInputComponent* eic = Cast<UEnhancedInputComponent>(PlayerInputComponent);
    if (!eic) return;

	if(ia_shoot) eic->BindAction(ia_shoot, ETriggerEvent::Triggered, this, &ABallisticsTestPawn::OnShoot);
    if(ia_zoom) 
    {
        eic->BindAction(ia_zoom, ETriggerEvent::Triggered, this, &ABallisticsTestPawn::OnZoom);
        eic->BindAction(ia_zoom_scroll, ETriggerEvent::Triggered, this, &ABallisticsTestPawn::OnZoomScroll);
        eic->BindAction(ia_zoom, ETriggerEvent::Completed, this, &ABallisticsTestPawn::OnZoomEnd);
    }

    APlayerController* pc = Cast<APlayerController>(GetController());
    if (!pc || !pc->IsLocalController() || !imc_ballistics)
        return;
    
    ULocalPlayer* lp = pc->GetLocalPlayer();
    if (!lp)
        return;
    
    UEnhancedInputLocalPlayerSubsystem* input_sys = lp->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();

    if (!input_sys)
        return;

    // Priority 0 is fine unless you want this to override other contexts.
    input_sys->AddMappingContext(imc_ballistics, 0);
}

void ABallisticsTestPawn::Tick(float dt)
{
    Super::Tick(dt);

    const ECollisionChannel channel = GetDefault<UBallisticsProjectSettings>()->projectile_trace_channel;

    FHitResult hit_result;
    FVector eye;
    FRotator rot;
    GetActorEyesViewPoint(eye, rot);
    auto ballistics_sys = GetWorld()->GetSubsystem<UBallisticsSubsystem>();

    if(follow_projectile)
    {
        if (ballistics_sys->IsProjectileAlive(current_proj))
        {
            auto proj_trans = ballistics_sys->GetProjectileTransform(current_proj);
            camera->SetWorldLocation(static_cast<FVector>(proj_trans->position));
        }
        else
        {
            camera->SetRelativeLocation(FVector(0.f));
        }
    }
    

    FVector3f start = static_cast<FVector3f>(eye + FVector(0.f, 0.f, -50.f));
    FVector3f dir = static_cast<FVector3f>(rot.Vector());

    FCollisionQueryParams query_params(SCENE_QUERY_STAT_NAME_ONLY(ProjectileTrace));
    GetWorld()->LineTraceSingleByChannel(hit_result, static_cast<FVector>(start), static_cast<FVector>(dir * 10000000.f),
        channel, query_params);


    GEngine->AddOnScreenDebugMessage(11, 1.f, FColor::Red, FString::Printf(TEXT("Distance: %f m"), hit_result.Distance * UE_TO_METRIC_UNITS));
}

void ABallisticsTestPawn::OnShoot(const FInputActionValue& action)
{
	auto ballistics_sys = GetWorld()->GetSubsystem<UBallisticsSubsystem>();

    FVector eye;
    FRotator rot;
    GetActorEyesViewPoint(eye, rot);

	FVector3f start = static_cast<FVector3f>(eye + FVector(0.f, 0.f, -5.f));
	FVector3f dir = static_cast<FVector3f>(rot.Vector());

    //.308 lapua 155 Scenar 
	FProjectileProperties projectile_properties;
	projectile_properties.diameter = 0.00782f;
	projectile_properties.length = 0.015f;
	projectile_properties.mass = 0.009f;
    projectile_properties.drag_model = FProjectileProperties::G7;
    projectile_properties.ballistic_coefficient = 0.236f;
    projectile_properties.quality = 1.f;

    for (size_t i = 0; i < 500; i++)
    {
        auto entity = ballistics_sys->Projectile(start + FVector3f(0.f, i * 1.f, 0.f), dir, projectile_properties, 900.f);
        current_proj = entity;
    }
}
void ABallisticsTestPawn::OnZoom(const FInputActionValue& action)
{
    camera->FieldOfView = fov_zoom;
}
void ABallisticsTestPawn::OnZoomScroll(const FInputActionValue& action)
{
    fov_zoom = FMath::Clamp(fov_zoom + action.Get<float>(), 2.f, 80.f);
}
void ABallisticsTestPawn::OnZoomEnd(const FInputActionValue& action)
{
    camera->FieldOfView = 90.f;
}