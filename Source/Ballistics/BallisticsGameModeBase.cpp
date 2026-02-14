// Fill out your copyright notice in the Description page of Project Settings.


#include "BallisticsGameModeBase.h"
#include "BallisticsTestPawn.h"

ABallisticsGameModeBase::ABallisticsGameModeBase()
{
	DefaultPawnClass = ABallisticsTestPawn::StaticClass();
}