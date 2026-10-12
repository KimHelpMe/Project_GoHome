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
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::Box;
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

	UWidget* FBuilder::Ring(const FLinearColor& Color, float Width) const
	{
		UOverlay* RingOverlay = Overlay();
		// 1px 변은 Rule/VRule과 같이 헤어라인(DPI 배율 < 1에서 채움 막대 변이 사라지는 문제 — kbd 칩 윗변 실측)
		AddOverlay(RingOverlay, Rule(*this, Color, Width), HAlign_Fill, VAlign_Top);
		AddOverlay(RingOverlay, Rule(*this, Color, Width), HAlign_Fill, VAlign_Bottom);
		AddOverlay(RingOverlay, VRule(*this, Color, Width), HAlign_Left, VAlign_Fill);
		AddOverlay(RingOverlay, VRule(*this, Color, Width), HAlign_Right, VAlign_Fill);
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

	void ShowIf(UWidget* Widget, bool bShow)
	{
		if (Widget)
		{
			Widget->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		}
	}
}
