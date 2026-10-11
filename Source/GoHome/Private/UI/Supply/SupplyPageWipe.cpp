#include "UI/Supply/SupplyPageWipe.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "UI/Supply/SupplyMotion.h"
#include "UI/Supply/SupplyUIStyle.h"

using namespace SupplyUI;

namespace
{
	const FName LeadTrack(TEXT("Wipe.Lead"));
	const FName FollowTrack(TEXT("Wipe.Follow"));
	const FName GhostTrack(TEXT("Wipe.Ghost"));
	const FName CleanupTrack(TEXT("Wipe.Cleanup"));

	// 판 폭(띠 폭 대비): 코발트 left -12% width 120%, 아이보리 left 1% width 112%
	constexpr float LeadWidth = 1.20f;
	constexpr float FollowWidth = 1.12f;
}

void USupplyPageWipe::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	const FBuilder B(WidgetTree);
	UCanvasPanel* Root = B.Canvas();
	WidgetTree->RootWidget = Root;

	// 띠: 위/아래 오프셋은 재생 때 페이지가 알려준다
	Band = B.Canvas();
	Band->SetClipping(EWidgetClipping::ClipToBounds);
	BandSlot = Root->AddChildToCanvas(Band);
	BandSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	BandSlot->SetOffsets(FMargin(0.f));

	// 순서: 코발트(::before) → 잔상(자식) → 아이보리(::after)
	auto AddPlate = [&B, this](const FLinearColor& Color, float MinX, float MaxX) -> UWidget*
	{
		UBorder* Plate = B.Fill(Color);
		UCanvasPanelSlot* PlateSlot = Band->AddChildToCanvas(Plate);
		PlateSlot->SetAnchors(FAnchors(MinX, -0.30f, MaxX, 1.30f));
		PlateSlot->SetOffsets(FMargin(0.f));
		Plate->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		return Plate;
	};
	LeadPlate = AddPlate(Blue(), -0.12f, 1.08f);

	Stage = B.Canvas();
	UCanvasPanelSlot* StageSlot = Band->AddChildToCanvas(Stage);
	StageSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	StageSlot->SetOffsets(FMargin(0.f));

	FollowPlate = AddPlate(Ivory(), 0.01f, 1.13f);

	SetVisibility(ESlateVisibility::Collapsed);
}

FVector2D USupplyPageWipe::GetViewSize() const
{
	const FVector2D Size = GetCachedGeometry().GetLocalSize();
	return Size.X > 1.f ? Size : FVector2D(1920.f, 1080.f);
}

void USupplyPageWipe::ApplyPlate(UWidget* Plate, float WidthScale, float T) const
{
	static const FSupplyKey Keys[] = { { 0.f, -1.25f }, { .48f, 0.f }, { 1.f, 1.25f } };
	const float X = SampleSupplyKeys(Keys, T, FSupplyEase::Page()) * Direction;
	FWidgetTransform Transform;
	Transform.Shear = FVector2D(-13.f, 0.f);
	Transform.Translation = FVector2D(X * GetViewSize().X * WidthScale, 0.f);
	Plate->SetRenderTransform(Transform);
}

void USupplyPageWipe::Play(FSupplyAnimator& Animator, float BandTop, float BandBottom, bool bBack, UWidget* InGhost, FOnGhostReleased InOnGhostReleased)
{
	Stop(Animator);
	if (Animator.bReducedMotion)
	{
		if (InGhost)
		{
			InOnGhostReleased.ExecuteIfBound(InGhost);
		}
		return;
	}

	bPlaying = true;
	Direction = bBack ? -1.f : 1.f;
	BandSlot->SetOffsets(FMargin(0.f, BandTop, 0.f, BandBottom));
	BandHeight = FMath::Max(0.f, GetViewSize().Y - BandTop - BandBottom);
	SetVisibility(ESlateVisibility::HitTestInvisible);

	Ghost = InGhost;
	OnGhostReleased = MoveTemp(InOnGhostReleased);
	if (Ghost)
	{
		// 원래 화면 좌표 그대로 — 띠 위쪽만큼 올려 놓는다
		const FVector2D View = GetViewSize();
		UCanvasPanelSlot* GhostSlot = Stage->AddChildToCanvas(Ghost);
		GhostSlot->SetAnchors(FAnchors(0.f, 0.f));
		GhostSlot->SetPosition(FVector2D(0.f, -BandTop));
		GhostSlot->SetSize(View);
		Ghost->SetVisibility(ESlateVisibility::HitTestInvisible);

		const float GhostShift = bBack ? 34.f : -34.f;
		TWeakObjectPtr<UWidget> WeakGhost = Ghost;
		Animator.Play(GhostTrack, 0.f, .18f, [WeakGhost, GhostShift](float T)
		{
			if (UWidget* Widget = WeakGhost.Get())
			{
				const float E = FSupplyEase::EaseIn().Eval(T);
				Widget->SetRenderOpacity(1.f - E);
				Widget->SetRenderTranslation(FVector2D(GhostShift * E, 0.f));
			}
		});
	}

	Animator.Play(LeadTrack, 0.f, .36f, [this](float T) { ApplyPlate(LeadPlate, LeadWidth, T); });
	Animator.Play(FollowTrack, .046f, .36f, [this](float T) { ApplyPlate(FollowPlate, FollowWidth, T); });
	Animator.Play(CleanupTrack, 0.f, .406f, nullptr, [this]()
	{
		ReleaseGhost();
		bPlaying = false;
		SetVisibility(ESlateVisibility::Collapsed);
	});
}

void USupplyPageWipe::Stop(FSupplyAnimator& Animator)
{
	Animator.Cancel(LeadTrack);
	Animator.Cancel(FollowTrack);
	Animator.Cancel(GhostTrack);
	Animator.Cancel(CleanupTrack);
	ReleaseGhost();
	bPlaying = false;
	SetVisibility(ESlateVisibility::Collapsed);
}

void USupplyPageWipe::ReleaseGhost()
{
	if (UWidget* Released = Ghost)
	{
		Ghost = nullptr;
		Released->RemoveFromParent();
		Released->SetRenderOpacity(1.f);
		Released->SetRenderTranslation(FVector2D::ZeroVector);
		OnGhostReleased.ExecuteIfBound(Released);
	}
	OnGhostReleased.Unbind();
}
