#include "UI/ZoneSelect/ZoneSelectScreenWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Core/ExpeditionZoneDataAsset.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/SlateRenderer.h"
#include "TextureResource.h"
#include "UI/Common/HairlineWidget.h"
#include "UI/Common/StrokeGlyphWidget.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "ZoneSelect"

DEFINE_LOG_CATEGORY_STATIC(LogZoneSelectUI, Log, All);

using namespace SupplyUI;

#if !UE_BUILD_SHIPPING
// 활성 프레임 캡처용: 모션 속도 배율(0.05 = 20배 느리게)
static TAutoConsoleVariable<float> CVarZoneSelectMotionScale(
	TEXT("GoHome.ZoneSelect.MotionScale"),
	1.f,
	TEXT("탐사 목적지 화면 모션 재생 속도 배율(개발용)."),
	ECVF_Cheat);
#endif

namespace
{
	// 시안 :root 토큰과 반투명 색. 반투명 선은 아래 배경 위 결과색으로 고정한다(UE 합성은 리니어라 CSS 알파를 그대로 쓰면 밝다).
	FLinearColor ZMuted() { return Hex(0xa1acc5); }
	FLinearColor ZLine() { return MixSRGB(0xb7c7ff, 0x30 / 255.f, 0x080f20); }
	FLinearColor CardEdge() { return MixSRGB(0x7283ac, 0x54 / 255.f, 0x112547); }
	FLinearColor KeyEdge() { return MixSRGB(0xa1acc5, 0x5c / 255.f, 0x080f20); }
	FLinearColor DisabledFace() { return Hex(0x172841); }
	FLinearColor DisabledEdge() { return MixSRGB(0xa3b9f0, 0x33 / 255.f, 0x172841); }

	const FSupplyEase& DefaultEase()
	{
		static const FSupplyEase Ease = FSupplyEase::Bezier(.22f, .8f, .2f, 1.f);	// --ease
		return Ease;
	}

	const FSupplyEase& CssEase()
	{
		static const FSupplyEase Ease = FSupplyEase::Bezier(.25f, .1f, .25f, 1.f);	// CSS 'ease' 키워드
		return Ease;
	}

	const FName EntryTrack(TEXT("Zone.Entry"));
	const FName SelectPhotoTrack(TEXT("Zone.SelectPhoto"));
	const FName SelectDetailTrack(TEXT("Zone.SelectDetail"));
	const FName FeedbackTrack(TEXT("Zone.Feedback"));
	const FName ExitTrack(TEXT("Zone.Exit"));

	constexpr float EntrySeconds = 1.04f;
	constexpr float FinalFrame = 100.f;		// ApplyEntranceFrame: 모든 트랙 끝 = 기본 CSS 상태
	constexpr float MaxAppWidth = 1920.f;
	constexpr float CardGap = 10.f;
	constexpr float SelectedGrow = 2.65f;
	constexpr float PhotoScale = 1.04f;

	// 시안 맑은 고딕은 굵기 2단(Regular/Bold)뿐이라 font-weight 700~900은 전부 Malgun Gothic Bold로 그려진다.
	// 그 획 굵기(잉크 밀도 17.7)에 가장 가까운 Noto Sans KR은 SemiBold(18.7) — Black(22.0)은 시안보다 확연히 두껍다(2026-10-13 측정).
	// Segoe UI 800은 Windows 글꼴 매칭에서 Segoe UI Black(900)으로 간다.
	constexpr EFace MalgunBold = EFace::SemiBold;

	// 미리보지 않는 카드 / hover 사진 보정(CSS filter)
	constexpr float IdleBrightness = .85f;
	constexpr float IdleSaturation = .35f;
	constexpr float HoverBrightness = 1.25f;
	constexpr float HoverSaturation = .6f;

	float Progress(float Time, float Delay, float Duration)
	{
		return FMath::Clamp((Time - Delay) / Duration, 0.f, 1.f);
	}

	UTextBlock* MakeText(const FBuilder& B, const FText& Value, EFace Face, float Px, float LetterEm, const FLinearColor& Color, ERef Ref, float LineHeight, float Baseline = -1.f, bool bItalic = false)
	{
		UTextBlock* Text = B.Text(Value, Font(Face, Px, LetterEm, bItalic), Color);
		Typeset(Text, Ref, LineHeight, Baseline);
		return Text;
	}

	// 투명도·이동(진입 등 임시 트랙). Typeset이 쓰는 텍스트 RenderTranslation과 겹치지 않게 텍스트가 아닌 래퍼에만 쓴다.
	void SetMotion(UWidget* Widget, float Opacity, float X, float Y)
	{
		if (Widget)
		{
			Widget->SetRenderOpacity(Opacity);
			Widget->SetRenderTranslation(FVector2D(X, Y));
		}
	}

	FLinearColor LerpSRGB(const FLinearColor& A, const FLinearColor& B, float T)
	{
		const FColor SA = A.ToFColorSRGB();
		const FColor SB = B.ToFColorSRGB();
		auto Channel = [T](uint8 X, uint8 Y) { return static_cast<uint8>(FMath::RoundToInt(FMath::Lerp(static_cast<float>(X), static_cast<float>(Y), T))); };
		return FLinearColor::FromSRGBColor(FColor(Channel(SA.R, SB.R), Channel(SA.G, SB.G), Channel(SA.B, SB.B)));
	}

	// 한국어 조사: 받침 유무로 을/를, (으)로(ㄹ 받침은 로)
	int32 FinalConsonant(const FString& Word)
	{
		for (int32 Index = Word.Len() - 1; Index >= 0; --Index)
		{
			const TCHAR Char = Word[Index];
			if (Char >= 0xAC00 && Char <= 0xD7A3)
			{
				return (Char - 0xAC00) % 28;
			}
			if (!FChar::IsWhitespace(Char))
			{
				return 0;
			}
		}
		return 0;
	}

	FString ObjectParticle(const FString& Word)
	{
		return FinalConsonant(Word) != 0 ? TEXT("을") : TEXT("를");
	}

	FString DirectionParticle(const FString& Word)
	{
		const int32 Final = FinalConsonant(Word);
		return Final == 0 || Final == 8 ? TEXT("로") : TEXT("으로");
	}

	FString GroupThousands(int32 Value)
	{
		FString Digits = FString::FromInt(FMath::Abs(Value));
		for (int32 Index = Digits.Len() - 3; Index > 0; Index -= 3)
		{
			Digits.InsertAt(Index, TEXT(","));
		}
		return Value < 0 ? TEXT("-") + Digits : Digits;
	}

	UStrokeGlyphWidget* Glyph(const FBuilder& B, const FVector2D& ViewBox, const TArray<TArray<FVector2D>>& Paths, float Stroke, const FLinearColor& Color)
	{
		UStrokeGlyphWidget* Widget = B.Tree->ConstructWidget<UStrokeGlyphWidget>();
		Widget->SetGlyph(ViewBox, Paths, Stroke);
		Widget->SetStrokeColor(Color);
		return Widget;
	}

	// kbd.control-key: 높이 22, border 1px, padding 2 4(가로는 키마다). Width < 0 이면 내용 폭.
	UWidget* ControlKey(const FBuilder& B, UWidget* Content, float Width, float PaddingX)
	{
		UOverlay* Chip = B.Overlay();
		AddFill(Chip, B.Ring(KeyEdge(), 1.f));
		AddOverlay(Chip, Content, HAlign_Center, VAlign_Center, FMargin(PaddingX + 1.f, 3.f));
		USizeBox* Size = B.Size(Width, 22.f);
		Size->AddChild(Chip);
		return Size;
	}

	// 포커스 링(outline 3px lime). Inset > 0: 안쪽(카드 outline-offset −5 → 테두리 상자 안 2..5), < 0: 바깥(+5 → −8..−5).
	UWidget* AddFocusRing(const FBuilder& B, UOverlay* Parent, float Inset)
	{
		UWidget* Ring = B.Ring(Lime(), 3.f);
		AddOverlay(Parent, Ring, HAlign_Fill, VAlign_Fill, FMargin(Inset));
		Ring->SetVisibility(ESlateVisibility::Collapsed);
		return Ring;
	}

	// 줄 상자를 CSS line-height로 고정한다 — 대체 글꼴의 Slate 줄 높이가 ±1px 달라 세로로 쌓이면 누적된다(제목 64.4 → 65.4 실측).
	UWidget* Lined(const FBuilder& B, UWidget* Content, float LineHeight)
	{
		USizeBox* Box = B.Size(-1.f, LineHeight);
		SetContent(Box, Content, HAlign_Fill, VAlign_Top);
		return Box;
	}

	void SetShown(UWidget* Widget, bool bShow)
	{
		if (Widget)
		{
			Widget->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}
	}
}

// --- FZoneSelectTween ---

void FZoneSelectTween::Snap(float InValue)
{
	From = To = Value = InValue;
	Elapsed = Duration = 0.f;
}

void FZoneSelectTween::Set(float Target, float DurationSeconds, const FSupplyEase& InEase)
{
	if (FMath::IsNearlyEqual(Target, To))
	{
		return;
	}
	From = Value;
	To = Target;
	Elapsed = 0.f;
	Duration = DurationSeconds;
	Ease = InEase;
	if (Duration <= 0.f)
	{
		Value = To;
	}
}

void FZoneSelectTween::Tick(float DeltaSeconds)
{
	if (Duration <= 0.f || Elapsed >= Duration)
	{
		Value = To;
		return;
	}
	Elapsed += DeltaSeconds;
	Value = FMath::Lerp(From, To, Ease.Eval(FMath::Clamp(Elapsed / Duration, 0.f, 1.f)));
}

// --- 수명 ---

void UZoneSelectScreenWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	// UserWidget 기본값은 SelfHitTestInvisible — 자식은 전부 HitTestInvisible이라 호스트가 직접 마우스를 받는다.
	SetVisibility(ESlateVisibility::Visible);
	BuildTree();
}

void UZoneSelectScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();
	bClosing = false;
	Backend = TWeakInterfacePtr<IZoneSelectBackend>(GetOwningPlayer());
	LoadedPhotoMaterial = PhotoMaterial.LoadSynchronous();

	Zones.Reset();
	if (Backend.IsValid())
	{
		for (const UExpeditionZoneDataAsset* Zone : Backend->GetSelectableZones())
		{
			Zones.Add(Zone);
		}
		DestinationHandle = Backend->OnTeamDestinationChanged().AddUObject(this, &ThisClass::HandleTeamDestinationChanged);
	}
	BuildCards();

	// 최초 진입은 서버 현재 값을 읽는다(매번 미지정으로 초기화하지 않음). 재진입은 마지막 미리보기 카드로 포커스 복원.
	Committed = Backend.IsValid() ? FindZone(Backend->GetTeamDestination()) : INDEX_NONE;
	const int32 LastPreview = Backend.IsValid() ? FindZone(Backend->GetLastPreviewedZone()) : INDEX_NONE;
	Selected = LastPreview != INDEX_NONE ? LastPreview : (Committed != INDEX_NONE ? Committed : 0);
	FocusKind = EFocus::Card;
	FocusCard = Selected;
	bFocusVisible = true;	// 상호작용 키로 연 화면 — 키보드 포커스(:focus-visible)로 본다

	const float ViewportScale = UWidgetLayoutLibrary::GetViewportScale(this);
	if (ViewportScale > KINDA_SMALL_NUMBER)
	{
		ApplyViewSize(UWidgetLayoutLibrary::GetViewportSize(this) / ViewportScale);
	}

	if (Status)
	{
		Status->SetText(LOCTEXT("Prompt", "탐사지를 고른 뒤 팀 목적지로 지정하세요."));
	}
	RenderState();
	ApplyCardTargets(true);
	RenderFocus();
	UpdateCardLayout();
	PlayEntrance();
}

void UZoneSelectScreenWidget::NativeDestruct()
{
	Animator.CancelAll();
	if (Backend.IsValid())
	{
		Backend->OnTeamDestinationChanged().Remove(DestinationHandle);
		if (Zones.IsValidIndex(Selected) && Zones[Selected].IsValid())
		{
			Backend->SetLastPreviewedZone(Zones[Selected]->ZoneId);
		}
	}
	DestinationHandle.Reset();
	Super::NativeDestruct();
}

void UZoneSelectScreenWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	ApplyViewSize(MyGeometry.GetLocalSize());

	float Delta = InDeltaTime;
#if !UE_BUILD_SHIPPING
	Delta *= FMath::Max(0.f, CVarZoneSelectMotionScale.GetValueOnGameThread());
#endif

	for (TArray<FZoneSelectTween>* Set : { &Grow, &Bright, &Sat, &TagOpacity, &CaptionOpacity, &Mark, &ArrowShift })
	{
		for (FZoneSelectTween& Tween : *Set)
		{
			Tween.Tick(Delta);
		}
	}
	ApplyHover.Tick(Delta);
	Animator.Tick(Delta);

	for (int32 Index = 0; Index < Cards.Num(); ++Index)
	{
		const FZoneSelectCardView& Card = Cards[Index];
		if (Card.PreviewTag)
		{
			Card.PreviewTag->SetRenderOpacity(TagOpacity[Index].Value);
		}
		if (Card.Caption)
		{
			Card.Caption->SetRenderOpacity(CaptionOpacity[Index].Value);
		}
		if (Card.ChosenMark)
		{
			Card.ChosenMark->SetRenderScale(FVector2D(1.f, Mark[Index].Value));
		}
		if (Card.Arrow)
		{
			Card.Arrow->SetRenderTranslation(FVector2D(ArrowShift[Index].Value, 0.f));
		}
	}
	// .apply:hover 배경 종이색 → 흰색(160ms ease). 지정된 맵이면 비활성 면이 이긴다(CSS 뒤 규칙).
	if (ApplyFace && Selected != Committed)
	{
		ApplyFace->SetBrushColor(LerpSRGB(Paper(), FLinearColor::White, ApplyHover.Value));
	}
	UpdateCardLayout();
}

// --- 트리 ---

void UZoneSelectScreenWidget::BuildTree()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}
	const FBuilder B(WidgetTree);

	UOverlay* Root = B.Overlay();
	Root->SetClipping(EWidgetClipping::ClipToBounds);
	Root->SetVisibility(ESlateVisibility::HitTestInvisible);
	WidgetTree->RootWidget = Root;

	// body 배경
	AddFill(Root, B.Fill(Ink()));

	// .app — 최대 폭 1920 가운데. :before 방사 그라데이션은 앱 상자 안이라 앱 투명도를 따른다.
	AppBox = B.Size(MaxAppWidth);
	AddOverlay(Root, AppBox, HAlign_Center, VAlign_Fill);
	UOverlay* AppOverlay = B.Overlay();
	AppBox->AddChild(AppOverlay);
	App = AppOverlay;

	Backdrop = WidgetTree->ConstructWidget<UImage>();
	AddFill(AppOverlay, Backdrop);

	AppPadding = B.Box(FMargin(63.36f, 25.f, 63.36f, 18.f));
	AddFill(AppOverlay, AppPadding);
	UVerticalBox* Column = B.VBox();
	SetContent(AppPadding, Column);

	AddV(Column, BuildHeader(B));
	AddV(Column, BuildHeading(B));
	AddV(Column, BuildGallery(B), FMargin(0.f), HAlign_Fill, true);
	AddV(Column, BuildRuler(B), FMargin(0.f, 9.f, 0.f, 0.f));
	AddV(Column, BuildBriefing(B));
	AddV(Column, BuildBottom(B));

	// .toast: margin-top 12, min-height 20, 12px/1.6 lime
	USizeBox* ToastBox = B.Size(-1.f, 20.f);
	Status = MakeText(B, FText::GetEmpty(), EFace::Regular, 12.f, 0.f, Lime(), ERef::Malgun, 19.2f);
	UBorder* StatusMotion = B.Box();
	SetContent(StatusMotion, Status);
	SetContent(ToastBox, StatusMotion, HAlign_Fill, VAlign_Top);
	StatusWrap = StatusMotion;
	AddV(Column, ToastBox, FMargin(0.f, 12.f, 0.f, 0.f));

	Opening = BuildOpening(B);
	AddFill(Root, Opening);
}

UWidget* UZoneSelectScreenWidget::BuildHeader(const FBuilder& B)
{
	// .top: 행(높이 38 — 닫기 버튼) + padding-bottom 19 + border-bottom 1
	UVerticalBox* Top = B.VBox();
	Header = Top;

	UHorizontalBox* Row = B.HBox();
	AddV(Top, Row);

	// .brand: 32×32 마름모(1px 종이색 테두리, 45°) + ↧ — gap 13
	UOverlay* Icon = B.Overlay();
	// 45° 회전한 1px 테두리 — 회전 채움 막대는 DPI < 1에서 변이 흐리게 끊겨(fresh 비교 지적) 안티에일리어싱 선으로 닫힌 마름모를 그린다.
	// 32 상자 대각선 45.25가 아이콘 상자 밖으로 나가므로 바깥 여백을 음수로.
	UStrokeGlyphWidget* Diamond = Glyph(B, FVector2D(46.25f, 46.25f), { {
		FVector2D(23.125f, .5f), FVector2D(45.75f, 23.125f), FVector2D(23.125f, 45.75f), FVector2D(.5f, 23.125f), FVector2D(23.125f, .5f) } }, 1.f, Paper());
	AddOverlay(Icon, Diamond, HAlign_Fill, VAlign_Fill, FMargin(-7.125f));
	// ↧(Segoe UI Symbol 19px — 대체 글꼴에 없음): 글리프 윤곽 좌표에서 옮긴 막대·대·화살촉
	AddFill(Icon, Glyph(B, FVector2D(32.f, 32.f), {
		{ FVector2D(12.89f, 11.72f), FVector2D(19.08f, 11.72f) },
		{ FVector2D(15.98f, 12.3f), FVector2D(15.98f, 23.4f) },
		{ FVector2D(12.95f, 20.4f), FVector2D(15.98f, 23.4f), FVector2D(19.02f, 20.4f) },
		{ FVector2D(13.9f, 20.6f), FVector2D(15.98f, 22.6f), FVector2D(18.06f, 20.6f) } }, 1.3f, Paper()));
	USizeBox* IconBox = B.Size(32.f, 32.f);
	IconBox->AddChild(Icon);
	AddH(Row, IconBox, FMargin(0.f), VAlign_Center);

	AddH(Row, MakeText(B, LOCTEXT("Brand", "탐사 목적지"), MalgunBold, 16.f, -.04f, Paper(), ERef::Malgun, 21.f), FMargin(13.f, 0.f, 0.f, 0.f), VAlign_Center);

	// .top-caption: border-left 1px, padding-left 20, 10px 700 .18em muted
	UHorizontalBox* Caption = B.HBox();
	USizeBox* CaptionRule = B.Size(1.f, 15.f);
	CaptionRule->AddChild(VRule(B, ZLine()));
	AddH(Caption, CaptionRule, FMargin(0.f), VAlign_Center);
	AddH(Caption, MakeText(B, LOCTEXT("Bureau", "EXPEDITION BUREAU / 탐사 항해국"), EFace::Bold, 10.f, .18f, ZMuted(), ERef::Segoe, 15.f), FMargin(20.f, 0.f, 0.f, 0.f), VAlign_Center);
	AddH(Row, Caption, FMargin(13.f, 0.f, 0.f, 0.f), VAlign_Center);

	AddH(Row, B.Spacer(0.f, 0.f), FMargin(0.f), VAlign_Fill, 1.f);

	// 닫기: padding 8 0 — [ESC] 10 닫기 10 ×
	UOverlay* CloseOverlay = B.Overlay();
	UHorizontalBox* CloseRow = B.HBox();
	UOverlay* EscChip = B.Overlay();
	AddFill(EscChip, B.Ring(KeyEdge(), 1.f));
	AddOverlay(EscChip, MakeText(B, FText::FromString(TEXT("ESC")), EFace::Black, 10.f, 0.f, ZMuted(), ERef::Segoe, 14.f), HAlign_Center, VAlign_Center, FMargin(6.f, 4.f));
	AddH(CloseRow, EscChip, FMargin(0.f), VAlign_Center);
	AddH(CloseRow, MakeText(B, LOCTEXT("Close", "닫기"), EFace::Regular, 12.f, 0.f, Paper(), ERef::Malgun, 16.f), FMargin(10.f, 0.f, 0.f, 0.f), VAlign_Center);
	// ×(맑은 고딕 12px — 상자 8.4×16 안 x 1.6..6.8, y 6.9..12.1): Noto ×는 훨씬 커서 선으로
	USizeBox* CrossIcon = B.Size(8.41f, 16.f);
	CrossIcon->AddChild(Glyph(B, FVector2D(8.41f, 16.f), {
		{ FVector2D(1.9f, 7.2f), FVector2D(6.5f, 11.8f) }, { FVector2D(6.5f, 7.2f), FVector2D(1.9f, 11.8f) } }, .9f, Paper()));
	AddH(CloseRow, CrossIcon, FMargin(10.f, 0.f, 0.f, 0.f), VAlign_Center);
	AddOverlay(CloseOverlay, CloseRow, HAlign_Fill, VAlign_Center, FMargin(0.f, 8.f));
	CloseFocus = AddFocusRing(B, CloseOverlay, -8.f);
	USizeBox* CloseBox = B.Size(-1.f, 38.f);
	CloseBox->AddChild(CloseOverlay);
	CloseButton = CloseBox;
	AddH(Row, CloseBox, FMargin(0.f), VAlign_Center);

	AddV(Top, Rule(B, ZLine()), FMargin(0.f, 19.f, 0.f, 0.f));
	return Top;
}

UWidget* UZoneSelectScreenWidget::BuildHeading(const FBuilder& B)
{
	// .heading: padding 35 0(≥1700), align-items flex-end
	UHorizontalBox* Row = B.HBox();
	Heading = Row;

	UVerticalBox* Left = B.VBox();
	AddV(Left, Lined(B, MakeText(B, LOCTEXT("Kicker", "DESTINATION SELECT / 탐사 목적지"), EFace::Bold, 10.f, .18f, ZMuted(), ERef::Segoe, 15.f), 15.f));

	// h1: 900 italic 56px/1.15 Arial Black, −.06em — DIVE만 라임
	UHorizontalBox* Title = B.HBox();
	AddH(Title, MakeText(B, FText::FromString(TEXT("NEXT ")), EFace::Display, 56.f, -.06f, Paper(), ERef::ArialBlack, 64.4f, -1.f, true));
	AddH(Title, MakeText(B, FText::FromString(TEXT("DIVE")), EFace::Display, 56.f, -.06f, Lime(), ERef::ArialBlack, 64.4f, -1.f, true));
	AddV(Left, Lined(B, Title, 64.4f), FMargin(0.f, 12.f, 0.f, 0.f));
	AddH(Row, Left, FMargin(0.f, 35.f, 0.f, 35.f), VAlign_Bottom);

	AddH(Row, B.Spacer(20.f, 0.f), FMargin(0.f), VAlign_Fill, 1.f);
	AddH(Row, MakeText(B, LOCTEXT("Guide", "탐사지를 살펴보고 팀의 다음 목적지를 정하세요."), EFace::Regular, 12.f, 0.f, ZMuted(), ERef::Malgun, 16.f),
		FMargin(0.f, 0.f, 0.f, 35.f), VAlign_Bottom);
	return Row;
}

UWidget* UZoneSelectScreenWidget::BuildGallery(const FBuilder& B)
{
	// .gallery-shell(flex 1) > .gallery(높이 clamp(360, 52vh, 700), 위 정렬) + .aperture(셸 전체)
	UOverlay* Shell = B.Overlay();

	GallerySize = B.Size(-1.f, 561.6f);
	Gallery = B.HBox();
	GallerySize->AddChild(Gallery);
	AddOverlay(Shell, GallerySize, HAlign_Fill, VAlign_Top);

	// 셔터: 폭 50.1%, 코발트, 안쪽 1px 선(위아래 20%, 이음매 반대쪽 18px)
	UCanvasPanel* Aperture = B.Canvas();
	Aperture->SetClipping(EWidgetClipping::ClipToBounds);
	auto MakeShutter = [&B, Aperture](bool bLeft) -> UWidget*
	{
		UOverlay* Shutter = B.Overlay();
		AddFill(Shutter, B.Fill(Blue()));
		UCanvasPanel* LineCanvas = B.Canvas();
		UCanvasPanelSlot* LineSlot = LineCanvas->AddChildToCanvas(VRule(B, MixSRGB(0xf0f1de, 0x66 / 255.f, 0x2747d8)));
		LineSlot->SetAnchors(FAnchors(bLeft ? 1.f : 0.f, .2f, bLeft ? 1.f : 0.f, .8f));
		LineSlot->SetOffsets(bLeft ? FMargin(-19.f, 0.f, 1.f, 0.f) : FMargin(18.f, 0.f, 1.f, 0.f));
		AddFill(Shutter, LineCanvas);
		UCanvasPanelSlot* ShutterSlot = Aperture->AddChildToCanvas(Shutter);
		ShutterSlot->SetAnchors(bLeft ? FAnchors(0.f, 0.f, .501f, 1.f) : FAnchors(.499f, 0.f, 1.f, 1.f));
		ShutterSlot->SetOffsets(FMargin(0.f));
		Shutter->SetVisibility(ESlateVisibility::Collapsed);
		return Shutter;
	};
	ShutterLeft = MakeShutter(true);
	ShutterRight = MakeShutter(false);
	AddFill(Shell, Aperture);
	return Shell;
}

UWidget* UZoneSelectScreenWidget::BuildRuler(const FBuilder& B)
{
	// .ruler: 높이 22, 위 6px 눈금(20px 간격 1px), 라벨 9px 700 .08em 아래 정렬(ink 바탕·padding-top 5)
	UOverlay* Ruler = B.Overlay();
	USizeBox* Height = B.Size(-1.f, 22.f);
	Height->AddChild(Ruler);

	RulerTicks = B.Canvas();
	USizeBox* TickBox = B.Size(-1.f, 6.f);
	TickBox->AddChild(RulerTicks);
	AddOverlay(Ruler, TickBox, HAlign_Fill, VAlign_Top);

	UHorizontalBox* Labels = B.HBox();
	auto Label = [&B](const FText& Value, const FLinearColor& Color, UTextBlock*& OutText) -> UWidget*
	{
		UBorder* Back = B.Fill(Ink(), FMargin(0.f, 5.f, 0.f, 0.f));
		OutText = MakeText(B, Value, EFace::Bold, 9.f, .08f, Color, ERef::Segoe, 12.f);
		SetContent(Back, OutText);
		return Back;
	};
	UTextBlock* Unused = nullptr;
	UTextBlock* Middle = nullptr;
	AddH(Labels, Label(FText::FromString(TEXT("FIELD RECORD / 01—03")), Hex(0x6b7898), Unused), FMargin(0.f), VAlign_Bottom);
	AddH(Labels, B.Spacer(0.f, 0.f), FMargin(0.f), VAlign_Fill, 1.f);
	AddH(Labels, Label(FText::GetEmpty(), Hex(0xc4cce0), Middle), FMargin(0.f), VAlign_Bottom);
	AddH(Labels, B.Spacer(0.f, 0.f), FMargin(0.f), VAlign_Fill, 1.f);
	AddH(Labels, Label(FText::FromString(TEXT("GOHOME")), Hex(0x6b7898), Unused), FMargin(0.f), VAlign_Bottom);
	RulerName = Middle;
	AddFill(Ruler, Labels);
	return Height;
}

UWidget* UZoneSelectScreenWidget::BuildBriefing(const FBuilder& B)
{
	// .briefing: padding 28 0(≥1700), border-bottom, grid 1.25fr .8fr .8fr 1.3fr gap 22, 가운데 정렬. 행 높이 = dl(margin 15 0) 89.19
	UVerticalBox* Section = B.VBox();
	Briefing = Section;

	UHorizontalBox* Grid = B.HBox();
	USizeBox* RowHeight = B.Size(-1.f, 89.19f);
	RowHeight->AddChild(Grid);
	AddV(Section, RowHeight, FMargin(0.f, 28.f, 0.f, 28.f));
	AddV(Section, Rule(B, ZLine()));

	// 살펴보는 탐사지(preview-title 10px .08em, mb 7 / selected-name 900 22px/1.5 −.04em)
	UVerticalBox* Name = B.VBox();
	AddV(Name, MakeText(B, LOCTEXT("Previewing", "살펴보는 탐사지"), EFace::Regular, 10.f, .08f, ZMuted(), ERef::Malgun, 15.f));
	ZoneNameText = MakeText(B, FText::GetEmpty(), MalgunBold, 22.f, -.04f, Paper(), ERef::Malgun, 33.f);
	AddV(Name, ZoneNameText, FMargin(0.f, 7.f, 0.f, 0.f));
	DetailName = Name;
	AddH(Grid, Name, FMargin(0.f), VAlign_Center, 1.25f);

	// .stat: margin 15 0(dl), border-left 1px, padding-left 24 — dt 11px/1.5, dd 34px/1.05 Consolas + small 11px(기준선 공유)
	auto Stat = [&B](const FText& Label, const FText& Unit, UTextBlock*& OutValue, UTextBlock*& OutUnit) -> UWidget*
	{
		UHorizontalBox* Box = B.HBox();
		AddH(Box, VRule(B, ZLine()), FMargin(0.f), VAlign_Fill);
		UVerticalBox* Content = B.VBox();
		AddV(Content, MakeText(B, Label, EFace::Regular, 11.f, 0.f, ZMuted(), ERef::Malgun, 16.5f));
		UHorizontalBox* Value = B.HBox();
		constexpr float LineHeight = 35.7f;
		constexpr float Baseline = 28.f;	// Chrome 측정: dd 줄의 기준선이 정수 px로 붙는다(span·small 상자 위치에서 역산)
		OutValue = MakeText(B, FText::GetEmpty(), EFace::Mono, 34.f, -.05f, Paper(), ERef::Consolas, LineHeight, Baseline);
		AddH(Value, OutValue, FMargin(0.f), VAlign_Top);
		OutUnit = MakeText(B, Unit, EFace::Regular, 11.f, 0.f, ZMuted(), ERef::Malgun, LineHeight, Baseline);
		AddH(Value, OutUnit, FMargin(7.f, 0.f, 0.f, 0.f), VAlign_Top);
		AddV(Content, Value, FMargin(0.f, 7.f, 0.f, 0.f));
		AddH(Box, Content, FMargin(24.f, 0.f, 0.f, 0.f), VAlign_Top);
		return Box;
	};
	UTextBlock* QuotaValue = nullptr;
	UTextBlock* QuotaUnit = nullptr;
	DetailQuota = Stat(LOCTEXT("Quota", "회수 할당량"), LOCTEXT("QuotaUnit", "목표"), QuotaValue, QuotaUnit);
	QuotaText = QuotaValue;
	AddH(Grid, DetailQuota, FMargin(22.f, 0.f, 0.f, 0.f), VAlign_Center, .8f);

	UTextBlock* TimeValue = nullptr;
	UTextBlock* TimeUnit = nullptr;
	DetailTime = Stat(LOCTEXT("Time", "탐사 제한 시간"), LOCTEXT("TimeUnit", "분 : 초"), TimeValue, TimeUnit);
	TimeText = TimeValue;
	TimeUnitText = TimeUnit;
	AddH(Grid, DetailTime, FMargin(22.f, 0.f, 0.f, 0.f), VAlign_Center, .8f);

	// .apply: 종이색, 높이 64(min 60 + padding 14 20), 900 15px/1.5 + 화살표 900 25px. 비활성: #172841·#c0c9dc·테두리, 라임 ✓
	UOverlay* Apply = B.Overlay();
	ApplyFace = B.Fill(Paper(), FMargin(20.f, 14.f));
	UHorizontalBox* ApplyRow = B.HBox();
	ApplyLabel = MakeText(B, FText::GetEmpty(), MalgunBold, 15.f, 0.f, Ink(), ERef::Malgun, 22.5f);
	AddH(ApplyRow, ApplyLabel, FMargin(0.f), VAlign_Center, 1.f);
	ApplyIcon = MakeText(B, FText::GetEmpty(), EFace::Black, 25.f, 0.f, Ink(), ERef::Segoe, 36.f);
	AddH(ApplyRow, ApplyIcon, FMargin(15.f, 0.f, 0.f, 0.f), VAlign_Center);
	SetContent(ApplyFace, ApplyRow);
	AddFill(Apply, ApplyFace);
	ApplyEdge = B.Ring(DisabledEdge(), 1.f);
	AddFill(Apply, ApplyEdge);
	ApplyFocus = AddFocusRing(B, Apply, -8.f);
	USizeBox* ApplyBox = B.Size(-1.f, 64.f);
	ApplyBox->AddChild(Apply);
	ApplyButton = ApplyBox;
	AddH(Grid, ApplyBox, FMargin(22.f, 0.f, 0.f, 0.f), VAlign_Center, 1.3f);
	return Section;
}

UWidget* UZoneSelectScreenWidget::BuildBottom(const FBuilder& B)
{
	// .bottom: padding-top 17, 공유 목적지(점 7·gap 12) ↔ 조작 안내(gap 19, 항목 안 gap 6)
	UHorizontalBox* Row = B.HBox();
	USizeBox* RowHeight = B.Size(-1.f, 22.f);
	RowHeight->AddChild(Row);
	UBorder* Section = B.Box(FMargin(0.f, 17.f, 0.f, 0.f));
	SetContent(Section, RowHeight);
	Bottom = Section;

	UHorizontalBox* Shared = B.HBox();
	USizeBox* Dot = B.Size(7.f, 7.f);
	SharedDot = B.Fill(Hex(0x667590));
	Dot->AddChild(SharedDot);
	AddH(Shared, Dot, FMargin(0.f), VAlign_Center);
	AddH(Shared, MakeText(B, LOCTEXT("SharedLabel", "현재 팀 목적지"), MalgunBold, 11.f, 0.f, ZMuted(), ERef::Malgun, 15.f), FMargin(12.f, 0.f, 0.f, 0.f), VAlign_Center);
	SharedName = MakeText(B, FText::GetEmpty(), MalgunBold, 12.f, 0.f, Paper(), ERef::Malgun, 16.f);
	AddH(Shared, SharedName, FMargin(12.f, 0.f, 0.f, 0.f), VAlign_Center);
	AddH(Row, Shared, FMargin(0.f), VAlign_Center);
	AddH(Row, B.Spacer(20.f, 0.f), FMargin(0.f), VAlign_Fill, 1.f);

	UHorizontalBox* Controls = B.HBox();
	auto Or = [&B]() { return MakeText(B, FText::FromString(TEXT("/")), EFace::Regular, 10.f, 0.f, ZMuted(), ERef::Segoe, 10.f); };
	auto Hint = [&B](const FText& Value) { return MakeText(B, Value, EFace::Regular, 10.f, 0.f, ZMuted(), ERef::Malgun, 13.f); };

	// WASD / 방향키 · 탐색
	AddH(Controls, ControlKey(B, MakeText(B, FText::FromString(TEXT("WASD")), EFace::Bold, 10.f, .04f, Paper(), ERef::Segoe, 10.f), -1.f, 5.f), FMargin(0.f), VAlign_Center);
	AddH(Controls, Or(), FMargin(6.f, 0.f, 0.f, 0.f), VAlign_Center);
	// ↑←↓→(Segoe UI Symbol 12px — 대체 글꼴 화살표는 두 배 가까이 크다): 글리프 윤곽 좌표(기준선 10.97)에서 옮긴 대·화살촉
	USizeBox* ArrowIcon = B.Size(32.f, 12.f);
	ArrowIcon->AddChild(Glyph(B, FVector2D(32.f, 12.f), {
		{ FVector2D(2.84f, 3.4f), FVector2D(2.84f, 11.04f) }, { FVector2D(1.1f, 4.75f), FVector2D(2.84f, 3.0f), FVector2D(4.57f, 4.75f) },
		{ FVector2D(7.0f, 7.47f), FVector2D(14.67f, 7.47f) }, { FVector2D(8.35f, 5.73f), FVector2D(6.6f, 7.47f), FVector2D(8.35f, 9.2f) },
		{ FVector2D(18.86f, 2.91f), FVector2D(18.86f, 10.6f) }, { FVector2D(17.12f, 9.2f), FVector2D(18.86f, 10.95f), FVector2D(20.59f, 9.2f) },
		{ FVector2D(23.04f, 7.47f), FVector2D(30.7f, 7.47f) }, { FVector2D(29.36f, 5.73f), FVector2D(31.1f, 7.47f), FVector2D(29.36f, 9.2f) } }, 1.f, Paper()));
	AddH(Controls, ControlKey(B, ArrowIcon, -1.f, 4.f), FMargin(6.f, 0.f, 0.f, 0.f), VAlign_Center);
	AddH(Controls, Hint(LOCTEXT("Navigate", "탐색")), FMargin(6.f, 0.f, 0.f, 0.f), VAlign_Center);

	// Space / Enter 아이콘 · 지정 — 시안 인라인 SVG(viewBox 24×16, stroke 1.5)를 그대로
	USizeBox* SpaceIcon = B.Size(20.f, 14.f);
	SpaceIcon->AddChild(Glyph(B, FVector2D(24.f, 16.f), { { FVector2D(3.f, 5.f), FVector2D(3.f, 11.f), FVector2D(21.f, 11.f), FVector2D(21.f, 5.f) } }, 1.5f, Paper()));
	AddH(Controls, ControlKey(B, SpaceIcon, 34.f, 4.f), FMargin(19.f, 0.f, 0.f, 0.f), VAlign_Center);
	AddH(Controls, Or(), FMargin(6.f, 0.f, 0.f, 0.f), VAlign_Center);
	USizeBox* EnterIcon = B.Size(15.f, 14.f);	// kbd 폭 25 − padding·테두리 → svg가 15로 줄어든다(측정)
	EnterIcon->AddChild(Glyph(B, FVector2D(24.f, 16.f), {
		{ FVector2D(20.f, 3.f), FVector2D(20.f, 9.f), FVector2D(4.f, 9.f) },
		{ FVector2D(9.f, 4.f), FVector2D(4.f, 9.f), FVector2D(9.f, 14.f) } }, 1.5f, Paper()));
	AddH(Controls, ControlKey(B, EnterIcon, 25.f, 4.f), FMargin(6.f, 0.f, 0.f, 0.f), VAlign_Center);
	AddH(Controls, Hint(LOCTEXT("Assign", "지정")), FMargin(6.f, 0.f, 0.f, 0.f), VAlign_Center);

	// <b>Esc</b> 닫기 — b 800 12px, margin-right 5, 같은 기준선
	const float EscBaseline = CssBaseline(12.f, ERef::Segoe, 16.f);
	AddH(Controls, MakeText(B, FText::FromString(TEXT("Esc")), EFace::Black, 12.f, 0.f, Paper(), ERef::Segoe, 16.f, EscBaseline), FMargin(19.f, 0.f, 0.f, 0.f), VAlign_Center);
	AddH(Controls, MakeText(B, LOCTEXT("EscClose", " 닫기"), EFace::Regular, 10.f, 0.f, ZMuted(), ERef::Malgun, 16.f, EscBaseline), FMargin(5.f, 0.f, 0.f, 0.f), VAlign_Center);
	AddH(Row, Controls, FMargin(0.f), VAlign_Center);
	return Section;
}

UWidget* UZoneSelectScreenWidget::BuildOpening(const FBuilder& B)
{
	// .route-opening: 화면 전체, hit-test 없음. 코발트 상/하 면(사선 이음매) + NEXT DIVE. 표제
	UCanvasPanel* Canvas = B.Canvas();
	Canvas->SetClipping(EWidgetClipping::ClipToBounds);

	WingUpper = B.Fill(Blue());
	WingLower = B.Fill(Blue());
	Canvas->AddChildToCanvas(WingUpper);
	Canvas->AddChildToCanvas(WingLower);

	UVerticalBox* Type = B.VBox();
	AddV(Type, MakeText(B, FText::FromString(TEXT("GOHOME / EXPEDITION CHANNEL")), EFace::Black, 11.f, .22f, Paper(), ERef::Segoe, 16.5f));
	UHorizontalBox* Title = B.HBox();
	AddH(Title, MakeText(B, FText::FromString(TEXT("NEXT ")), EFace::Display, 156.f, -.075f, Paper(), ERef::ArialBlack, 132.6f, -1.f, true));
	AddH(Title, MakeText(B, FText::FromString(TEXT("DIVE.")), EFace::Display, 156.f, -.075f, Lime(), ERef::ArialBlack, 132.6f, -1.f, true), FMargin(14.04f, 0.f, 0.f, 0.f));	// margin-left .09em
	AddV(Type, Title, FMargin(0.f, 16.f, 0.f, 0.f));
	AddV(Type, MakeText(B, LOCTEXT("RouteLine", "다음 탐사지를 펼치다."), MalgunBold, 25.f, -.05f, Paper(), ERef::Malgun, 32.5f), FMargin(0.f, 20.f, 0.f, 0.f));
	UCanvasPanelSlot* TypeSlot = Canvas->AddChildToCanvas(Type);
	TypeSlot->SetAutoSize(true);
	RouteType = Type;

	Canvas->SetVisibility(ESlateVisibility::Collapsed);
	return Canvas;
}

void UZoneSelectScreenWidget::BuildCards()
{
	if (!Gallery || !WidgetTree)
	{
		return;
	}
	Gallery->ClearChildren();
	Cards.Reset();
	const FBuilder B(WidgetTree);
	for (int32 Index = 0; Index < Zones.Num(); ++Index)
	{
		Cards.Add(BuildCard(B, Zones[Index].Get(), Index));
		AddH(Gallery, Cards.Last().Slot, FMargin(Index > 0 ? CardGap : 0.f, 0.f, 0.f, 0.f), VAlign_Fill);
	}

	const int32 Count = Cards.Num();
	for (TArray<FZoneSelectTween>* Set : { &Grow, &Bright, &Sat, &TagOpacity, &CaptionOpacity, &Mark, &ArrowShift })
	{
		Set->SetNum(Count);
	}
	PhotoShift.Init(0.f, Count);
	PhotoFade.Init(1.f, Count);
}

FZoneSelectCardView UZoneSelectScreenWidget::BuildCard(const FBuilder& B, const UExpeditionZoneDataAsset* Zone, int32 Index)
{
	FZoneSelectCardView Card;
	const FText Name = Zone ? Zone->DisplayName : FText::GetEmpty();
	if (Zone)
	{
		Card.ZoneId = Zone->ZoneId;
		Card.Focus = Zone->PreviewFocus;
		Card.SelectedBrightness = Zone->PreviewBrightness;
		Card.SelectedSaturation = Zone->PreviewSaturation;
		Card.Texture = Zone->PreviewImage.LoadSynchronous();
	}

	Card.Slot = B.Size(400.f);
	// 테두리·포커스 링은 클립 밖 겉 층에 — 1px 헤어라인이 물리 픽셀로 스냅되며 카드 경계 밖으로 반 픽셀 나가면 클립에 통째로 잘린다(DPI .92 실측)
	UOverlay* Shell = B.Overlay();
	Card.Slot->AddChild(Shell);
	Card.Body = Shell;
	UOverlay* Body = B.Overlay();
	Body->SetClipping(EWidgetClipping::ClipToBounds);	// overflow hidden
	AddFill(Shell, Body);

	AddFill(Body, B.Fill(Navy()));

	// .fallback — 사진이 없을 때 맵 이름과 실패 문구(선택은 막지 않음)
	UVerticalBox* Fallback = B.VBox();
	AddV(Fallback, MakeText(B, Name, EFace::Regular, 26.f, 0.f, Paper(), ERef::Malgun, 46.8f), FMargin(0.f), HAlign_Center);
	AddV(Fallback, MakeText(B, LOCTEXT("PhotoFailed", "이미지를 불러오지 못했습니다"), EFace::Regular, 13.f, 0.f, Paper(), ERef::Malgun, 23.4f), FMargin(0.f), HAlign_Center);
	AddOverlay(Body, Fallback, HAlign_Center, VAlign_Center);
	Card.Fallback = Fallback;

	// .photo + :before(코발트 color 블렌드) + :after(세로 그라데이션) — 한 UI 머티리얼이 sRGB로 합성(1px 테두리 안쪽 = 패딩 상자)
	Card.Photo = WidgetTree->ConstructWidget<UImage>();
	AddOverlay(Body, Card.Photo, HAlign_Fill, VAlign_Fill, FMargin(1.f));
	if (LoadedPhotoMaterial && Card.Texture)
	{
		Card.PhotoMaterial = UMaterialInstanceDynamic::Create(LoadedPhotoMaterial, this);
		Card.PhotoMaterial->SetTextureParameterValue(TEXT("Photo"), Card.Texture);
		Card.Photo->SetBrushFromMaterial(Card.PhotoMaterial);
		Fallback->SetVisibility(ESlateVisibility::Collapsed);
	}
	else
	{
		Card.Photo->SetVisibility(ESlateVisibility::Collapsed);
	}

	// .chosen-mark: 왼쪽 4px 라임, 아래 기준 scaleY
	USizeBox* MarkBox = B.Size(4.f);
	MarkBox->AddChild(B.Fill(Lime()));
	MarkBox->SetRenderTransformPivot(FVector2D(.5f, 1.f));
	AddOverlay(Body, MarkBox, HAlign_Left, VAlign_Fill, FMargin(1.f, 1.f, 0.f, 1.f));
	Card.ChosenMark = MarkBox;

	// .card-top: top 30 / left·right 32(≥1700) — 인덱스(900 italic 49px/.95, −.08em) ↔ 살펴보는 중(10px 700 .09em, 테두리, padding 5 7)
	UHorizontalBox* Top = B.HBox();
	AddH(Top, MakeText(B, FText::FromString(FString::Printf(TEXT("0%d"), Index + 1)), EFace::Display, 49.f, -.08f, Paper(), ERef::ArialBlack, 46.55f, -1.f, true), FMargin(0.f), VAlign_Top);
	AddH(Top, B.Spacer(8.f, 0.f), FMargin(0.f), VAlign_Fill, 1.f);
	UOverlay* Tag = B.Overlay();
	AddFill(Tag, B.Fill(Hex(0x080f20, .2f)));
	AddFill(Tag, B.Ring(Paper(), 1.f));
	AddOverlay(Tag, MakeText(B, LOCTEXT("PreviewTag", "살펴보는 중"), MalgunBold, 10.f, .09f, Paper(), ERef::Malgun, 13.f), HAlign_Fill, VAlign_Fill, FMargin(8.f, 6.f));
	AddH(Top, Tag, FMargin(0.f), VAlign_Top);
	Card.PreviewTag = Tag;
	AddOverlay(Body, Top, HAlign_Fill, VAlign_Top, FMargin(33.f, 31.f, 33.f, 0.f));

	// .cross: right 22, top 96, 15×15(1px 십자), opacity .65
	UOverlay* Cross = B.Overlay();
	AddOverlay(Cross, VRule(B, Paper()), HAlign_Center, VAlign_Fill);
	AddOverlay(Cross, Rule(B, Paper()), HAlign_Fill, VAlign_Center);
	USizeBox* CrossBox = B.Size(15.f, 15.f);
	CrossBox->AddChild(Cross);
	CrossBox->SetRenderOpacity(.65f);
	AddOverlay(Body, CrossBox, HAlign_Right, VAlign_Top, FMargin(0.f, 97.f, 23.f, 0.f));

	// .card-bottom: bottom 33, left·right 32
	UVerticalBox* CardBottom = B.VBox();
	const FText English = Zone ? Zone->EnglishName : FText::GetEmpty();
	AddV(CardBottom, MakeText(B, English, EFace::Bold, 10.f, .18f, Hex(0xc3d0e9), ERef::Segoe, 14.f));
	Card.Title = MakeText(B, Name, MalgunBold, 33.f, -.05f, Paper(), ERef::Malgun, 39.6f);
	AddV(CardBottom, Card.Title, FMargin(0.f, 10.f, 0.f, 0.f));

	UTextBlock* Caption = MakeText(B, Zone ? Zone->Caption : FText::GetEmpty(), EFace::Regular, 11.f, 0.f, Hex(0xc2ccdf), ERef::Malgun, 16.5f);
	Caption->SetAutoWrapText(true);
	USizeBox* CaptionBox = B.Size();
	CaptionBox->SetMaxDesiredWidth(360.f);
	CaptionBox->AddChild(Caption);
	// 숨은 설명은 height 0이지만 margin-top 13은 남는다 — 여백은 늘 두고 안쪽 내용만 접는다
	UBorder* CaptionWrap = B.Box();
	SetContent(CaptionWrap, CaptionBox, HAlign_Left);
	AddV(CardBottom, CaptionWrap, FMargin(0.f, 13.f, 0.f, 0.f));
	Card.Caption = CaptionWrap;

	// .card-line: margin-top 21, border-top 1px #c0cffb55, padding-top 12 — 탐사지 0N / ✓ 팀 목적지 ↔ ↗(10px)
	UVerticalBox* Line = B.VBox();
	AddV(Line, Rule(B, MixSRGB(0xc0cffb, 0x55 / 255.f, 0x0b1428)));
	UHorizontalBox* LineRow = B.HBox();
	Card.ViewLabel = MakeText(B, FText::Format(LOCTEXT("ViewLabel", "탐사지 0{0}"), FText::AsNumber(Index + 1)), EFace::Regular, 10.f, 0.f, Hex(0xd5dced), ERef::Malgun, 13.f);
	AddH(LineRow, Card.ViewLabel, FMargin(0.f), VAlign_Center);
	// ✓ 팀 목적지 — ✓는 맑은 고딕에 없어 Segoe UI Symbol(10px, 폭 7.5)로 그려진다. 대체 글꼴 Regular에는 글리프가 없어 선으로
	UHorizontalBox* TeamTag = B.HBox();
	USizeBox* Check = B.Size(7.49f, 13.f);
	Check->AddChild(Glyph(B, FVector2D(7.49f, 13.f), { { FVector2D(.75f, 8.2f), FVector2D(2.9f, 10.35f), FVector2D(6.9f, 3.5f) } }, 1.1f, Lime()));
	AddH(TeamTag, Check, FMargin(0.f), VAlign_Center);
	AddH(TeamTag, MakeText(B, LOCTEXT("TeamTag", " 팀 목적지"), EFace::Regular, 10.f, 0.f, Lime(), ERef::Malgun, 13.f), FMargin(0.f), VAlign_Center);
	Card.TeamTag = TeamTag;
	AddH(LineRow, TeamTag, FMargin(0.f), VAlign_Center);
	AddH(LineRow, B.Spacer(8.f, 0.f), FMargin(0.f), VAlign_Fill, 1.f);
	UBorder* ArrowWrap = B.Box();
	SetContent(ArrowWrap, MakeText(B, FText::FromString(TEXT("↗")), EFace::Regular, 10.f, 0.f, Hex(0xd5dced), ERef::Malgun, 10.f));
	AddH(LineRow, ArrowWrap, FMargin(0.f), VAlign_Center);
	Card.Arrow = ArrowWrap;
	AddV(Line, LineRow, FMargin(0.f, 12.f, 0.f, 0.f));
	AddV(CardBottom, Line, FMargin(0.f, 21.f, 0.f, 0.f));
	AddOverlay(Body, CardBottom, HAlign_Fill, VAlign_Bottom, FMargin(33.f, 0.f, 33.f, 34.f));

	Card.Edge = B.Ring(CardEdge(), 1.f);
	AddFill(Shell, Card.Edge);
	Card.FocusRing = AddFocusRing(B, Shell, 2.f);
	return Card;
}

// --- 레이아웃 ---

void UZoneSelectScreenWidget::ApplyViewSize(const FVector2D& ViewSize)
{
	if (ViewSize.X < 1.f || ViewSize.Equals(LastViewSize, .5f))
	{
		return;
	}
	LastViewSize = ViewSize;

	// 시안은 폭 1000/720 이하에서 재배치하지만, 16:9 기본 DPI(ShortestSide)에선 논리 폭이 ≥1700이라 ≥1700 규칙만 쓴다.
	const float AppWidth = FMath::Min(ViewSize.X, MaxAppWidth);
	const float Pad = FMath::Clamp(ViewSize.X * .033f, 20.f, 64.f);	// clamp(20px, 3.3vw, 64px)
	if (AppBox)
	{
		AppBox->SetWidthOverride(AppWidth);
	}
	if (AppPadding)
	{
		AppPadding->SetPadding(FMargin(Pad, 25.f, Pad, 18.f));
	}
	GalleryWidth = AppWidth - Pad * 2.f;
	GalleryHeight = FMath::Clamp(ViewSize.Y * .52f, 360.f, 700.f);	// clamp(360px, 52vh, 700px)
	if (GallerySize)
	{
		GallerySize->SetHeightOverride(GalleryHeight);
	}
	RebuildRulerTicks(GalleryWidth);
	RebuildBackdrop(FVector2D(AppWidth, ViewSize.Y));

	// 오프닝: 상/하 면은 사선 이음매(왼쪽 56% ↔ 오른쪽 44%)를 아래/윗변으로 가진 큰 면을 이음매 가운데 기준으로 기울여 만든다.
	const float SeamAngle = FMath::RadiansToDegrees(FMath::Atan2(-.12f * ViewSize.Y, ViewSize.X));
	const FVector2D WingSize(ViewSize.X * 1.3f, ViewSize.Y * 1.3f);
	auto PlaceWing = [&](UWidget* Wing, bool bUpper)
	{
		if (UCanvasPanelSlot* WingSlot = Wing ? Cast<UCanvasPanelSlot>(Wing->Slot) : nullptr)
		{
			WingSlot->SetAnchors(FAnchors(0.f, 0.f));
			WingSlot->SetPosition(FVector2D(ViewSize.X * .5f - WingSize.X * .5f, bUpper ? ViewSize.Y * .5f - WingSize.Y : ViewSize.Y * .5f));
			WingSlot->SetSize(WingSize);
			Wing->SetRenderTransformPivot(FVector2D(.5f, bUpper ? 1.f : 0.f));
			Wing->SetRenderTransformAngle(SeamAngle);
		}
	};
	PlaceWing(WingUpper, true);
	PlaceWing(WingLower, false);
	if (UCanvasPanelSlot* TypeSlot = RouteType ? Cast<UCanvasPanelSlot>(RouteType->Slot) : nullptr)
	{
		// left clamp(30px, 10vw, 192px), top 50% − 145
		TypeSlot->SetPosition(FVector2D(FMath::Clamp(ViewSize.X * .1f, 30.f, 192.f), ViewSize.Y * .5f - 145.f));
	}
	UpdateCardLayout();
}

void UZoneSelectScreenWidget::UpdateCardLayout()
{
	const int32 Count = Cards.Num();
	if (Count == 0 || GalleryWidth <= 0.f)
	{
		return;
	}
	// flex: 1 1 0 + border 1px — 기본 크기가 테두리 2px이라 남는 폭(전체 − gap − 테두리)을 grow 비율로 나눈 뒤 2px을 더한다(측정 1009.16 / 382.06).
	float GrowSum = 0.f;
	for (const FZoneSelectTween& Tween : Grow)
	{
		GrowSum += Tween.Value;
	}
	const float Free = GalleryWidth - CardGap * (Count - 1) - 2.f * Count;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float Width = 2.f + (GrowSum > 0.f ? Free * Grow[Index].Value / GrowSum : Free / Count);
		if (Cards[Index].Slot)
		{
			Cards[Index].Slot->SetWidthOverride(Width);
		}
		UpdatePhoto(Index);
	}
}

void UZoneSelectScreenWidget::UpdatePhoto(int32 Index)
{
	FZoneSelectCardView& Card = Cards[Index];
	if (!Card.PhotoMaterial || !Card.Texture || !Card.Slot)
	{
		return;
	}
	// 패딩 상자(테두리 안) 크기
	const float Width = Card.Slot->GetWidthOverride() - 2.f;
	const float Height = GalleryHeight - 2.f;
	const float ImageWidth = FMath::Max(1, Card.Texture->GetSizeX());
	const float ImageHeight = FMath::Max(1, Card.Texture->GetSizeY());
	if (Width <= 0.f || Height <= 0.f)
	{
		return;
	}

	// object-fit cover + object-position, 그다음 scale(1.04)(요소 가운데 기준), 그다음 .photo-motion translateX.
	// 화면 점 p → 이미지 상자 점 q = C + (p − t − C) / 1.04 → 이미지 UV = (q − 크롭 오프셋) / 렌더 크기
	const float CoverScale = FMath::Max(Width / ImageWidth, Height / ImageHeight);
	const FVector2D Rendered(ImageWidth * CoverScale, ImageHeight * CoverScale);
	const FVector2D CropOffset((Width - Rendered.X) * Card.Focus.X, (Height - Rendered.Y) * Card.Focus.Y);
	const FVector2D Center(Width * .5f, Height * .5f);
	const FVector2D Shift(PhotoShift.IsValidIndex(Index) ? PhotoShift[Index] : 0.f, 0.f);
	const FVector2D Scale(Width / (PhotoScale * Rendered.X), Height / (PhotoScale * Rendered.Y));
	const FVector2D Offset = (Center - Center / PhotoScale - Shift / PhotoScale - CropOffset) / Rendered;

	Card.PhotoMaterial->SetVectorParameterValue(TEXT("UVTransform"), FLinearColor(Scale.X, Scale.Y, Offset.X, Offset.Y));
	Card.PhotoMaterial->SetScalarParameterValue(TEXT("Brightness"), Bright[Index].Value);
	Card.PhotoMaterial->SetScalarParameterValue(TEXT("Saturation"), Sat[Index].Value);
	Card.PhotoMaterial->SetScalarParameterValue(TEXT("Fade"), PhotoFade.IsValidIndex(Index) ? PhotoFade[Index] : 1.f);
}

void UZoneSelectScreenWidget::RebuildRulerTicks(float Width)
{
	if (!RulerTicks || !WidgetTree || FMath::IsNearlyEqual(Width, LastRulerWidth, .5f))
	{
		return;
	}
	LastRulerWidth = Width;
	RulerTicks->ClearChildren();
	// repeating-linear-gradient(90deg, #7c8bac5e 0 1px, transparent 1px 20px) — 1px 선은 헤어라인(DPI < 1에서 사라지지 않게)
	const FLinearColor TickColor = MixSRGB(0x7c8bac, 0x5e / 255.f, 0x080f20);
	for (float X = 0.f; X < Width; X += 20.f)
	{
		UHairlineWidget* Tick = WidgetTree->ConstructWidget<UHairlineWidget>();
		Tick->SetOrientation(Orient_Vertical);
		Tick->SetLineColor(TickColor);
		AddCanvas(RulerTicks, Tick, FVector2D(X, 0.f), FVector2D(1.f, 6.f));
	}
}

void UZoneSelectScreenWidget::RebuildBackdrop(const FVector2D& AppSize)
{
	if (!Backdrop || AppSize.X < 1.f || AppSize.Y < 1.f)
	{
		return;
	}
	// .app:before radial-gradient(ellipse at 50% −30%, #2747d828, transparent 60%) — 크기 farthest-corner.
	// Chrome은 먼 모서리까지의 가로·세로 거리(W/2, 1.3H) 비율로 타원을 잡고 √2배 한다(시안 렌더 픽셀에서 역산 — closest-side 비율 아님).
	constexpr int32 TextureWidth = 320;
	const int32 TextureHeight = FMath::Clamp(FMath::RoundToInt(TextureWidth * AppSize.Y / AppSize.X), 16, 1024);
	if (!BackdropTexture || BackdropTexture->GetSizeX() != TextureWidth || BackdropTexture->GetSizeY() != TextureHeight)
	{
		BackdropTexture = UTexture2D::CreateTransient(TextureWidth, TextureHeight, PF_B8G8R8A8);
		if (!BackdropTexture)
		{
			return;
		}
		BackdropTexture->SRGB = true;
		BackdropTexture->Filter = TF_Bilinear;
		BackdropTexture->AddressX = TA_Clamp;
		BackdropTexture->AddressY = TA_Clamp;
	}

	const float RadiusX = UE_SQRT_2 * AppSize.X * .5f;
	const float RadiusY = UE_SQRT_2 * AppSize.Y * 1.3f;
	const FColor InkColor(0x08, 0x0f, 0x20);
	const FColor BlueColor(0x27, 0x47, 0xd8);

	FTexture2DMipMap& Mip = BackdropTexture->GetPlatformData()->Mips[0];
	FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	for (int32 Y = 0; Y < TextureHeight; ++Y)
	{
		for (int32 X = 0; X < TextureWidth; ++X)
		{
			const float PX = (X + .5f) / TextureWidth * AppSize.X - AppSize.X * .5f;
			const float PY = (Y + .5f) / TextureHeight * AppSize.Y + AppSize.Y * .3f;
			const float Distance = FMath::Sqrt(FMath::Square(PX / RadiusX) + FMath::Square(PY / RadiusY));
			const float Alpha = (0x28 / 255.f) * FMath::Max(0.f, 1.f - Distance / .6f);
			auto Channel = [Alpha](uint8 Under, uint8 Over) { return static_cast<uint8>(FMath::RoundToInt(Under + (Over - Under) * Alpha)); };
			Pixels[Y * TextureWidth + X] = FColor(Channel(InkColor.R, BlueColor.R), Channel(InkColor.G, BlueColor.G), Channel(InkColor.B, BlueColor.B), 255);
		}
	}
	Mip.BulkData.Unlock();
	BackdropTexture->UpdateResource();
	Backdrop->SetBrushFromTexture(BackdropTexture, false);
}

// --- 상태 → 표시 ---

FText UZoneSelectScreenWidget::ZoneName(int32 Index) const
{
	return Zones.IsValidIndex(Index) && Zones[Index].IsValid() ? Zones[Index]->DisplayName : FText::GetEmpty();
}

void UZoneSelectScreenWidget::RenderState()
{
	for (int32 Index = 0; Index < Cards.Num(); ++Index)
	{
		FZoneSelectCardView& Card = Cards[Index];
		const bool bPreview = Index == Selected;
		const bool bCommitted = Index == Committed;

		if (Card.Edge)
		{
			SetRingColor(Card.Edge, bPreview ? Paper() : CardEdge());
		}
		if (Card.Title)
		{
			// card-title clamp(21px, 2.15vw, 33px) / 미리보기 clamp(32px, 3.2vw, 49px), line-height 1.2 — 크기는 즉시 바뀐다(transition 없음)
			const float Px = bPreview ? 49.f : 33.f;
			Card.Title->SetFont(Font(MalgunBold, Px, -.05f));
			Typeset(Card.Title, ERef::Malgun, Px * 1.2f);
		}
		// 설명: 높이는 auto/0으로 즉시, 투명도만 180ms. 접는 건 안쪽 내용 — 래퍼 슬롯의 margin-top 13은 숨어도 남는다
		// (Slate는 접힌 자식의 슬롯 여백까지 빼므로 래퍼를 접으면 안 됨)
		if (UBorder* CaptionWrap = Cast<UBorder>(Card.Caption))
		{
			UWidget* Content = CaptionWrap->GetContent();
			const bool bWasShown = Content && Content->GetVisibility() != ESlateVisibility::Collapsed;
			SetShown(Content, bPreview);
			if (!bPreview || !bWasShown)
			{
				CaptionOpacity[Index].Snap(0.f);
			}
		}
		SetShown(Card.ViewLabel, !bCommitted);
		SetShown(Card.TeamTag, bCommitted);
	}

	const UExpeditionZoneDataAsset* Zone = Zones.IsValidIndex(Selected) ? Zones[Selected].Get() : nullptr;
	if (ZoneNameText)
	{
		ZoneNameText->SetText(ZoneName(Selected));
	}
	if (QuotaText)
	{
		QuotaText->SetText(FText::FromString(Zone ? GroupThousands(Zone->MapQuota) : TEXT("—")));
	}
	if (TimeText && TimeUnitText)
	{
		// 시간 0 이하 = 제한 없음(ExpeditionZoneDataAsset 계약). 시안 고정 숫자로 덮지 않는다.
		const int32 Seconds = Zone ? FMath::RoundToInt(Zone->TimeLimitSeconds) : 0;
		TimeText->SetText(FText::FromString(Seconds > 0 ? FString::Printf(TEXT("%02d:%02d"), Seconds / 60, Seconds % 60) : TEXT("--:--")));
		TimeUnitText->SetText(Seconds > 0 ? LOCTEXT("TimeUnit", "분 : 초") : LOCTEXT("NoLimit", "제한 없음"));
	}
	if (RulerName)
	{
		const FString English = Zone ? Zone->EnglishName.ToString() : FString();
		RulerName->SetText(FText::FromString(FString::Printf(TEXT("0%d / %s"), Selected + 1, *English)));
	}

	// 지정 버튼: 이미 팀 목적지면 '현재 팀 목적지'·회색 면·✓(포커스는 가능, 재입력 무시)
	const bool bSame = Committed == Selected;
	if (ApplyLabel && ApplyIcon && ApplyFace)
	{
		ApplyLabel->SetText(bSame ? LOCTEXT("ApplyCurrent", "현재 팀 목적지") : LOCTEXT("Apply", "팀 목적지로 지정"));
		ApplyLabel->SetColorAndOpacity(FSlateColor(bSame ? Hex(0xc0c9dc) : Ink()));
		ApplyIcon->SetText(FText::FromString(bSame ? TEXT("✓") : TEXT("↗")));
		ApplyIcon->SetColorAndOpacity(FSlateColor(bSame ? Lime() : Ink()));
		ApplyFace->SetBrushColor(bSame ? DisabledFace() : LerpSRGB(Paper(), FLinearColor::White, ApplyHover.Value));
	}
	SetShown(ApplyEdge, bSame);

	if (SharedDot)
	{
		SharedDot->SetBrushColor(Committed != INDEX_NONE ? Lime() : Hex(0x667590));
	}
	if (SharedName)
	{
		SharedName->SetText(Committed != INDEX_NONE ? ZoneName(Committed) : LOCTEXT("Unassigned", "미지정"));
	}
}

void UZoneSelectScreenWidget::ApplyCardTargets(bool bInstant)
{
	for (int32 Index = 0; Index < Cards.Num(); ++Index)
	{
		const FZoneSelectCardView& Card = Cards[Index];
		const bool bPreview = Index == Selected;
		const bool bHover = Index == Hovered;
		const float TargetBright = bPreview ? Card.SelectedBrightness : (bHover ? HoverBrightness : IdleBrightness);
		const float TargetSat = bPreview ? Card.SelectedSaturation : (bHover ? HoverSaturation : IdleSaturation);
		const float TargetGrow = bPreview ? SelectedGrow : 1.f;
		const float TargetTag = bPreview ? 1.f : 0.f;
		const float TargetCaption = bPreview ? 1.f : 0.f;
		const float TargetMark = Index == Committed ? 1.f : 0.f;
		const float TargetArrow = bHover ? 4.f : 0.f;
		if (bInstant)
		{
			Bright[Index].Snap(TargetBright);
			Sat[Index].Snap(TargetSat);
			Grow[Index].Snap(TargetGrow);
			TagOpacity[Index].Snap(TargetTag);
			CaptionOpacity[Index].Snap(TargetCaption);
			Mark[Index].Snap(TargetMark);
			ArrowShift[Index].Snap(TargetArrow);
			continue;
		}
		Bright[Index].Set(TargetBright, .44f, DefaultEase());
		Sat[Index].Set(TargetSat, .44f, DefaultEase());
		Grow[Index].Set(TargetGrow, .44f, DefaultEase());
		TagOpacity[Index].Set(TargetTag, .13f, CssEase());
		CaptionOpacity[Index].Set(TargetCaption, .18f, CssEase());
		Mark[Index].Set(TargetMark, .18f, DefaultEase());
		ArrowShift[Index].Set(TargetArrow, .16f, CssEase());
	}
	const float TargetHover = bApplyHovered ? 1.f : 0.f;
	if (bInstant)
	{
		ApplyHover.Snap(TargetHover);
	}
	else
	{
		ApplyHover.Set(TargetHover, .16f, CssEase());
	}
}

void UZoneSelectScreenWidget::RenderFocus()
{
	for (int32 Index = 0; Index < Cards.Num(); ++Index)
	{
		SetShown(Cards[Index].FocusRing, bFocusVisible && FocusKind == EFocus::Card && FocusCard == Index);
	}
	SetShown(CloseFocus, bFocusVisible && FocusKind == EFocus::Close);
	SetShown(ApplyFocus, bFocusVisible && FocusKind == EFocus::Apply);
}

// --- 동작 ---

int32 UZoneSelectScreenWidget::FindZone(FName ZoneId) const
{
	if (ZoneId == NAME_None)
	{
		return INDEX_NONE;
	}
	return Zones.IndexOfByPredicate([ZoneId](const TWeakObjectPtr<const UExpeditionZoneDataAsset>& Zone)
	{
		return Zone.IsValid() && Zone->ZoneId == ZoneId;
	});
}

void UZoneSelectScreenWidget::SelectMap(int32 Index)
{
	if (!Cards.IsValidIndex(Index))
	{
		return;
	}
	CancelEntrance();
	if (Selected == Index)
	{
		return;
	}
	const float Direction = Index > Selected ? 1.f : -1.f;
	Selected = Index;
	if (Backend.IsValid() && Zones[Index].IsValid())
	{
		Backend->SetLastPreviewedZone(Zones[Index]->ZoneId);
	}

	// 채널 selection 취소 — 이전 카드의 사진 이동·상세 등장도 즉시 기본 상태로
	Animator.Cancel(SelectPhotoTrack);
	Animator.Cancel(SelectDetailTrack);
	PhotoShift.Init(0.f, Cards.Num());
	PhotoFade.Init(1.f, Cards.Num());
	for (UWidget* Detail : { DetailName.Get(), DetailQuota.Get(), DetailTime.Get() })
	{
		SetMotion(Detail, 1.f, 0.f, 0.f);
	}

	RenderState();
	ApplyCardTargets(false);

	// .photo-motion: X ±12→0, opacity .65→1, 320ms
	TWeakObjectPtr<UZoneSelectScreenWidget> WeakThis = this;
	Animator.Play(SelectPhotoTrack, 0.f, .32f, [WeakThis, Index, Direction](float T)
	{
		if (UZoneSelectScreenWidget* Screen = WeakThis.Get())
		{
			const float E = DefaultEase().Eval(T);
			if (Screen->PhotoShift.IsValidIndex(Index))
			{
				Screen->PhotoShift[Index] = Direction * 12.f * (1.f - E);
				Screen->PhotoFade[Index] = FMath::Lerp(.65f, 1.f, E);
			}
		}
	});
	// 하단 맵 이름·할당량·시간: 80ms 지연 + 220ms, Y 8→0, opacity 0→1(지연 중 시작 상태 — fill backwards)
	Animator.Play(SelectDetailTrack, .08f, .22f, [WeakThis](float T)
	{
		if (UZoneSelectScreenWidget* Screen = WeakThis.Get())
		{
			const float E = DefaultEase().Eval(T);
			for (UWidget* Detail : { Screen->DetailName.Get(), Screen->DetailQuota.Get(), Screen->DetailTime.Get() })
			{
				SetMotion(Detail, E, 0.f, 8.f * (1.f - E));
			}
		}
	});
}

void UZoneSelectScreenWidget::ConfirmDestination()
{
	CancelEntrance();
	if (!Backend.IsValid() || !Zones.IsValidIndex(Selected) || !Zones[Selected].IsValid() || Committed == Selected)
	{
		return;
	}
	// 요청만 보낸다 — 표시는 서버 복제 결과(HandleTeamDestinationChanged)로만 바뀐다.
	Backend->RequestTeamDestination(Zones[Selected]->ZoneId);
}

void UZoneSelectScreenWidget::HandleTeamDestinationChanged(FName ZoneId, EZoneDestinationChange Change)
{
	if (bClosing)
	{
		return;
	}
	const int32 Previous = Committed;
	Committed = FindZone(ZoneId);
	RenderState();
	ApplyCardTargets(false);

	const FString Name = ZoneName(Committed).ToString();
	switch (Change)
	{
	case EZoneDestinationChange::Mine:
		Announce(FText::Format(LOCTEXT("Assigned", "{0}{1} 팀 목적지로 지정했습니다. 출발은 잠수정의 출발 버튼을 이용하세요."), FText::FromString(Name), FText::FromString(ObjectParticle(Name))));
		break;
	case EZoneDestinationChange::Other:
		if (Committed != Previous)
		{
			Announce(Committed != INDEX_NONE
				? FText::Format(LOCTEXT("ChangedByOther", "다른 팀원이 팀 목적지를 {0}{1} 바꿨습니다."), FText::FromString(Name), FText::FromString(DirectionParticle(Name)))
				: LOCTEXT("ClearedByOther", "팀 목적지가 미지정으로 바뀌었습니다."));
		}
		break;
	case EZoneDestinationChange::Failed:
		Announce(LOCTEXT("AssignFailed", "팀 목적지를 지정하지 못했습니다. 다시 시도하세요."));
		break;
	}
}

void UZoneSelectScreenWidget::Announce(const FText& Message)
{
	if (Status)
	{
		Status->SetText(Message);
	}
	// 결과 문구: opacity 0→1, Y 6→0, 180ms
	TWeakObjectPtr<UWidget> Target = StatusWrap.Get();
	Animator.Play(FeedbackTrack, 0.f, .18f, [Target](float T)
	{
		const float E = DefaultEase().Eval(T);
		SetMotion(Target.Get(), E, 0.f, 6.f * (1.f - E));
	});
}

void UZoneSelectScreenWidget::Close()
{
	if (bClosing)
	{
		return;
	}
	bClosing = true;

	// 모든 임시 연출을 취소(기본 상태로)한 뒤 종료: opacity 1→0, Y 0→10, 180ms
	Animator.CancelAll();
	ApplyEntranceFrame(FinalFrame);
	PhotoShift.Init(0.f, Cards.Num());
	PhotoFade.Init(1.f, Cards.Num());
	for (UWidget* Detail : { DetailName.Get(), DetailQuota.Get(), DetailTime.Get(), StatusWrap.Get() })
	{
		SetMotion(Detail, 1.f, 0.f, 0.f);
	}

	TWeakObjectPtr<UZoneSelectScreenWidget> WeakThis = this;
	Animator.Play(ExitTrack, 0.f, .18f, [WeakThis](float T)
	{
		if (UZoneSelectScreenWidget* Screen = WeakThis.Get())
		{
			const float E = DefaultEase().Eval(T);
			SetMotion(Screen->App.Get(), 1.f - E, 0.f, 10.f * E);
		}
	}, [WeakThis]()
	{
		UZoneSelectScreenWidget* Screen = WeakThis.Get();
		if (!Screen)
		{
			return;
		}
		if (IZoneSelectBackend* ScreenBackend = Screen->Backend.Get())
		{
			ScreenBackend->RequestCloseZoneSelect();
		}
		else
		{
			Screen->RemoveFromParent();
		}
	});
}

void UZoneSelectScreenWidget::MoveFocus(bool bForward)
{
	// Tab 순서: 닫기 → 카드들 → 지정 버튼, 화면 안에서 순환
	const int32 Count = Cards.Num() + 2;
	int32 Current = FocusKind == EFocus::Close ? 0 : (FocusKind == EFocus::Card ? 1 + FocusCard : Count - 1);
	Current = (Current + (bForward ? 1 : -1) + Count) % Count;
	if (Current == 0)
	{
		FocusKind = EFocus::Close;
	}
	else if (Current == Count - 1)
	{
		FocusKind = EFocus::Apply;
	}
	else
	{
		FocusKind = EFocus::Card;
		FocusCard = Current - 1;
	}
	bFocusVisible = true;
	RenderFocus();
}

void UZoneSelectScreenWidget::Activate()
{
	switch (FocusKind)
	{
	case EFocus::Close:
		Close();
		break;
	case EFocus::Card:
		// 포커스된 맵을 미리보기로 맞춘 뒤 지정
		SelectMap(FocusCard);
		ConfirmDestination();
		break;
	case EFocus::Apply:
		ConfirmDestination();
		break;
	}
}

// --- 진입 연출(1,040ms) ---

void UZoneSelectScreenWidget::PlayEntrance()
{
	Animator.CancelAll();
	bEntrancePlaying = true;
	TWeakObjectPtr<UZoneSelectScreenWidget> WeakThis = this;
	Animator.Play(EntryTrack, 0.f, EntrySeconds, [WeakThis](float T)
	{
		if (UZoneSelectScreenWidget* Screen = WeakThis.Get())
		{
			Screen->ApplyEntranceFrame(T * EntrySeconds);
		}
	}, [WeakThis]()
	{
		if (UZoneSelectScreenWidget* Screen = WeakThis.Get())
		{
			Screen->bEntrancePlaying = false;
			Screen->ApplyEntranceFrame(FinalFrame);
		}
	});
}

void UZoneSelectScreenWidget::CancelEntrance()
{
	if (!bEntrancePlaying)
	{
		return;
	}
	Animator.Cancel(EntryTrack);
	bEntrancePlaying = false;
	ApplyEntranceFrame(FinalFrame);
}

void UZoneSelectScreenWidget::ApplyEntranceFrame(float Time)
{
	const FSupplyEase& Ease = DefaultEase();
	auto Eased = [&Ease, Time](float Delay, float Duration) { return Ease.Eval(Progress(Time, Delay, Duration)); };
	const FVector2D View = LastViewSize.X > 1.f ? LastViewSize : FVector2D(1920.f, 1080.f);

	// 오프닝 컨테이너: 0..620ms만 보임(hit-test 없음)
	const bool bOpening = Time < .62f;
	if (Opening)
	{
		Opening->SetVisibility(bOpening ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (bOpening)
	{
		// 코발트 면: 트랙 linear, 키 0 → 23% 결합 → 53%까지 유지 → 바깥. 진입/퇴장 구간에 기본 곡선.
		static const FSupplyKey WingKeys[] = { { 0.f, 1.05f }, { .23f, 0.f }, { .53f, 0.f }, { 1.f, 1.05f } };
		const float Out = SampleSupplyKeys(WingKeys, Progress(Time, 0.f, .62f), Ease);
		SetMotion(WingUpper, 1.f, 0.f, -Out * View.Y);
		SetMotion(WingLower, 1.f, 0.f, Out * View.Y);

		// 표제: 80ms 지연 500ms, 곡선은 트랙 전체에 걸고 키(0/.25/.66/1)는 선형(WAAPI options easing)
		static const FSupplyKey TypeOpacity[] = { { 0.f, 0.f }, { .25f, 1.f }, { .66f, 1.f }, { 1.f, 0.f } };
		static const FSupplyKey TypeX[] = { { 0.f, -54.f }, { .25f, 0.f }, { .66f, 0.f }, { 1.f, 44.f } };
		const float TypeT = Ease.Eval(Progress(Time, .08f, .5f));
		const bool bTypeDone = Time >= .58f;
		SetMotion(RouteType, bTypeDone ? 0.f : SampleSupplyKeys(TypeOpacity, TypeT, FSupplyEase::Linear()), bTypeDone ? 0.f : SampleSupplyKeys(TypeX, TypeT, FSupplyEase::Linear()), 0.f);
	}

	// 페이지가 오프닝 뒤에서 조립된다
	const float AppOpacity = Eased(.24f, .24f);
	if (!bClosing)
	{
		SetMotion(App, AppOpacity, 0.f, 0.f);
	}
	SetMotion(Header, 1.f, 0.f, -10.f * (1.f - Eased(.34f, .18f)));
	const float HeadingE = Eased(.36f, .26f);
	SetMotion(Heading, HeadingE, -16.f * (1.f - HeadingE), 0.f);

	// 사진 중앙 셔터: 420ms 지연 620ms, X 0 → ∓101%(지연 중에도 보임 — fill backwards)
	const bool bShutter = Time < .42f + .62f;
	SetShown(ShutterLeft, bShutter);
	SetShown(ShutterRight, bShutter);
	if (bShutter)
	{
		const float ShutterE = Eased(.42f, .62f);
		const float ShutterWidth = GalleryWidth * .501f;
		SetMotion(ShutterLeft, 1.f, -1.01f * ShutterWidth * ShutterE, 0.f);
		SetMotion(ShutterRight, 1.f, 1.01f * ShutterWidth * ShutterE, 0.f);
	}

	for (int32 Index = 0; Index < Cards.Num(); ++Index)
	{
		const float CardE = Eased(.46f + .07f * Index, .36f);
		SetMotion(Cards[Index].Body, CardE, 0.f, 16.f * (1.f - CardE));
	}
	const float BriefingE = Eased(.7f, .24f);
	SetMotion(Briefing, BriefingE, 0.f, 10.f * (1.f - BriefingE));
	const float BottomE = Eased(.76f, .22f);
	SetMotion(Bottom, BottomE, 0.f, 8.f * (1.f - BottomE));
	if (!Animator.IsPlaying(FeedbackTrack))
	{
		SetMotion(StatusWrap, Eased(.82f, .18f), 0.f, 0.f);
	}
}

// --- 입력 ---

FReply UZoneSelectScreenWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (bClosing)
	{
		return FReply::Handled();
	}
	if (InKeyEvent.IsAltDown() || InKeyEvent.IsControlDown() || InKeyEvent.IsCommandDown())
	{
		return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
	}

	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right)
	{
		Close();
		return FReply::Handled();
	}
	if (Key == EKeys::Tab)
	{
		MoveFocus(!InKeyEvent.IsShiftDown());
		return FReply::Handled();
	}

	// 방향(WASD는 물리 키 — UE 키는 레이아웃과 무관). 위/아래도 이전/다음, 양 끝에서 멈춤. 카드 포커스에서만.
	int32 Step = 0;
	bool bHome = false;
	bool bEnd = false;
	if (Key == EKeys::Left || Key == EKeys::Up || Key == EKeys::A || Key == EKeys::W || Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_LeftStick_Left || Key == EKeys::Gamepad_LeftStick_Up)
	{
		Step = -1;
	}
	else if (Key == EKeys::Right || Key == EKeys::Down || Key == EKeys::D || Key == EKeys::S || Key == EKeys::Gamepad_DPad_Right || Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_LeftStick_Right || Key == EKeys::Gamepad_LeftStick_Down)
	{
		Step = 1;
	}
	bHome = Key == EKeys::Home;
	bEnd = Key == EKeys::End;
	if (Step != 0 || bHome || bEnd)
	{
		if (FocusKind == EFocus::Card && Cards.Num() > 0)
		{
			const int32 Next = bHome ? 0 : (bEnd ? Cards.Num() - 1 : FMath::Clamp(FocusCard + Step, 0, Cards.Num() - 1));
			SelectMap(Next);
			FocusCard = Next;
			bFocusVisible = true;
			RenderFocus();
		}
		return FReply::Handled();
	}

	if (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom)
	{
		// 길게 눌러 반복 지정하지 않음
		if (!InKeyEvent.IsRepeat())
		{
			bFocusVisible = true;
			RenderFocus();
			Activate();
		}
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

bool UZoneSelectScreenWidget::HitWidget(const UWidget* Widget, const FVector2D& ScreenPosition) const
{
	return Widget && Widget->GetCachedWidget().IsValid() && Widget->GetCachedGeometry().IsUnderLocation(ScreenPosition);
}

int32 UZoneSelectScreenWidget::HitCard(const FVector2D& ScreenPosition) const
{
	for (int32 Index = 0; Index < Cards.Num(); ++Index)
	{
		if (HitWidget(Cards[Index].Slot, ScreenPosition))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

FReply UZoneSelectScreenWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bClosing || InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Handled();
	}
	const FVector2D Position = InMouseEvent.GetScreenSpacePosition();
	// 클릭은 그 버튼에 포커스만 옮기고 링은 그리지 않는다(:focus-visible)
	if (HitWidget(CloseButton, Position))
	{
		FocusKind = EFocus::Close;
		bFocusVisible = false;
		RenderFocus();
		Close();
	}
	else if (HitWidget(ApplyButton, Position))
	{
		FocusKind = EFocus::Apply;
		bFocusVisible = false;
		RenderFocus();
		ConfirmDestination();
	}
	else
	{
		const int32 Card = HitCard(Position);
		if (Card != INDEX_NONE)
		{
			// 카드 클릭은 미리보기만(즉시 지정하지 않음)
			FocusKind = EFocus::Card;
			FocusCard = Card;
			bFocusVisible = false;
			RenderFocus();
			SelectMap(Card);
		}
	}
	return FReply::Handled();
}

FReply UZoneSelectScreenWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	const FVector2D Position = InMouseEvent.GetScreenSpacePosition();
	const int32 NewHovered = HitCard(Position);
	const bool bNewApplyHovered = HitWidget(ApplyButton, Position);
	if (NewHovered != Hovered || bNewApplyHovered != bApplyHovered)
	{
		Hovered = NewHovered;
		bApplyHovered = bNewApplyHovered;
		ApplyCardTargets(false);
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

void UZoneSelectScreenWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	if (Hovered != INDEX_NONE || bApplyHovered)
	{
		Hovered = INDEX_NONE;
		bApplyHovered = false;
		ApplyCardTargets(false);
	}
}

#if !UE_BUILD_SHIPPING
namespace
{
	// 자기 RenderTranslation(Typeset 기준선 보정·임시 모션)을 뺀 레이아웃 위치. 부모 래퍼 모션은 정착 후 0이라 가정.
	void LogBox(const FGeometry& HostGeometry, const UWidget* Widget, const FString& Label)
	{
		if (!Widget || !Widget->GetCachedWidget().IsValid())
		{
			return;
		}
		const FGeometry& Geometry = Widget->GetCachedGeometry();
		const FVector2D TopLeft = HostGeometry.AbsoluteToLocal(Geometry.GetAbsolutePosition()) - Widget->GetRenderTransform().Translation;
		const FVector2D Size = Geometry.GetLocalSize();
		UE_LOG(LogZoneSelectUI, Display, TEXT("BOX [%.2f, %.2f, %.2f, %.2f] %s"), TopLeft.X, TopLeft.Y, Size.X, Size.Y, *Label);
	}

	void DumpZoneTexts(const UWidget* Widget, const FGeometry& HostGeometry)
	{
		if (!Widget || !Widget->IsVisible())
		{
			return;
		}
		if (const UTextBlock* Text = Cast<UTextBlock>(Widget))
		{
			LogBox(HostGeometry, Text, FString::Printf(TEXT("TEXT \"%s\""), *Text->GetText().ToString()));
		}
		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
			{
				DumpZoneTexts(Panel->GetChildAt(Index), HostGeometry);
			}
		}
	}
}

void UZoneSelectScreenWidget::DumpLayout() const
{
	const FGeometry& HostGeometry = GetCachedGeometry();
	UE_LOG(LogZoneSelectUI, Display, TEXT("HOST size %.1f x %.1f"), HostGeometry.GetLocalSize().X, HostGeometry.GetLocalSize().Y);
	LogBox(HostGeometry, AppBox, TEXT("app"));
	LogBox(HostGeometry, Header, TEXT("top"));
	LogBox(HostGeometry, CloseButton, TEXT("close"));
	LogBox(HostGeometry, Heading, TEXT("heading"));
	LogBox(HostGeometry, GallerySize, TEXT("gallery"));
	for (int32 Index = 0; Index < Cards.Num(); ++Index)
	{
		LogBox(HostGeometry, Cards[Index].Slot, FString::Printf(TEXT("card %d"), Index));
		LogBox(HostGeometry, Cards[Index].PreviewTag, FString::Printf(TEXT("preview-tag %d"), Index));
		LogBox(HostGeometry, Cards[Index].Caption, FString::Printf(TEXT("caption-wrap %d"), Index));
	}
	LogBox(HostGeometry, RulerTicks, TEXT("ruler-ticks"));
	LogBox(HostGeometry, Briefing, TEXT("briefing"));
	LogBox(HostGeometry, DetailName, TEXT("detail-name"));
	LogBox(HostGeometry, DetailQuota, TEXT("stat-quota"));
	LogBox(HostGeometry, DetailTime, TEXT("stat-time"));
	LogBox(HostGeometry, ApplyButton, TEXT("apply"));
	LogBox(HostGeometry, Bottom, TEXT("bottom"));
	LogBox(HostGeometry, SharedDot, TEXT("status-dot"));
	LogBox(HostGeometry, StatusWrap, TEXT("toast"));
	if (WidgetTree)
	{
		DumpZoneTexts(WidgetTree->RootWidget, HostGeometry);
	}
}

void UZoneSelectScreenWidget::DebugFreezeEntrance(float Milliseconds)
{
	if (Milliseconds < 0.f)
	{
		PlayEntrance();
		return;
	}
	Animator.CancelAll();
	bEntrancePlaying = false;
	ApplyEntranceFrame(Milliseconds / 1000.f);
}

static FAutoConsoleCommand GZoneSelectEntryAtCommand(
	TEXT("GoHome.ZoneSelect.EntryAt"),
	TEXT("탐사 목적지 진입 연출을 지정 시각(ms)에 멈춘다. 음수면 처음부터 다시 재생."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const float Milliseconds = Args.Num() > 0 ? FCString::Atof(*Args[0]) : -1.f;
		for (TObjectIterator<UZoneSelectScreenWidget> It; It; ++It)
		{
			if (It->IsInViewport())
			{
				It->DebugFreezeEntrance(Milliseconds);
			}
		}
	}));

// 시안 비교용: GoHome.ZoneSelect.Dump
static FAutoConsoleCommand GZoneSelectDumpCommand(
	TEXT("GoHome.ZoneSelect.Dump"),
	TEXT("열린 탐사 목적지 화면의 상자·텍스트 레이아웃 위치를 LogZoneSelectUI로 출력한다."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		for (TObjectIterator<UZoneSelectScreenWidget> It; It; ++It)
		{
			if (It->IsInViewport())
			{
				It->DumpLayout();
			}
		}
	}));

// 실제 Slate 마우스 경로 검증용: GoHome.ZoneSelect.Click <x> <y> — 화면 논리 좌표 왼쪽 클릭(이동 이벤트 포함)
static FAutoConsoleCommand GZoneSelectClickCommand(
	TEXT("GoHome.ZoneSelect.Click"),
	TEXT("탐사 목적지 화면 논리 좌표(x y)를 왼쪽 클릭한다(Slate 경로). 세 번째 인자 move면 이동만."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() < 2 || !FSlateApplication::IsInitialized())
		{
			return;
		}
		for (TObjectIterator<UZoneSelectScreenWidget> It; It; ++It)
		{
			if (!It->IsInViewport())
			{
				continue;
			}
			const FVector2D Local(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]));
			const FVector2D Screen = It->GetCachedGeometry().LocalToAbsolute(Local);
			FSlateApplication& App = FSlateApplication::Get();
			const TSet<FKey> NoButtons;
			const FPointerEvent Move(0, Screen, Screen, NoButtons, EKeys::Invalid, 0.f, App.GetModifierKeys());
			App.ProcessMouseMoveEvent(Move);
			if (Args.Num() > 2 && Args[2].Equals(TEXT("move"), ESearchCase::IgnoreCase))
			{
				return;
			}
			const FPointerEvent Down(0, Screen, Screen, NoButtons, EKeys::LeftMouseButton, 0.f, App.GetModifierKeys());
			App.ProcessMouseButtonDownEvent(nullptr, Down);
			const FPointerEvent Up(0, Screen, Screen, NoButtons, EKeys::LeftMouseButton, 0.f, App.GetModifierKeys());
			App.ProcessMouseButtonUpEvent(Up);
			return;
		}
	}));
#endif

#undef LOCTEXT_NAMESPACE
