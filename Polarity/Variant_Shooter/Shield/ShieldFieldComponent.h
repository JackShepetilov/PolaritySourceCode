// ShieldFieldComponent.h
// The enemy's shield, as a thing of its own.
//
// PHASE 0 OF THE SHIELD REWORK (Docs/Shield_Field_Rework_Plan_2026-09-22.md). Today this component
// owns no state of its own: every getter answers from the EMF charge meter, which is exactly what
// the rest of the game reads today, so routing the gates through here changes nothing yet. What it
// buys is ONE door - the weapon's damage gate, the AI's push/peek condition and (Phase 1) the HUD
// all ask the same question, and the pool of real shield HP lands behind that door without any of
// them being touched again.
//
// THE INVARIANT THIS FILE EXISTS FOR: shield state flows ONE WAY, shield -> charge. The shield may
// tell an UEMFVelocityModifier how polarised its owner has become (Phase 2); it must never read the
// charge meter back as its own state. Until Phase 2 it does exactly that, inside ResolveChargeSource,
// which is the one function that dies then.
//
// Deliberately no EMF includes in this header: the plugin stays out of the shield's face, and the
// temporary adapter that needs it lives in the .cpp.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ShieldFieldComponent.generated.h"

class UEMFVelocityModifier;
class AActor;

UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class POLARITY_API UShieldFieldComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UShieldFieldComponent();

	// ==================== The questions the game asks ====================

	/** True while the owner still has a shield to hide behind.
	 *
	 *  This is the single source of truth. AShooterWeapon::IsTargetShieldDown, the AI's push/peek
	 *  condition, the impact-surface choice and the hit feedback all come through here instead of
	 *  each reading a charge of their own, which is what let "may I hurt it" and "should I stop
	 *  pushing" drift apart in the first place. */
	UFUNCTION(BlueprintPure, Category = "Shield")
	bool IsShieldUp() const;

	/** The same answer, spelled out for the call sites that ask it that way. */
	UFUNCTION(BlueprintPure, Category = "Shield")
	bool IsBroken() const { return !IsShieldUp(); }

	/** How much of the shield is still there: 1 = whole, 0 = none. This is the number a bar draws. */
	UFUNCTION(BlueprintPure, Category = "Shield")
	float GetShieldFraction() const { return 1.0f - GetStrippedFraction(); }

	/** How much of the shield is GONE: 0 = whole, 1 = stripped. The shape the AI's recovered
	 *  fraction and the class passives ("more reach the more it is stripped") are written in. */
	UFUNCTION(BlueprintPure, Category = "Shield")
	float GetStrippedFraction() const;

protected:
	/** The one EMF dependency in this component, and it is temporary.
	 *
	 *  PHASE 0/1 ONLY: the shield is mirrored off the charge meter so that building it changes no
	 *  behaviour. Phase 2 deletes this function and its callers, and the shield stops reading charge
	 *  at all. It lives in the .cpp on purpose, so this header needs no EMF include. */
	UEMFVelocityModifier* ResolveChargeSource() const;
};

/**
 * One door for "has this actor got a shield up".
 *
 * An actor carrying a UShieldFieldComponent answers for itself. Everyone else - a player before it
 * gets one, a physics prop, a bare charge carrier - keeps the answer the game had before the field
 * existed, and that answer still lives at the call site (AShooterWeapon::IsTargetShieldDown's own
 * branches), deliberately NOT here: those targets have no shield state to report, and pretending
 * otherwise is how a crate ends up with a shield bar.
 */
UCLASS()
class POLARITY_API UShieldFieldStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The shield field on this actor, or null when it has none. */
	UFUNCTION(BlueprintPure, Category = "Shield")
	static UShieldFieldComponent* GetShieldField(const AActor* Actor);

	/** True while this actor still has a shield.
	 *
	 *  FALSE FOR AN ACTOR WITH NO FIELD, and that means "there is no shield state here", not "the
	 *  shield is down": a chargeless target is freely hurtable, and the callers that must treat it
	 *  that way (the weapon gate, the AI condition) keep their own rule for such actors. */
	UFUNCTION(BlueprintPure, Category = "Shield")
	static bool IsShieldUp(const AActor* Actor);
};
