// AimGlintComponent.h
// The glint of a scope that is being held on you.
//
// An enemy with a gun that has AShooterWeapon::AIAimHoldSeconds holds it on its target before the
// shot. This is what the target sees in that time: a white star where the scope is, growing and
// turning slowly, at its brightest on the shot [author, 2026-10-12: "a glint, as in Battlefield;
// brighter toward the shot, and let it turn a little"].
//
// As in Battlefield, it is seen only by whoever the gun is pointed at: the brightness falls to
// nothing a few degrees off the line of the barrel. So it is worked out on each machine for the
// player at that machine's screen, and nothing about it travels: the hold itself does
// (AShooterWeapon::Multicast_AIAimHold), the rest is local and cosmetic.
//
// Put on a weapon blueprint to tune it, or leave it off: a weapon with AIAimGlintMaterial set makes
// one with these defaults the first time it is held.

#pragma once

#include "CoreMinimal.h"
#include "Components/MaterialBillboardComponent.h"
#include "AimGlintComponent.generated.h"

class AShooterWeapon;
class UMaterialInstanceDynamic;
class UMaterialInterface;

UCLASS(ClassGroup = (Weapons), meta = (BlueprintSpawnableComponent))
class POLARITY_API UAimGlintComponent : public UMaterialBillboardComponent
{
	GENERATED_BODY()

public:
	UAimGlintComponent();

	/** Unlit, additive, with the scalars "Intensity" and "Rotation" (turns, 0 to 1). Empty: the
	 *  weapon's AIAimGlintMaterial. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glint")
	TObjectPtr<UMaterialInterface> GlintMaterial;

	/** Size on screen when the hold begins and on the shot, as a share of the screen's width. On
	 *  screen and not in the world: at 50 m a star the size of a scope is a pixel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glint", meta = (ClampMin = "0.0"))
	float StartSize = 0.008f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glint", meta = (ClampMin = "0.0"))
	float EndSize = 0.045f;

	/** "Intensity" on the shot. It starts at StartIntensityShare of this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glint", meta = (ClampMin = "0.0"))
	float MaxIntensity = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glint", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StartIntensityShare = 0.15f;

	/** Off the line of the barrel by no more than this: full brightness (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glint", meta = (ClampMin = "0.0", Units = "deg"))
	float FullAngle = 4.0f;

	/** Off it by this much or more: nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glint", meta = (ClampMin = "0.0", Units = "deg"))
	float ZeroAngle = 12.0f;

	/** How fast the star turns (degrees a second). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glint", meta = (Units = "deg"))
	float SpinDegreesPerSecond = 40.0f;

	/** Where the scope is, from the muzzle: back along the barrel and up (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glint", meta = (Units = "cm"))
	float BackFromMuzzle = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glint", meta = (Units = "cm"))
	float AboveBarrel = 9.0f;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GlintInstance;

	void EnsureInstance(const AShooterWeapon* Weapon);
};
