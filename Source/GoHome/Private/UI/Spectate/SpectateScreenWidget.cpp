#include "UI/Spectate/SpectateScreenWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/Texture2D.h"
#include "UI/Supply/SupplyUIStyle.h"

#define LOCTEXT_NAMESPACE "SpectateScreen"

using namespace SupplyUI;

namespace
{
	// 시안 :root 토큰(Docs/Dev/UI/spectate/prototype.html — 인게임 HUD와 같은 팔레트)
	FLinearColor SpecPanel() { return Hex(0x051012, 0.86f); }
	FLinearColor SpecCyan(float A = 1.f) { return Hex(0x19e3ee, A); }
	FLinearColor SpecInk() { return Hex(0xeef6f6); }
	FLinearColor SpecDim() { return Hex(0x7e9a9a); }
	FLinearColor SpecWarn() { return Hex(0xffb547); }
	FLinearColor SpecCrit() { return Hex(0xff4d4d); }
	FLinearColor SpecOk() { return Hex(0x5ef0b0); }
	FLinearColor SpecSignal() { return Hex(0xff5a4d); }	// SIGNAL LOST 제목
	FLinearColor SpecEyebrow() { return Hex(0xdaebeb, 0.62f); }
	// 플레이트(.86 패널) 위 속 빈 핍 구멍 — 어두운 장면 위 합성색으로 고정
	FLinearColor SpecPipHole() { return Hex(0x0a1416); }

	constexpr float SpecPlateWidth = 560.f;
	// 플레이트 안쪽(560 - 좌 3 테두리 포함 패딩 18 - 우 16) - 라벨 34 - 수치 44 - 간격 10×2
	constexpr float SpecTrackWidth = SpecPlateWidth - 18.f - 16.f - 34.f - 44.f - 20.f;

	const FSupplyEase SpecOutCurve = FSupplyEase::EaseOut();
	const FSupplyEase SpecInCurve = FSupplyEase::EaseIn();
	const FSupplyEase SpecSlideCurve = FSupplyEase::Bezier(.2f, .8f, .2f, 1.f);
	const FSupplyEase SpecVignetteCurve = FSupplyEase::Bezier(.3f, 0.f, .2f, 1.f);

	// 0..1 구간 진행도(시작 전 0, 끝나면 1)
	float SpecWindow(float T, float Start, float Duration)
	{
		return Duration <= 0.f ? (T >= Start ? 1.f : 0.f) : FMath::Clamp((T - Start) / Duration, 0.f, 1.f);
	}

	FText SpecFormatClock(float Seconds)
	{
		const int32 Total = FMath::Max(0, FMath::FloorToInt(Seconds));
		return FText::FromString(FString::Printf(TEXT("T+ %02d:%02d"), Total / 60, Total % 60));
	}

	UImage* SpecMakeImage(UWidgetTree* Tree, UTexture2D* Texture, const FLinearColor& Tint, bool bTile)
	{
		UImage* Image = Tree->ConstructWidget<UImage>();
		FSlateBrush Brush;
		Brush.SetResourceObject(Texture);
		Brush.ImageSize = Texture ? FVector2D(Texture->GetSizeX(), Texture->GetSizeY()) : FVector2D(1.f, 1.f);
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Brush.Tiling = bTile ? ESlateBrushTileType::Both : ESlateBrushTileType::NoTile;
		Image->SetBrush(Brush);
		Image->SetColorAndOpacity(Tint);
		return Image;
	}

	UTexture2D* SpecMakeTexture(int32 Width, int32 Height, TFunctionRef<FColor(int32, int32)> Pixel, TextureFilter Filter)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
		if (!Texture)
		{
			return nullptr;
		}
		Texture->Filter = Filter;
		Texture->SRGB = true;
		FColor* Data = static_cast<FColor*>(Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				Data[Y * Width + X] = Pixel(X, Y);
			}
		}
		Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}
}

// --- 수명 ---

void USpectateScreenWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetVisibility(ESlateVisibility::HitTestInvisible);
	BuildTextures();
	BuildTree();
}

void USpectateScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();
	Backend = TWeakInterfacePtr<ISpectateBackend>(GetOwningPlayer());
	LastPhase = ESpectatePhase::Inactive;
	LastSwitchSerial = 0;
	ShownCause = NAME_None;
	PipSignature.Reset();
	Animator.CancelAll();
}

void USpectateScreenWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!Backend.IsValid())
	{
		return;
	}
	const FSpectateView& View = Backend->GetSpectateView();

	if (View.Phase != LastPhase)
	{
		const ESpectatePhase Previous = LastPhase;
		LastPhase = View.Phase;
		EnterPhase(View.Phase, Previous, View);
	}
	if (View.SwitchSerial != LastSwitchSerial)
	{
		const bool bFirst = LastSwitchSerial == 0;
		LastSwitchSerial = View.SwitchSerial;
		if (!bFirst)
		{
			PlayCut(View.SwitchDirection);
		}
	}

	// 사망 화면 타임라인은 단계 경과 시간의 함수(motion.md A안) — 프레임이 튀어도 같은 장면
	const float T = View.PhaseSeconds;
	if (View.Phase == ESpectatePhase::Dying)
	{
		const float V = SpecVignetteCurve.Eval(SpecWindow(T, 0.f, .35f));
		VignetteAlpha = V;
		VignetteScale = FMath::Lerp(1.35f, 1.f, V);
		NoiseAlpha = (T >= .35f && T < .55f) ? .55f : 0.f;
		BlackAlpha = .93f * SpecWindow(T, .55f, .12f);
		CardAlpha = SpecOutCurve.Eval(SpecWindow(T, .55f, .16f));
		ConnectAlpha = SpecOutCurve.Eval(SpecWindow(T, 2.f, .15f));
		// 제목 글리치: 40ms × 3회, 각 2스텝
		if (T >= .55f && T < .67f)
		{
			const bool bOdd = FMath::FloorToInt((T - .55f) / .02f) % 2 == 1;
			TitleJitter = bOdd ? FVector2D(5.f, -2.f) : FVector2D(-6.f, 2.f);
		}
		else
		{
			TitleJitter = FVector2D::ZeroVector;
		}
	}
	else if (View.Phase == ESpectatePhase::Connecting)
	{
		const bool bFirstConnect = View.SwitchSerial == 0;
		VignetteAlpha = 0.f;
		NoiseAlpha = .7f;
		BlackAlpha = bFirstConnect ? .93f : 0.f;
		TitleJitter = FVector2D::ZeroVector;
		CardAlpha = bFirstConnect ? 1.f - SpecWindow(T, 0.f, .1f) : 0.f;
		// 첫 연결은 카드와 함께 빠지고, 대상 폰 복제를 기다리는 동안엔 다시 띄운다
		ConnectAlpha = (bFirstConnect && T < .6f) ? 1.f - SpecWindow(T, 0.f, .1f) : SpecOutCurve.Eval(SpecWindow(T, .6f, .15f));
	}
	else if (View.Phase == ESpectatePhase::TargetLost)
	{
		NoiseAlpha = .4f;
	}

	// 사인은 사망과 같은 번치로 오지만 늦게 와도 카드가 떠 있는 동안이면 채운다
	if (View.DeathCause != ShownCause && (View.Phase == ESpectatePhase::Dying || View.Phase == ESpectatePhase::Connecting))
	{
		ShowCause(View.DeathCause, CardAlpha > 0.f);
	}
	if (MetaText)
	{
		ShowIf(MetaText, View.DeathElapsedSeconds >= 0.f);
		MetaText->SetText(SpecFormatClock(View.DeathElapsedSeconds));
	}
	if (ConnectText)
	{
		ConnectText->SetText(FText::Format(LOCTEXT("Connecting", "피드 연결 중 ▸ {0}"), FText::FromString(View.TargetName)));
	}
	if (CamText)
	{
		CamText->SetText(FText::FromString(FString::Printf(TEXT("CAM %02d / %02d"), View.TargetSlot, View.Roster.Num())));
	}

	RenderPlate(View);
	Animator.Tick(InDeltaTime);
	ApplyLayers(InDeltaTime);
}

// --- 단계 연출 ---

void USpectateScreenWidget::EnterPhase(ESpectatePhase Phase, ESpectatePhase Previous, const FSpectateView& View)
{
	switch (Phase)
	{
	case ESpectatePhase::Spectating:
		NoiseAlpha = 0.f;
		VignetteAlpha = 0.f;
		ConnectAlpha = 0.f;
		CardAlpha = 0.f;
		if (FeedAlpha < 1.f)
		{
			// 피드 연결: 검정 .93→0 220ms ease-out, 프레임·플레이트 0→1 200ms
			const float BlackFrom = BlackAlpha;
			Animator.Play(TEXT("Black"), 0.f, .22f, [this, BlackFrom](float A) { BlackAlpha = FMath::Lerp(BlackFrom, 0.f, SpecOutCurve.Eval(A)); });
			Animator.Play(TEXT("Feed"), 0.f, .2f, [this](float A) { FeedAlpha = A; });
		}
		if (Previous == ESpectatePhase::TargetLost && ToastAlpha > 0.f)
		{
			// 토스트는 전환 뒤 1.6s 유지 후 250ms ease-in으로 빠진다
			Animator.Play(TEXT("Toast"), 1.6f, .25f, [this](float A) { ToastAlpha = 1.f - SpecInCurve.Eval(A); });
		}
		break;

	case ESpectatePhase::TargetLost:
		if (ToastText)
		{
			ToastText->SetText(FText::FromString(View.LostName));
		}
		Animator.Play(TEXT("Toast"), 0.f, .18f, [this](float A)
		{
			const float E = SpecSlideCurve.Eval(A);
			ToastAlpha = E;
			ToastShift = FMath::Lerp(10.f, 0.f, E);
		});
		break;

	case ESpectatePhase::NoSurvivor:
		NoiseAlpha = 0.f;
		ConnectAlpha = 0.f;
		CardAlpha = 0.f;
		VignetteAlpha = 0.f;
		Animator.Cancel(TEXT("Toast"));
		ToastAlpha = 0.f;
		{
			const float FeedFrom = FeedAlpha;
			const float BlackFrom = BlackAlpha;
			Animator.Play(TEXT("Feed"), 0.f, .2f, [this, FeedFrom](float A) { FeedAlpha = FeedFrom * (1.f - A); });
			Animator.Play(TEXT("Black"), 0.f, .4f, [this, BlackFrom](float A) { BlackAlpha = FMath::Lerp(BlackFrom, FMath::Max(BlackFrom, .7f), SpecOutCurve.Eval(A)); });
			Animator.Play(TEXT("NoSurvivor"), 0.f, .4f, [this](float A) { NoSurvivorAlpha = SpecOutCurve.Eval(A); });
		}
		break;

	default:
		break;
	}
}

void USpectateScreenWidget::PlayCut(int32 Direction)
{
	// 대상 전환: 밝은 플래시 .25→0 140ms + 노이즈 90ms, 이름 X ±18→0 · 0→1 180ms
	Animator.Play(TEXT("Flash"), 0.f, .14f, [this](float A) { FlashAlpha = .25f * (1.f - SpecOutCurve.Eval(A)); });
	Animator.Play(TEXT("CutNoise"), 0.f, .09f, [this](float A) { NoiseAlpha = A < 1.f ? .35f : 0.f; }, [this]() { NoiseAlpha = 0.f; });
	const float Sign = Direction >= 0 ? 1.f : -1.f;
	Animator.Play(TEXT("Name"), 0.f, .18f, [this, Sign](float A)
	{
		const float E = SpecSlideCurve.Eval(A);
		NameAlpha = E;
		NameShift = Sign * 18.f * (1.f - E);
	});
}

void USpectateScreenWidget::ShowCause(FName Cause, bool bAnimate)
{
	ShownCause = Cause;
	const bool bKnown = !Cause.IsNone();
	ShowIf(CauseRow, bKnown);
	if (!bKnown || !CauseValue)
	{
		return;
	}
	CauseValue->SetText(CauseText(Cause));
	if (bAnimate)
	{
		Animator.Play(TEXT("Cause"), 0.f, .15f, [this](float A)
		{
			if (CauseRow)
			{
				CauseRow->SetRenderOpacity(SpecOutCurve.Eval(A));
			}
		});
	}
	else if (CauseRow)
	{
		CauseRow->SetRenderOpacity(1.f);
	}
}

FText USpectateScreenWidget::CauseText(FName Cause)
{
	static const TMap<FName, FText> Texts = {
		{ TEXT("Suffocation"), LOCTEXT("CauseSuffocation", "산소가 바닥났다") },
		{ TEXT("Monster"), LOCTEXT("CauseMonster", "무언가에게 습격당했다") },
		{ TEXT("HydrothermalVent"), LOCTEXT("CauseVent", "열수에 휩쓸렸다") },
		{ TEXT("UnderwaterMine"), LOCTEXT("CauseMine", "기뢰가 폭발했다") },
		{ TEXT("ElectricSwitchboard"), LOCTEXT("CauseSwitchboard", "배전반에 감전됐다") },
		{ TEXT("SubmarineLeftBehind"), LOCTEXT("CauseLeftBehind", "잠수정에 오르지 못했다") },
	};
	const FText* Found = Texts.Find(Cause);
	return Found ? *Found : LOCTEXT("CauseUnknown", "원인을 알 수 없는 사고");
}

void USpectateScreenWidget::RenderPlate(const FSpectateView& View)
{
	if (!Plate)
	{
		return;
	}
	RebuildPips(View);

	const bool bLast = View.AliveCount <= 1;
	if (AliveText)
	{
		AliveText->SetText(FText::Format(LOCTEXT("Alive", "생존 {0}/{1}"), View.AliveCount, View.Roster.Num()));
	}
	if (PrevArrow && NextArrow)
	{
		PrevArrow->SetRenderOpacity(bLast ? .22f : 1.f);
		NextArrow->SetRenderOpacity(bLast ? .22f : 1.f);
	}
	if (NameText)
	{
		NameText->SetText(FText::FromString(View.TargetName));
	}
	if (LastText)
	{
		LastText->SetText(bLast ? LOCTEXT("LastSurvivor", "마지막 생존자") : FText::GetEmpty());
	}

	const FLinearColor HPColor = View.TargetHP <= 25.f ? SpecCrit() : SpecOk();
	const FLinearColor O2Color = View.TargetOxygenRatio <= .1f ? SpecCrit() : (View.TargetOxygenRatio <= .25f ? SpecWarn() : SpecCyan());
	if (HPFill && HPFillFace && HPValue)
	{
		HPFill->SetWidthOverride(SpecTrackWidth * View.TargetHPRatio);
		HPFillFace->SetBrushColor(HPColor);
		HPValue->SetText(View.bTargetVitalsValid ? FText::AsNumber(FMath::RoundToInt(View.TargetHP)) : Dash());
		HPValue->SetColorAndOpacity(FSlateColor(HPColor));
	}
	if (O2Fill && O2FillFace && O2Value)
	{
		O2Fill->SetWidthOverride(SpecTrackWidth * View.TargetOxygenRatio);
		O2FillFace->SetBrushColor(O2Color);
		O2Value->SetText(View.bTargetVitalsValid
			? FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(View.TargetOxygenRatio * 100.f)))
			: Dash());
		O2Value->SetColorAndOpacity(FSlateColor(O2Color));
	}
}

void USpectateScreenWidget::ApplyLayers(float DeltaTime)
{
	Clock += DeltaTime;

	// 노이즈 프레임 30fps 교체
	NoiseFrameTime += DeltaTime;
	if (Noise && NoiseTextures.Num() > 0 && NoiseFrameTime >= 1.f / 30.f)
	{
		NoiseFrameTime = 0.f;
		NoiseFrame = (NoiseFrame + 1) % NoiseTextures.Num();
		Noise->SetBrushResourceObject(NoiseTextures[NoiseFrame]);
	}

	if (Vignette)
	{
		Vignette->SetRenderOpacity(VignetteAlpha);
		Vignette->SetRenderScale(FVector2D(VignetteScale, VignetteScale));
	}
	if (Noise)
	{
		Noise->SetRenderOpacity(NoiseAlpha);
	}
	if (Black)
	{
		Black->SetRenderOpacity(BlackAlpha);
	}
	if (Flash)
	{
		Flash->SetRenderOpacity(FlashAlpha);
	}
	if (DeathCard)
	{
		DeathCard->SetRenderOpacity(CardAlpha);
	}
	if (CardTitle)
	{
		CardTitle->SetRenderTranslation(TitleJitter);
	}
	if (Connect)
	{
		Connect->SetRenderOpacity(ConnectAlpha);
	}
	for (int32 Index = 0; Index < ConnectDots.Num(); ++Index)
	{
		// 0.6s 주기 step, 0.2s씩 엇갈림
		const float Phase = FMath::Fmod(Clock - .2f * Index + 6.f, .6f);
		ConnectDots[Index]->SetRenderOpacity(Phase < .3f ? 1.f : .15f);
	}
	if (Frame)
	{
		Frame->SetRenderOpacity(FeedAlpha);
	}
	if (Scanlines)
	{
		Scanlines->SetRenderOpacity(FeedAlpha);
	}
	if (Plate)
	{
		Plate->SetRenderOpacity(FeedAlpha);
	}
	if (LiveDot)
	{
		LiveDot->SetRenderOpacity(FMath::Fmod(Clock, 1.2f) < .6f ? 1.f : .15f);
	}
	if (Toast)
	{
		Toast->SetRenderOpacity(ToastAlpha);
		Toast->SetRenderTranslation(FVector2D(0.f, ToastShift));
	}
	if (NameText)
	{
		NameText->SetRenderOpacity(NameAlpha);
		NameText->SetRenderTranslation(FVector2D(NameShift, 0.f));
	}
	if (NoSurvivor)
	{
		NoSurvivor->SetRenderOpacity(NoSurvivorAlpha);
	}
}

// --- 텍스처 ---

void USpectateScreenWidget::BuildTextures()
{
	// 노이즈 4프레임(240×135 회색 점) — 매 프레임 업로드 대신 순환
	FRandomStream Random(0x5eed);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		NoiseTextures.Add(SpecMakeTexture(240, 135, [&Random](int32, int32)
		{
			const uint8 V = static_cast<uint8>(Random.RandRange(0, 255));
			return FColor(V, V, V, 255);
		}, TF_Nearest));
	}

	// 붉은 비네트: 가운데 투명 → 가장자리 불투명(시안 inset box-shadow 420px 120px)
	VignetteTexture = SpecMakeTexture(256, 144, [](int32 X, int32 Y)
	{
		const float U = (X + .5f) / 256.f * 2.f - 1.f;
		const float V = (Y + .5f) / 144.f * 2.f - 1.f;
		const float R = FMath::Sqrt(U * U + V * V) / FMath::Sqrt(2.f);
		const float A = FMath::SmoothStep(.28f, .95f, R);
		return FColor(255, 255, 255, static_cast<uint8>(FMath::Clamp(A, 0.f, 1.f) * 255.f));
	}, TF_Bilinear);

	// 스캔라인: 1px 흰 줄(.035) + 2px 투명 반복
	ScanlineTexture = SpecMakeTexture(1, 3, [](int32, int32 Y)
	{
		return FColor(255, 255, 255, Y == 0 ? 9 : 0);
	}, TF_Nearest);
}

// --- 트리 ---

UWidget* USpectateScreenWidget::Circle(float Size, const FLinearColor& Color) const
{
	UImage* Image = WidgetTree->ConstructWidget<UImage>();
	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.OutlineSettings = FSlateBrushOutlineSettings(FVector4(0.f, 0.f, 0.f, 0.f), FSlateColor(FLinearColor::Transparent), 0.f);
	Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
	Brush.ImageSize = FVector2D(Size, Size);
	Image->SetBrush(Brush);
	Image->SetColorAndOpacity(Color);

	USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
	Box->SetWidthOverride(Size);
	Box->SetHeightOverride(Size);
	Box->AddChild(Image);
	return Box;
}

void USpectateScreenWidget::BuildTree()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}
	const FBuilder B(WidgetTree);

	UOverlay* Root = B.Overlay();
	Root->SetVisibility(ESlateVisibility::HitTestInvisible);
	WidgetTree->RootWidget = Root;

	Scanlines = SpecMakeImage(WidgetTree, ScanlineTexture, FLinearColor::White, true);
	AddFill(Root, Scanlines);

	Vignette = SpecMakeImage(WidgetTree, VignetteTexture, Hex(0xaa0000, .85f), false);
	Vignette->SetRenderTransformPivot(FVector2D(.5f, .5f));
	AddFill(Root, Vignette);

	Black = B.Fill(FLinearColor::Black);
	AddFill(Root, Black);

	Noise = SpecMakeImage(WidgetTree, NoiseTextures.Num() > 0 ? NoiseTextures[0].Get() : nullptr, FLinearColor::White, false);
	AddFill(Root, Noise);

	Frame = BuildFrame();
	AddFill(Root, Frame);

	DeathCard = BuildDeathCard();
	AddOverlay(Root, DeathCard, HAlign_Center, VAlign_Top, FMargin(0.f, 400.f, 0.f, 0.f));

	Connect = BuildConnect();
	AddOverlay(Root, Connect, HAlign_Center, VAlign_Top, FMargin(0.f, 640.f, 0.f, 0.f));

	Toast = BuildToast();
	AddOverlay(Root, Toast, HAlign_Center, VAlign_Bottom, FMargin(0.f, 0.f, 0.f, 252.f));

	Plate = BuildPlate();
	AddOverlay(Root, Plate, HAlign_Center, VAlign_Bottom, FMargin(0.f, 0.f, 0.f, 28.f));

	NoSurvivor = BuildNoSurvivor();
	AddOverlay(Root, NoSurvivor, HAlign_Center, VAlign_Top, FMargin(0.f, 480.f, 0.f, 0.f));

	Flash = B.Fill(Hex(0xccffee));
	AddFill(Root, Flash);

	ApplyLayers(0.f);
}

UWidget* USpectateScreenWidget::BuildFrame()
{
	const FBuilder B(WidgetTree);
	UOverlay* Layer = B.Overlay();

	// 네 귀퉁이 브래킷: 40×40, 2px, 화면 가장자리에서 12
	const FLinearColor BracketColor = SpecCyan(.55f);
	struct FCorner { EHorizontalAlignment H; EVerticalAlignment V; };
	for (const FCorner Corner : { FCorner{ HAlign_Left, VAlign_Top }, FCorner{ HAlign_Right, VAlign_Top }, FCorner{ HAlign_Left, VAlign_Bottom }, FCorner{ HAlign_Right, VAlign_Bottom } })
	{
		USizeBox* Box = B.Size(40.f, 40.f);
		UOverlay* Bracket = B.Overlay();
		Box->AddChild(Bracket);
		USizeBox* Horizontal = B.Size(40.f, 2.f);
		Horizontal->AddChild(B.Fill(BracketColor));
		AddOverlay(Bracket, Horizontal, HAlign_Fill, Corner.V);
		USizeBox* Vertical = B.Size(2.f, 40.f);
		Vertical->AddChild(B.Fill(BracketColor));
		AddOverlay(Bracket, Vertical, Corner.H, VAlign_Fill);
		AddOverlay(Layer, Box, Corner.H, Corner.V, FMargin(12.f));
	}

	// LIVE · CAM 02 / 04 (right 36 / top 30)
	UHorizontalBox* Live = B.HBox();
	LiveDot = Circle(10.f, SpecCrit());
	AddH(Live, LiveDot, FMargin(0.f, 0.f, 10.f, 0.f), VAlign_Center);
	AddH(Live, B.Text(LOCTEXT("Live", "LIVE"), Font(EFace::Mono, 14.f, .18f), Hex(0xdfe9e9)), FMargin(0.f), VAlign_Center);
	CamText = B.Text(FText::GetEmpty(), Font(EFace::Mono, 14.f, .18f), SpecDim());
	AddH(Live, CamText, FMargin(14.f, 0.f, 0.f, 0.f), VAlign_Center);
	AddOverlay(Layer, Live, HAlign_Right, VAlign_Top, FMargin(0.f, 30.f, 36.f, 0.f));
	return Layer;
}

UWidget* USpectateScreenWidget::BuildDeathCard()
{
	const FBuilder B(WidgetTree);
	UVerticalBox* Card = B.VBox();

	UTextBlock* Title = B.Text(LOCTEXT("SignalLost", "SIGNAL LOST"), Font(EFace::Mono, 84.f, .22f), SpecSignal());
	CardTitle = Title;
	AddV(Card, Title, FMargin(0.f), HAlign_Center);
	AddV(Card, B.Text(LOCTEXT("SignalLostKo", "신호 소실"), Font(EFace::SemiBold, 22.f, .5f), Hex(0xffc8c3, .8f)), FMargin(0.f, 16.f, 0.f, 0.f), HAlign_Center);

	UHorizontalBox* Cause = B.HBox();
	AddH(Cause, B.Text(LOCTEXT("CauseLabel", "CAUSE"), Font(EFace::Mono, 13.f, .2f), SpecDim()), FMargin(0.f, 0.f, 14.f, 0.f), VAlign_Center);
	CauseValue = B.Text(FText::GetEmpty(), Font(EFace::Medium, 20.f), Hex(0xe9eeee));
	AddH(Cause, CauseValue, FMargin(0.f), VAlign_Center);
	CauseRow = Cause;
	// 사인이 아직 안 왔어도 줄 높이는 유지(min-height 28) — 도착해도 아래 줄이 튀지 않게
	USizeBox* CauseBox = B.Size(-1.f, 28.f);
	CauseBox->AddChild(Cause);
	AddV(Card, CauseBox, FMargin(0.f, 44.f, 0.f, 0.f), HAlign_Center);
	ShowIf(CauseRow, false);

	MetaText = B.Text(FText::GetEmpty(), Font(EFace::Mono, 13.f, .14f), SpecDim());
	AddV(Card, MetaText, FMargin(0.f, 10.f, 0.f, 0.f), HAlign_Center);
	return Card;
}

UWidget* USpectateScreenWidget::BuildConnect()
{
	const FBuilder B(WidgetTree);
	UHorizontalBox* Row = B.HBox();
	ConnectText = B.Text(FText::GetEmpty(), Font(EFace::SemiBold, 16.f, .14f), SpecCyan());
	AddH(Row, ConnectText, FMargin(0.f), VAlign_Center);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		USizeBox* Dot = B.Size(6.f, 6.f);
		Dot->AddChild(B.Fill(SpecCyan()));
		AddH(Row, Dot, FMargin(6.f, 0.f, 0.f, 0.f), VAlign_Center);
		ConnectDots.Add(Dot);
	}
	return Row;
}

UWidget* USpectateScreenWidget::BuildToast()
{
	const FBuilder B(WidgetTree);
	UHorizontalBox* Shell = B.HBox();
	USizeBox* Bar = B.Size(3.f, -1.f);
	Bar->AddChild(B.Fill(SpecCrit()));
	AddH(Shell, Bar);

	UBorder* Body = B.Fill(SpecPanel(), FMargin(11.f, 9.f, 18.f, 9.f));
	UHorizontalBox* Row = B.HBox();
	Body->SetContent(Row);
	AddH(Row, B.Text(LOCTEXT("ToastLabel", "SIGNAL LOST"), Font(EFace::Mono, 12.f, .16f), SpecCrit()), FMargin(0.f, 0.f, 12.f, 0.f), VAlign_Center);
	ToastText = B.Text(FText::GetEmpty(), Font(EFace::SemiBold, 16.f), SpecInk());
	AddH(Row, ToastText, FMargin(0.f), VAlign_Center);
	AddH(Row, B.Text(LOCTEXT("ToastSuffix", " 신호 소실"), Font(EFace::SemiBold, 16.f), SpecInk()), FMargin(0.f), VAlign_Center);
	AddH(Shell, Body);
	return Shell;
}

UWidget* USpectateScreenWidget::BuildPlate()
{
	const FBuilder B(WidgetTree);
	USizeBox* Box = B.Size(SpecPlateWidth);
	UHorizontalBox* Shell = B.HBox();
	Box->AddChild(Shell);

	USizeBox* Bar = B.Size(3.f, -1.f);
	Bar->AddChild(B.Fill(SpecCyan()));
	AddH(Shell, Bar);
	UBorder* Body = B.Fill(SpecPanel(), FMargin(15.f, 12.f, 16.f, 14.f));
	AddH(Shell, Body, FMargin(0.f), VAlign_Fill, 1.f);
	UVerticalBox* Column = B.VBox();
	Body->SetContent(Column);

	// SPECTATING ··· 생존 2/4 ●●○●
	UHorizontalBox* Top = B.HBox();
	AddH(Top, B.Text(LOCTEXT("Spectating", "SPECTATING"), Font(EFace::Mono, 12.f, .17f), SpecEyebrow()), FMargin(0.f), VAlign_Center, 1.f);
	AliveText = B.Text(FText::GetEmpty(), Font(EFace::SemiBold, 12.f), SpecDim());
	AddH(Top, AliveText, FMargin(0.f, 0.f, 12.f, 0.f), VAlign_Center);
	Pips = B.HBox();
	AddH(Top, Pips, FMargin(0.f), VAlign_Center);
	AddV(Column, Top);

	// ◀ 이름 ▶ (72 | 1fr | 72)
	auto Arrow = [&B](const FText& Glyph, const FText& Key) -> UWidget*
	{
		USizeBox* Cell = B.Size(72.f);
		UVerticalBox* Stack = B.VBox();
		Cell->AddChild(Stack);
		AddV(Stack, B.Text(Glyph, Font(EFace::Bold, 26.f), SpecCyan()), FMargin(0.f), HAlign_Center);
		AddV(Stack, B.Text(Key, Font(EFace::Mono, 10.f, .12f), SpecDim()), FMargin(0.f, 3.f, 0.f, 0.f), HAlign_Center);
		return Cell;
	};
	UHorizontalBox* Who = B.HBox();
	PrevArrow = Arrow(LOCTEXT("PrevGlyph", "◀"), LOCTEXT("PrevKey", "RMB"));
	AddH(Who, PrevArrow, FMargin(0.f), VAlign_Center);
	NameText = B.Text(FText::GetEmpty(), Font(EFace::Bold, 28.f), SpecInk());
	NameText->SetJustification(ETextJustify::Center);
	NameText->SetClipping(EWidgetClipping::ClipToBounds);
	NameText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);	// 시안 text-overflow: ellipsis
	AddH(Who, NameText, FMargin(0.f), VAlign_Center, 1.f);
	NextArrow = Arrow(LOCTEXT("NextGlyph", "▶"), LOCTEXT("NextKey", "LMB"));
	AddH(Who, NextArrow, FMargin(0.f), VAlign_Center);
	AddV(Column, Who, FMargin(0.f, 10.f, 0.f, 0.f));

	USizeBox* LastBox = B.Size(-1.f, 14.f);
	LastText = B.Text(FText::GetEmpty(), Font(EFace::Bold, 11.f, .2f), SpecWarn());
	LastBox->AddChild(LastText);
	AddV(Column, LastBox, FMargin(0.f, 2.f, 0.f, 0.f), HAlign_Center);

	// HP / O₂ 바
	auto Vital = [&B, Column](const FText& Label, USizeBox*& OutFill, UBorder*& OutFace, UTextBlock*& OutValue)
	{
		UHorizontalBox* Row = B.HBox();
		USizeBox* LabelBox = B.Size(34.f);
		LabelBox->AddChild(B.Text(Label, Font(EFace::Mono, 11.f, .14f), SpecDim()));
		AddH(Row, LabelBox, FMargin(0.f, 0.f, 10.f, 0.f), VAlign_Center);

		USizeBox* Track = B.Size(SpecTrackWidth, 8.f);
		UOverlay* TrackLayer = B.Overlay();
		Track->AddChild(TrackLayer);
		AddFill(TrackLayer, B.Fill(Hex(0x122223)));
		OutFill = B.Size(0.f, 8.f);
		OutFace = B.Fill(SpecOk());
		OutFill->AddChild(OutFace);
		AddOverlay(TrackLayer, OutFill, HAlign_Left, VAlign_Fill);
		AddH(Row, Track, FMargin(0.f, 0.f, 10.f, 0.f), VAlign_Center);

		USizeBox* ValueBox = B.Size(44.f);
		OutValue = B.Text(FText::GetEmpty(), Font(EFace::Mono, 15.f), SpecOk());
		OutValue->SetJustification(ETextJustify::Right);
		ValueBox->AddChild(OutValue);
		AddH(Row, ValueBox, FMargin(0.f), VAlign_Center);
		AddV(Column, Row, FMargin(0.f, 8.f, 0.f, 0.f));
	};
	USizeBox* Fill = nullptr;
	UBorder* Face = nullptr;
	UTextBlock* Value = nullptr;
	Vital(LOCTEXT("HP", "HP"), Fill, Face, Value);
	HPFill = Fill;
	HPFillFace = Face;
	HPValue = Value;
	Vital(LOCTEXT("O2", "O₂"), Fill, Face, Value);
	O2Fill = Fill;
	O2FillFace = Face;
	O2Value = Value;
	return Box;
}

UWidget* USpectateScreenWidget::BuildNoSurvivor()
{
	const FBuilder B(WidgetTree);
	UVerticalBox* Stack = B.VBox();
	AddV(Stack, B.Text(LOCTEXT("NoSurvivors", "NO SURVIVORS"), Font(EFace::Mono, 44.f, .24f), SpecSignal()), FMargin(0.f), HAlign_Center);
	AddV(Stack, B.Text(LOCTEXT("NoSurvivorsSub", "생존자 없음 — 구조 실패 화면으로 넘어갑니다"), Font(EFace::Regular, 15.f), SpecDim()), FMargin(0.f, 12.f, 0.f, 0.f), HAlign_Center);
	return Stack;
}

void USpectateScreenWidget::RebuildPips(const FSpectateView& View)
{
	FString Signature;
	for (const FSpectateRosterEntry& Entry : View.Roster)
	{
		Signature.AppendChar(Entry.bSelf ? TEXT('m') : (Entry.bDead ? TEXT('d') : TEXT('a')));
		Signature.AppendChar(Entry.bCurrent ? TEXT('*') : TEXT('.'));
	}
	if (!Pips || Signature == PipSignature)
	{
		return;
	}
	PipSignature = Signature;
	Pips->ClearChildren();

	const FBuilder B(WidgetTree);
	for (int32 Index = 0; Index < View.Roster.Num(); ++Index)
	{
		const FSpectateRosterEntry& Entry = View.Roster[Index];
		// 현재 대상: 바깥 청록 고리(3.5) + 어두운 틈(2) / 나: 빨간 테두리 / 사망: 회색 테두리 / 생존: 초록 채움
		UOverlay* Pip = B.Overlay();
		if (Entry.bCurrent)
		{
			AddOverlay(Pip, Circle(17.f, SpecCyan()), HAlign_Center, VAlign_Center);
			AddOverlay(Pip, Circle(14.f, Hex(0x051012)), HAlign_Center, VAlign_Center);
		}
		const bool bHollow = Entry.bSelf || Entry.bDead;
		const FLinearColor Color = Entry.bSelf ? SpecCrit() : (Entry.bDead ? Hex(0x4a5a5a) : SpecOk());
		AddOverlay(Pip, Circle(10.f, Color), HAlign_Center, VAlign_Center);
		if (bHollow)
		{
			AddOverlay(Pip, Circle(7.f, SpecPipHole()), HAlign_Center, VAlign_Center);
		}
		// 셀 17(현재 대상 고리 포함) — 핍 중심 간격 17 ≈ 시안 16(10 + gap 6)
		USizeBox* Cell = B.Size(17.f, 17.f);
		Cell->AddChild(Pip);
		AddH(Pips, Cell, FMargin(0.f), VAlign_Center);
	}
}

#undef LOCTEXT_NAMESPACE
