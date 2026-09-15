#include "AnimNotify_ReloadReady.h"

#include "Components/SkeletalMeshComponent.h"
#include "ShooterCharacter.h"
#include "Weapons/ShooterWeapon.h"

UAnimNotify_ReloadReady::UAnimNotify_ReloadReady()
{
#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(255, 191, 26);
#endif
}

void UAnimNotify_ReloadReady::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);
	if (!MeshComp)
	{
		return;
	}

	AShooterCharacter* Character = Cast<AShooterCharacter>(MeshComp->GetOwner());
	if (!Character || !Character->IsLocallyControlled())
	{
		return;
	}

	if (AShooterWeapon* Weapon = Character->GetCurrentWeapon())
	{
		Weapon->CommitReloadFromNotify();
	}
}

FString UAnimNotify_ReloadReady::GetNotifyName_Implementation() const
{
	return TEXT("Weapon: Reload Ready");
}
