#include "UI/Supply/SupplyPageBase.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "UI/Supply/SupplyMotion.h"
#include "UI/Supply/SupplyScreenWidget.h"

void USupplyPageBase::InitPage(USupplyScreenWidget* InHost)
{
	Host = InHost;
}

void USupplyPageBase::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (WidgetTree && !WidgetTree->RootWidget)
	{
		const SupplyUI::FBuilder B(WidgetTree);
		PageRoot = B.Overlay();
		Surface = PageRoot;
		WidgetTree->RootWidget = PageRoot;
		SupplyUI::AddFill(PageRoot, BuildPage(B));
	}
}

void USupplyPageBase::ApplyViewSize(const FVector2D& InViewSize)
{
	CurrentViewSize = InViewSize;
	const bool bWantCompact = SupportsCompact() && InViewSize.Y <= 850.f;
	if (bWantCompact != bCompact)
	{
		bCompact = bWantCompact;
		RebuildPage();
	}
	OnViewSizeChanged(InViewSize);
}

void USupplyPageBase::RebuildPage()
{
	if (!WidgetTree || !PageRoot)
	{
		return;
	}

	// 진행 중 트랙은 옛 위젯을 잡고 있다 — 끝 값으로 정리한 뒤 트리를 버린다(해상도 전환 때만 일어나 호스트 연출까지 끝내도 무방).
	if (FSupplyAnimator* Animator = GetAnimator())
	{
		Animator->FinishAll();
	}
	HitTargets.Reset();
	Masthead = nullptr;
	Footer = nullptr;
	PageRoot->ClearChildren();

	const SupplyUI::FBuilder B(WidgetTree);
	SupplyUI::AddFill(PageRoot, BuildPage(B));
	OnPageRebuilt();
}

bool USupplyPageBase::HandleClick(const FVector2D& ScreenPosition)
{
	// 뒤에 등록된(더 안쪽·위) 대상부터
	for (int32 Index = HitTargets.Num() - 1; Index >= 0; --Index)
	{
		UWidget* Widget = HitTargets[Index].Widget.Get();
		if (!Widget || !Widget->IsVisible() || HitTargets[Index].Layer != HitLayer)
		{
			continue;
		}
		const FGeometry& Geometry = Widget->GetCachedGeometry();
		if (Geometry.GetLocalSize().X > 0.f && Geometry.IsUnderLocation(ScreenPosition))
		{
			HitTargets[Index].OnClick();
			return true;
		}
	}
	return false;
}

void USupplyPageBase::GetWipeBand(float& OutTop, float& OutBottom) const
{
	OutTop = PagePadding.Top + 14.f;
	OutBottom = PagePadding.Bottom;
	if (Masthead)
	{
		OutTop += Masthead->GetDesiredSize().Y;
	}
	if (Footer)
	{
		OutBottom += Footer->GetDesiredSize().Y;
	}
}

void USupplyPageBase::AddHitTarget(UWidget* Target, TFunction<void()> OnClick, int32 Layer)
{
	HitTargets.Add({ Target, MoveTemp(OnClick), Layer });
}

FSupplyAnimator* USupplyPageBase::GetAnimator() const
{
	return Host.IsValid() ? &Host->GetAnimator() : nullptr;
}

void USupplyPageBase::PlayLift(UWidget* Target, FName Id, float DelayMs, float DurationMs, bool bPageEase)
{
	FSupplyAnimator* Animator = GetAnimator();
	if (!Animator || !Target)
	{
		return;
	}
	const FSupplyEase Ease = bPageEase ? FSupplyEase::Page() : FSupplyEase::EaseOut();
	TWeakObjectPtr<UWidget> Weak = Target;
	Animator->Play(Id, DelayMs / 1000.f, DurationMs / 1000.f, [Weak, Ease](float T)
	{
		if (UWidget* Widget = Weak.Get())
		{
			const float E = Ease.Eval(T);
			Widget->SetRenderOpacity(E);
			Widget->SetRenderTranslation(FVector2D(0.f, 9.f * (1.f - E)));
		}
	});
}

UWidget* USupplyPageBase::MakeBand(const SupplyUI::FBuilder& B, const FLinearColor& Color, float AnchorMinX, float AnchorMinY, float AnchorMaxX, float AnchorMaxY, float AngleDeg) const
{
	UCanvasPanel* Canvas = B.Canvas();
	Canvas->SetVisibility(ESlateVisibility::HitTestInvisible);
	UBorder* Band = B.Fill(Color);
	UCanvasPanelSlot* BandSlot = Canvas->AddChildToCanvas(Band);
	BandSlot->SetAnchors(FAnchors(AnchorMinX, AnchorMinY, AnchorMaxX, AnchorMaxY));
	BandSlot->SetOffsets(FMargin(0.f));
	Band->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	Band->SetRenderTransformAngle(AngleDeg);
	return Canvas;
}
