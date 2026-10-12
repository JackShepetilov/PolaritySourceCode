// HelmetComponent.cpp

#include "Variant_Shooter/Shield/HelmetComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Variant_Shooter/DamageTypes/DamageType_Melee.h"

UHelmetComponent::UHelmetComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);

	HeadBones.Add(FName("head"));
}

void UHelmetComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UHelmetComponent, HeadDamageTaken);
	DOREPLIFETIME(UHelmetComponent, bHelmetOff);
}

UHelmetComponent* UHelmetComponent::Find(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UHelmetComponent>() : nullptr;
}

bool UHelmetComponent::IsHeadBone(FName BoneName) const
{
	return !BoneName.IsNone() && HeadBones.Contains(BoneName);
}

float UHelmetComponent::GetHelmetFraction() const
{
	if (bHelmetOff || HelmetPool <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}
	return FMath::Clamp(1.0f - HeadDamageTaken / HelmetPool, 0.0f, 1.0f);
}

FHelmetOutcome UHelmetComponent::ProcessHit(float Damage, const FDamageEvent& DamageEvent, AActor* Attacker)
{
	FHelmetOutcome Outcome;

	const AActor* const Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || Damage <= 0.0f || !DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		return Outcome;
	}

	const FName Bone = static_cast<const FPointDamageEvent&>(DamageEvent).HitInfo.BoneName;
	if (!IsHeadBone(Bone))
	{
		return Outcome;
	}

	if (bHelmetOff)
	{
		// A bare head. A bullet ends it; a fist or a blade is an ordinary hit and goes on to health.
		const bool bMelee = DamageEvent.DamageTypeClass && DamageEvent.DamageTypeClass->IsChildOf(UDamageType_Melee::StaticClass());
		if (bLethalWithoutHelmet && !bMelee)
		{
			Outcome.bHandled = true;
			Outcome.bLethal = true;
		}
		return Outcome;
	}

	Outcome.bHandled = true;

	const float Before = HeadDamageTaken;
	HeadDamageTaken = FMath::Min(HeadDamageTaken + Damage, HelmetPool);

	// One hit may cross both marks (a sniper round): the helmet comes off and the owner does not go
	// down, because the second mark is the one that says "no knockdown" [author, 2026-10-12].
	if (HeadDamageTaken >= HelmetPool - KINDA_SMALL_NUMBER)
	{
		bHelmetOff = true;
		bSeenHelmetOff = true;
		Outcome.bHelmetOff = true;
		PresentHelmetOff(Attacker);
	}
	else if (!bKnockdownSpent && KnockdownThreshold > 0.0f && Before < KnockdownThreshold && HeadDamageTaken >= KnockdownThreshold)
	{
		bKnockdownSpent = true;
		Outcome.bKnockdown = true;
		OnHelmetKnockdown.Broadcast(this, Attacker);
	}

	UE_LOG(LogTemp, Log, TEXT("[HELMET_DEBUG] %s head hit %.0f: helmet %.0f/%.0f knockdown=%d off=%d"),
		*Owner->GetName(), Damage, HeadDamageTaken, HelmetPool, Outcome.bKnockdown ? 1 : 0, Outcome.bHelmetOff ? 1 : 0);
	return Outcome;
}

void UHelmetComponent::ResetForReuse()
{
	HeadDamageTaken = 0.0f;
	bHelmetOff = false;
	bSeenHelmetOff = false;
	bKnockdownSpent = false;
}

void UHelmetComponent::OnRep_HelmetOff()
{
	if (bHelmetOff && !bSeenHelmetOff)
	{
		PresentHelmetOff(nullptr);
	}
	bSeenHelmetOff = bHelmetOff;
}

void UHelmetComponent::PresentHelmetOff(AActor* Instigator)
{
	const AActor* const Owner = GetOwner();
	if (Owner && GetNetMode() != NM_DedicatedServer)
	{
		// At the head bone when the body has one, at the actor otherwise.
		const ACharacter* const Character = Cast<ACharacter>(Owner);
		USkeletalMeshComponent* const Mesh = Character ? Character->GetMesh() : nullptr;
		const FName HeadBone = HeadBones.Num() > 0 ? HeadBones[0] : NAME_None;
		const bool bHasBone = Mesh && Mesh->GetBoneIndex(HeadBone) != INDEX_NONE;
		const FVector Where = bHasBone ? Mesh->GetBoneLocation(HeadBone) : Owner->GetActorLocation();

		if (HelmetOffEffect)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, HelmetOffEffect, Where, Owner->GetActorRotation());
		}
		if (HelmetOffSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, HelmetOffSound, Where, 1.0f, 1.0f, 0.0f, HelmetOffSoundAttenuation);
		}
	}

	OnHelmetKnockedOff.Broadcast(this, Instigator);
}
