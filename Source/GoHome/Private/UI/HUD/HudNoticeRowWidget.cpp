#include "UI/HUD/HudNoticeRowWidget.h"

#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

namespace HudNoticeRow
{
	// 가로 Width×1 알파 램프(RGB 흰색). AlphaAt(u) 는 u=0(왼쪽)~1(오른쪽) → 0~1.
	template <typename FAlphaFn>
	UTexture2D* MakeAlphaRamp(int32 Width, FAlphaFn AlphaAt)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(Width, 1, PF_B8G8R8A8);
		if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.IsEmpty())
		{
			return nullptr;
		}
		Texture->SRGB = false;
		Texture->Filter = TF_Bilinear;
		Texture->AddressX = TA_Clamp;
		Texture->AddressY = TA_Clamp;

		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		for (int32 X = 0; X < Width; ++X)
		{
			const float U = Width > 1 ? static_cast<float>(X) / static_cast<float>(Width - 1) : 1.f;
			const uint8 Alpha = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(AlphaAt(U), 0.f, 1.f) * 255.f));
			Pixels[X] = FColor(255, 255, 255, Alpha);
		}
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}

	float EaseOutCubic(float T) { return 1.f - FMath::Pow(1.f - T, 3.f); }
	float EaseIn(float T) { return T * T; }
	float EaseInOut(float T) { return T < 0.5f ? 2.f * T * T : 1.f - FMath::Square(-2.f * T + 2.f) * 0.5f; }
}

void UHudNoticeRowWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	EnsureTextures();
}

void UHudNoticeRowWidget::EnsureTextures()
{
	if (!BackgroundRamp)
	{
		// 프로토타입: linear-gradient(to left, .82 0%, .6 55%, 0 100%) → 오른쪽 1, 45% 지점 .6/.82, 왼쪽 0
		constexpr float MidAlpha = 0.6f / 0.82f;
		BackgroundRamp = HudNoticeRow::MakeAlphaRamp(128, [MidAlpha](float U)
		{
			return U < 0.45f ? FMath::Lerp(0.f, MidAlpha, U / 0.45f) : FMath::Lerp(MidAlpha, 1.f, (U - 0.45f) / 0.55f);
		});
	}
	if (!LineRamp)
	{
		LineRamp = HudNoticeRow::MakeAlphaRamp(128, [](float U) { return U; });
	}
	if (!BandRamp)
	{
		// 앞 10px 페이드 + 뒤 6px 불투명 — Box 브러시 왼쪽 마진으로 페이드 폭을 고정하고 나머지를 늘린다
		BandRamp = HudNoticeRow::MakeAlphaRamp(16, [](float U) { return FMath::Clamp(U * 15.f / 10.f, 0.f, 1.f); });
	}
}

void UHudNoticeRowWidget::Setup(const FHudNotice& InNotice)
{
	Notice = InNotice;
	Phase = EPhase::Enter;
	PhaseElapsed = 0.f;
	PulseElapsed = -1.f;
	if (CollapseBox)
	{
		CollapseBox->ClearHeightOverride();
	}
	ApplyContent();
	ApplyMotion();
}

void UHudNoticeRowWidget::Refresh(const FHudNotice& InNotice)
{
	Notice = InNotice;
	ApplyContent();
	PulseElapsed = 0.f;
	if (Phase == EPhase::Hold)
	{
		PhaseElapsed = 0.f;
	}
	ApplyMotion();
}

void UHudNoticeRowWidget::BeginRemove(bool bFast)
{
	if (IsRemoving())
	{
		return;
	}
	Phase = EPhase::Exit;
	PhaseElapsed = 0.f;
	PulseElapsed = -1.f;
	ExitDuration = bFast ? FastExitSeconds : ExitSeconds;
}

FLinearColor UHudNoticeRowWidget::AccentColor() const
{
	switch (Notice.Severity)
	{
	case EHudNoticeSeverity::Good:
		return GoodColor;
	case EHudNoticeSeverity::Warning:
		return WarningColor;
	case EHudNoticeSeverity::Danger:
		return DangerColor;
	default:
		return InfoColor;
	}
}

void UHudNoticeRowWidget::ApplyContent()
{
	EnsureTextures();
	const FLinearColor Accent = AccentColor();
	// Danger·Good 은 제목 뒤 액센트 띠(몬헌 "토벌 달성" 문법)
	const bool bBand = Notice.Severity == EHudNoticeSeverity::Danger || Notice.Severity == EHudNoticeSeverity::Good;

	if (BackgroundImage)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Brush.SetResourceObject(BackgroundRamp);
		Brush.ImageSize = FVector2D(128.f, 1.f);
		Brush.TintColor = FSlateColor(BackgroundColor);
		BackgroundImage->SetBrush(Brush);
	}

	if (BottomLine)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Brush.SetResourceObject(LineRamp);
		Brush.ImageSize = FVector2D(128.f, 1.f);
		Brush.TintColor = FSlateColor(Accent);
		BottomLine->SetBrush(Brush);
	}

	if (IconImage)
	{
		// RoundedBox + HalfHeightRadius = 텍스처를 원으로 잘라 그리고 외곽선을 액센트 링으로
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.ImageSize = FVector2D(IconSize, IconSize);
		if (Notice.Icon)
		{
			Brush.SetResourceObject(Notice.Icon);
			Brush.TintColor = FSlateColor(FLinearColor::White);
		}
		else
		{
			Brush.TintColor = FSlateColor(IconFallbackColor);
		}
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
		Brush.OutlineSettings.Width = IconRingWidth;
		Brush.OutlineSettings.Color = FSlateColor(Accent);
		IconImage->SetBrush(Brush);
	}

	const bool bHasBadge = !Notice.Badge.IsEmpty();
	if (BadgeImage)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.ImageSize = FVector2D(BadgeSize, BadgeSize);
		Brush.TintColor = FSlateColor(Accent);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
		Brush.OutlineSettings.Width = 0.f;
		BadgeImage->SetBrush(Brush);
		BadgeImage->SetVisibility(bHasBadge ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (BadgeText)
	{
		BadgeText->SetText(Notice.Badge);
		BadgeText->SetColorAndOpacity(FSlateColor(BandInkColor));
		BadgeText->SetVisibility(bHasBadge ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (TitleBand)
	{
		if (bBand)
		{
			FSlateBrush Brush;
			Brush.DrawAs = ESlateBrushDrawType::Box;
			Brush.SetResourceObject(BandRamp);
			Brush.ImageSize = FVector2D(16.f, 1.f);
			Brush.Margin = FMargin(10.f / 16.f, 0.f, 0.f, 0.f);
			Brush.TintColor = FSlateColor(Accent);
			TitleBand->SetBrush(Brush);
			TitleBand->SetPadding(FMargin(TitleBandFadeWidth + 2.f, 1.f, 10.f, 1.f));
		}
		else
		{
			FSlateBrush Brush;
			Brush.DrawAs = ESlateBrushDrawType::NoDrawType;
			TitleBand->SetBrush(Brush);
			TitleBand->SetPadding(FMargin(0.f, 1.f, 0.f, 1.f));
		}
	}
	if (TitleText)
	{
		TitleText->SetText(Notice.Title);
		TitleText->SetColorAndOpacity(FSlateColor(bBand ? BandInkColor : InkColor));
		// 띠 없는 흰 제목은 밝은 배경 대비용 그림자(프로토타입 text-shadow)
		TitleText->SetShadowColorAndOpacity(bBand ? FLinearColor::Transparent : FLinearColor(0.f, 0.f, 0.f, 0.9f));
		TitleText->SetShadowOffset(FVector2D(0.f, 1.f));
	}
	if (SubtitleText)
	{
		SubtitleText->SetText(Notice.Subtitle);
		SubtitleText->SetColorAndOpacity(FSlateColor(Accent));
		SubtitleText->SetVisibility(Notice.Subtitle.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}

void UHudNoticeRowWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	PhaseElapsed += InDeltaTime;
	if (PulseElapsed >= 0.f)
	{
		PulseElapsed += InDeltaTime;
		if (PulseElapsed >= PulseSeconds)
		{
			PulseElapsed = -1.f;
		}
	}

	switch (Phase)
	{
	case EPhase::Enter:
		if (PhaseElapsed >= EnterSeconds)
		{
			Phase = EPhase::Hold;
			PhaseElapsed = 0.f;
		}
		break;
	case EPhase::Hold:
		if (PhaseElapsed >= HoldSeconds)
		{
			BeginRemove(false);
		}
		break;
	case EPhase::Exit:
		if (PhaseElapsed >= ExitDuration)
		{
			Phase = EPhase::Collapse;
			PhaseElapsed = 0.f;
			CollapseFromHeight = MyGeometry.GetLocalSize().Y;
		}
		break;
	case EPhase::Collapse:
		if (PhaseElapsed >= CollapseSeconds)
		{
			Phase = EPhase::Finished;
		}
		break;
	default:
		break;
	}

	ApplyMotion();
}

void UHudNoticeRowWidget::ApplyMotion()
{
	float Opacity = 1.f;
	float OffsetX = 0.f;

	switch (Phase)
	{
	case EPhase::Enter:
	{
		const float T = EnterSeconds > 0.f ? FMath::Clamp(PhaseElapsed / EnterSeconds, 0.f, 1.f) : 1.f;
		const float Eased = HudNoticeRow::EaseOutCubic(T);
		Opacity = Eased;
		OffsetX = EnterOffsetX * (1.f - Eased);
		break;
	}
	case EPhase::Exit:
	{
		const float T = ExitDuration > 0.f ? FMath::Clamp(PhaseElapsed / ExitDuration, 0.f, 1.f) : 1.f;
		const float Eased = HudNoticeRow::EaseIn(T);
		Opacity = 1.f - Eased;
		OffsetX = ExitOffsetX * Eased;
		break;
	}
	case EPhase::Collapse:
	case EPhase::Finished:
	{
		Opacity = 0.f;
		OffsetX = ExitOffsetX;
		if (CollapseBox)
		{
			const float T = CollapseSeconds > 0.f ? FMath::Clamp(PhaseElapsed / CollapseSeconds, 0.f, 1.f) : 1.f;
			CollapseBox->SetHeightOverride(FMath::Max(0.f, CollapseFromHeight * (1.f - HudNoticeRow::EaseInOut(T))));
		}
		break;
	}
	default:
		break;
	}

	SetRenderOpacity(Opacity);
	SetRenderTranslation(FVector2D(OffsetX, 0.f));

	// 갱신 펄스: 밝기 PulseBrightness → 1 (ease-out)
	float Brightness = 1.f;
	if (PulseElapsed >= 0.f && PulseSeconds > 0.f)
	{
		const float T = FMath::Clamp(PulseElapsed / PulseSeconds, 0.f, 1.f);
		Brightness = FMath::Lerp(PulseBrightness, 1.f, HudNoticeRow::EaseOutCubic(T));
	}
	SetColorAndOpacity(FLinearColor(Brightness, Brightness, Brightness, 1.f));
}
