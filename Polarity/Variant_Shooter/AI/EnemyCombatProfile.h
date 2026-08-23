// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EnemyCombatProfile.generated.h"

class AShooterWeapon;

/**
 * What kind of enemy this is, as data.
 *
 * The behaviour tree is SHARED. Every shooter NPC in the game runs ST_ShooterNPC_Push, and a
 * StateTree stores its task properties in the asset, which means DuelDistance and PeekDuration and
 * the rest are one set of numbers for everybody running it. Tuning a shotgunner to fight at three
 * metres would have moved the sniper there too.
 *
 * So the per-class numbers live here instead, on an asset the NPC carries, and the tasks read them
 * on entry and override their own defaults. One tree, many classes. The alternative was a tree per
 * weapon, which multiplies every future behaviour change by the number of classes and, in this
 * project specifically, risks losing the bindings that cannot be read back out of a tree asset.
 *
 * Mirrors UPlayerClassDefinition on the player side, deliberately: "what is this fighter" is the
 * same kind of question for both teams and should look the same in the editor.
 *
 * Leave a profile off an NPC entirely and it behaves exactly as it did before this existed - every
 * default here matches the task default it overrides.
 */
UCLASS(BlueprintType)
class POLARITY_API UEnemyCombatProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:

	/** Shown in logs and debug draws. Not a gameplay name; the classes are identified by what they
	 *  carry, not by a string. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName ProfileName = NAME_None;

	// ==================== Equipment ====================

	/** Overrides AShooterNPC::WeaponClass when set. The point of putting it here rather than leaving
	 *  it on the blueprint is that a class IS its weapon: the rocketeer is the enemy who drops a
	 *  rocket launcher for your sniper, and splitting that fact across two assets invites a
	 *  juggernaut that fights like a juggernaut while dropping a pistol. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	TSubclassOf<AShooterWeapon> WeaponClass;

	/** Overrides the charge ceiling on this NPC's UEMFVelocityModifier when above zero, which is
	 *  the same thing as how much shield it has: charge fills up to the cap and the shield is gone
	 *  when it gets there, so a bigger cap is a longer fight. Zero leaves the component alone. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment", meta = (ClampMin = "0.0"))
	float ShieldCharge = 0.0f;

	// ==================== What this class is allowed to do ====================

	/** Whether this enemy closes distance at all. A rocketeer that pushes is a rocketeer that dies
	 *  to its own splash and hands its launcher to the nearest player; it should be trading from
	 *  cover at range and moving between corners, never charging.
	 *
	 *  Implemented by the push task refusing on entry, so no tree surgery is needed: the state fails
	 *  and TrySelectChildrenInOrder moves on to the next sibling, which is the peek. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour")
	bool bCanPush = true;

	/** Whether the charge phase of a push actually sprints. The juggernaut wants a slow, inevitable
	 *  advance rather than a rush, but must still be able to sprint when RELOCATING under covering
	 *  fire, and those are two different code paths - so this gates only the push. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour")
	bool bSprintWhenPushing = true;

	/** Below this distance to the target the weapon is not fired at all. Zero disables the rule.
	 *
	 *  Exists for explosives: a rocket launched at a target this close catches its owner in the
	 *  blast. It is a fire inhibitor and NOT a movement rule, so an enemy pinned at close range
	 *  still moves, still takes cover and still relocates, it simply holds fire until the range
	 *  opens - which reads as an enemy backing off a weapon it cannot use. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour", meta = (ClampMin = "0.0"))
	float MinFireDistance = 0.0f;

	/** Shell the last place the target was seen when there is currently no line to it.
	 *
	 *  The grenadier's whole reason to exist. Everything else in the roster punishes standing in the
	 *  open; this punishes standing in the SAME PLACE, because a player who peeks twice from one
	 *  corner is peeking into a grenade that was already on its way. Last-seen rather than a
	 *  predicted intercept on purpose: players return to the corner that worked, so the position
	 *  they last fired from is a better guess than any extrapolation of their velocity, and it costs
	 *  nothing to know. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour")
	bool bFireAtLastSeenWhenBlind = false;

	// ==================== Peek rhythm ====================

	/** Take the peek numbers below instead of the ones stored in the tree. Off means this class uses
	 *  whatever the shared tree says, which is what every enemy did before profiles existed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Peek")
	bool bOverridePeekTuning = false;

	/** Minimum seconds behind cover before stepping out. The floor only: an NPC waiting for a
	 *  magazine or a burst cooldown stays down longer. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Peek", meta = (EditCondition = "bOverridePeekTuning", ClampMin = "0.0"))
	float HideDuration = 1.5f;

	/** Seconds held at the peek point before withdrawing. Small values are the point for a fast
	 *  round trip: firing happens across the whole exposed stretch, not only while standing still. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Peek", meta = (EditCondition = "bOverridePeekTuning", ClampMin = "0.1"))
	float PeekDuration = 2.0f;

	/** Abandon this corner after a burst and go find another one, instead of settling into the
	 *  hide/peek rhythm from the same spot.
	 *
	 *  This is the rocketeer, and it needs no separate task: shoot, leave, reload on the way, arrive
	 *  somewhere else, shoot again. What makes it hard to pin down is that the corner is never used
	 *  twice in a row, so learning where it shot from teaches you nothing about where it is now. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Peek", meta = (EditCondition = "bOverridePeekTuning"))
	bool bRelocateAfterFiring = false;

	/** Floor on how long one corner is used before bRelocateAfterFiring may abandon it.
	 *
	 *  Without it, an arena with few corners turns the rocketeer into a metronome bouncing between
	 *  the same two spots every time its weapon comes up. The floor is what makes leaving a decision
	 *  rather than a reflex. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Peek", meta = (EditCondition = "bOverridePeekTuning", ClampMin = "0.0"))
	float MinCornerSeconds = 3.0f;

	// ==================== Push distances ====================

	/** Take the push distances below instead of the ones stored in the tree. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Push")
	bool bOverridePushTuning = false;

	/** Beyond this it runs at the target; inside it, it fights. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Push", meta = (EditCondition = "bOverridePushTuning", ClampMin = "0.0"))
	float SprintDistance = 2000.0f;

	/** The ring the charge aims at and the range the trade happens at. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Push", meta = (EditCondition = "bOverridePushTuning", ClampMin = "0.0"))
	float DuelDistance = 600.0f;

	/** How long one enemy holds the front before rotating out. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Push", meta = (EditCondition = "bOverridePushTuning", ClampMin = "0.0"))
	float DuelDuration = 6.0f;
};
