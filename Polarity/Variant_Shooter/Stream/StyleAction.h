// StyleAction.h
// Lightweight event description registered with UStyleComponent on stylish gameplay moments.
// EStyleCategory drives both spectacle scoring (in UStreamConfig) and freshness tracking
// (repeated same-category actions lose value via FFreshnessFalloff).

#pragma once

#include "CoreMinimal.h"
#include "StyleAction.generated.h"

UENUM(BlueprintType)
enum class EStyleCategory : uint8
{
	None              UMETA(Hidden),
	Kill,                                  // Generic kill, no special qualifier
	Headshot,
	AirDashKill,                           // Killed while airborne after dash
	SlideKill,
	MeleeKill,
	Multikill,                             // Multiple kills in a short window
	ChainElectrify,                        // Chain-electrified ≥3 enemies
	YankKill,                              // Killed enemy after yanking their weapon/shield
	EnvironmentalKill,
	ParryReflect,                          // Killed attacker via reflected damage
	NoHitClear                             // Cleared a wave without taking damage
};

/** A discrete stylish event registered with UStyleComponent. */
USTRUCT(BlueprintType)
struct FStyleAction
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Style")
	EStyleCategory Category = EStyleCategory::None;

	/** Where in world the action happened. UI uses this to spawn hearts at the right screen position. */
	UPROPERTY(BlueprintReadWrite, Category = "Style")
	FVector WorldLocation = FVector::ZeroVector;

	/** Per-instance multiplier (e.g. multikill scales by N kills, chain-electrify by chain length). */
	UPROPERTY(BlueprintReadWrite, Category = "Style")
	float InstanceMultiplier = 1.0f;
};
