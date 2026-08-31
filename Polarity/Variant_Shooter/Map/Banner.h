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

	UPROPERTY(BlueprintAssignable, Category = "Banner")
	FOnBannerBroken OnBroken;

protected:

	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

	/** A plain box so a banner is visible and shootable before anybody makes art for it. A Blueprint
	 *  subclass replaces the mesh and keeps everything else. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	float MaxHealth = 400.0f;

	TWeakObjectPtr<APoiActor> OwningPoi;
};
