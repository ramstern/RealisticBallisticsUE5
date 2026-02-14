// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SpectatorPawn.h"
#include "MassEntityHandle.h"
#include "BallisticsTestPawn.generated.h"

/**
 * 
 */

class UInputMappingContext;
class UInputAction;
class UCameraComponent;
struct FInputActionValue;
struct FProjectileTransform;

UCLASS()
class BALLISTICS_API ABallisticsTestPawn : public ASpectatorPawn
{
	GENERATED_BODY()
	
public:
	ABallisticsTestPawn();

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> imc_ballistics = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> ia_shoot = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> ia_zoom = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> ia_zoom_scroll = nullptr;

	UPROPERTY(EditAnywhere, Category = "Behaviour")
	bool follow_projectile = false;

	UPROPERTY(EditAnywhere, Category = "Behaviour")
	float fov_zoom;

	void Tick(float dt) override;

private:
	UCameraComponent* camera = nullptr;
	FMassEntityHandle current_proj;

	void OnShoot(const FInputActionValue& action);
	void OnZoom(const FInputActionValue& action);
	void OnZoomScroll(const FInputActionValue& action);
	void OnZoomEnd(const FInputActionValue& action);
};
