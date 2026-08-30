// PoiActor.h
// A point of interest, as the map event layer sees it.
//
// This actor is the presence of a point in the world: where it is, how big it is, what garrison
// stands on it, what loot it puts down and which prize the war produces there. It deliberately does
// NOT own the state of the war on that point - that lives in URunDirectorSubsystem, keyed by
// PoiTag, because points live in streamed sublevels and a sublevel that unloads must not take the
// run with it.
//
// So: the actor reports presence (who is standing here right now), the director decides (who holds
// it, whether the mission window is open). Orders go down, events go up, the same rule the faction
// war architecture uses between its layers.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Variant_Shooter/Map/MapEventTypes.h"
#include "PoiActor.generated.h"

class USphereComponent;
class USquadLoadout;
class URunDirectorSubsystem;

/**
 * Editor-placed point of interest. One per meaningful place on the map: the three mission points,
 * the two headquarters, the final, and every plain loot point between them.
 */
UCLASS()
class POLARITY_API APoiActor : public AActor
{
	GENERATED_BODY()

public:

	APoiActor();

	// ==================== Identity ====================

	/** How everything else addresses this point: the director, missions, headquarters sorties and
	 *  console commands. Must be unique on the map. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "POI")
	FName PoiTag = NAME_None;

	/** Not called Role: AActor already has one, and it is the network role. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "POI")
	EPoiRole PoiRole = EPoiRole::Plain;

	/** Who holds it when the run starts. Neutral (255) means nobody, which is the right answer for
	 *  a plain point nobody bothers to garrison. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "POI", meta = (ClampMin = "0", ClampMax = "255"))
	uint8 StartingTeam = 255;

	/** Inside this radius a pawn counts as being on the point (cm). This is the fighting radius of
	 *  the place, not its art footprint: 40 m by default, which is longer than the longest shot the
	 *  POI research allows inside one. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "POI", meta = (ClampMin = "200.0"))
	float InfluenceRadius = 4000.0f;

	// ==================== Prize ====================

	/** What the war produces here that is worth taking. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "POI|Prize")
	EPoiPrize Prize = EPoiPrize::None;

	/** How long the prize survives after it appears (s). Zero means it lasts until the capture ends,
	 *  which is what PointPower does. Fast points and slow points are this number and nothing else. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "POI|Prize", meta = (ClampMin = "0.0"))
	float PrizeLifetimeSeconds = 0.0f;

	// ==================== Mission ====================

	/** Only read when Role is Mission. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "POI|Mission")
	EMissionKind MissionKind = EMissionKind::Elimination;

	/** What completing it pays into the final. Not power: see FFinalConditions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "POI|Mission")
	FFinalConditions MissionReward;

	/** One line for the HUD and the ping. All three missions are visible from the first minute, so
	 *  this text exists before anybody goes anywhere near the point. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "POI|Mission")
	FText MissionBrief;

	// ==================== Garrison ====================

	/** Who is standing here when the point first loads. Null means an empty point. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "POI|Garrison")
	TObjectPtr<USquadLoadout> GarrisonLoadout = nullptr;

	/** Garrison members are scattered within this radius (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "POI|Garrison", meta = (ClampMin = "100.0"))
	float GarrisonScatterRadius = 1500.0f;

	// ==================== Loot ====================

	/** What this point puts on the floor, once, the first time it loads. Money entries also feed the
	 *  budget audit: the whole map should carry a quarter to a third of the team's cells. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "POI|Loot")
	TArray<FPoiLootEntry> Loot;

	// ==================== Queries ====================

	/** Everything about this point the director is holding right now. Null before registration. */
	UFUNCTION(BlueprintPure, Category = "POI")
	bool GetWarState(FPoiWarState& OutState) const;

	UFUNCTION(BlueprintPure, Category = "POI")
	float GetInfluenceRadius() const { return InfluenceRadius; }

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Count who is standing here and hand the numbers to the director. Server only, and not every
	 *  frame: a point is a place, and places change slowly. */
	void ReportPresence(float DeltaSeconds);

	/** Put the garrison down. Runs once per run, guarded by the director's war state, so a sublevel
	 *  that unloads and loads again does not refill the point with fresh enemies. */
	void SpawnGarrisonOnce();

	/** Same rule as the garrison, for the loot. */
	void SpawnLootOnce();

	URunDirectorSubsystem* GetDirector() const;

	/** Editor gizmo for the influence radius; no collision, it only has to be visible. */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USphereComponent> InfluenceGizmo;

	/** Seconds since the last presence count. */
	float PresenceTimer = 0.0f;

	/** How often presence is counted (s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "POI|Advanced", meta = (ClampMin = "0.1"))
	float PresenceIntervalSeconds = 1.0f;
};
