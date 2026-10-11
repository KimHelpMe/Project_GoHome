#include "Core/HudNoticeSubsystem.h"

#include "Core/GoHomePlayerController.h"
#include "GameFramework/Pawn.h"

void UHudNoticeSubsystem::Push(const FHudNotice& Notice)
{
	OnNoticePushed.Broadcast(Notice);
}

void UHudNoticeSubsystem::SendToPawnOwner(const APawn* Pawn, const FHudNotice& Notice)
{
	if (!Pawn || !Pawn->HasAuthority())
	{
		return;
	}

	if (AGoHomePlayerController* PlayerController = Cast<AGoHomePlayerController>(Pawn->GetController()))
	{
		PlayerController->Client_PushHudNotice(Notice);
	}
}
