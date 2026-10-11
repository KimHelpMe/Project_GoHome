#include "UI/HUD/HudNoticeFeedWidget.h"

#include "Components/PanelWidget.h"
#include "Core/HudNoticeSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Pawn.h"
#include "Player/HealthComponent.h"
#include "UI/HUD/HudNoticeRowWidget.h"

void UHudNoticeFeedWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UHudNoticeSubsystem* Notices = ULocalPlayer::GetSubsystem<UHudNoticeSubsystem>(GetOwningLocalPlayer()))
	{
		NoticeHandle = Notices->OnNoticePushed.AddUObject(this, &UHudNoticeFeedWidget::HandleNoticePushed);
	}
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UHudNoticeFeedWidget::NativeDestruct()
{
	if (UHudNoticeSubsystem* Notices = ULocalPlayer::GetSubsystem<UHudNoticeSubsystem>(GetOwningLocalPlayer()))
	{
		Notices->OnNoticePushed.Remove(NoticeHandle);
	}
	NoticeHandle.Reset();
	ClearRows();

	Super::NativeDestruct();
}

void UHudNoticeFeedWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 사망·관전: 개인 알림이라 비우고 숨긴다
	const bool bHasPawn = HasLivePawn();
	if (bHasPawn != bHadPawn)
	{
		bHadPawn = bHasPawn;
		if (!bHasPawn)
		{
			ClearRows();
		}
		SetVisibility(bHasPawn ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	for (int32 Index = Rows.Num() - 1; Index >= 0; --Index)
	{
		UHudNoticeRowWidget* Row = Rows[Index];
		if (!Row || Row->IsFinished())
		{
			if (Row)
			{
				Row->RemoveFromParent();
			}
			Rows.RemoveAt(Index);
		}
	}
}

void UHudNoticeFeedWidget::PushNotice(const FHudNotice& Notice)
{
	HandleNoticePushed(Notice);
}

void UHudNoticeFeedWidget::HandleNoticePushed(const FHudNotice& Notice)
{
	if (!NoticeBox || !RowClass || !HasLivePawn())
	{
		return;
	}

	// 같은 Key: 기존 줄 갱신 + 맨 아래로
	if (!Notice.Key.IsNone())
	{
		for (int32 Index = 0; Index < Rows.Num(); ++Index)
		{
			UHudNoticeRowWidget* Row = Rows[Index];
			if (Row && !Row->IsRemoving() && Row->GetKey() == Notice.Key)
			{
				Row->Refresh(Notice);
				if (Index != Rows.Num() - 1)
				{
					Rows.RemoveAt(Index);
					Rows.Add(Row);
					NoticeBox->RemoveChild(Row);
					NoticeBox->AddChild(Row);
				}
				return;
			}
		}
	}

	UHudNoticeRowWidget* Row = CreateWidget<UHudNoticeRowWidget>(this, RowClass);
	if (!Row)
	{
		return;
	}
	NoticeBox->AddChild(Row);
	Row->Setup(Notice);
	Rows.Add(Row);

	// 넘침: 살아 있는 줄 중 가장 오래된 것부터 빠르게 내보낸다
	int32 LiveCount = 0;
	for (const UHudNoticeRowWidget* Each : Rows)
	{
		LiveCount += (Each && !Each->IsRemoving()) ? 1 : 0;
	}
	for (UHudNoticeRowWidget* Each : Rows)
	{
		if (LiveCount <= MaxRows)
		{
			break;
		}
		if (Each && !Each->IsRemoving())
		{
			Each->BeginRemove(true);
			--LiveCount;
		}
	}
}

bool UHudNoticeFeedWidget::HasLivePawn() const
{
	// 사망해도 폰은 남는다(관전 중 폰 유지) — 복제된 사망 플래그까지 본다
	const APawn* Pawn = GetOwningPlayerPawn();
	if (!Pawn)
	{
		return false;
	}
	const UHealthComponent* Health = Pawn->FindComponentByClass<UHealthComponent>();
	return !Health || !Health->IsDead();
}

void UHudNoticeFeedWidget::ClearRows()
{
	for (UHudNoticeRowWidget* Row : Rows)
	{
		if (Row)
		{
			Row->RemoveFromParent();
		}
	}
	Rows.Reset();
}
