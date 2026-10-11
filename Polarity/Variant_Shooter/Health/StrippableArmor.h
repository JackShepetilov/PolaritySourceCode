// StrippableArmor.h
// Armour left on a dead enemy, to be taken off it by hand.
//
// Health and armour are two bars that come back in different ways [author, 2026-10-11]: health from
// medkits, armour off the enemies and from the shop. Off the enemies means off the BODY: a player
// walks up to it and holds a key for a couple of seconds (UFieldCareComponent). So armour is not
// something a fight hands back in the middle of itself; it is what is collected after one.
//
// Two classes:
//   UStrippableArmorComponent  hung on any enemy blueprint the author wants to pay armour. When its
//                              owner dies it leaves an AStrippableArmor where the body fell.
//   AStrippableArmor           the thing on the ground. Holds the amount, goes when it is taken or
//                              when its time runs out. The enemy's own actor is destroyed soon after
//                              death, so the armour cannot live on it.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "StrippableArmor.generated.h"

class AShooterNPC;
class UTextRenderComponent;

UCLASS(Blueprintable)
class POLARITY_API AStrippableArmor : public AActor
{
	GENERATED_BODY()

public:

	AStrippableArmor();

	/** How much armour is here. Set by whoever left it, before BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_ArmorAmount, Category = "Armor")
	float ArmorAmount = 50.0f;

	/** How long it lies there before it is gone (s). 0: for ever. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Armor", meta = (ClampMin = "0.0", Units = "s"))
	float Lifetime = 180.0f;

	UFUNCTION(BlueprintPure, Category = "Armor")
	bool IsTaken() const { return bTaken; }

	/** Take all of it. Returns what was there; nothing the second time. Authority only. */
	float TakeArmor();

protected:

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Armor")
	TObjectPtr<USceneComponent> SceneRoot;

	/** A plain label until the HUD prompt and a mesh exist: what it is and how much. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Armor")
	TObjectPtr<UTextRenderComponent> Label;

private:

	UFUNCTION()
	void OnRep_ArmorAmount();

	void RefreshLabel();

	UPROPERTY(Replicated)
	bool bTaken = false;
};

UCLASS(ClassGroup = (Polarity), meta = (BlueprintSpawnableComponent))
class POLARITY_API UStrippableArmorComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UStrippableArmorComponent();

	/** What the body gives. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor", meta = (ClampMin = "0.0"))
	float ArmorAmount = 50.0f;

	/** The actor left on the ground. Empty: the plain AStrippableArmor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor")
	TSubclassOf<AStrippableArmor> ArmorClass;

	/** Leave the armour where the owner is now. Called by itself when an AShooterNPC owner dies; any
	 *  other owner calls it from its own death. Authority only, once. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Armor")
	void LeaveArmorHere();

protected:

	virtual void BeginPlay() override;

private:

	UFUNCTION()
	void HandleNPCDeath(AShooterNPC* DeadNPC);

	bool bLeft = false;
};
