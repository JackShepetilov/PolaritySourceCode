// AbilityHandler.cpp

#include "AbilityHandler.h"
#include "AbilityComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "EMFVelocityModifier.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"

UAbilityHandler::UAbilityHandler()
{
}

void UAbilityHandler::Initialize(UAbilityComponent* InOwningComponent, UAbilityDefinition* InDefinition, int32 InLevel)
{
	OwningComponent = InOwningComponent;
	Definition = InDefinition;
	OwningCharacter = InOwningComponent ? Cast<AShooterCharacter>(InOwningComponent->GetOwner()) : nullptr;

	const int32 MaxLevel = Definition ? FMath::Max(1, Definition->GetMaxLevel()) : 1;
	CurrentLevel = FMath::Clamp(InLevel, 1, MaxLevel);
}

void UAbilityHandler::SetLevel(int32 NewLevel)
{
	const int32 MaxLevel = Definition ? FMath::Max(1, Definition->GetMaxLevel()) : 1;
	const int32 Clamped = FMath::Clamp(NewLevel, 1, MaxLevel);
	if (Clamped == CurrentLevel)
	{
		return;
	}
	CurrentLevel = Clamped;
	OnLevelChanged(CurrentLevel);
}

FAbilityCommonStats UAbilityHandler::GetCommonStats() const
{
	if (!Definition)
	{
		return FAbilityCommonStats{};
	}
	return Definition->GetCommonStatsAtLevel(CurrentLevel);
}

// ==================== Animation Helpers ====================

USkeletalMeshComponent* UAbilityHandler::GetFPMesh() const
{
	return OwningCharacter ? OwningCharacter->GetFirstPersonMesh() : nullptr;
}

UAnimInstance* UAbilityHandler::GetFPAnimInstance() const
{
	USkeletalMeshComponent* Mesh = GetFPMesh();
	return Mesh ? Mesh->GetAnimInstance() : nullptr;
}

float UAbilityHandler::PlayFPMontage(UAnimMontage* Montage, float PlayRate, FName StartSection)
{
	if (!Montage)
	{
		return 0.0f;
	}
	UAnimInstance* AnimInst = GetFPAnimInstance();
	if (!AnimInst)
	{
		return 0.0f;
	}
	const float Length = AnimInst->Montage_Play(Montage, PlayRate);
	if (Length > 0.0f && StartSection != NAME_None)
	{
		AnimInst->Montage_JumpToSection(StartSection, Montage);
	}
	return Length;
}

void UAbilityHandler::StopFPMontage(UAnimMontage* Montage, float BlendOutTime)
{
	if (!Montage)
	{
		return;
	}
	if (UAnimInstance* AnimInst = GetFPAnimInstance())
	{
		AnimInst->Montage_Stop(BlendOutTime, Montage);
	}
}

void UAbilityHandler::BindFPMontageEnd(UAnimMontage* Montage, FName CallbackFunctionName)
{
	if (!Montage)
	{
		return;
	}
	UAnimInstance* AnimInst = GetFPAnimInstance();
	if (!AnimInst)
	{
		return;
	}
	FOnMontageEnded EndDelegate;
	EndDelegate.BindUFunction(this, CallbackFunctionName);
	AnimInst->Montage_SetEndDelegate(EndDelegate, Montage);
}

// ==================== Charge Helpers ====================

float UAbilityHandler::GetPlayerChargeModule() const
{
	if (!OwningCharacter)
	{
		return 0.0f;
	}
	if (UEMFVelocityModifier* Mod = OwningCharacter->FindComponentByClass<UEMFVelocityModifier>())
	{
		return FMath::Abs(Mod->GetCharge());
	}
	return 0.0f;
}

bool UAbilityHandler::TryDeductCharge(float Amount)
{
	if (Amount <= 0.0f)
	{
		return true;
	}
	if (!OwningCharacter)
	{
		return false;
	}
	UEMFVelocityModifier* Mod = OwningCharacter->FindComponentByClass<UEMFVelocityModifier>();
	if (!Mod)
	{
		return false;
	}
	if (FMath::Abs(Mod->GetCharge()) < Amount)
	{
		return false;
	}
	Mod->DeductCharge(Amount);
	return true;
}

// ==================== Completion ====================

void UAbilityHandler::NotifyAbilityComplete()
{
	if (OwningComponent)
	{
		OwningComponent->NotifyAbilityCompletedFromHandler(this);
	}
}

void UAbilityHandler::NotifyAbilityCancelled()
{
	if (OwningComponent)
	{
		OwningComponent->NotifyAbilityCancelledFromHandler(this);
	}
}
