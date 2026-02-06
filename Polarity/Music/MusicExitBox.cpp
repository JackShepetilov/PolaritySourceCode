// MusicExitBox.cpp

#include "MusicExitBox.h"
#include "MusicPlayerSubsystem.h"
#include "Components/BoxComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY(LogMusicExitBox);

AMusicExitBox::AMusicExitBox()
{
	PrimaryActorTick.bCanEverTick = false;

	// Create trigger box
	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	RootComponent = TriggerBox;

	TriggerBox->SetBoxExtent(BoxExtent);
	TriggerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TriggerBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	TriggerBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	TriggerBox->SetGenerateOverlapEvents(true);

	// Editor visualization
	TriggerBox->SetHiddenInGame(true);
	TriggerBox->ShapeColor = FColor::Red;
}

void AMusicExitBox::BeginPlay()
{
	Super::BeginPlay();

	// Cache music subsystem
	UGameInstance* GI = GetGameInstance();
	if (GI)
	{
		MusicSubsystem = GI->GetSubsystem<UMusicPlayerSubsystem>();
	}

	if (!MusicSubsystem)
	{
		UE_LOG(LogMusicExitBox, Warning, TEXT("[EMB:%s] MusicPlayerSubsystem not found!"), *GetName());
	}

	// Bind overlap event
	TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &AMusicExitBox::OnBoxBeginOverlap);

	LogDebug(TEXT("MusicExitBox initialized"));
}

#if WITH_EDITOR
void AMusicExitBox::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Update box extent if changed in editor
	if (PropertyChangedEvent.Property &&
		PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(AMusicExitBox, BoxExtent))
	{
		if (TriggerBox)
		{
			TriggerBox->SetBoxExtent(BoxExtent);
		}
	}
}
#endif

void AMusicExitBox::OnBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// Only react to player
	AShooterCharacter* Player = Cast<AShooterCharacter>(OtherActor);
	if (!Player)
	{
		return;
	}

	LogDebug(TEXT("=== Player ENTERED EMB - Stopping music ==="));

	if (MusicSubsystem)
	{
		MusicSubsystem->StopTrack();
	}
	else
	{
		UE_LOG(LogMusicExitBox, Warning, TEXT("[EMB:%s] Cannot stop music - no subsystem"), *GetName());
	}
}

void AMusicExitBox::LogDebug(const FString& Message) const
{
	UE_LOG(LogMusicExitBox, Log, TEXT("[EMB:%s] %s"), *GetName(), *Message);
}
