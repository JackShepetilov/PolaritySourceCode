// SabotageTarget.h
// The one thing in a headquarters that is worth breaking.
//
// A headquarters is not captured. Four players besieging a base turns them into a fourth faction and
// the run into an unbroken siege; breaking one named object is quick, loud, and sits on the verbs
// the game already has (seminar, section 10). One target = one function the faction loses for the
// rest of the run.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Variant_Shooter/Map/MapEventTypes.h"
#include "SabotageTarget.generated.h"

class AFactionHq;
class ASabotageTarget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSabotageTargetBroken, ASabotageTarget*, Target);

UCLASS()
class POLARITY_API ASabotageTarget : public AActor
{
	GENERATED_BODY()

public:

	ASabotageTarget();

	/** Which function of the headquarters this object is. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sabotage")
	ESabotageKind Kind = ESabotageKind::Reinforcements;

	/** Damage it takes before it goes. Deliberately small: the fight around it is the content, the
	 *  object itself is a button with a health bar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sabotage", meta = (ClampMin = "1.0"))
	float Health = 500.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Sabotage")
	bool bBroken = false;

	/** Break it now, whatever the health says. Blueprint-callable so a channelled interaction, a
	 *  charge thrown at it or a console command all end up in the same place. */
	UFUNCTION(BlueprintCallable, Category = "Sabotage")
	void Break(AActor* Breaker);

	/** The headquarters this belongs to. Set by AFactionHq on BeginPlay from its own list, so the
	 *  link is authored by dragging the actor into the array and nowhere else. */
	void SetOwningHq(AFactionHq* Hq) { OwningHq = Hq; }

	UPROPERTY(BlueprintAssignable, Category = "Sabotage")
	FOnSabotageTargetBroken OnBroken;

protected:

	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

	TWeakObjectPtr<AFactionHq> OwningHq;
};
