// 


#include "Core/ExpeditionTravelSubsystem.h"

void UExpeditionTravelSubsystem::SetActiveZone(const UExpeditionZoneDataAsset* ZoneDataAsset, int32 ZoneNumber)
{
	ActiveZone = const_cast<UExpeditionZoneDataAsset*>(ZoneDataAsset);
	ActiveZoneNumber = ZoneDataAsset ? ZoneNumber : 0;
}
