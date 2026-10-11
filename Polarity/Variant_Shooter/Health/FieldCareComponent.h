// FieldCareComponent.h
// The two things a player does with a held key to get a bar back [author, 2026-10-11]:
//
//   a medkit     held for MedkitUseTime, gives health back. The medkit is a cell of the bag
//                (EInventorySlotKind::Medkit), so carrying the means to heal costs the run a cell.
//   strip armour held for StripTime next to a dead enemy that left some (AStrippableArmor), gives
//                armour back.
//
// Both are a hold: let the key go before the time is up and nothing happens, nothing is spent. While
// either runs the hands are busy: no firing, no aiming. Walking is free. Being hurt does not break
// it; it only means the time was spent in the open.
//
// Health no longer comes back by itself (regeneration is off by default), and nothing a fight drops
// heals in the middle of the fight. That is the point: a wound stays until the player stops to
// treat it.
//
// The server decides and does it; the owner is told through the replicated action. The owner's own
// press blocks its fire at once (a short prediction window), so a shot cannot slip out in the round
// trip.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FieldCareComponent.generated.h"

class AStrippableArmor;

UENUM(BlueprintType)
enum class EFieldCareAction : uint8
{
	None,
	Medkit,
	StripArmor
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFieldCareChanged, EFieldCareAction, Action, float, Duration);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFieldCareFinished, EFieldCareAction, Action, float, Amount);

UCLASS(ClassGroup = (Polarity), meta = (BlueprintSpawnableComponent))
class POLARITY_API UFieldCareComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UFieldCareComponent();

	// ==================== Tuning ====================

	/** How long the key is held for a medkit (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field Care|Medkit", meta = (ClampMin = "0.1", Units = "s"))
	float MedkitUseTime = 3.0f;

	/** Health one medkit gives back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field Care|Medkit", meta = (ClampMin = "1.0"))
	float MedkitHealAmount = 100.0f;

	/** How long the key is held to take the armour off a body (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field Care|Armor", meta = (ClampMin = "0.1", Units = "s"))
	float StripTime = 2.0f;

	/** How near the body has to be (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field Care|Armor", meta = (ClampMin = "50.0", Units = "cm"))
	float StripRange = 220.0f;

	// ==================== Doing it ====================

	/** Key down. Does nothing when there is no medkit, or health is full. */
	UFUNCTION(BlueprintCallable, Category = "Field Care")
	void BeginMedkit();

	/** Key down. Does nothing when no body with armour is near, or armour is full. */
	UFUNCTION(BlueprintCallable, Category = "Field Care")
	void BeginStripArmor();

	/** Key up, either key. Nothing is spent. */
	UFUNCTION(BlueprintCallable, Category = "Field Care")
	void Cancel();

	// ==================== Asking ====================

	/** Hands busy: the owner must not fire or aim. */
	UFUNCTION(BlueprintPure, Category = "Field Care")
	bool IsBusy() const;

	UFUNCTION(BlueprintPure, Category = "Field Care")
	EFieldCareAction GetAction() const { return Action; }

	/** 0 at key down, 1 when done. As this machine saw it start. */
	UFUNCTION(BlueprintPure, Category = "Field Care")
	float GetProgress() const;

	UFUNCTION(BlueprintPure, Category = "Field Care")
	int32 GetMedkitCount() const;

	UFUNCTION(BlueprintPure, Category = "Field Care")
	bool CanUseMedkit() const;

	/** The nearest body with armour within StripRange, or null. What the HUD prompt asks. */
	UFUNCTION(BlueprintPure, Category = "Field Care")
	AStrippableArmor* FindStrippableArmor() const;

	UFUNCTION(BlueprintPure, Category = "Field Care")
	bool CanStripArmor() const;

	/** The action began or ended, on every machine that knows about it. Duration 0 with None. */
	UPROPERTY(BlueprintAssignable, Category = "Field Care")
	FOnFieldCareChanged OnActionChanged;

	/** It ran to the end and gave Amount of health or armour. Server and owner. */
	UPROPERTY(BlueprintAssignable, Category = "Field Care")
	FOnFieldCareFinished OnFinished;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:

	void Begin(EFieldCareAction Wanted);

	UFUNCTION(Server, Reliable)
	void Server_Begin(EFieldCareAction Wanted);

	UFUNCTION(Server, Reliable)
	void Server_Cancel();

	UFUNCTION(Client, Reliable)
	void Client_Finished(EFieldCareAction Done, float Amount);

	UFUNCTION()
	void OnRep_Action();

	void StartOnServer(EFieldCareAction Wanted);
	void FinishOnServer();
	void SetAction(EFieldCareAction NewAction, float NewDuration);
	void FreeHands();
	bool IsOwnerLocal() const;

	UPROPERTY(ReplicatedUsing = OnRep_Action)
	EFieldCareAction Action = EFieldCareAction::None;

	UPROPERTY(Replicated)
	float Duration = 0.0f;

	/** When this machine saw the action start. */
	float LocalStartTime = 0.0f;

	/** The owner's press holds its own fire until the server has answered. */
	float PredictedBusyUntil = -1.0f;

	TWeakObjectPtr<AStrippableArmor> StripTarget;
	FTimerHandle FinishTimer;
};
