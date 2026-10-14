#include "UI/Entry/ExpeditionOpeningWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Core/ExpeditionEntryBackend.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Rendering/SlateRenderer.h"
#include "TextureResource.h"
#include "UI/Common/StrokeGlyphWidget.h"
#include "UI/Supply/SupplyMotion.h"
#include "UI/Supply/SupplyUIStyle.h"
#include "UObject/UObjectIterator.h"
#if !UE_BUILD_SHIPPING
#include "Core/ExpeditionZoneDataAsset.h"
#endif

#define LOCTEXT_NAMESPACE "ExpeditionOpening"

using namespace SupplyUI;

/*
 * 수치 출처: 시안을 1920×1080 무대로 렌더해 잰 값(요소 상자·각 줄 기준선·글자 시작 x). 레이아웃 px = Slate 단위(1080p DPI 1.0),
 * 폰트만 SupplyUI::Font가 0.75배. cqw 글자(색인·스탬프·컷인·아웃라인 단어)는 묶음째 W/1920 배율, 위치는 CSS %/clamp 그대로.
 * 시간: opening.js의 track() — WAAPI는 easing을 트랙 전체 진행도에 걸고 키 사이는 선형이다(motion.md 표의 24%/66%는 "곡선 적용 뒤" 진행도).
 */
namespace ExpeditionOpeningUI
{
	const FSupplyEase& OpeningEase()
	{
		static const FSupplyEase Ease = FSupplyEase::Bezier(.22f, .8f, .2f, 1.f);
		return Ease;
	}

	constexpr float ReferenceWidth = 1920.f;
	// 첫 컷(컷인)은 진입·퇴장 속도는 시안 그대로 두고, 판과 문구가 모두 멈춰 있는 순간에 시간을 멈춰 정지 컷만 늘린다(사용자 결정 2026-10-14).
	// 시안에서 아이보리 판 정지 83~254ms, 코발트 148~319ms, 문구 완전 노출 228~355ms → 공통 구간 안 240ms에서 640ms 멈춘다(문구 노출 0.13s → 0.77s).
	// 그 뒤 트랙(판 퇴장·브리핑·스탬프·도착 안내)은 멈춘 만큼 통째로 늦는다 — 아래 수치는 시안 기준, ApplyFrame이 시각을 되돌려 쓴다.
	constexpr float CutinHoldAtMs = 240.f;
	constexpr float CutinShift = 640.f;
	constexpr float TimelineMs = 5200.f + CutinShift;	// 이 시각에 서버가 탐사를 연다(AExplorationGameMode::OpeningDurationSeconds 5.84와 같아야 한다)
	constexpr float ArrivalOutMs = 6200.f;		// 도착 안내 유지 뒤 퇴장(시안 기준 시각, 사용자 결정: 시안 끝 화면 대신 짧게 보이고 사라짐)
	constexpr float ArrivalOutLength = 300.f;
	constexpr float EndMs = ArrivalOutMs + ArrivalOutLength + CutinShift;	// = TimelineMs + UExpeditionEntryComponent::OpeningTailSeconds

	// 트랙 진행도(곡선 적용). 지연 전 0, 끝난 뒤 1 = fill both.
	float Track(float T, float Start, float Length)
	{
		return OpeningEase().Eval(FMath::Clamp((T - Start) / Length, 0.f, 1.f));
	}

	float Keys(TConstArrayView<FSupplyKey> InKeys, float Eased)
	{
		return SampleSupplyKeys(InKeys, Eased, FSupplyEase::Linear());
	}

	// CSS 반투명색을 UE(리니어 합성)에서 같은 결과가 나오게 하는 알파. Under 위 결과색을 브라우저(sRGB 합성)와 맞춘다.
	float LinearAlpha(uint32 Over, float Alpha, uint32 Under)
	{
		const FLinearColor O = Hex(Over);
		const FLinearColor U = Hex(Under);
		const FLinearColor Target = MixSRGB(Over, Alpha, Under);
		float Sum = 0.f;
		float Weight = 0.f;
		for (int32 Channel = 0; Channel < 3; ++Channel)
		{
			const float Delta = O.Component(Channel) - U.Component(Channel);
			if (FMath::Abs(Delta) > .02f)
			{
				Sum += (Target.Component(Channel) - U.Component(Channel)) / Delta * FMath::Abs(Delta);
				Weight += FMath::Abs(Delta);
			}
		}
		return Weight > 0.f ? FMath::Clamp(Sum / Weight, 0.f, 1.f) : Alpha;
	}

	// 단색 면. 렌더 변환(기울임·회전)을 걸어도 가장자리가 계단지지 않게 RoundedBox(반경 0)로 그린다(ENGINE_BINDING).
	FSlateBrush SolidBrush(const FLinearColor& Color)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(Color);
		Brush.OutlineSettings.CornerRadii = FVector4(0.f, 0.f, 0.f, 0.f);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		Brush.OutlineSettings.Width = 0.f;
		return Brush;
	}

	UImage* Solid(UWidgetTree* Tree, const FLinearColor& Color)
	{
		UImage* Image = Tree->ConstructWidget<UImage>();
		Image->SetBrush(SolidBrush(Color));
		Image->SetVisibility(ESlateVisibility::HitTestInvisible);
		return Image;
	}

	UCanvasPanelSlot* Put(UCanvasPanel* Parent, UWidget* Child, float X, float Y, float Width, float Height)
	{
		UCanvasPanelSlot* Slot = Parent->AddChildToCanvas(Child);
		Slot->SetAutoSize(false);
		Slot->SetPosition(FVector2D(X, Y));
		Slot->SetSize(FVector2D(Width, Height));
		return Slot;
	}

	UCanvasPanel* Group(UWidgetTree* Tree, UCanvasPanel* Parent, float X, float Y, float Width, float Height)
	{
		UCanvasPanel* Canvas = Tree->ConstructWidget<UCanvasPanel>();
		Canvas->SetVisibility(ESlateVisibility::HitTestInvisible);
		Put(Parent, Canvas, X, Y, Width, Height);
		return Canvas;
	}

	struct FTextSpec
	{
		EFace Face = EFace::Regular;
		float Px = 16.f;
		float LetterEm = 0.f;
		bool bItalic = false;
		ERef Ref = ERef::Malgun;
		float LineHeight = -1.f;
	};

	// 첫 줄 기준선이 Baseline에 오도록 놓는다. AlignX: 0 왼쪽, .5 가운데, 1 오른쪽 기준(X가 그 점).
	void PlaceText(UTextBlock* Text, float X, float Baseline, ERef Ref, float LineHeight, float AlignX = 0.f)
	{
		Typeset(Text, Ref, LineHeight);
		const float CssPx = Text->GetFont().Size / .75f;
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Text->Slot))
		{
			Slot->SetAutoSize(true);
			Slot->SetAlignment(FVector2D(AlignX, 0.f));
			Slot->SetPosition(FVector2D(X, Baseline - CssBaseline(CssPx, Ref, LineHeight)));
		}
	}

	UTextBlock* AddText(UWidgetTree* Tree, UCanvasPanel* Parent, const FText& Value, const FTextSpec& Spec, const FLinearColor& Color,
		float X, float Baseline, float AlignX = 0.f)
	{
		UTextBlock* Text = Tree->ConstructWidget<UTextBlock>();
		Text->SetText(Value);
		Text->SetFont(Font(Spec.Face, Spec.Px, Spec.LetterEm, Spec.bItalic));
		Text->SetColorAndOpacity(FSlateColor(Color));
		Text->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (AlignX > .75f)
		{
			Text->SetJustification(ETextJustify::Right);
		}
		else if (AlignX > .25f)
		{
			Text->SetJustification(ETextJustify::Center);
		}
		Parent->AddChildToCanvas(Text);
		PlaceText(Text, X, Baseline, Spec.Ref, Spec.LineHeight, AlignX);
		return Text;
	}

	FTextSpec Spec(EFace Face, float Px, float LetterEm = 0.f, ERef Ref = ERef::Malgun, bool bItalic = false, float LineHeight = -1.f)
	{
		FTextSpec Out;
		Out.Face = Face;
		Out.Px = Px;
		Out.LetterEm = LetterEm;
		Out.bItalic = bItalic;
		Out.Ref = Ref;
		Out.LineHeight = LineHeight;
		return Out;
	}

	// 맑은 고딕 700은 Malgun Bold 한 단계라 획이 가장 가까운 Noto Sans KR SemiBold로(탐사 목적지 화면 측정, 2026-10-13)
	constexpr EFace MalgunBold = EFace::SemiBold;

	FTextSpec Consolas(float Px, float LetterEm = 0.f, bool bBold = false) { return Spec(bBold ? EFace::MonoBold : EFace::Mono, Px, LetterEm, ERef::Consolas); }
	FTextSpec ArialBlack(float Px, float LetterEm = 0.f) { return Spec(EFace::Display, Px, LetterEm, ERef::ArialBlack, true); }
	FTextSpec Malgun(float Px, bool bBold, float LetterEm = 0.f, float LineHeight = -1.f)
	{
		return Spec(bBold ? MalgunBold : EFace::Regular, Px, LetterEm, ERef::Malgun, false, LineHeight);
	}

	/*
	 * 화살표 글리프. 시안의 ↘ ↗ ⟶는 Arial Black·Arial·맑은 고딕에 없어 브라우저가 대체 글꼴(굽은 미늘)로 그린다 — 배포 글꼴에 같은 모양이
	 * 없으므로 300px로 크게 렌더한 모양을 선(중심선·굵기)으로 옮겼다. 좌표: 그 렌더의 칸 왼쪽 = 펜 위치, Baseline = 기준선.
	 */
	struct FGlyphPart
	{
		TArray<FVector2D> Points;
		float Width = 0.f;
	};

	struct FGlyphDef
	{
		float CellLeft = 0.f;
		float Baseline = 0.f;
		TArray<FGlyphPart> Parts;
	};

	enum class EArrow : uint8 { BoldSouthEast, ThinSouthEast, LongRight, NorthEast, SouthEast };

	const FGlyphDef& GlyphDef(EArrow Arrow)
	{
		static const FGlyphDef Defs[] = {
			// .brand i — Arial Black(합성 이탤릭) 문맥의 ↘
			{ 0.f, 348.f, { { { { 106, 137 }, { 232, 302 } }, 40.f }, { { { 258, 221 }, { 249, 262 }, { 241, 316 } }, 33.f }, { { { 148, 310 }, { 195, 309 }, { 241, 316 } }, 33.f } } },
			// .index>span — 맑은 고딕 ↘
			{ 400.f, 327.f, { { { { 457, 151 }, { 622, 316 } }, 15.5f }, { { { 614.5f, 242 }, { 609, 267.5f }, { 612, 291 }, { 628, 321 } }, 14.f }, { { { 547.5f, 309.5f }, { 572.5f, 304.5f }, { 592.5f, 305.5f }, { 613, 316 }, { 628, 321 } }, 14.f } } },
			// .long-arrow — ⟶
			{ 800.f, 277.f, { { { { 817.5f, 190.5f }, { 1120, 190.5f } }, 19.f }, { { { 1076, 134 }, { 1127, 190.5f }, { 1076, 247 } }, 15.f } } },
			// .ticket-top>i — Arial ↗
			{ 1200.f, 348.f, { { { { 1253, 331 }, { 1428, 156 } }, 19.5f }, { { { 1341, 150.5f }, { 1400, 146 }, { 1444, 139 } }, 19.f }, { { { 1442, 141 }, { 1434, 200 }, { 1432, 242.5f } }, 20.f } } },
			// .stamp i — Arial ↘
			{ 2000.f, 348.f, { { { { 2053, 137 }, { 2228, 312 } }, 19.5f }, { { { 2141, 317.5f }, { 2200, 322 }, { 2244, 329 } }, 19.f }, { { { 2242, 327 }, { 2234, 268 }, { 2232, 225.5f } }, 20.f } } },
		};
		return Defs[static_cast<int32>(Arrow)];
	}

	// 펜 위치(PenX, Baseline)에 글자 크기 Px로 놓는다. 칸 400×400(기준선 = 위에서 300) — 획이 칸을 넘어도 잘리지 않는다.
	UWidget* AddArrow(UWidgetTree* Tree, UCanvasPanel* Parent, EArrow Arrow, float Px, const FLinearColor& Color, float PenX, float Baseline)
	{
		const FGlyphDef& Def = GlyphDef(Arrow);
		const float Scale = Px / 300.f;
		UOverlay* Box = Tree->ConstructWidget<UOverlay>();
		Box->SetVisibility(ESlateVisibility::HitTestInvisible);
		for (const FGlyphPart& Part : Def.Parts)
		{
			TArray<FVector2D> Points;
			for (const FVector2D& Point : Part.Points)
			{
				Points.Add(FVector2D(Point.X - Def.CellLeft, Point.Y - Def.Baseline + 300.f));
			}
			UStrokeGlyphWidget* Glyph = Tree->ConstructWidget<UStrokeGlyphWidget>();
			Glyph->SetGlyph(FVector2D(400.f, 400.f), { Points }, Part.Width);
			Glyph->SetStrokeColor(Color);
			UOverlaySlot* Slot = Box->AddChildToOverlay(Glyph);
			Slot->SetHorizontalAlignment(HAlign_Fill);
			Slot->SetVerticalAlignment(VAlign_Fill);
		}
		Put(Parent, Box, PenX, Baseline - 300.f * Scale, 400.f * Scale, 400.f * Scale);
		return Box;
	}

	void MoveArrow(UWidget* Arrow, float Px, float PenX, float Baseline)
	{
		if (UCanvasPanelSlot* Slot = Arrow ? Cast<UCanvasPanelSlot>(Arrow->Slot) : nullptr)
		{
			Slot->SetPosition(FVector2D(PenX, Baseline - Px));
		}
	}

	void SetMotion(UWidget* Widget, float Opacity, const FVector2D& Translation, float Angle = 0.f, float Scale = 1.f)
	{
		if (!Widget)
		{
			return;
		}
		Widget->SetRenderOpacity(Opacity);
		Widget->SetRenderTransform(FWidgetTransform(Translation, FVector2D(Scale, Scale), FVector2D::ZeroVector, Angle));
		Widget->SetVisibility(Opacity > 0.f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}

	void SetShown(UWidget* Widget, bool bShow)
	{
		if (Widget)
		{
			Widget->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}
	}

	float MeasureWidth(const UTextBlock* Text)
	{
		if (!Text || !FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
		{
			return 0.f;
		}
		return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text->GetText(), Text->GetFont(), 1.f).X;
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

	// 시안 캡션은 문장마다 줄을 바꾼다(데이터 에셋은 한 줄) — ". " 를 줄바꿈으로
	FText SplitSentences(const FText& Caption)
	{
		return FText::FromString(Caption.ToString().Replace(TEXT(". "), TEXT(".\n")));
	}
}

// --- 수명 ---

void UExpeditionOpeningWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildTree();
}

void UExpeditionOpeningWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::HitTestInvisible);
	View = FVector2D::ZeroVector;
	DataHash = 0;
	bModeApplied = false;
	bFinishRequested = false;
}

void UExpeditionOpeningWidget::NativeDestruct()
{
	Super::NativeDestruct();
}

void UExpeditionOpeningWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	const FVector2D Size = MyGeometry.GetLocalSize();
	if (Size.X > 1.f && Size.Y > 1.f && !Size.Equals(View, .5f))
	{
		ApplyLayout(Size);
	}

	FExpeditionEntryState State;
	EMode Mode = EMode::Timeline;
	float TimeMs = 0.f;
	IExpeditionEntryBackend* Backend = nullptr;

#if !UE_BUILD_SHIPPING
	if (bDebug)
	{
		State = DebugState;
		Mode = DebugMode == 1 ? EMode::Loading : (DebugMode == 2 ? EMode::Empty : EMode::Timeline);
		if (bDebugPlaying)
		{
			DebugTimeMs += InDeltaTime * 1000.f;
			if (DebugTimeMs >= ExpeditionOpeningUI::EndMs)
			{
				RemoveFromParent();
				return;
			}
		}
		TimeMs = DebugTimeMs;
	}
	else
#endif
	{
		APlayerController* PC = GetOwningPlayer();
		Backend = PC ? Cast<IExpeditionEntryBackend>(PC->FindComponentByInterface(UExpeditionEntryBackend::StaticClass())) : nullptr;
		if (!Backend || !Backend->GetEntryView().bOpeningVisible)
		{
			return;
		}
		const FExpeditionEntryView& EntryView = Backend->GetEntryView();
		State = EntryView.State;
		const double Elapsed = Backend->GetEntryServerTime() - State.OpeningStartServerTime;
		TimeMs = static_cast<float>(Elapsed * 1000.0);
		if (State.Phase == EExpeditionEntryPhase::Preparing || (State.Phase == EExpeditionEntryPhase::Opening && TimeMs < 0.f))
		{
			Mode = EMode::Loading;	// 팀원 준비 대기 / 시작 예약 직전 — 검은 화면
			TimeMs = 0.f;
		}
		else if (State.DisplayName.IsEmpty())
		{
			Mode = EMode::Empty;
		}
	}

	const uint32 Hash = HashCombine(HashCombine(GetTypeHash(State.DisplayName.ToString()), GetTypeHash(State.EnglishName.ToString())),
		HashCombine(HashCombine(GetTypeHash(State.MapQuota), GetTypeHash(State.TimeLimitSeconds)), HashCombine(GetTypeHash(State.ZoneNumber), GetTypeHash(State.Caption.ToString()))));
	if (Hash != DataHash)
	{
		DataHash = Hash;
		ApplyData(State);
	}
	if (!bModeApplied || Mode != CurrentMode)
	{
		ApplyMode(Mode);
	}

	// 데이터 없음: 대체 화면만, 탐사가 열리면(시안 끝) 걷는다. 정상: 도착 안내 퇴장까지.
	const float LastMs = Mode == EMode::Empty ? ExpeditionOpeningUI::TimelineMs : ExpeditionOpeningUI::EndMs;
	if (Mode != EMode::Loading && TimeMs >= LastMs && Backend && !bFinishRequested)
	{
		bFinishRequested = true;
		Backend->FinishOpening(Backend->GetEntryView().Context);
		return;
	}

	if (Mode == EMode::Timeline)
	{
		ApplyFrame(TimeMs);
	}
}

// --- 트리 ---

void UExpeditionOpeningWidget::BuildTree()
{
	if (!WidgetTree)
	{
		return;
	}
	Stage = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Stage"));
	Stage->SetVisibility(ESlateVisibility::HitTestInvisible);
	WidgetTree->RootWidget = Stage;

	// DOM 순서: .world(도착 안내) → .brief → .cutin → .fallback (.skip은 없음)
	BuildArrival();
	BuildBrief();
	BuildCutin();
	BuildFallback();
	ApplyLayout(FVector2D(1920.f, 1080.f));
}

void UExpeditionOpeningWidget::BuildArrival()
{
	UWidgetTree* T = WidgetTree;
	// .arrival: left 6.2%, bottom 11%, padding-left 24. 상자 높이 100(위치는 ApplyLayout)
	Arrival = ExpeditionOpeningUI::Group(T, Stage, 0.f, 0.f, 360.f, 100.f);
	ExpeditionOpeningUI::Put(Arrival, ExpeditionOpeningUI::Solid(T, Lime()), 0.f, 3.f, 4.f, 92.f);	// .arrival-line top 3 / bottom 5
	ExpeditionOpeningUI::AddText(T, Arrival, LOCTEXT("ArrivalKicker", "탐사 시작"), ExpeditionOpeningUI::Malgun(12.f, true), Lime(), 24.f, 17.f);
	ArrivalName = ExpeditionOpeningUI::AddText(T, Arrival, FText::GetEmpty(), ExpeditionOpeningUI::Malgun(32.f, true, -.05f), Paper(), 24.f, 64.f);
	ExpeditionOpeningUI::AddText(T, Arrival, LOCTEXT("ArrivalLine", "유물을 회수하고, 함께 돌아오세요."), ExpeditionOpeningUI::Malgun(12.f, false), Hex(0xbcc7d7), 24.f, 97.f);
}

void UExpeditionOpeningWidget::BuildBrief()
{
	UWidgetTree* T = WidgetTree;
	Brief = ExpeditionOpeningUI::Group(T, Stage, 0.f, 0.f, 1920.f, 1080.f);

	// .blue-field / .paper-edge: clip-path 사선 면을 기울인 상자로(위치·기울기는 ApplyLayout)
	BlueField = ExpeditionOpeningUI::Solid(T, Blue());
	ExpeditionOpeningUI::Put(Brief, BlueField, 0.f, 0.f, 1.f, 1.f);
	PaperEdge = ExpeditionOpeningUI::Solid(T, Paper());
	ExpeditionOpeningUI::Put(Brief, PaperEdge, 0.f, 0.f, 1.f, 1.f);

	// .halftone: 6px 격자 점 + 130° 마스크(런타임 텍스처)
	Halftone = T->ConstructWidget<UImage>();
	Halftone->SetVisibility(ESlateVisibility::HitTestInvisible);
	ExpeditionOpeningUI::Put(Brief, Halftone, 0.f, 0.f, 1.f, 1.f);

	// .outline-word: 29cqw, 글자 칠 투명 + 1px #f0f1de1c 외곽선
	{
		FSlateFontInfo OutlineFont = Font(EFace::Display, 556.8f, -.08f, true);
		OutlineFont.OutlineSettings.OutlineSize = 1;
		OutlineFont.OutlineSettings.bSeparateFillAlpha = true;
		FLinearColor Stroke = Paper();
		Stroke.A = ExpeditionOpeningUI::LinearAlpha(0xf0f1de, 0x1c / 255.f, 0x2747d8);
		OutlineFont.OutlineSettings.OutlineColor = Stroke;
		OutlineWord = T->ConstructWidget<UTextBlock>();
		OutlineWord->SetText(FText::FromString(TEXT("DEEP")));
		OutlineWord->SetFont(OutlineFont);
		OutlineWord->SetColorAndOpacity(FSlateColor(FLinearColor(Paper().R, Paper().G, Paper().B, 0.f)));
		OutlineWord->SetVisibility(ESlateVisibility::HitTestInvisible);
		Brief->AddChildToCanvas(OutlineWord);
		ExpeditionOpeningUI::PlaceText(OutlineWord, 0.f, 0.f, ERef::ArialBlack, 556.8f * .85f);
	}

	BuildMasthead();

	// .vertical-label: vertical-rl + rotate(180deg) = 가로 글자를 -90° 돌린 것(아래에서 위로 읽힘). 상자 10×262.
	{
		UCanvasPanel* Line = T->ConstructWidget<UCanvasPanel>();
		Line->SetVisibility(ESlateVisibility::HitTestInvisible);
		UTextBlock* Text = ExpeditionOpeningUI::AddText(T, Line, FText::FromString(TEXT("LEAVE WITH SOMETHING. COME BACK TOGETHER.")), ExpeditionOpeningUI::Consolas(9.f, .16f), Paper(), 0.f, 0.f);
		// 시안 잉크: x 40..45(대문자 위 → 기준선), 상자 왼쪽 38.4 → 회전 전 줄 위에서 기준선 7.6
		Typeset(Text, ERef::Consolas, 10.f, 7.6f);
		Cast<UCanvasPanelSlot>(Text->Slot)->SetPosition(FVector2D(0.f, 0.f));
		ExpeditionOpeningUI::Put(Brief, Line, 0.f, 0.f, 262.f, 10.f);
		Line->SetRenderTransformAngle(-90.f);
		VerticalLabel = Line;
	}

	// .index: right 4.5%, top 22% — grid(ZONE 한 줄 + 번호·화살표). 묶음 상자 301.6×166, cqw라 W/1920 배율(오른쪽 위 기준)
	{
		Index = ExpeditionOpeningUI::Group(T, Brief, 0.f, 0.f, 301.6f, 166.f);
		Index->SetRenderTransformPivot(FVector2D(1.f, 0.f));
		ExpeditionOpeningUI::AddText(T, Index, FText::FromString(TEXT("ZONE")), ExpeditionOpeningUI::Consolas(10.f, .25f), Lime(), 301.6f, 9.f, 1.f);
		IndexNumber = ExpeditionOpeningUI::AddText(T, Index, FText::GetEmpty(), ExpeditionOpeningUI::ArialBlack(153.6f, -.1f), Paper(), 0.f, 149.f);
		ExpeditionOpeningUI::AddArrow(T, Index, ExpeditionOpeningUI::EArrow::ThinSouthEast, 115.2f, Lime(), 192.1f, 156.4f);
	}

	BuildTitle();
	BuildMission();

	// .brief-footer: left 8%, bottom 5% — 두 번째 span(GH / EXP—01)은 시안 CSS가 숨김
	{
		Footer = ExpeditionOpeningUI::Group(T, Brief, 0.f, 0.f, 300.f, 12.f);
		ExpeditionOpeningUI::AddText(T, Footer, FText::FromString(TEXT("GO DEEP.")), ExpeditionOpeningUI::Consolas(10.f, .13f), Paper(), 0.f, 9.f);
		ExpeditionOpeningUI::AddText(T, Footer, FText::FromString(TEXT("GO HOME.")), ExpeditionOpeningUI::Consolas(10.f, .13f, true), Lime(), 61.2f, 9.f);
	}

	BuildStamp();
}

void UExpeditionOpeningWidget::BuildMasthead()
{
	UWidgetTree* T = WidgetTree;
	// .masthead: inset 5% 4.5% auto, 높이 68(워드마크). 묶음 원점 = 워드마크 왼쪽 위.
	Masthead = ExpeditionOpeningUI::Group(T, Brief, 0.f, 0.f, 1747.2f, 68.f);
	ExpeditionOpeningUI::AddText(T, Masthead, FText::FromString(TEXT("GO")), ExpeditionOpeningUI::ArialBlack(40.f, -.1f), Paper(), 0.f, 33.f);
	ExpeditionOpeningUI::AddText(T, Masthead, FText::FromString(TEXT("HOME")), ExpeditionOpeningUI::ArialBlack(40.f, -.1f), Paper(), 0.f, 67.f);
	ExpeditionOpeningUI::AddArrow(T, Masthead, ExpeditionOpeningUI::EArrow::BoldSouthEast, 46.f, Lime(), 105.3f, 37.7f);

	// 부서 표기: border-left 1px #f0f1de6b(파란 면 위 결과색), padding-left 20
	ExpeditionOpeningUI::Put(Masthead, ExpeditionOpeningUI::Solid(T, MixSRGB(0xf0f1de, 0x6b / 255.f, 0x2747d8)), 174.6f, 17.f, 1.f, 33.f);
	ExpeditionOpeningUI::AddText(T, Masthead, FText::FromString(TEXT("EXPEDITION DEPARTMENT")), ExpeditionOpeningUI::Consolas(9.f, .07f), Paper(), 196.3f, 28.3f);
	ExpeditionOpeningUI::AddText(T, Masthead, LOCTEXT("Department", "탐사 구역 진입"), ExpeditionOpeningUI::Malgun(11.f, true, .06f), Paper(), 196.3f, 45.5f);

	// .edition: 오른쪽 정렬
	ExpeditionOpeningUI::AddText(T, Masthead, FText::FromString(TEXT("FIELD RECORD")), ExpeditionOpeningUI::Consolas(9.f, .07f), Paper(), 1747.2f, 28.8f, 1.f);
	Record = ExpeditionOpeningUI::AddText(T, Masthead, FText::GetEmpty(), ExpeditionOpeningUI::Consolas(11.f, .06f), Paper(), 1747.2f, 45.f, 1.f);
}

void UExpeditionOpeningWidget::BuildTitle()
{
	UWidgetTree* T = WidgetTree;
	// .title-block: top 29%, left 8%, width 79%. 아래 y는 묶음 위에서부터.
	Title = ExpeditionOpeningUI::Group(T, Brief, 0.f, 0.f, 1516.8f, 520.f);
	ExpeditionOpeningUI::Put(Title, ExpeditionOpeningUI::Solid(T, Lime()), 0.f, 10.f, 22.f, 4.f);	// .eyebrow>span
	ExpeditionOpeningUI::AddText(T, Title, FText::FromString(TEXT("NOW ENTERING")), ExpeditionOpeningUI::Consolas(11.f, .15f), Paper(), 34.f, 15.f);
	ExpeditionOpeningUI::Put(Title, ExpeditionOpeningUI::Solid(T, Ink()), 138.4f, 0.f, 67.f, 23.f);	// .eyebrow>b padding 5 9
	ExpeditionOpeningUI::AddText(T, Title, LOCTEXT("EyebrowChip", "탐사 개시"), ExpeditionOpeningUI::Malgun(10.f, true, .1f), Lime(), 147.4f, 16.f);

	TitleText = ExpeditionOpeningUI::AddText(T, Title, FText::GetEmpty(), ExpeditionOpeningUI::ArialBlack(210.f, -.065f), Paper(), 0.f, 191.f);
	TitleText->SetShadowOffset(FVector2D(7.f, 7.f));
	TitleText->SetShadowColorAndOpacity(Ink());
	NameText = ExpeditionOpeningUI::AddText(T, Title, FText::GetEmpty(), ExpeditionOpeningUI::Malgun(44.f, true, -.055f), Paper(), 0.f, 316.3f);
	NameArrow = ExpeditionOpeningUI::AddArrow(T, Title, ExpeditionOpeningUI::EArrow::LongRight, 55.f, Lime(), 150.7f, 316.3f);
	CaptionText = ExpeditionOpeningUI::AddText(T, Title, FText::GetEmpty(), ExpeditionOpeningUI::Malgun(16.f, false, 0.f, 29.6f), Hex(0xe0e6ff), 0.f, 374.3f);
}

void UExpeditionOpeningWidget::BuildMission()
{
	UWidgetTree* T = WidgetTree;
	// .mission: right 5.5%, bottom 15%, width min(32%, 520), rotate(-4deg), box-shadow 9px 10px 0 ink — 그림자는 상자와 함께 돈다.
	constexpr float W = 520.f;
	constexpr float H = 269.f;
	Mission = ExpeditionOpeningUI::Group(T, Brief, 0.f, 0.f, W, H);
	ExpeditionOpeningUI::Put(Mission, ExpeditionOpeningUI::Solid(T, Ink()), 9.f, 10.f, W, H);
	ExpeditionOpeningUI::Put(Mission, ExpeditionOpeningUI::Solid(T, Paper()), 0.f, 0.f, W, H);

	const FLinearColor Rule = MixSRGB(0x080f20, 0x50 / 255.f, 0xf0f1de);
	const FLinearColor Dash = MixSRGB(0x080f20, 0x80 / 255.f, 0xf0f1de);
	const FLinearColor Bar = MixSRGB(0x080f20, .75f, 0xf0f1de);

	// .ticket-top(높이 57, border-bottom 1px)
	ExpeditionOpeningUI::AddText(T, Mission, FText::FromString(TEXT("YOUR ASSIGNMENT")), ExpeditionOpeningUI::Consolas(9.f, .04f, true), Ink(), 20.f, 31.2f);
	ExpeditionOpeningUI::AddText(T, Mission, LOCTEXT("TicketTitle", "회수 지시서"), ExpeditionOpeningUI::Malgun(11.f, true), Ink(), 111.6f, 32.7f);
	ExpeditionOpeningUI::AddArrow(T, Mission, ExpeditionOpeningUI::EArrow::NorthEast, 26.f, Ink(), 474.f, 37.2f);
	ExpeditionOpeningUI::Put(Mission, ExpeditionOpeningUI::Solid(T, Rule), 0.f, 56.f, W, 1.f);

	// .ticket-main: 두 칸(240), 둘째 칸 border-left 1px + padding-left 20
	ExpeditionOpeningUI::AddText(T, Mission, LOCTEXT("QuotaLabel", "회수 할당량"), ExpeditionOpeningUI::Malgun(11.f, true), Ink(), 20.f, 91.2f);
	QuotaText = ExpeditionOpeningUI::AddText(T, Mission, FText::GetEmpty(), ExpeditionOpeningUI::Consolas(60.f, -.05f), Ink(), 20.f, 153.2f);
	ExpeditionOpeningUI::AddText(T, Mission, LOCTEXT("QuotaUnit", "목표"), ExpeditionOpeningUI::Malgun(9.f, false), Hex(0x536072), 20.f, 189.2f);
	ExpeditionOpeningUI::Put(Mission, ExpeditionOpeningUI::Solid(T, Rule), 260.f, 79.f, 1.f, 114.f);
	ExpeditionOpeningUI::AddText(T, Mission, LOCTEXT("TimeLabel", "탐사 제한 시간"), ExpeditionOpeningUI::Malgun(11.f, true), Ink(), 281.f, 91.2f);
	TimeText = ExpeditionOpeningUI::AddText(T, Mission, FText::GetEmpty(), ExpeditionOpeningUI::Consolas(60.f, -.05f), Ink(), 281.f, 153.2f);
	TimeUnitText = ExpeditionOpeningUI::AddText(T, Mission, LOCTEXT("TimeUnit", "분 : 초"), ExpeditionOpeningUI::Malgun(9.f, false), Hex(0x536072), 281.f, 189.2f);

	// .ticket-bottom: border-top 1px dashed(브라우저 렌더: 3px 선 · 2px 틈), 바코드 80×25(14px 반복 무늬, opacity .75)
	for (float X = 0.f; X < W; X += 5.f)
	{
		ExpeditionOpeningUI::Put(Mission, ExpeditionOpeningUI::Solid(T, Dash), X, 213.f, FMath::Min(3.f, W - X), 1.f);
	}
	static const float BarRuns[][2] = { { 0.f, 2.f }, { 4.f, 1.f }, { 8.f, 4.f } };
	for (float Repeat = 0.f; Repeat < 80.f; Repeat += 14.f)
	{
		for (const float* Run : BarRuns)
		{
			const float X = Repeat + Run[0];
			if (X < 80.f)
			{
				ExpeditionOpeningUI::Put(Mission, ExpeditionOpeningUI::Solid(T, Bar), 20.f + X, 229.f, FMath::Min(Run[1], 80.f - X), 25.f);
			}
		}
	}
	ExpeditionOpeningUI::AddText(T, Mission, FText::FromString(TEXT("BRING IT BACK.")), ExpeditionOpeningUI::Consolas(8.f, .08f), Ink(), 500.f, 235.2f, 1.f);
	ExpeditionOpeningUI::AddText(T, Mission, LOCTEXT("TicketPromise", "반드시, 돌아올 것."), ExpeditionOpeningUI::Malgun(10.f, true, .04f), Ink(), 500.f, 251.6f, 1.f);
}

void UExpeditionOpeningWidget::BuildStamp()
{
	UWidgetTree* T = WidgetTree;
	// .stamp: right 7%, top 23%, rotate(9deg), border 3px ink, box-shadow 8px 8px 0 ink, padding 15 23 15 19
	constexpr float W = 412.f;
	constexpr float H = 162.f;
	Stamp = ExpeditionOpeningUI::Group(T, Brief, 0.f, 0.f, W, H);
	ExpeditionOpeningUI::Put(Stamp, ExpeditionOpeningUI::Solid(T, Ink()), 8.f, 8.f, W, H);
	ExpeditionOpeningUI::Put(Stamp, ExpeditionOpeningUI::Solid(T, Ink()), 0.f, 0.f, W, H);
	ExpeditionOpeningUI::Put(Stamp, ExpeditionOpeningUI::Solid(T, Lime()), 3.f, 3.f, W - 6.f, H - 6.f);
	ExpeditionOpeningUI::AddText(T, Stamp, FText::FromString(TEXT("LET’S")), ExpeditionOpeningUI::ArialBlack(20.f), Ink(), 22.2f, 40.4f);
	ExpeditionOpeningUI::AddText(T, Stamp, FText::FromString(TEXT("DIVE!")), ExpeditionOpeningUI::ArialBlack(96.f, -.07f), Ink(), 22.2f, 129.4f);
	ExpeditionOpeningUI::AddArrow(T, Stamp, ExpeditionOpeningUI::EArrow::SouthEast, 96.f, Ink(), 290.6f, 129.4f);
}

void UExpeditionOpeningWidget::BuildCutin()
{
	UWidgetTree* T = WidgetTree;
	Cutin = ExpeditionOpeningUI::Group(T, Stage, 0.f, 0.f, 1920.f, 1080.f);
	// .cutin-paper / .cutin-blue: inset -25% -20%, skewX(-13deg)
	CutinPaper = ExpeditionOpeningUI::Solid(T, Paper());
	ExpeditionOpeningUI::Put(Cutin, CutinPaper, 0.f, 0.f, 1.f, 1.f);
	CutinBlue = ExpeditionOpeningUI::Solid(T, Blue());
	ExpeditionOpeningUI::Put(Cutin, CutinBlue, 0.f, 0.f, 1.f, 1.f);

	// .cutin-type: left 23%, top 18%, rotate(-6deg). 상자 1179×486(내용 크기), cqw라 W/1920 배율.
	CutinType = ExpeditionOpeningUI::Group(T, Cutin, 0.f, 0.f, 1179.f, 486.f);
	ExpeditionOpeningUI::AddText(T, CutinType, FText::FromString(TEXT("GOHOME / EXPEDITION")), ExpeditionOpeningUI::Consolas(11.f, .25f), Paper(), 0.f, 17.f);
	for (const TPair<const TCHAR*, float>& Line : { TPair<const TCHAR*, float>(TEXT("INTO"), 235.f), TPair<const TCHAR*, float>(TEXT("THE"), 433.1f) })
	{
		UTextBlock* Word = ExpeditionOpeningUI::AddText(T, CutinType, FText::FromString(Line.Key), ExpeditionOpeningUI::ArialBlack(230.4f, -.065f), Paper(), 0.f, Line.Value);
		Word->SetShadowOffset(FVector2D(8.f, 8.f));
		Word->SetShadowColorAndOpacity(Ink());
	}
	UTextBlock* Deep = ExpeditionOpeningUI::AddText(T, CutinType, FText::FromString(TEXT("DEEP.")), ExpeditionOpeningUI::ArialBlack(230.4f, -.065f), Lime(), 541.6f, 433.1f);
	Deep->SetShadowOffset(FVector2D(8.f, 8.f));
	Deep->SetShadowColorAndOpacity(Ink());
	ExpeditionOpeningUI::AddText(T, CutinType, LOCTEXT("CutinLine", "다시, 깊은 곳으로."), ExpeditionOpeningUI::Malgun(14.f, true, .2f), Paper(), 0.f, 482.3f);
}

void UExpeditionOpeningWidget::BuildFallback()
{
	UWidgetTree* T = WidgetTree;
	// 팀 대기·존 정보 없음: 문구 없이 검은 화면만(사용자 결정 2026-10-14 — 시안의 .fallback 대기/오류 문구는 쓰지 않는다)
	Fallback = ExpeditionOpeningUI::Group(T, Stage, 0.f, 0.f, 1920.f, 1080.f);
	UImage* Back = ExpeditionOpeningUI::Solid(T, FLinearColor::Black);
	if (UCanvasPanelSlot* BackSlot = Fallback->AddChildToCanvas(Back))
	{
		BackSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		BackSlot->SetOffsets(FMargin(0.f));
	}
}

// --- 레이아웃 ---

void UExpeditionOpeningWidget::ApplyLayout(const FVector2D& ViewSize)
{
	View = ViewSize;
	const float W = ViewSize.X;
	const float H = ViewSize.Y;
	const float K = W / ExpeditionOpeningUI::ReferenceWidth;	// 1cqw = W/100
	auto Move = [](UWidget* Widget, float X, float Y, float Width = -1.f, float Height = -1.f)
	{
		if (UCanvasPanelSlot* WidgetSlot = Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr)
		{
			WidgetSlot->SetPosition(FVector2D(X, Y));
			if (Width >= 0.f)
			{
				WidgetSlot->SetSize(FVector2D(Width, Height));
			}
		}
	};

	Move(Arrival, .062f * W, H - .11f * H - 100.f);
	Move(Brief, 0.f, 0.f, W, H);
	Move(Cutin, 0.f, 0.f, W, H);
	Move(Fallback, 0.f, 0.f, W, H);

	// 사선 면: 꼭짓점 (0,0)(64%,0)(49%,100%)(0,100%) — 가운데 높이에서 오른쪽 변 56.5%, 기울기 −15%W/H
	const float Slant = FMath::RadiansToDegrees(FMath::Atan(-.15f * W / H));
	Move(BlueField, -.2f * W, 0.f, .765f * W, H);
	Move(PaperEdge, .565f * W, 0.f, .0055f * W, H);
	Move(Halftone, 0.f, .18f * H, .6f * W, .82f * H);
	RebuildHalftone(FVector2D(.6f * W, .82f * H));
	for (UWidget* Panel : { static_cast<UWidget*>(BlueField), static_cast<UWidget*>(PaperEdge) })
	{
		Panel->SetRenderTransform(FWidgetTransform(FVector2D::ZeroVector, FVector2D(1.f, 1.f), FVector2D(Slant, 0.f), 0.f));
	}
	if (OutlineWord)
	{
		ExpeditionOpeningUI::PlaceText(OutlineWord, -.02f * W, .02f * H + 457.f * K, ERef::ArialBlack, 556.8f * .85f);
		OutlineWord->SetRenderTransformPivot(FVector2D(0.f, 0.f));
		OutlineWord->SetRenderScale(FVector2D(K, K));
	}

	// 워드마크 clamp(22px, 2.5cqw, 40px)는 W ≥ 1600에서 40 고정 — 그 범위만 시안 수치 그대로
	Move(Masthead, .045f * W, .05f * H, W - .09f * W, 68.f);
	if (UCanvasPanel* Panel = Masthead)
	{
		for (UWidget* Child : Panel->GetAllChildren())
		{
			UCanvasPanelSlot* ChildSlot = Cast<UCanvasPanelSlot>(Child->Slot);
			if (ChildSlot && ChildSlot->GetAlignment().X > .5f)
			{
				ChildSlot->SetPosition(FVector2D(W - .09f * W, ChildSlot->GetPosition().Y));
			}
		}
	}
	Move(VerticalLabel, .02f * W + 5.f - 131.f, .36f * H + 131.f - 5.f);
	Move(Index, W - .045f * W - 301.6f, .22f * H);
	if (Index)
	{
		Index->SetRenderScale(FVector2D(K, K));
	}
	Move(Title, .08f * W, .29f * H, .79f * W, 520.f);
	Move(Mission, W - .055f * W - 520.f, H - .15f * H - 269.f);
	Move(Footer, .08f * W, H - .05f * H - 12.f);
	// 회전 묶음은 가운데 기준이라 배율 뒤 가운데가 CSS 상자 가운데에 오도록
	Move(Stamp, W - .07f * W - 206.f * K - 206.f, .23f * H + 81.f * K - 81.f);

	Move(CutinPaper, -.2f * W, -.25f * H, 1.4f * W, 1.5f * H);
	Move(CutinBlue, -.2f * W, -.25f * H, 1.4f * W, 1.5f * H);
	Move(CutinType, .23f * W + 589.5f * K - 589.5f, .18f * H + 243.f * K - 243.f);

	LayoutTitle();
}

void UExpeditionOpeningWidget::LayoutTitle()
{
	if (!TitleText)
	{
		return;
	}
	const float Cqw = (View.X > 1.f ? View.X : ExpeditionOpeningUI::ReferenceWidth) / 100.f;
	const FString English = TitleText->GetText().ToString();
	// h1(clamp 60..210 12.4cqw) / .medium 9.6cqw / .long 8.2cqw(단어마다 줄)
	const bool bLong = TitleLines > 1;
	const bool bMedium = !bLong && English.Len() >= 8;
	TitlePx = bLong ? 8.2f * Cqw : (bMedium ? 9.6f * Cqw : FMath::Clamp(12.4f * Cqw, 60.f, 210.f));
	const float LineHeight = (bLong ? 1.02f : 1.03f) * TitlePx;
	TitleText->SetFont(Font(EFace::Display, TitlePx, -.065f, true));
	constexpr float TitleTop = 41.f;	// .eyebrow 23 + h1 margin-top 18
	ExpeditionOpeningUI::PlaceText(TitleText, 0.f, TitleTop + CssBaseline(TitlePx, ERef::ArialBlack, LineHeight), ERef::ArialBlack, LineHeight);

	// .name-line: h1 margin-bottom 3, 높이 75(h2 줄 59 + margin 8·8). h2 clamp(22, 2.6cqw, 44)
	const float NameTop = TitleTop + TitleLines * LineHeight + 3.f;
	const float NamePx = FMath::Clamp(2.6f * Cqw, 22.f, 44.f);
	NameText->SetFont(Font(ExpeditionOpeningUI::MalgunBold, NamePx, -.055f));
	const float NameBaseline = NameTop + 8.f + CssBaseline(NamePx, ERef::Malgun);
	ExpeditionOpeningUI::PlaceText(NameText, 0.f, NameBaseline, ERef::Malgun, -1.f);
	ExpeditionOpeningUI::MoveArrow(NameArrow, 55.f, ExpeditionOpeningUI::MeasureWidth(NameText) + 26.f, NameBaseline);

	// .caption: margin-top 18, clamp(11, 1cqw, 16) / 1.85
	const float CaptionPx = FMath::Clamp(Cqw, 11.f, 16.f);
	CaptionText->SetFont(Font(EFace::Regular, CaptionPx));
	const float CaptionTop = NameTop + 75.f + 18.f;
	ExpeditionOpeningUI::PlaceText(CaptionText, 0.f, CaptionTop + CssBaseline(CaptionPx, ERef::Malgun, CaptionPx * 1.85f), ERef::Malgun, CaptionPx * 1.85f);
}

void UExpeditionOpeningWidget::RebuildHalftone(const FVector2D& Size)
{
	if (!Halftone)
	{
		return;
	}
	const int32 Width = FMath::Clamp(FMath::RoundToInt(Size.X), 8, 4096);
	const int32 Height = FMath::Clamp(FMath::RoundToInt(Size.Y), 8, 4096);
	if (HalftoneTexture && HalftoneTexture->GetSizeX() == Width && HalftoneTexture->GetSizeY() == Height)
	{
		return;
	}
	HalftoneTexture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (!HalftoneTexture)
	{
		return;
	}
	HalftoneTexture->SRGB = true;
	HalftoneTexture->Filter = TF_Bilinear;
	HalftoneTexture->AddressX = TA_Clamp;
	HalftoneTexture->AddressY = TA_Clamp;

	// radial-gradient(ink .9px, transparent 1.4px) 0 0/6px 6px — 칸 가운데 점. opacity .16.
	// mask linear-gradient(130deg, transparent 15%, #000 90%) — 선 길이 = W|sin| + H|cos|.
	// 파란 면 위 결과색을 브라우저(sRGB 합성)와 맞추도록 알파를 리니어로 환산해 굽는다.
	const FVector2D Direction(FMath::Sin(FMath::DegreesToRadians(130.f)), -FMath::Cos(FMath::DegreesToRadians(130.f)));
	const float GradientLength = Width * FMath::Abs(Direction.X) + Height * FMath::Abs(Direction.Y);
	const FVector2D Center(Width * .5f, Height * .5f);
	uint8 AlphaLut[256];
	for (int32 Level = 0; Level < 256; ++Level)
	{
		AlphaLut[Level] = static_cast<uint8>(FMath::RoundToInt(ExpeditionOpeningUI::LinearAlpha(0x080f20, Level / 255.f, 0x2747d8) * 255.f));
	}

	FTexture2DMipMap& Mip = HalftoneTexture->GetPlatformData()->Mips[0];
	FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const FVector2D P(X + .5f, Y + .5f);
			const float DX = FMath::Fmod(P.X, 6.f) - 3.f;
			const float DY = FMath::Fmod(P.Y, 6.f) - 3.f;
			const float Distance = FMath::Sqrt(DX * DX + DY * DY);
			const float Dot = Distance <= .9f ? 1.f : (Distance >= 1.4f ? 0.f : (1.4f - Distance) / .5f);
			const float Along = FVector2D::DotProduct(P - Center, Direction) / GradientLength + .5f;
			const float Mask = FMath::Clamp((Along - .15f) / .75f, 0.f, 1.f);
			const uint8 Alpha = AlphaLut[FMath::Clamp(FMath::RoundToInt(Dot * Mask * .16f * 255.f), 0, 255)];
			Pixels[Y * Width + X] = FColor(0x08, 0x0f, 0x20, Alpha);
		}
	}
	Mip.BulkData.Unlock();
	HalftoneTexture->UpdateResource();
	Halftone->SetBrushFromTexture(HalftoneTexture, false);
}

// --- 데이터 ---

void UExpeditionOpeningWidget::ApplyData(const FExpeditionEntryState& State)
{
	FString English = State.EnglishName.ToString().ToUpper();
	if (English.IsEmpty())
	{
		English = State.DisplayName.ToString();
	}
	TArray<FString> Words;
	English.ParseIntoArrayWS(Words);
	TitleLines = FMath::Max(1, Words.Num());
	TitleText->SetText(FText::FromString(FString::Join(Words, TEXT("\n"))));
	NameText->SetText(State.DisplayName);
	CaptionText->SetText(ExpeditionOpeningUI::SplitSentences(State.Caption));
	ArrivalName->SetText(State.DisplayName);

	const FString Number = State.ZoneNumber > 0 ? FString::Printf(TEXT("%02d"), State.ZoneNumber) : FString(TEXT("—"));
	IndexNumber->SetText(FText::FromString(Number));
	Record->SetText(FText::FromString(FString::Printf(TEXT("%s / %s"), *Number, *English.Replace(TEXT("\n"), TEXT(" ")))));
	QuotaText->SetText(FText::FromString(ExpeditionOpeningUI::GroupThousands(State.MapQuota)));
	if (State.TimeLimitSeconds > 0.f)
	{
		const int32 Seconds = FMath::RoundToInt(State.TimeLimitSeconds);
		TimeText->SetText(FText::FromString(FString::Printf(TEXT("%02d:%02d"), Seconds / 60, Seconds % 60)));
		TimeUnitText->SetText(LOCTEXT("TimeUnit", "분 : 초"));
	}
	else
	{
		TimeText->SetText(FText::FromString(TEXT("—")));
		TimeUnitText->SetText(LOCTEXT("NoTimeLimit", "제한 없음"));
	}
	LayoutTitle();
}

void UExpeditionOpeningWidget::ApplyMode(EMode Mode)
{
	CurrentMode = Mode;
	bModeApplied = true;
	const bool bTimeline = Mode == EMode::Timeline;
	ExpeditionOpeningUI::SetShown(Fallback, !bTimeline);
	if (bTimeline)
	{
		return;
	}
	ExpeditionOpeningUI::SetShown(Arrival, false);
	ExpeditionOpeningUI::SetShown(Brief, false);
	ExpeditionOpeningUI::SetShown(Cutin, false);
}

// --- 타임라인 ---

void UExpeditionOpeningWidget::ApplyFrame(float T)
{
	const float W = View.X > 1.f ? View.X : ExpeditionOpeningUI::ReferenceWidth;
	const float K = W / ExpeditionOpeningUI::ReferenceWidth;

	// 컷인(시안 0~1345ms): 아이보리 → 65ms 뒤 코발트, 키 −120% / 0(.24) / 0(.66) / 125%(자기 폭 140%W 기준). 정지 컷만 CutinShift만큼 늘어난다.
	const float HoldAt = ExpeditionOpeningUI::CutinHoldAtMs;
	const float CutinT = T < HoldAt ? T : (T < HoldAt + ExpeditionOpeningUI::CutinShift ? HoldAt : T - ExpeditionOpeningUI::CutinShift);
	const bool bCutin = CutinT < 1345.f;
	ExpeditionOpeningUI::SetShown(Cutin, bCutin);
	if (bCutin)
	{
		static const FSupplyKey PanelKeys[] = { { 0.f, -1.2f }, { .24f, 0.f }, { .66f, 0.f }, { 1.f, 1.25f } };
		const float Slant = -13.f;
		CutinPaper->SetRenderTransform(FWidgetTransform(FVector2D(ExpeditionOpeningUI::Keys(PanelKeys, ExpeditionOpeningUI::Track(CutinT, 0.f, 1280.f)) * 1.4f * W, 0.f), FVector2D(1.f, 1.f), FVector2D(Slant, 0.f), 0.f));
		CutinBlue->SetRenderTransform(FWidgetTransform(FVector2D(ExpeditionOpeningUI::Keys(PanelKeys, ExpeditionOpeningUI::Track(CutinT, 65.f, 1280.f)) * 1.4f * W, 0.f), FVector2D(1.f, 1.f), FVector2D(Slant, 0.f), 0.f));
		static const FSupplyKey TypeOpacity[] = { { 0.f, 0.f }, { .2f, 1.f }, { .65f, 1.f }, { 1.f, 0.f } };
		static const FSupplyKey TypeX[] = { { 0.f, -100.f }, { .2f, 0.f }, { .65f, 0.f }, { 1.f, 160.f } };
		const float TypeT = ExpeditionOpeningUI::Track(CutinT, 180.f, 900.f);
		ExpeditionOpeningUI::SetMotion(CutinType, ExpeditionOpeningUI::Keys(TypeOpacity, TypeT), FVector2D(ExpeditionOpeningUI::Keys(TypeX, TypeT), 0.f), -6.f, K);
	}

	// 이후 트랙(브리핑·스탬프·도착 안내)은 시안 시각 그대로 쓰고, 정지 컷이 늘어난 만큼 늦춘다
	T -= ExpeditionOpeningUI::CutinShift;

	// 브리핑: 740ms에 표시(1ms 트랙), 4600ms 이후엔 모든 조각이 퇴장 완료
	const bool bBrief = T >= 740.f && T < 4600.f;
	ExpeditionOpeningUI::SetShown(Brief, bBrief);
	if (bBrief)
	{
		const float Slant = FMath::RadiansToDegrees(FMath::Atan(-.15f * W / View.Y));
		BlueField->SetRenderTransform(FWidgetTransform(FVector2D(-1.1f * W * ExpeditionOpeningUI::Track(T, 3920.f, 650.f), 0.f), FVector2D(1.f, 1.f), FVector2D(Slant, 0.f), 0.f));
		PaperEdge->SetRenderTransform(FWidgetTransform(FVector2D(-1.15f * W * ExpeditionOpeningUI::Track(T, 3980.f, 600.f), 0.f), FVector2D(1.f, 1.f), FVector2D(Slant, 0.f), 0.f));
		Halftone->SetRenderOpacity(1.f - ExpeditionOpeningUI::Track(T, 3860.f, 220.f));
		OutlineWord->SetRenderOpacity(1.f - ExpeditionOpeningUI::Track(T, 3860.f, 220.f));

		if (T < 3860.f)
		{
			const float E = ExpeditionOpeningUI::Track(T, 920.f, 220.f);
			ExpeditionOpeningUI::SetMotion(Masthead, E, FVector2D(0.f, -15.f * (1.f - E)));
		}
		else
		{
			ExpeditionOpeningUI::SetMotion(Masthead, 1.f - ExpeditionOpeningUI::Track(T, 3860.f, 160.f), FVector2D::ZeroVector);
		}

		if (T < 3250.f)
		{
			const float E = ExpeditionOpeningUI::Track(T, 1090.f, 280.f);
			ExpeditionOpeningUI::SetMotion(Index, E, FVector2D(0.f, -30.f * (1.f - E)), 0.f, K);
		}
		else
		{
			ExpeditionOpeningUI::SetMotion(Index, 1.f - ExpeditionOpeningUI::Track(T, 3250.f, 150.f), FVector2D::ZeroVector, 0.f, K);
		}

		// 세로 라벨·푸터: opacity 트랙이 CSS opacity(.65)를 덮어 1까지 오른다(시안 렌더 그대로)
		const float EdgeOpacity = T < 3760.f ? ExpeditionOpeningUI::Track(T, 1200.f, 200.f) : 1.f - ExpeditionOpeningUI::Track(T, 3760.f, 200.f);
		ExpeditionOpeningUI::SetMotion(VerticalLabel, EdgeOpacity, FVector2D::ZeroVector, -90.f);
		ExpeditionOpeningUI::SetMotion(Footer, EdgeOpacity, FVector2D::ZeroVector);

		if (T < 3820.f)
		{
			const float E = ExpeditionOpeningUI::Track(T, 880.f, 390.f);
			ExpeditionOpeningUI::SetMotion(Title, E, FVector2D(-90.f * (1.f - E), 0.f));
		}
		else
		{
			const float E = ExpeditionOpeningUI::Track(T, 3820.f, 350.f);
			ExpeditionOpeningUI::SetMotion(Title, 1.f - E, FVector2D(-120.f * E, 0.f));
		}

		// 지시서: 진입은 translate(120,24)·rotate +7°(화면 기준), 퇴장은 rotate(−4°) translateX(160) — 기울어진 축으로 민다
		if (T < 3910.f)
		{
			const float E = ExpeditionOpeningUI::Track(T, 1230.f, 440.f);
			ExpeditionOpeningUI::SetMotion(Mission, E, FVector2D(120.f, 24.f) * (1.f - E), -4.f + 7.f * (1.f - E));
		}
		else
		{
			const float E = ExpeditionOpeningUI::Track(T, 3910.f, 330.f);
			const float Radians = FMath::DegreesToRadians(-4.f);
			ExpeditionOpeningUI::SetMotion(Mission, 1.f - E, FVector2D(FMath::Cos(Radians), FMath::Sin(Radians)) * 160.f * E, -4.f);
		}

		// 스탬프: scale 1.65 → .94(.75) → 1, opacity 0 → 1(.75), 퇴장 translate(120,−30)
		if (T < 3970.f)
		{
			static const FSupplyKey StampOpacity[] = { { 0.f, 0.f }, { .75f, 1.f }, { 1.f, 1.f } };
			static const FSupplyKey StampScale[] = { { 0.f, 1.65f }, { .75f, .94f }, { 1.f, 1.f } };
			const float E = ExpeditionOpeningUI::Track(T, 3370.f, 260.f);
			ExpeditionOpeningUI::SetMotion(Stamp, ExpeditionOpeningUI::Keys(StampOpacity, E), FVector2D::ZeroVector, 9.f, ExpeditionOpeningUI::Keys(StampScale, E) * K);
		}
		else
		{
			const float E = ExpeditionOpeningUI::Track(T, 3970.f, 260.f);
			ExpeditionOpeningUI::SetMotion(Stamp, 1.f - E, FVector2D(120.f, -30.f) * E, 9.f, K);
		}
	}

	// 도착 안내: 4450ms 진입(시안), 6200ms 퇴장
	if (T < ExpeditionOpeningUI::ArrivalOutMs)
	{
		const float E = ExpeditionOpeningUI::Track(T, 4450.f, 300.f);
		ExpeditionOpeningUI::SetMotion(Arrival, E, FVector2D(-16.f * (1.f - E), 0.f));
	}
	else
	{
		ExpeditionOpeningUI::SetMotion(Arrival, 1.f - ExpeditionOpeningUI::Track(T, ExpeditionOpeningUI::ArrivalOutMs, ExpeditionOpeningUI::ArrivalOutLength), FVector2D::ZeroVector);
	}
}

// --- 디버그 ---

#if !UE_BUILD_SHIPPING
void UExpeditionOpeningWidget::SetDebugPreview(const FExpeditionEntryState& State, int32 Mode, float TimeMs)
{
	bDebug = true;
	DebugState = State;
	DebugMode = Mode;
	bDebugPlaying = TimeMs < 0.f;
	DebugTimeMs = FMath::Max(0.f, TimeMs);
	DataHash = 0;
	bModeApplied = false;
}

// GoHome.Opening.Preview <존 1~3|0> [ms] [mode] — 로컬 플레이어 화면에 오프닝을 띄운다(ms 생략 = 처음부터 재생, 지정 = 그 시각에 정지)
static FAutoConsoleCommand GOpeningPreviewCommand(
	TEXT("GoHome.Opening.Preview"),
	TEXT("탐사 진입 오프닝 미리보기. 인자: <존 1~3, 0 = 데이터 없음> [ms, 생략 = 재생] [mode 0 정상/1 대기/2 데이터 없음]"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const int32 ZoneNumber = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 1;
		const float TimeMs = Args.Num() > 1 ? FCString::Atof(*Args[1]) : -1.f;
		int32 Mode = Args.Num() > 2 ? FCString::Atoi(*Args[2]) : 0;
		FExpeditionEntryState State;
		State.OpeningDuration = ExpeditionOpeningUI::TimelineMs / 1000.f;
		if (ZoneNumber > 0)
		{
			const FString Path = FString::Printf(TEXT("/Game/GoHome/Data/DA_Zone_Lv%d.DA_Zone_Lv%d"), ZoneNumber, ZoneNumber);
			if (const UExpeditionZoneDataAsset* Zone = LoadObject<UExpeditionZoneDataAsset>(nullptr, *Path))
			{
				State.ZoneId = Zone->ZoneId;
				State.ZoneNumber = ZoneNumber;
				State.DisplayName = Zone->DisplayName;
				State.EnglishName = Zone->EnglishName;
				State.Caption = Zone->Caption;
				State.MapQuota = Zone->MapQuota;
				State.TimeLimitSeconds = Zone->TimeLimitSeconds;
			}
		}
		else if (Mode == 0)
		{
			Mode = 2;
		}
		for (TObjectIterator<APlayerController> It; It; ++It)
		{
			APlayerController* PC = *It;
			if (!PC->IsLocalController() || !PC->GetWorld() || !PC->GetWorld()->IsGameWorld())
			{
				continue;
			}
			for (TObjectIterator<UExpeditionOpeningWidget> Old; Old; ++Old)
			{
				if (Old->GetOwningPlayer() == PC)
				{
					Old->RemoveFromParent();
				}
			}
			if (UExpeditionOpeningWidget* Widget = CreateWidget<UExpeditionOpeningWidget>(PC, UExpeditionOpeningWidget::StaticClass()))
			{
				Widget->SetDebugPreview(State, Mode, TimeMs);
				Widget->AddToViewport(90);
			}
			return;
		}
		UE_LOG(LogTemp, Warning, TEXT("GoHome.Opening.Preview: 로컬 플레이어 없음"));
	}));
#endif

#undef LOCTEXT_NAMESPACE
