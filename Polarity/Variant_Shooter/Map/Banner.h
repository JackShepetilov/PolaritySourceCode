// Banner.h
// The thing you break to change what a place does.
//
// One verb, two consequences, and the consequence is not the banner's business. A banner standing
// in a headquarters means "this faction can still field its proper squads"; a banner standing on a
// point means "the prize here has not been opened yet". Break it and the point it belongs to
// decides what that costs: a faction drops to its weakened sorties, or the loot hits the floor.
//
// Keeping the meaning on the point rather than on the banner is what stops this growing a kind
// enum with a branch per case. A banner is a target with hit points; everything else is a place.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Banner.generated.h"

class APoiActor;
class ABannerActor;
class UStaticMeshComponent;
class UGeometryCollection;
class AGeometryCollectionActor;
class UNiagaraSystem;
class USoundBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBannerBroken, ABannerActor*, Banner);

UCLASS()
class POLARITY_API ABannerActor : public AActor
{
	GENERATED_BODY()

public:

	ABannerActor();

	/** Damage it takes before it goes. Small on purpose: the fight around a banner is the content,
	 *  the banner itself is a button with a health bar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Banner", meta = (ClampMin = "1.0"))
	float Health = 400.0f;

	/** Only players can break one.
	 *
	 *  Without this a stray rocket from a drone breaks its own side's banner and weakens the faction
	 *  that fired it, which reads as a bug no matter how it is explained. Breaking a banner is
	 *  something the team goes and does. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Banner")
	bool bOnlyPlayersCanBreak = true;

	UPROPERTY(BlueprintReadOnly, Category = "Banner")
	bool bBroken = false;

	/** Break it now, whatever the health says. Blueprint-callable so a charge, a channelled
	 *  interaction and a console command all end up in the same place. */
	UFUNCTION(BlueprintCallable, Category = "Banner")
	void Break(AActor* Breaker);

	/** How much of it is left, 0..1, for a bar or a shader. */
	UFUNCTION(BlueprintPure, Category = "Banner")
	float GetIntegrity() const;

	/** The point this banner stands on. Set by APoiActor on BeginPlay from its own Banner field, so
	 *  the link is authored by dragging the actor into the point and nowhere else.
	 *
	 *  Defined in the .cpp rather than inline: assigning a TWeakObjectPtr from a raw pointer needs
	 *  the complete type, and APoiActor is only forward-declared up here. Inline it and the failure
	 *  arrives as C2679 inside the engine's WeakObjectPtrTemplates.h, pointing nowhere near this
	 *  file. */
	void SetOwningPoi(APoiActor* Poi);

	// ==================== Coming apart ====================
	//
	// The recipe is ARewardContainer's, which is the one in this project that has been watched
	// working: hide the mesh, spawn a geometry collection in its place, shatter it with a strain
	// field, and kick the pieces one tick LATER because the bodies do not exist yet in the frame
	// the strain is applied.

	/** Geometry collection matching the banner's mesh. Null means the banner simply disappears, and
	 *  everything else about breaking it still happens. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Banner|Destruction")
	TObjectPtr<UGeometryCollection> BannerGC;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Banner|Destruction")
	FName GibCollisionProfile = FName("Ragdoll");

	/** Radial impulse on the pieces (velocity change, cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Banner|Destruction", meta = (ClampMin = "0.0"))
	float BreakImpulse = 700.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Banner|Destruction", meta = (ClampMin = "10.0"))
	float BreakRadius = 300.0f;

	/** Seconds the pieces lie around. Zero keeps them forever. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Banner|Destruction", meta = (ClampMin = "0.0"))
	float GibLifetime = 30.0f;

	/** Seconds before the pieces stop simulating. Zero leaves them live, which costs frames for the
	 *  rest of the run on a map with several broken banners. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Banner|Destruction", meta = (ClampMin = "0.0"))
	float GibFreezeTime = 4.0f;

	// ==================== Noise ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Banner|Effects")
	TObjectPtr<UNiagaraSystem> BreakVFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Banner|Effects", meta = (ClampMin = "0.1"))
	float BreakVFXScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Banner|Effects")
	TObjectPtr<USoundBase> BreakSound;

	UPROPERTY(BlueprintAssignable, Category = "Banner")
	FOnBannerBroken OnBroken;

protected:

	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

	/** A plain box so a banner is visible and shootable before anybody makes art for it. A Blueprint
	 *  subclass replaces the mesh and keeps everything else. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Put the geometry collection where the mesh was and shatter it. */
	void ShatterIntoGibs();

	/** Kick the pieces apart. Deliberately a tick late: the strain field creates the bodies, and an
	 *  impulse in the same frame lands on nothing. */
	void ApplyScatterImpulse();

	float MaxHealth = 400.0f;

	TWeakObjectPtr<APoiActor> OwningPoi;

	UPROPERTY(Transient)
	TObjectPtr<AGeometryCollectionActor> SpawnedGibs;

	FTimerHandle ScatterImpulseHandle;
	FTimerHandle GibFreezeHandle;
};
