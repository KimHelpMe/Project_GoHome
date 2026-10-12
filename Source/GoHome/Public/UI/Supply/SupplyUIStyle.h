#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Layout/Margin.h"
#include "Types/SlateEnums.h"

class UBorder;
class UCanvasPanel;
class UCanvasPanelSlot;
class UHorizontalBox;
class UHorizontalBoxSlot;
class UOverlay;
class UOverlaySlot;
class UPanelWidget;
class USizeBox;
class UTextBlock;
class UVerticalBox;
class UVerticalBoxSlot;
class UWidget;
class UWidgetTree;

/**
 * 보급 UI(분기·상점·보관함) 공통 토큰과 위젯 조립 도우미.
 * 출처: Docs/Dev/UI/shop/candidate-persona-locker/prototype.html :root 토큰. 레이아웃 단위는 CSS px = Slate 단위(1080p DPI 1.0),
 * 폰트 크기만 72DPI 포인트라 CSS px × 0.75로 넣는다(ENGINE_BINDING 시각 검증 원칙).
 */
namespace SupplyUI
{
	GOHOME_API FLinearColor Hex(uint32 RGB, float Alpha = 1.f);

	inline FLinearColor Ink() { return Hex(0x080f20); }
	inline FLinearColor Deep() { return Hex(0x101e36); }
	inline FLinearColor Navy() { return Hex(0x112547); }      // 사선 배경 면
	inline FLinearColor Blue() { return Hex(0x2747d8); }
	inline FLinearColor Paper() { return Hex(0xf0f1de); }
	inline FLinearColor Ivory() { return Hex(0xf3f6eb); }     // Sophia 카드·추종 판
	inline FLinearColor Lime() { return Hex(0xdafa65); }
	inline FLinearColor Muted() { return Hex(0xaebbd0); }
	inline FLinearColor Line() { return Hex(0x3b4c65); }
	inline FLinearColor Alert() { return Hex(0xffb89c); }

	// 시안 폰트(맑은 고딕/Segoe UI/Arial Black)의 배포 가능한 대체. 굵기는 CSS 의도 기준.
	enum class EFace : uint8
	{
		Regular,
		Medium,
		SemiBold,
		Bold,
		ExtraBold,
		Black,
		Display	// Arial Black 대체(Archivo Black)
	};

	// 브라우저 합성 이탤릭(oblique) 기울기. FreeType 변환 행렬 xy 값.
	constexpr float ItalicSkew = 0.2f;

	// 시안에서 그 텍스트를 그린 원래 글꼴. 줄 상자 안 기준선 위치(ascent/descent)를 시안과 같게 맞추는 데 쓴다.
	enum class ERef : uint8
	{
		Malgun,		// 맑은 고딕(한글 본문 기본)
		Segoe,		// Segoe UI(영문 kicker·숫자)
		ArialBlack	// Arial Black(워드마크·대제목)
	};

	// CSS line-height:normal = 시안 글꼴 ascent+descent, 브라우저가 정수 px로 반올림
	GOHOME_API float NormalLineHeight(float CssPx, ERef Ref = ERef::Malgun);

	/**
	 * CSS 줄 상자 재현: 줄 높이(LineHeightPx, 음수면 normal)마다 줄 간격을 맞추고, 첫 기준선이 시안 글꼴의 기준선과 같은 높이에
	 * 오도록 텍스트를 옮긴다. 대체 글꼴의 실제 Slate 지표(FontMeasure)를 읽어 계산한다 — 폰트를 바꾼 뒤 호출할 것.
	 */
	// BaselinePx >= 0 이면 첫 기준선 위치(줄 상자 위에서부터)를 직접 지정 — 크기가 다른 글자가 한 줄을 공유할 때(CSS 공통 기준선).
	GOHOME_API void Typeset(UTextBlock* TextWidget, ERef Ref, float LineHeightPx = -1.f, float BaselinePx = -1.f);

	// CSS 기준선 위치(줄 상자 위에서부터): (LH - (A+D)px)/2 + A·px
	GOHOME_API float CssBaseline(float CssPx, ERef Ref, float LineHeightPx = -1.f);

	// 시안 #rrggbb + alpha 위에 배경을 얹은 결과색(sRGB 합성) — 브라우저 반투명 결과를 불투명 색으로 고정할 때.
	GOHOME_API FLinearColor MixSRGB(uint32 Over, float Alpha, uint32 Under);

	/** CssPx: CSS font-size. LetterSpacingEm: CSS letter-spacing(em). bItalic: 합성 이탤릭. */
	GOHOME_API FSlateFontInfo Font(EFace Face, float CssPx, float LetterSpacingEm = 0.f, bool bItalic = false);

	/** 위젯 트리 조립 축약. 모든 위젯은 Tree가 소유한다. */
	struct GOHOME_API FBuilder
	{
		explicit FBuilder(UWidgetTree* InTree) : Tree(InTree) {}

		UWidgetTree* Tree;

		UOverlay* Overlay() const;
		UCanvasPanel* Canvas() const;
		UVerticalBox* VBox() const;
		UHorizontalBox* HBox() const;
		USizeBox* Size(float Width = -1.f, float Height = -1.f) const;
		UBorder* Fill(const FLinearColor& Color, const FMargin& Padding = FMargin(0.f)) const;
		UBorder* Box(const FMargin& Padding = FMargin(0.f)) const;	// 투명 컨테이너(Border = 패딩+정렬)
		UTextBlock* Text(const FText& Value, const FSlateFontInfo& InFont, const FLinearColor& Color) const;
		UWidget* Spacer(float Width, float Height) const;

		// 테두리만(안쪽 채움 없음). CSS outline/inset box-shadow 재현용 — 슬롯 패딩으로 offset을 준다.
		// 네 변을 단색 막대로 그린다: RoundedBox 브러시의 외곽선(OutlineSettings)은 부모 RenderOpacity를 따르지 않아
		// 페이드 아웃(스윕 잔상 등) 중에도 그대로 남는다(2026-10-12 확인).
		UWidget* Ring(const FLinearColor& Color, float Width) const;

		// Typeset 후 그 텍스트를 돌려준다(조립 체인용)
		UTextBlock* LineBox(UTextBlock* TextWidget, float LineHeightPx = -1.f, ERef Ref = ERef::Malgun) const;
	};

	GOHOME_API UOverlaySlot* AddFill(UOverlay* Parent, UWidget* Child);
	GOHOME_API UOverlaySlot* AddOverlay(UOverlay* Parent, UWidget* Child, EHorizontalAlignment H, EVerticalAlignment V, const FMargin& Padding = FMargin(0.f));
	GOHOME_API UVerticalBoxSlot* AddV(UVerticalBox* Parent, UWidget* Child, const FMargin& Padding = FMargin(0.f), EHorizontalAlignment H = HAlign_Fill, bool bFill = false);
	GOHOME_API UHorizontalBoxSlot* AddH(UHorizontalBox* Parent, UWidget* Child, const FMargin& Padding = FMargin(0.f), EVerticalAlignment V = VAlign_Fill, float FillRatio = 0.f);
	GOHOME_API UCanvasPanelSlot* AddCanvas(UCanvasPanel* Parent, UWidget* Child, const FVector2D& Position, const FVector2D& Size);
	GOHOME_API void SetContent(UPanelWidget* Parent, UWidget* Child, EHorizontalAlignment H = HAlign_Fill, EVerticalAlignment V = VAlign_Fill);

	// 페이지 공용 소품(상점·보관함)
	GOHOME_API FText Dash();	// 값 없음 "—"
	// 가로/세로 줄(CSS border). 1px은 UHairlineWidget — DPI 배율 < 1에서 1px 채움 막대는 픽셀 중심 사이에 끼면 통째로 사라진다
	GOHOME_API UWidget* Rule(const FBuilder& B, const FLinearColor& Color, float Thickness = 1.f);
	GOHOME_API UWidget* VRule(const FBuilder& B, const FLinearColor& Color, float Thickness = 1.f);
	GOHOME_API void ShowIf(UWidget* Widget, bool bShow);
	GOHOME_API void SetRingColor(UWidget* Ring, const FLinearColor& Color);	// FBuilder::Ring 네 변 색 바꾸기(채움 막대·헤어라인 둘 다)	// 보이면 HitTestInvisible, 아니면 Hidden(자리 유지)
}
