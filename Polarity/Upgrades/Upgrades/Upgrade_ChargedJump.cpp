// Upgrade_ChargedJump.cpp

#include "Upgrade_ChargedJump.h"
#include "UpgradeDefinition_ChargedJump.h"
#include "JumpSlotParams.h"
#include "ShooterCharacter.h"
#include "ApexMovementComponent.h"
#include "Camera/CameraShakeBase.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

UUpgrade_ChargedJump::UUpgrade_ChargedJump()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

bool UUpgrade_ChargedJump::GetJumpSlotParams(FJumpSlotParams& Out) const
{
	const UUpgradeDefinition_ChargedJump* Def = Cast<UUpgradeDefinition_ChargedJump>(UpgradeDefinition);
	if (!Def)
	{
		return false;
	}

	const FChargedJumpLevelData& Data = Def->GetLevelData(CurrentLevel);
	Out.Mode = EJumpSlotMode::ChargedJump;
	Out.ChargeMaxZVelocity = FMath::Max(0.0f, Data.MaxZVelocity);
	// A charge time of zero would hand out a full-strength jump for a tap.
	Out.ChargeTime = FMath::Max(0.05f, Data.ChargeTime);
	Out.ChargeForwardBoost = FMath::Max(0.0f, Data.ForwardBoost);
	Out.ChargeMoveScale = FMath::Clamp(Data.ChargeMoveScale, 0.0f, 1.0f);
	Out.Cooldown = FMath::Max(0.0f, Data.Cooldown);
	return true;
}

void UUpgrade_ChargedJump::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Cosmetic and for the one player who is charging: the server and the other players' machines
	// have nobody to show it to.
	const AShooterCharacter* Character = GetShooterCharacter();
	const UApexMovementComponent* Movement = Character ? Character->GetApexMovement() : nullptr;
	if (!Character || !Character->IsLocallyControlled() || !Movement || !Movement->IsChargingJump())
	{
		StopFeedback();
		return;
	}

	UpdateFeedback(Movement->GetJumpChargeAlpha());
}

void UUpgrade_ChargedJump::UpdateFeedback(float Alpha)
{
	const UUpgradeDefinition_ChargedJump* Def = Cast<UUpgradeDefinition_ChargedJump>(UpgradeDefinition);
	const AShooterCharacter* Character = GetShooterCharacter();
	if (!Def || !Character)
	{
		return;
	}

	if (!bFeedbackActive)
	{
		bFeedbackActive = true;
		if (Def->ChargeSound)
		{
			ChargeAudio = UGameplayStatics::SpawnSound2D(this, Def->ChargeSound, 1.0f, Def->ChargePitchStart);
		}
	}

	if (UAudioComponent* Audio = ChargeAudio.Get())
	{
		Audio->SetPitchMultiplier(FMath::Lerp(Def->ChargePitchStart, Def->ChargePitchFull, Alpha));
	}

	const APlayerController* PC = Cast<APlayerController>(Character->GetController());
	APlayerCameraManager* Camera = PC ? PC->PlayerCameraManager.Get() : nullptr;
	if (!Camera || !Def->ChargeCameraShake)
	{
		return;
	}

	const float Scale = FMath::Lerp(Def->ChargeShakeScaleStart, Def->ChargeShakeScaleFull, Alpha);
	UCameraShakeBase* Shake = ChargeShake.Get();
	if (!Shake || Shake->IsFinished())
	{
		// Restarted rather than required to loop: any shake asset works, including one-shots.
		ChargeShake = Camera->StartCameraShake(Def->ChargeCameraShake, Scale);
	}
	else
	{
		Shake->ShakeScale = Scale;
	}
}

void UUpgrade_ChargedJump::StopFeedback()
{
	if (!bFeedbackActive)
	{
		return;
	}
	bFeedbackActive = false;

	if (UAudioComponent* Audio = ChargeAudio.Get())
	{
		Audio->FadeOut(0.08f, 0.0f);
	}
	ChargeAudio.Reset();

	if (UCameraShakeBase* Shake = ChargeShake.Get())
	{
		const AShooterCharacter* Character = GetShooterCharacter();
		const APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
		if (PC && PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->StopCameraShake(Shake, false);
		}
	}
	ChargeShake.Reset();
}

void UUpgrade_ChargedJump::OnUpgradeDeactivated()
{
	StopFeedback();
	Super::OnUpgradeDeactivated();
}

void UUpgrade_ChargedJump::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	StopFeedback();
	Super::EndPlay(EndPlayReason);
}
