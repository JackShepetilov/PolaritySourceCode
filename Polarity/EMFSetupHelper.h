// EMFSetupHelper.h
// Helper functions for setting up EMF components with proper owner types

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EMFSetupHelper.generated.h"

class UEMFVelocityModifier;
class UEMF_FieldComponent;

/**
 * Blueprint function library for EMF setup utilities
 */
UCLASS()
class POLARITY_API UEMFSetupHelper : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Configure EMF components for a Player character
	 * - Sets owner type to Player
	 * - Ignores forces from NPC sources (NPCForceMultiplier = 0.0)
	 */
	UFUNCTION(BlueprintCallable, Category = "EMF|Setup")
	static void SetupPlayerEMF(UEMFVelocityModifier* EMFModifier, UEMF_FieldComponent* FieldComp);

	/**
	 * Configure EMF components for an NPC
	 * - Sets owner type to NPC
	 * - Ignores forces from other NPC sources (NPCForceMultiplier = 0.0)
	 */
	UFUNCTION(BlueprintCallable, Category = "EMF|Setup")
	static void SetupNPCEMF(UEMFVelocityModifier* EMFModifier, UEMF_FieldComponent* FieldComp);

	/**
	 * Configure EMF components for a Projectile
	 * - Sets owner type to Projectile
	 * - Reacts to all forces (all multipliers = 1.0)
	 */
	UFUNCTION(BlueprintCallable, Category = "EMF|Setup")
	static void SetupProjectileEMF(UEMFVelocityModifier* EMFModifier, UEMF_FieldComponent* FieldComp);
};
