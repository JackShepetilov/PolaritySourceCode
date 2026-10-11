// ScreenEdgeComponent.h
// Coloured glow around the edges of the local player's screen, for "something is running on you".

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ScreenEdgeComponent.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPostProcessComponent;

/**
 * Several effects can run at once, each under its own key and in its own colour (a regen and a
 * cooldown cut landing on the same kill). Each is one instance of a post-process material that
 * takes a vector "Color" and a scalar "Intensity".
 *
 * It owns a post-process volume of its own rather than borrowing the camera's settings: the
 * berserk filter drives the camera's blend weight and zeroes it whenever it is off, which would
 * switch anything else sitting there off with it.
 *
 * Local and cosmetic. Created on demand by Play(), on the locally controlled pawn only.
 */
UCLASS(ClassGroup = (UI), meta = (BlueprintSpawnableComponent))
class POLARITY_API UScreenEdgeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UScreenEdgeComponent();

	/** Find the component on Owner or make one. Null for anything but a locally controlled pawn. */
	UFUNCTION(BlueprintCallable, Category = "Screen Edge")
	static UScreenEdgeComponent* GetOrCreate(AActor* Owner);

	/** Start (or restart) the glow under Key. It holds for Duration and then fades over FadeTime. */
	UFUNCTION(BlueprintCallable, Category = "Screen Edge")
	void Play(FName Key, UMaterialInterface* Material, FLinearColor Color, float Duration, float FadeTime = 0.35f);

	/** Fade the glow under Key out now. */
	UFUNCTION(BlueprintCallable, Category = "Screen Edge")
	void Stop(FName Key, float FadeTime = 0.2f);

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FEdgeEffect
	{
		FName Key;
		TWeakObjectPtr<UMaterialInstanceDynamic> Material;
		float HoldLeft = 0.0f;
		float FadeTime = 0.35f;
		float Intensity = 0.0f;
		bool bFadingIn = true;
	};

	TArray<FEdgeEffect> Effects;

	/** Keeps the instances alive; the array above only looks at them. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Instances;

	UPROPERTY(Transient)
	TObjectPtr<UPostProcessComponent> Volume;

	void EnsureVolume();
};
