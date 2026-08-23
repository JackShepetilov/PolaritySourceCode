#include "AnimNotify_WeaponSwitch.h"
#include "ShooterCharacter.h"
#include "Components/SkeletalMeshComponent.h"

void UAnimNotify_WeaponSwitchSwapPoint::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp)
	{
		return;
	}

	// Both meshes carry the montage, so the third-person copy on a teammate's machine reaches this
	// too. The character's own guard sorts it out: only the machine actually running the swap is in
	// the Holstering phase, everywhere else this is a no-op.
	if (AShooterCharacter* Character = Cast<AShooterCharacter>(MeshComp->GetOwner()))
	{
		Character->OnWeaponSwitchSwapNotify();
	}
}
