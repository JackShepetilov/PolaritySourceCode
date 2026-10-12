// StrippableArmor.h
// Armour left on a dead enemy, to be taken off it by hand.
//
// Health and armour are two bars that come back in different ways [author, 2026-10-11]: health from
// medkits, armour off the enemies and from the shop. Off the enemies means off the BODY: a player
// walks up to it and holds a key for a couple of seconds (UFieldCareComponent). So armour is not
// something a fight hands back in the middle of itself; it is what is collected after one.
//
// How much is there depends on how the enemy was killed [author, 2026-10-12]: armour that was shot
// to pieces is worth a fraction of armour that was never touched (a kill through the head or a limb).
//
// Two classes:
//   UStrippableArmorComponent  hung on any enemy blueprint the author wants to pay armour. When its
//                              owner dies it leaves an AStrippableArmor where the body fell.
//   AStrippableArmor           the thing to take. Holds the amount, goes when it is taken or when
//                              its time runs out. An enemy that leaves a body (AShooterNPC::
//                              CorpseSeconds) carries it on the body and the body glows while the
//                              armour is still on it; otherwise it lies on the ground.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "StrippableArmor.generated.h"

class AShooterNPC;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMeshComponent;
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

	/** The body this armour is still on, when the enemy left one. Set before BeginPlay. */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Glow, Category = "Armor")
	TObjectPtr<AActor> Body;

	/** Worn by the body as its overlay material while the armour is on it. Scalar "Amount" is how
	 *  much of the full armour is left, 0..1. Set before BeginPlay. */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Glow, Category = "Armor")
	TObjectPtr<UMaterialInterface> BodyGlowMaterial;

	/** ArmorAmount as a part of what the enemy wore whole, 0..1. Set before BeginPlay. */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Glow, Category = "Armor")
	float ArmorFraction = 1.0f;

	UFUNCTION(BlueprintPure, Category = "Armor")
	bool IsTaken() const { return bTaken; }

	/** Take all of it. Returns what was there; nothing the second time. Authority only. */
	float TakeArmor();

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Armor")
	TObjectPtr<USceneComponent> SceneRoot;

	/** A plain label until the HUD prompt and a mesh exist: what it is and how much. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Armor")
	TObjectPtr<UTextRenderComponent> Label;

private:

	UFUNCTION()
	void OnRep_ArmorAmount();

	UFUNCTION()
	void OnRep_Glow();

	UFUNCTION()
	void OnRep_Taken();

	void RefreshLabel();

	/** Put the glow on the body, or take it off once the armour is gone. Every machine. */
	void RefreshBodyGlow();

	UMeshComponent* GetBodyMesh() const;

	UPROPERTY(ReplicatedUsing = OnRep_Taken)
	bool bTaken = false;

	/** The overlay this actor put on the body, so it only ever takes its own off. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GlowInstance;
};

UCLASS(ClassGroup = (Polarity), meta = (BlueprintSpawnableComponent))
class POLARITY_API UStrippableArmorComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UStrippableArmorComponent();

	/** What the body gives when its armour was never hit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor", meta = (ClampMin = "0.0"))
	float ArmorAmount = 50.0f;

	/** What part of ArmorAmount is left to take when the armour was shot to nothing. Between broken
	 *  and whole it goes by how much of the owner's armour pool (UShieldFieldComponent) is left. An
	 *  owner with no armour pool always gives ArmorAmount. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BrokenArmorFraction = 0.25f;

	/** The actor left behind. Empty: the plain AStrippableArmor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor")
	TSubclassOf<AStrippableArmor> ArmorClass;

	/** Overlay the dead body wears while its armour is still on it (scalar "Amount", 0..1). Only for
	 *  an owner that leaves a body (AShooterNPC::CorpseSeconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor")
	TObjectPtr<UMaterialInterface> BodyGlowMaterial;

	/** Bone of the body the armour rides on, so it stays with a body that slides or rolls. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor")
	FName BodyBone = FName("pelvis");

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
