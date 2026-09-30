// SiegeMechBay.h
// The farm's barn where the mech is put together: the end of the demo.
//
// A player walks into the trigger at the barn doors. If the team holds, between all of its players'
// inventories, one mech part of every slot (UMechPartDefinition, EInventorySlotKind::MechPart), those
// parts are taken out of the bags and the mech is assembled (author 2026-09-30: the demo ends with the
// mech's assembly). Where a slot is held more than once, the rarest part goes in.
//
// The bay only decides and tells: OnMechAssembled fires on every machine, and the end screen hangs off it
// in the Blueprint. Server decides; bAssembled and the parts used replicate.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SiegeMechBay.generated.h"

class UBoxComponent;
class UMechPartDefinition;
class UPrimitiveComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMechAssembled, const TArray<UMechPartDefinition*>&, Parts);

UCLASS()
class POLARITY_API ASiegeMechBay : public AActor
{
	GENERATED_BODY()

public:

	ASiegeMechBay();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: assemble now if the team has every slot. Returns true when the mech is (or already was)
	 *  assembled. Console: polarity.mech.assemble. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Mech")
	bool TryAssemble();

	UFUNCTION(BlueprintPure, Category = "Mech")
	bool IsAssembled() const { return bAssembled; }

	UFUNCTION(BlueprintPure, Category = "Mech")
	TArray<UMechPartDefinition*> GetAssembledParts() const;

	/** On every machine, once. The end of the demo hangs off this. */
	UPROPERTY(BlueprintAssignable, Category = "Mech")
	FOnMechAssembled OnMechAssembled;

	UFUNCTION(BlueprintImplementableEvent, Category = "Mech", meta = (DisplayName = "On Mech Assembled"))
	void BP_OnMechAssembled(const TArray<UMechPartDefinition*>& Parts);

	/** Server, when a player walks in without the full set: the slots still missing. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Mech", meta = (DisplayName = "On Assembly Refused"))
	void BP_OnAssemblyRefused(const TArray<FString>& MissingSlots);

protected:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> Trigger;

private:

	UFUNCTION()
	void OnTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnRep_Assembled();

	UPROPERTY(ReplicatedUsing = OnRep_Assembled)
	bool bAssembled = false;

	UPROPERTY(Replicated)
	TArray<TObjectPtr<UMechPartDefinition>> AssembledParts;

	bool bAnnounced = false;
};
