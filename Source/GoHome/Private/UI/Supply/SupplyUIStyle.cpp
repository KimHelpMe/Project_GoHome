#include "UI/Supply/SupplyUIStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/FontFace.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "UI/Common/HairlineWidget.h"

namespace SupplyUI
{
	FLinearColor Hex(uint32 RGB, float Alpha)
	{
		FLinearColor Color = FLinearColor::FromSRGBColor(FColor((RGB >> 16) & 0xff, (RGB >> 8) & 0xff, RGB & 0xff));
		Color.A = Alpha;
		return Color;
	}

	namespace
	{
		const TCHAR* FacePath(EFace Face)
		{
			switch (Face)
			{
			case EFace::Regular: return TEXT("/Game/GoHome/UI/Style/Fonts/FF_NotoSansKR_Regular.FF_NotoSansKR_Regular");
			case EFace::Medium: return TEXT("/Game/GoHome/UI/Style/Fonts/FF_NotoSansKR_Medium.FF_NotoSansKR_Medium");
			case EFace::SemiBold: return TEXT("/Game/GoHome/UI/Style/Fonts/FF_NotoSansKR_SemiBold.FF_NotoSansKR_SemiBold");
			case EFace::Bold: return TEXT("/Game/GoHome/UI/Style/Fonts/FF_NotoSansKR_Bold.FF_NotoSansKR_Bold");
			case EFace::ExtraBold: return TEXT("/Game/GoHome/UI/Style/Fonts/FF_NotoSansKR_ExtraBold.FF_NotoSansKR_ExtraBold");
			case EFace::Black: return TEXT("/Game/GoHome/UI/Style/Fonts/FF_NotoSansKR_Black.FF_NotoSansKR_Black");
			case EFace::Display: return TEXT("/Game/GoHome/UI/Style/Fonts/FF_ArchivoBlack.FF_ArchivoBlack");
			case EFace::Mono: return TEXT("/Game/GoHome/UI/Style/Fonts/FF_Inconsolata_SemiExpanded.FF_Inconsolata_SemiExpanded");
			}
			return nullptr;
		}

		// 컴포지트 폰트 에셋 없이 FontFace 하나로 런타임 컴포지트를 만든다. Display는 한글이 없으므로 Black을 한글 서브폰트로 붙인다.
		TSharedPtr<const FCompositeFont> GetComposite(EFace Face)
		{
			static TMap<EFace, TSharedPtr<const FCompositeFont>> Cache;
			if (const TSharedPtr<const FCompositeFont>* Found = Cache.Find(Face))
			{
				return *Found;
			}

			auto LoadFace = [](EFace InFace) -> UFontFace*
			{
				UFontFace* FontFace = LoadObject<UFontFace>(nullptr, FacePath(InFace));
				if (FontFace)
				{
					FontFace->AddToRoot();
				}
				return FontFace;
			};

			TSharedRef<FCompositeFont> Composite = MakeShared<FCompositeFont>();
			if (UFontFace* Primary = LoadFace(Face))
			{
				FTypefaceEntry& Entry = Composite->DefaultTypeface.Fonts.Emplace_GetRef(TEXT("Default"));
				Entry.Font = FFontData(Primary);
			}

			if (Face == EFace::Display)
			{
				if (UFontFace* Korean = LoadFace(EFace::Black))
				{
					FCompositeSubFont& SubFont = Composite->SubTypefaces.AddDefaulted_GetRef();
					SubFont.CharacterRanges.Add(FInt32Range(FInt32Range::BoundsType::Inclusive(0x1100), FInt32Range::BoundsType::Inclusive(0x11FF)));
					SubFont.CharacterRanges.Add(FInt32Range(FInt32Range::BoundsType::Inclusive(0x3130), FInt32Range::BoundsType::Inclusive(0x318F)));
					SubFont.CharacterRanges.Add(FInt32Range(FInt32Range::BoundsType::Inclusive(0xAC00), FInt32Range::BoundsType::Inclusive(0xD7AF)));
					SubFont.CharacterRanges.Add(FInt32Range(FInt32Range::BoundsType::Inclusive(0x2190), FInt32Range::BoundsType::Inclusive(0x21FF)));
					FTypefaceEntry& Entry = SubFont.Typeface.Fonts.Emplace_GetRef(TEXT("Default"));
					Entry.Font = FFontData(Korean);
				}
			}

			Cache.Add(Face, Composite);
			return Composite;
		}
	}

	namespace
	{
		// 시안 글꼴 hhea/win 지표(em). fontTools로 Windows 글꼴에서 읽은 값.
		void RefMetrics(ERef Ref, float& OutAscent, float& OutDescent)
		{
			switch (Ref)
			{
			case ERef::Segoe: OutAscent = 1.079f; OutDescent = .251f; break;
			case ERef::ArialBlack: OutAscent = 1.101f; OutDescent = .310f; break;
			case ERef::Consolas: OutAscent = .920f; OutDescent = .251f; break;
			default: OutAscent = 1.088f; OutDescent = .242f; break;
			}
		}
	}

	float NormalLineHeight(float CssPx, ERef Ref)
	{
		float Ascent = 0.f;
		float Descent = 0.f;
		RefMetrics(Ref, Ascent, Descent);
		return FMath::RoundToFloat((Ascent + Descent) * CssPx);
	}

	float CssBaseline(float CssPx, ERef Ref, float LineHeightPx)
	{
		float Ascent = 0.f;
		float Descent = 0.f;
		RefMetrics(Ref, Ascent, Descent);
		const float LineHeight = LineHeightPx > 0.f ? LineHeightPx : NormalLineHeight(CssPx, Ref);
		return (LineHeight - (Ascent + Descent) * CssPx) * .5f + Ascent * CssPx;
	}

	FLinearColor MixSRGB(uint32 Over, float Alpha, uint32 Under)
	{
		auto Channel = [Alpha](uint32 A, uint32 B, int32 Shift)
		{
			const float Top = (A >> Shift) & 0xff;
			const float Bottom = (B >> Shift) & 0xff;
			return static_cast<uint8>(FMath::RoundToInt(Top * Alpha + Bottom * (1.f - Alpha)));
		};
		return FLinearColor::FromSRGBColor(FColor(Channel(Over, Under, 16), Channel(Over, Under, 8), Channel(Over, Under, 0)));
	}

	void Typeset(UTextBlock* TextWidget, ERef Ref, float LineHeightPx, float BaselinePx)
	{
		if (!TextWidget)
		{
			return;
		}
		const FSlateFontInfo FontInfo = TextWidget->GetFont();
		const float CssPx = FontInfo.Size / 0.75f;
		const float LineHeight = LineHeightPx > 0.f ? LineHeightPx : NormalLineHeight(CssPx, Ref);

		// Slate 줄: 글리프는 줄 맨 위에서 시작하고 높이×LineHeightPercentage 만큼 내려간다(TextLayout.cpp).
		float SlateHeight = CssPx * 1.2f;
		float SlateDescent = CssPx * .2f;
		if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer())
		{
			const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			SlateHeight = Measure->GetMaxCharacterHeight(FontInfo, 1.f);
			SlateDescent = -Measure->GetBaseline(FontInfo, 1.f);
		}
		if (SlateHeight <= 0.f)
		{
			return;
		}

		TextWidget->SetLineHeightPercentage(LineHeight / SlateHeight);
		const float Baseline = BaselinePx >= 0.f ? BaselinePx : CssBaseline(CssPx, Ref, LineHeight);
		TextWidget->SetRenderTranslation(FVector2D(0.f, Baseline - (SlateHeight - SlateDescent)));
	}

	FSlateFontInfo Font(EFace Face, float CssPx, float LetterSpacingEm, bool bItalic)
	{
		const float SizePt = CssPx * 0.75f;
		FSlateFontInfo Info(GetComposite(Face), SizePt);

		// Slate 자간 = LetterSpacing × Size(pt) / 1000 을 정수 px로 자른다 → 목표 px(N)에서 역산(ENGINE_BINDING (4)).
		const int32 TargetPx = FMath::RoundToInt(LetterSpacingEm * CssPx);
		if (TargetPx != 0 && SizePt > 0.f)
		{
			const float Signed = TargetPx + (TargetPx > 0 ? 0.5f : -0.5f);
			Info.LetterSpacing = FMath::RoundToInt(Signed * 1000.f / SizePt);
		}

		if (bItalic)
		{
			Info.SkewAmount = ItalicSkew;
		}
		return Info;
	}

	UOverlay* FBuilder::Overlay() const { return Tree->ConstructWidget<UOverlay>(); }
	UCanvasPanel* FBuilder::Canvas() const { return Tree->ConstructWidget<UCanvasPanel>(); }
	UVerticalBox* FBuilder::VBox() const { return Tree->ConstructWidget<UVerticalBox>(); }
	UHorizontalBox* FBuilder::HBox() const { return Tree->ConstructWidget<UHorizontalBox>(); }

	USizeBox* FBuilder::Size(float Width, float Height) const
	{
		USizeBox* SizeBox = Tree->ConstructWidget<USizeBox>();
		if (Width >= 0.f)
		{
			SizeBox->SetWidthOverride(Width);
		}
		if (Height >= 0.f)
		{
			SizeBox->SetHeightOverride(Height);
		}
		return SizeBox;
	}

	UBorder* FBuilder::Fill(const FLinearColor& Color, const FMargin& Padding) const
	{
		UBorder* Border = Tree->ConstructWidget<UBorder>();
		// Box(평면 사각형)는 렌더 변환으로 회전·기울이면 가장자리가 계단으로 깨진다. RoundedBox는 SDF 셰이더가 자기 공간에서
		// 가장자리 커버리지를 계산해 회전해도 매끈하다 — 반지름 0·외곽선 없음으로 모서리는 그대로(외곽선은 부모 투명도를 안 따르므로 안 씀).
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.OutlineSettings = FSlateBrushOutlineSettings(FVector4(0.f, 0.f, 0.f, 0.f), FSlateColor(FLinearColor::Transparent), 0.f);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		Brush.TintColor = FSlateColor(FLinearColor::White);
		Border->SetBrush(Brush);
		Border->SetBrushColor(Color);
		Border->SetPadding(Padding);
		return Border;
	}

	UBorder* FBuilder::Box(const FMargin& Padding) const
	{
		UBorder* Border = Tree->ConstructWidget<UBorder>();
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::NoDrawType;
		Border->SetBrush(Brush);
		Border->SetPadding(Padding);
		return Border;
	}

	UTextBlock* FBuilder::Text(const FText& Value, const FSlateFontInfo& InFont, const FLinearColor& Color) const
	{
		UTextBlock* TextBlock = Tree->ConstructWidget<UTextBlock>();
		TextBlock->SetText(Value);
		TextBlock->SetFont(InFont);
		TextBlock->SetColorAndOpacity(FSlateColor(Color));

		// 기본 KerningOnly 경로는 레거시 kern 테이블만 읽는다 — 대체 글꼴(Archivo Black·Noto)의 GPOS 커닝까지 쓰도록 HarfBuzz 전체 셰이핑.
		FShapedTextOptions Shaping;
		Shaping.bOverride_TextShapingMethod = true;
		Shaping.TextShapingMethod = ETextShapingMethod::FullShaping;
		TextBlock->SetShapedTextOptions(Shaping);
		return TextBlock;
	}

	UWidget* FBuilder::Spacer(float Width, float Height) const
	{
		USpacer* SpacerWidget = Tree->ConstructWidget<USpacer>();
		SpacerWidget->SetSize(FVector2D(Width, Height));
		return SpacerWidget;
	}

	UTextBlock* FBuilder::LineBox(UTextBlock* TextWidget, float LineHeightPx, ERef Ref) const
	{
		Typeset(TextWidget, Ref, LineHeightPx);
		return TextWidget;
	}

	UWidget* FBuilder::Ring(const FLinearColor& Color, float Width, bool bRotated) const
	{
		UOverlay* RingOverlay = Overlay();
		// 1px 변은 Rule/VRule과 같이 헤어라인(DPI 배율 < 1에서 채움 막대 변이 사라지는 문제 — kbd 칩 윗변 실측)
		if (bRotated)
		{
			const float Half = Width * .5f;
			AddOverlay(RingOverlay, Fill(Color, FMargin(0.f, Half)), HAlign_Fill, VAlign_Top);
			AddOverlay(RingOverlay, Fill(Color, FMargin(0.f, Half)), HAlign_Fill, VAlign_Bottom);
			AddOverlay(RingOverlay, Fill(Color, FMargin(Half, 0.f)), HAlign_Left, VAlign_Fill);
			AddOverlay(RingOverlay, Fill(Color, FMargin(Half, 0.f)), HAlign_Right, VAlign_Fill);
		}
		else
		{
			AddOverlay(RingOverlay, Rule(*this, Color, Width), HAlign_Fill, VAlign_Top);
			AddOverlay(RingOverlay, Rule(*this, Color, Width), HAlign_Fill, VAlign_Bottom);
			AddOverlay(RingOverlay, VRule(*this, Color, Width), HAlign_Left, VAlign_Fill);
			AddOverlay(RingOverlay, VRule(*this, Color, Width), HAlign_Right, VAlign_Fill);
		}
		RingOverlay->SetVisibility(ESlateVisibility::HitTestInvisible);
		return RingOverlay;
	}

	UOverlaySlot* AddFill(UOverlay* Parent, UWidget* Child)
	{
		return AddOverlay(Parent, Child, HAlign_Fill, VAlign_Fill);
	}

	UOverlaySlot* AddOverlay(UOverlay* Parent, UWidget* Child, EHorizontalAlignment H, EVerticalAlignment V, const FMargin& Padding)
	{
		UOverlaySlot* Slot = Parent->AddChildToOverlay(Child);
		Slot->SetHorizontalAlignment(H);
		Slot->SetVerticalAlignment(V);
		Slot->SetPadding(Padding);
		return Slot;
	}

	UVerticalBoxSlot* AddV(UVerticalBox* Parent, UWidget* Child, const FMargin& Padding, EHorizontalAlignment H, bool bFill)
	{
		UVerticalBoxSlot* Slot = Parent->AddChildToVerticalBox(Child);
		Slot->SetPadding(Padding);
		Slot->SetHorizontalAlignment(H);
		Slot->SetSize(FSlateChildSize(bFill ? ESlateSizeRule::Fill : ESlateSizeRule::Automatic));
		return Slot;
	}

	UHorizontalBoxSlot* AddH(UHorizontalBox* Parent, UWidget* Child, const FMargin& Padding, EVerticalAlignment V, float FillRatio)
	{
		UHorizontalBoxSlot* Slot = Parent->AddChildToHorizontalBox(Child);
		Slot->SetPadding(Padding);
		Slot->SetVerticalAlignment(V);
		if (FillRatio > 0.f)
		{
			FSlateChildSize ChildSize(ESlateSizeRule::Fill);
			ChildSize.Value = FillRatio;
			Slot->SetSize(ChildSize);
		}
		else
		{
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		}
		return Slot;
	}

	UCanvasPanelSlot* AddCanvas(UCanvasPanel* Parent, UWidget* Child, const FVector2D& Position, const FVector2D& Size)
	{
		UCanvasPanelSlot* Slot = Parent->AddChildToCanvas(Child);
		Slot->SetAnchors(FAnchors(0.f, 0.f));
		Slot->SetAlignment(FVector2D::ZeroVector);
		Slot->SetPosition(Position);
		Slot->SetSize(Size);
		return Slot;
	}

	void SetContent(UPanelWidget* Parent, UWidget* Child, EHorizontalAlignment H, EVerticalAlignment V)
	{
		UPanelSlot* Slot = Parent->AddChild(Child);
		if (UBorderSlot* BorderSlot = Cast<UBorderSlot>(Slot))
		{
			BorderSlot->SetHorizontalAlignment(H);
			BorderSlot->SetVerticalAlignment(V);
		}
		else if (USizeBoxSlot* SizeSlot = Cast<USizeBoxSlot>(Slot))
		{
			SizeSlot->SetHorizontalAlignment(H);
			SizeSlot->SetVerticalAlignment(V);
		}
	}

	FText Dash()
	{
		return FText::FromString(TEXT("—"));
	}

	namespace
	{
		UWidget* Hairline(const FBuilder& B, const FLinearColor& Color, EOrientation Orientation)
		{
			UHairlineWidget* Line = B.Tree->ConstructWidget<UHairlineWidget>();
			Line->SetLineColor(Color);
			Line->SetOrientation(Orientation);
			return Line;
		}
	}

	UWidget* Rule(const FBuilder& B, const FLinearColor& Color, float Thickness)
	{
		return Thickness <= 1.f ? Hairline(B, Color, Orient_Horizontal) : B.Fill(Color, FMargin(0.f, Thickness * .5f));
	}

	UWidget* VRule(const FBuilder& B, const FLinearColor& Color, float Thickness)
	{
		return Thickness <= 1.f ? Hairline(B, Color, Orient_Vertical) : B.Fill(Color, FMargin(Thickness * .5f, 0.f));
	}

	void SetRingColor(UWidget* Ring, const FLinearColor& Color)
	{
		UPanelWidget* Edges = Cast<UPanelWidget>(Ring);
		for (int32 Index = 0; Edges && Index < Edges->GetChildrenCount(); ++Index)
		{
			UWidget* Edge = Edges->GetChildAt(Index);
			if (UBorder* Bar = Cast<UBorder>(Edge))
			{
				Bar->SetBrushColor(Color);
			}
			else if (UHairlineWidget* Line = Cast<UHairlineWidget>(Edge))
			{
				Line->SetLineColor(Color);
			}
		}
	}

	void ShowIf(UWidget* Widget, bool bShow)
	{
		if (Widget)
		{
			Widget->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		}
	}

	namespace
	{
		// kbd: 800 11px, border 1px #7b8aa0, box-shadow 0 2px 0 #48556a, padding 2 5
		UWidget* KeyChip(const FBuilder& B, const FString& Key)
		{
			UOverlay* Chip = B.Overlay();
			UBorder* ChipShadow = B.Fill(Hex(0x48556a));
			ChipShadow->SetRenderTranslation(FVector2D(0.f, 2.f));
			AddFill(Chip, ChipShadow);
			AddFill(Chip, B.Fill(Ink()));
			AddFill(Chip, B.Ring(Hex(0x7b8aa0), 1.f));
			if (Key == TEXT("Space"))
			{
				// ␣: 밑변 + 양끝 짧은 세로 — 글리프 대신 그린다(대체 글꼴에 없을 수 있음)
				UOverlay* Bracket = B.Overlay();
				AddOverlay(Bracket, Rule(B, Paper()), HAlign_Fill, VAlign_Bottom);
				for (EHorizontalAlignment Side : { HAlign_Left, HAlign_Right })
				{
					USizeBox* Tick = B.Size(-1.f, 4.f);
					Tick->AddChild(VRule(B, Paper()));
					AddOverlay(Bracket, Tick, Side, VAlign_Bottom);
				}
				USizeBox* Shape = B.Size(22.f, 9.f);
				Shape->AddChild(Bracket);
				AddOverlay(Chip, Shape, HAlign_Center, VAlign_Center, FMargin(6.f, 4.f));
			}
			else if (Key == TEXT("Enter"))
			{
				// ↵: 대체 글꼴에 글리프가 없어 ← 글자 오른끝에 위로 올라가는 세로 획을 얹는다
				UHorizontalBox* Glyph = B.HBox();
				AddH(Glyph, B.LineBox(B.Text(FText::FromString(TEXT("←")), Font(EFace::ExtraBold, 11.f), Paper()), -1.f, ERef::Segoe), FMargin(0.f), VAlign_Center);
				USizeBox* Stem = B.Size(-1.f, 4.5f);
				Stem->AddChild(VRule(B, Paper()));
				AddH(Glyph, Stem, FMargin(-3.f, 2.f, 1.f, 0.f), VAlign_Top);	// 글리프 오른쪽 여백만큼 당겨 화살대 끝에 붙인다
				AddOverlay(Chip, Glyph, HAlign_Fill, VAlign_Fill, FMargin(6.f, 3.f));
			}
			else
			{
				AddOverlay(Chip, B.LineBox(B.Text(FText::FromString(Key), Font(EFace::ExtraBold, 11.f), Paper()), -1.f, ERef::Segoe), HAlign_Fill, VAlign_Fill, FMargin(6.f, 3.f));
			}
			return Chip;
		}
	}

	void FillKeyHints(const FBuilder& B, UHorizontalBox* Box, const TArray<FKeyHint>& Hints, float LabelPx)
	{
		if (!Box)
		{
			return;
		}
		Box->ClearChildren();
		for (int32 Index = 0; Index < Hints.Num(); ++Index)
		{
			const FKeyHint& Hint = Hints[Index];
			for (int32 KeyIndex = 0; KeyIndex < Hint.Keys.Num(); ++KeyIndex)
			{
				const FString& Key = Hint.Keys[KeyIndex];
				const float Left = KeyIndex > 0 ? 0.f : (Index == 0 ? 0.f : 20.f);
				if (KeyIndex > 0)
				{
					AddH(Box, B.LineBox(B.Text(FText::FromString(TEXT("/")), Font(EFace::Regular, LabelPx), Muted())), FMargin(6.f, 0.f), VAlign_Center);
				}
				UWidget* KeyWidget = Key == TEXT("Esc")
					? static_cast<UWidget*>(B.LineBox(B.Text(FText::FromString(Key), Font(EFace::ExtraBold, LabelPx), Paper()), -1.f, ERef::Segoe))
					: KeyChip(B, Key);
				AddH(Box, KeyWidget, FMargin(Left, 0.f, 0.f, 0.f), VAlign_Center);
			}
			AddH(Box, B.LineBox(B.Text(Hint.Label, Font(EFace::Regular, LabelPx), Muted())), FMargin(6.f, 0.f, 0.f, 0.f), VAlign_Center);
		}
	}
}
