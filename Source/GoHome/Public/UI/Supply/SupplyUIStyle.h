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
	GOHOME_API void Typeset(UTextBlock* TextWidget, ERef Ref, float LineHeightPx = -1.f);

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

		// Typeset 후 그 텍스트를 돌려준다(조립 체인용)
		UTextBlock* LineBox(UTextBlock* TextWidget, float LineHeightPx = -1.f, ERef Ref = ERef::Malgun) const;
	};

	GOHOME_API UOverlaySlot* AddFill(UOverlay* Parent, UWidget* Child);
	GOHOME_API UOverlaySlot* AddOverlay(UOverlay* Parent, UWidget* Child, EHorizontalAlignment H, EVerticalAlignment V, const FMargin& Padding = FMargin(0.f));
	GOHOME_API UVerticalBoxSlot* AddV(UVerticalBox* Parent, UWidget* Child, const FMargin& Padding = FMargin(0.f), EHorizontalAlignment H = HAlign_Fill, bool bFill = false);
	GOHOME_API UHorizontalBoxSlot* AddH(UHorizontalBox* Parent, UWidget* Child, const FMargin& Padding = FMargin(0.f), EVerticalAlignment V = VAlign_Fill, float FillRatio = 0.f);
	GOHOME_API UCanvasPanelSlot* AddCanvas(UCanvasPanel* Parent, UWidget* Child, const FVector2D& Position, const FVector2D& Size);
	GOHOME_API void SetContent(UPanelWidget* Parent, UWidget* Child, EHorizontalAlignment H = HAlign_Fill, EVerticalAlignment V = VAlign_Fill);
}
