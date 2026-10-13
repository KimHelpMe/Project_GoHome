// 


#include "Core/Actors/DepartureButton.h"

#include "Core/ExpeditionZoneDataAsset.h"
#include "Core/GoHomeGameMode.h"
#include "Core/LobbyGameState.h"
#include "Core/ExpeditionTravelSubsystem.h"
#include "Core/ExplorationGameMode.h"

ADepartureButton::ADepartureButton()
{
}

bool ADepartureButton::CanInteract(APawn* InstigatorPawn) const
{
	return true;
}

void ADepartureButton::OnInteract(APawn* InstigatorPawn)
{
	AGoHomeGameMode* GoHomeGameMode = GetWorld()->GetAuthGameMode<AGoHomeGameMode>();
	if (!GoHomeGameMode) return;
	
	//  현재 로비 맵일 때
	if (ALobbyGameState* LobbyGameState = GetWorld()->GetGameState<ALobbyGameState>())
	{
		// 탐사 맵으롷 이동한다.
		const UExpeditionZoneDataAsset* SelectedZone = LobbyGameState->GetSelectedZone();
		if (!SelectedZone) return;

		// 선택된 존 에셋을 트래블 간 유지되는 서브시스템에 넘겨둔다.
		if (UExpeditionTravelSubsystem* TravelSubsystem = GetGameInstance()->GetSubsystem<UExpeditionTravelSubsystem>())
		{
			const int32 ZoneIndex = LobbyGameState->GetAvailableZones().IndexOfByKey(SelectedZone);
			TravelSubsystem->SetActiveZone(SelectedZone, ZoneIndex + 1);
		}
		
		GoHomeGameMode->ServerTravelViaLoadingScreen(SelectedZone->MapPath.ToSoftObjectPath().GetLongPackageName());
	}
	
	// 현재 탐사 맵일 때
	else if (AGoHomeGameState* GoHomeGameState = GetWorld()->GetGameState<AGoHomeGameState>())
	{
		if (AExplorationGameMode* ExplorationGameMode = GetWorld()->GetAuthGameMode<AExplorationGameMode>())
		{
			ExplorationGameMode->HandleReturn();
		}
	}
}

