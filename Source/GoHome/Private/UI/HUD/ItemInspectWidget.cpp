#include "UI/HUD/ItemInspectWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/SizeBox.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Core/GoHomeGameState.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Interaction/InteractionComponent.h"
#include "Item/ItemActorBase.h"
#include "Item/ItemDataAsset.h"
#include "Misc/PackageName.h"
#include "Player/HealthComponent.h"
#include "Rendering/DrawElements.h"
#include "UI/Supply/SupplyUIStyle.h"

#define LOCTEXT_NAMESPACE "ItemInspect"

using namespace SupplyUI;

namespace
{
#if !UE_BUILD_SHIPPING
	TAutoConsoleVariable<float> CVarItemInspectSlow(
		TEXT("GoHome.ItemInspect.Slow"), 1.f,
		TEXT("조준 정보 UI 모션 시간 배율(4 = 시안 '느리게 ×4')."));
#endif

	// Pack README :root 토큰
	FLinearColor PanelFace() { return Hex(0x03111b, .74f); }
	FLinearColor Edge() { return Hex(0x6ecde8, .45f); }
	FLinearColor Bracket() { return Hex(0xc9f5ff); }
	FLinearColor Glow() { return Hex(0x5ad7ff, .55f); }
	FLinearColor PanelGlow() { return Hex(0x3cbee6, .12f); }
	FLinearColor IconInk() { return Hex(0xbff1ff); }
	FLinearColor LabelInk() { return Hex(0xa8d4e2); }
	FLinearColor ValueInk() { return Hex(0xf1fbff); }
	FLinearColor WasInk() { return Hex(0x7fa3b2); }
	FLinearColor ReticleInk() { return Hex(0xe6fbff); }
	FLinearColor Warn() { return Hex(0xffb15c); }
	FLinearColor Crit() { return Hex(0xff6b6b); }

	FLinearColor GradeColor(EItemGrade Grade)
	{
		switch (Grade)
		{
		case EItemGrade::Rare: return Hex(0x6cc6ff);
		case EItemGrade::Epic: return Hex(0xc493ff);
		case EItemGrade::Legendary: return Hex(0xf2c94c);
		default: return Hex(0xd7e6ec);
		}
	}

	// 배치 규칙 표(README) — 레퍼런스 구 중심 기준 1920 px, k=1
	struct FSlotDef
	{
		bool bLeft;
		float In;		// 패널 안쪽 변 x
		float Cy;		// 패널 중앙 y
		float Ex;		// 선 출발 높이(패널 위에서 비율)
		float Run;		// 수평 run
		FVector2D A;	// 선 끝(대상 위)
		float MinW;
		float Ic;		// 아이콘 박스
	};

	const FSlotDef Slots[] = {
		{ true,  -235.f, -212.f, .5f,  92.f, FVector2D(-63.f, -132.f), 352.f, 82.f },	// 이름
		{ true,  -264.f,    9.f, .5f,  81.f, FVector2D(-132.f, -63.f), 341.f, 72.f },	// 가치
		{ false,  286.f, -167.f, .5f,  45.f, FVector2D(190.f, -103.f), 278.f, 72.f },	// 무게
		{ false,  331.f,    2.f, .5f,  61.f, FVector2D(218.f, 46.f),   256.f, 72.f },	// 등급
		{ false,  300.f,  195.f, .25f, 99.f, FVector2D(138.f, 98.f),   450.f, 72.f },	// 정보
	};

	constexpr float PanelGap = 32.f;	// 외곽선 기준 최소 간격
	constexpr float SafeL = 40.f, SafeR = 40.f, SafeT = 64.f, SafeB = 210.f;	// 1080 기준 아래 870 — 나침반·하단 콘솔 회피
	constexpr float EnterShift = 8.f;
	constexpr float PadL = 16.f;		// 패널 padding 16 34 16 16

	// CSS 블러 그림자 세기 근사(erfc, Abramowitz–Stegun 7.1.26)
	float Erfc(float X)
	{
		const float T = 1.f / (1.f + .3275911f * X);
		const float Poly = T * (.254829592f + T * (-.284496736f + T * (1.421413741f + T * (-1.453152027f + T * 1.061405429f))));
		return Poly * FMath::Exp(-X * X);
	}

	void Polyline(FSlateWindowElementList& Out, int32 Layer, const FPaintGeometry& Geo, const TArray<FVector2f>& Points,
		const FLinearColor& Color, float Thickness)
	{
		if (Points.Num() >= 2 && Color.A > 0.f)
		{
			FSlateDrawElement::MakeLines(Out, Layer, Geo, Points, ESlateDrawEffect::None, Color, true, Thickness);
		}
	}

	TArray<FVector2f> RectLoop(const FVector2D& Min, const FVector2D& Max)
	{
		return { FVector2f(Min.X, Min.Y), FVector2f(Max.X, Min.Y), FVector2f(Max.X, Max.Y), FVector2f(Min.X, Max.Y), FVector2f(Min.X, Min.Y) };
	}

	TArray<FVector2f> RoundedLoop(const FVector2D& Min, const FVector2D& Max, float Radius)
	{
		TArray<FVector2f> Points;
		const FVector2D Centers[] = {
			FVector2D(Max.X - Radius, Min.Y + Radius), FVector2D(Max.X - Radius, Max.Y - Radius),
			FVector2D(Min.X + Radius, Max.Y - Radius), FVector2D(Min.X + Radius, Min.Y + Radius) };
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			for (int32 Step = 0; Step <= 4; ++Step)
			{
				const float Angle = FMath::DegreesToRadians(-90.f + 90.f * Corner + 22.5f * Step);
				Points.Add(FVector2f(Centers[Corner] + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius));
			}
		}
		const FVector2f First = Points[0];	// Add(Points[0])는 재할당 중 자기 원소 참조(Array.h 어서트)
		Points.Add(First);
		return Points;
	}

	TArray<FVector2f> Circle(const FVector2D& Center, float Radius, int32 Segments = 48)
	{
		TArray<FVector2f> Points;
		for (int32 Index = 0; Index <= Segments; ++Index)
		{
			const float Angle = 2.f * PI * Index / Segments;
			Points.Add(FVector2f(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius));
		}
		return Points;
	}

	// 꺾은선 전체 길이의 T 만큼(패널 쪽 → 대상 쪽)
	TArray<FVector2f> ClipPolyline(const TArray<FVector2D>& Points, float T)
	{
		TArray<FVector2f> Out;
		if (Points.Num() < 2 || T <= 0.f)
		{
			return Out;
		}
		float Total = 0.f;
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			Total += FVector2D::Distance(Points[Index - 1], Points[Index]);
		}
		float Left = Total * FMath::Min(T, 1.f);
		Out.Add(FVector2f(Points[0]));
		for (int32 Index = 1; Index < Points.Num() && Left > 0.f; ++Index)
		{
			const float Segment = FVector2D::Distance(Points[Index - 1], Points[Index]);
			if (Segment <= Left)
			{
				Out.Add(FVector2f(Points[Index]));
				Left -= Segment;
			}
			else
			{
				Out.Add(FVector2f(FMath::Lerp(Points[Index - 1], Points[Index], Left / Segment)));
				Left = 0.f;
			}
		}
		return Out;
	}

	const FSupplyEase& EasePanel()
	{
		static const FSupplyEase Ease = FSupplyEase::Bezier(.2f, .8f, .2f, 1.f);
		return Ease;
	}

	const FSupplyEase& EaseLine()
	{
		static const FSupplyEase Ease = FSupplyEase::Bezier(.4f, 0.f, .2f, 1.f);
		return Ease;
	}
}

void UItemInspectWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetVisibility(ESlateVisibility::HitTestInvisible);
	BuildTree();
}

void UItemInspectWidget::NativeDestruct()
{
	BindPawn(nullptr);
	Animator.CancelAll();
	Super::NativeDestruct();
}

UTexture2D* UItemInspectWidget::LoadIcon(const TSoftObjectPtr<UTexture2D>& Icon) const
{
	// 아이콘은 에디터에서 만드는 에셋 — 아직 없으면 경고 없이 빈 박스
	const FString Package = Icon.ToSoftObjectPath().GetLongPackageName();
	if (Package.IsEmpty() || !FPackageName::DoesPackageExist(Package))
	{
		return nullptr;
	}
	return Icon.LoadSynchronous();
}

UTextBlock* UItemInspectWidget::MakeLabel(const FText& Text) const
{
	const FBuilder B(WidgetTree);
	return B.LineBox(B.Text(Text, Font(EFace::Regular, 19.f), LabelInk()), 26.f);
}

UTextBlock* UItemInspectWidget::MakeValue(const FText& Text) const
{
	const FBuilder B(WidgetTree);
	return B.LineBox(B.Text(Text, Font(EFace::Bold, 33.f), ValueInk()), 42.f);
}

UWidget* UItemInspectWidget::MakeChip(const FText& Text, const FLinearColor& Color) const
{
	const FBuilder B(WidgetTree);
	UOverlay* Chip = B.Overlay();
	AddFill(Chip, B.Ring(Color, 1.f));
	AddOverlay(Chip, B.LineBox(B.Text(Text, Font(EFace::Bold, 13.f), Color), 20.f), HAlign_Fill, VAlign_Fill, FMargin(8.f, 0.f));
	return Chip;
}

UWidget* UItemInspectWidget::BuildPanel(EPanel Kind, UWidget* Content, float IconSize)
{
	const FBuilder B(WidgetTree);
	FPanel& Panel = Panels[(int32)Kind];
	const FSlotDef& Def = Slots[(int32)Kind];

	USizeBox* Root = B.Size();
	Root->SetMinDesiredWidth(Def.MinW);

	UBackgroundBlur* Blur = WidgetTree->ConstructWidget<UBackgroundBlur>();
	Blur->SetBlurStrength(2.f);
	Blur->SetApplyAlphaToBlur(true);
	Blur->SetPadding(FMargin(0.f));
	SetContent(Root, Blur);

	UBorder* Face = B.Fill(PanelFace(), FMargin(PadL, 16.f, 34.f, 16.f));
	SetContent(Blur, Face);

	UHorizontalBox* Row = B.HBox();
	SetContent(Face, Row);

	// 아이콘 박스: 채움만 위젯, 둥근 1px 외곽선은 NativePaint(둥근 브러시 외곽선은 부모 RenderOpacity 를 안 따른다)
	USizeBox* IconBox = B.Size(IconSize, IconSize);
	UOverlay* IconStack = B.Overlay();
	SetContent(IconBox, IconStack);
	UBorder* IconFace = B.Box();
	IconFace->SetBrush(FSlateRoundedBoxBrush(Hex(0x000a10, .45f), 5.f));
	AddFill(IconStack, IconFace);

	const TSoftObjectPtr<UTexture2D>* Sources[] = { &IconName, &IconValue, &IconWeight, &IconGrade, &IconInfo };
	UImage* Icon = WidgetTree->ConstructWidget<UImage>();
	if (UTexture2D* Texture = LoadIcon(*Sources[(int32)Kind]))
	{
		LoadedIcons.Add(Texture);
		Icon->SetBrushFromTexture(Texture);
	}
	else
	{
		Icon->SetVisibility(ESlateVisibility::Hidden);
	}
	Icon->SetColorAndOpacity(IconInk());
	// 글리프 46px — 브러시 크기는 기본 32로 남으므로 SizeBox로 고정한다
	USizeBox* Glyph = B.Size(46.f, 46.f);
	SetContent(Glyph, Icon);
	AddOverlay(IconStack, Glyph, HAlign_Center, VAlign_Center);

	AddH(Row, IconBox, FMargin(0.f), VAlign_Center);
	AddH(Row, Content, FMargin(20.f, 0.f, 0.f, 0.f), VAlign_Center);

	Root->SetVisibility(ESlateVisibility::HitTestInvisible);
	Root->SetRenderOpacity(0.f);
	UCanvasPanelSlot* CanvasSlot = Canvas->AddChildToCanvas(Root);
	CanvasSlot->SetAutoSize(true);

	Panel.Root = Root;
	Panel.Slot = CanvasSlot;
	Panel.Icon = Icon;
	Panel.IconSize = IconSize;
	Panel.ExitRatio = Def.Ex;
	PanelRoots.Add(Root);
	return Root;
}

void UItemInspectWidget::BuildTree()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}
	const FBuilder B(WidgetTree);

	Canvas = B.Canvas();
	Canvas->SetVisibility(ESlateVisibility::Collapsed);
	WidgetTree->RootWidget = Canvas;

	// 이름
	{
		UVerticalBox* Text = B.VBox();
		NameLabel = MakeLabel(LOCTEXT("NameLabel", "아이템 이름"));
		NameValue = MakeValue(FText::GetEmpty());
		AddV(Text, NameLabel);
		AddV(Text, NameValue, FMargin(0.f, 3.f, 0.f, 0.f));
		BuildPanel(EPanel::Name, Text, Slots[(int32)EPanel::Name].Ic);
	}
	// 가치 — 원가는 같은 줄 상자에 기준선을 맞춘 작은 취소선 글자
	{
		UVerticalBox* Text = B.VBox();
		AddV(Text, MakeLabel(LOCTEXT("ValueLabel", "가치")));
		UHorizontalBox* Line = B.HBox();
		ValueText = MakeValue(FText::GetEmpty());
		ValueWas = B.Text(FText::GetEmpty(), Font(EFace::Regular, 18.f), WasInk());
		Typeset(ValueWas, ERef::Malgun, 42.f, CssBaseline(33.f, ERef::Malgun, 42.f));
		FSlateBrush Strike;
		Strike.TintColor = FSlateColor(WasInk());
		ValueWas->SetStrikeBrush(Strike);
		AddH(Line, ValueText, FMargin(0.f), VAlign_Top);
		AddH(Line, ValueWas, FMargin(10.f, 0.f, 0.f, 0.f), VAlign_Top);
		AddV(Text, Line, FMargin(0.f, 3.f, 0.f, 0.f));
		BuildPanel(EPanel::Value, Text, Slots[(int32)EPanel::Value].Ic);
	}
	// 무게
	{
		UVerticalBox* Text = B.VBox();
		AddV(Text, MakeLabel(LOCTEXT("WeightLabel", "무게")));
		WeightText = MakeValue(FText::GetEmpty());
		AddV(Text, WeightText, FMargin(0.f, 3.f, 0.f, 0.f));
		BuildPanel(EPanel::Weight, Text, Slots[(int32)EPanel::Weight].Ic);
	}
	// 등급
	{
		UVerticalBox* Text = B.VBox();
		AddV(Text, MakeLabel(LOCTEXT("GradeLabel", "등급")));
		GradeText = MakeValue(FText::GetEmpty());
		AddV(Text, GradeText, FMargin(0.f, 3.f, 0.f, 0.f));
		BuildPanel(EPanel::Grade, Text, Slots[(int32)EPanel::Grade].Ic);
	}
	// 정보 — 위험 칩(파손·충격·소음) + 설명
	{
		UVerticalBox* Text = B.VBox();
		AddV(Text, MakeLabel(LOCTEXT("InfoLabel", "정보")));
		Chips = B.HBox();
		AddV(Text, Chips, FMargin(0.f, 5.f, 0.f, 2.f), HAlign_Left);
		DescText = B.LineBox(B.Text(FText::GetEmpty(), Font(EFace::Medium, 20.f), ValueInk()), 35.f);
		AddV(Text, DescText, FMargin(0.f, 3.f, 0.f, 0.f));
		BuildPanel(EPanel::Info, Text, Slots[(int32)EPanel::Info].Ic);
	}
}

void UItemInspectWidget::BindPawn(APawn* Pawn)
{
	if (UInteractionComponent* Old = BoundInteraction.Get())
	{
		Old->OnInteractableTargetChanged.RemoveDynamic(this, &UItemInspectWidget::HandleTargetChanged);
		Old->OnInteractRequested.RemoveDynamic(this, &UItemInspectWidget::HandleInteractRequested);
	}
	BoundPawn = Pawn;
	BoundInteraction = Pawn ? Pawn->FindComponentByClass<UInteractionComponent>() : nullptr;
	AimedActor = nullptr;
	PickupRequestedActor = nullptr;
	if (UInteractionComponent* Interaction = BoundInteraction.Get())
	{
		Interaction->OnInteractableTargetChanged.AddUniqueDynamic(this, &UItemInspectWidget::HandleTargetChanged);
		Interaction->OnInteractRequested.AddUniqueDynamic(this, &UItemInspectWidget::HandleInteractRequested);
		AimedActor = Interaction->GetCurrentTarget();
	}
}

void UItemInspectWidget::HandleTargetChanged(AActor* NewTarget)
{
	AimedActor = NewTarget;
	if (NewTarget != PickupRequestedActor.Get())
	{
		PickupRequestedActor = nullptr;
	}
}

void UItemInspectWidget::HandleInteractRequested(AActor* Target)
{
	if (!Cast<AItemActorBase>(Target))
	{
		return;
	}
	// 줍는 중엔 띄우지 않는다 — 페이드 없이 바로 걷는다(서버 승인 왕복 동안 패널이 남거나 등장하지 않게)
	PickupRequestedActor = Target;
	if (DisplayItem.Get() == Target || NextItem.Get() == Target)
	{
		Animator.CancelAll();
		Phase = EPhase::Hidden;
		DisplayItem = nullptr;
		NextItem = nullptr;
		RootAlpha = 0.f;
		GraceLeft = -1.f;
	}
}

bool UItemInspectWidget::IsStillValid(const AItemActorBase* Item) const
{
	APawn* Pawn = BoundPawn.Get();
	if (!Item || !Item->ItemData || !Pawn || !Item->CanInteract(Pawn))
	{
		return false;
	}
	if (const UHealthComponent* Health = Pawn->FindComponentByClass<UHealthComponent>(); Health && Health->IsDead())
	{
		return false;
	}
	// 정산·실패 화면이 뜨면 걷는다
	const AGoHomeGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AGoHomeGameState>() : nullptr;
	return !GameState || (GameState->GetCurrentState() != EExpeditionState::Settlement && GameState->GetCurrentState() != EExpeditionState::Failed);
}

AItemActorBase* UItemInspectWidget::ResolveCandidate() const
{
	AItemActorBase* Item = Cast<AItemActorBase>(AimedActor.Get());
	return Item && Item != PickupRequestedActor.Get() && IsStillValid(Item) ? Item : nullptr;
}

float UItemInspectWidget::MotionScale() const
{
#if !UE_BUILD_SHIPPING
	return FMath::Max(CVarItemInspectSlow.GetValueOnGameThread(), .01f);
#else
	return 1.f;
#endif
}

void UItemInspectWidget::ApplyItem(const AItemActorBase* Item)
{
	const UItemDataAsset* Data = Item->ItemData;
	// 장비·보관함 상품은 납품 대상이 아니라 가치·등급이 없다(정산 루프와 같은 기준)
	const bool bTrade = Item->IsDeliverable() && !Item->IsSharedLockerItem();

	NameLabel->SetText(bTrade ? LOCTEXT("NameLabel", "아이템 이름") : LOCTEXT("GearLabel", "장비 이름"));
	NameValue->SetText(Data->DisplayName);

	FNumberFormattingOptions OneDecimal;
	OneDecimal.MinimumFractionalDigits = 1;
	OneDecimal.MaximumFractionalDigits = 1;
	WeightText->SetText(FText::Format(LOCTEXT("Weight", "{0} kg"), FText::AsNumber(Item->GetTotalWeight(), &OneDecimal)));

	const FLinearColor Grade = GradeColor(Data->Grade);
	GradeText->SetText(StaticEnum<EItemGrade>()->GetDisplayNameTextByValue((int64)Data->Grade));
	GradeText->SetColorAndOpacity(FSlateColor(Grade));
	Panels[(int32)EPanel::Grade].Icon->SetColorAndOpacity(Grade);

	DescText->SetText(Data->Description);

	Panels[(int32)EPanel::Value].bVisible = bTrade;
	Panels[(int32)EPanel::Grade].bVisible = bTrade;
	RefreshDynamic(Item);

	for (FPanel& Panel : Panels)
	{
		Panel.Root->SetVisibility(Panel.bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (Panel.bVisible)
		{
			Panel.Root->ForceLayoutPrepass();
		}
	}
}

void UItemInspectWidget::RefreshDynamic(const AItemActorBase* Item)
{
	const UItemDataAsset* Data = Item->ItemData;
	const int32 Breaks = Item->GetBreakCount();
	ShownBreakCount = Breaks;

	const int32 Value = FMath::RoundToInt(Item->GetCurrentValue());
	const int32 Base = FMath::RoundToInt(Data->Value);
	ValueText->SetText(FText::Format(LOCTEXT("Money", "{0} C"), FText::AsNumber(Value)));
	ValueWas->SetText(FText::AsNumber(Base));
	ValueWas->SetVisibility(Value < Base ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

	Chips->ClearChildren();
	auto AddChip = [this](const FText& Text, const FLinearColor& Color)
	{
		AddH(Chips, MakeChip(Text, Color), FMargin(Chips->GetChildrenCount() > 0 ? 6.f : 0.f, 0.f, 0.f, 0.f));
	};
	if (Data->bCanBreak && Breaks > 0)
	{
		AddChip(FText::Format(LOCTEXT("Broken", "파손 {0}/{1}"), Breaks, Data->MaxBreakCount), Breaks >= Data->MaxBreakCount ? Crit() : Warn());
	}
	if (Data->bCanBreak)
	{
		AddChip(LOCTEXT("Fragile", "충격 주의"), Warn());
	}
	if (Data->bMakesNoise)
	{
		AddChip(LOCTEXT("Noisy", "소음 발생"), Crit());
	}
	const bool bChips = Chips->GetChildrenCount() > 0;
	Chips->SetVisibility(bChips ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	DescText->SetVisibility(Data->Description.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);

	// 설명도 위험 정보도 없으면 빈 패널 대신 정보 패널을 뺀다(DataAsset 값 입력 전 상태)
	Panels[(int32)EPanel::Info].bVisible = bChips || !Data->Description.IsEmpty();
}

void UItemInspectWidget::Enter(AItemActorBase* Item)
{
	Animator.CancelAll();
	DisplayItem = Item;
	NextItem = nullptr;
	Phase = EPhase::Showing;
	GraceLeft = -1.f;
	ApplyItem(Item);

	RootAlpha = 1.f;
	ReticleAlpha = 0.f;
	ReticleScale = 1.5f;
	for (FPanel& Panel : Panels)
	{
		Panel.Alpha = 0.f;
		Panel.Shift = 1.f;
		Panel.LineProgress = 0.f;
	}

	// motion.md: 레티클 0 → 선 i 80+40i → 패널 i 220+40i, 각 180ms. 숨긴 패널은 건너뛰고 당긴다
	const float M = MotionScale();
	Animator.Play(TEXT("Reticle"), 0.f, .18f * M, [this](float T)
	{
		const float E = EasePanel().Eval(T);
		ReticleAlpha = E;
		ReticleScale = FMath::Lerp(1.5f, 1.f, E);
	});
	int32 Order = 0;
	for (int32 Index = 0; Index < (int32)EPanel::Count; ++Index)
	{
		if (!Panels[Index].bVisible)
		{
			continue;
		}
		const float Delay = (.08f + .04f * Order) * M;
		Animator.Play(FName(TEXT("Line"), Index + 1), Delay, .18f * M, [this, Index](float T)
		{
			Panels[Index].LineProgress = EaseLine().Eval(T);
		});
		Animator.Play(FName(TEXT("Panel"), Index + 1), Delay + .14f * M, .18f * M, [this, Index](float T)
		{
			const float E = EasePanel().Eval(T);
			Panels[Index].Alpha = E;
			Panels[Index].Shift = 1.f - E;
		});
		++Order;
	}
}

void UItemInspectWidget::Leave(float Seconds, EPhase NextPhase)
{
	// 등장 중이어도 현재 값에서 바로 퇴장
	Animator.CancelAll();
	Phase = NextPhase;
	GraceLeft = -1.f;
	const float From = RootAlpha;
	Animator.Play(TEXT("Out"), 0.f, Seconds * MotionScale(), [this, From](float T)
	{
		RootAlpha = From * (1.f - T);
	}, [this]()
	{
		AItemActorBase* Next = NextItem.Get();
		if (Phase == EPhase::Switching && IsStillValid(Next))
		{
			Enter(Next);
			return;
		}
		Phase = EPhase::Hidden;
		DisplayItem = nullptr;
		NextItem = nullptr;
	});
}

bool UItemInspectWidget::LayoutPanels(const FVector2D& ViewSize)
{
	const AItemActorBase* Item = DisplayItem.Get();
	const APlayerController* PC = GetOwningPlayer();
	if (!Item || !Item->MeshComponent || !PC || !PC->PlayerCameraManager)
	{
		return false;
	}

	// 초점 = 메시 바운드 중심, 기준 크기 = 바운드 반 크기 평균을 카메라 오른쪽으로 투영한 길이
	const FBoxSphereBounds& Bounds = Item->MeshComponent->Bounds;
	const float Radius = (Bounds.BoxExtent.X + Bounds.BoxExtent.Y + Bounds.BoxExtent.Z) / 3.f;
	const FVector Right = PC->PlayerCameraManager->GetCameraRotation().Quaternion().GetRightVector();
	FVector2D Center, Edge;
	if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, Bounds.Origin, Center, false)
		|| !UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, Bounds.Origin + Right * Radius, Edge, false))
	{
		return false;
	}
	Anchor = Center;
	const float K = FMath::Clamp(FVector2D::Distance(Center, Edge) / ReferenceRadius, ScaleRange.X, ScaleRange.Y);
	Scale = K;

	const float Right1 = ViewSize.X - SafeR;
	const float Bottom = ViewSize.Y - SafeB;

	TArray<int32, TInlineAllocator<5>> Visible;
	for (int32 Index = 0; Index < (int32)EPanel::Count; ++Index)
	{
		FPanel& Panel = Panels[Index];
		if (!Panel.bVisible)
		{
			continue;
		}
		Panel.Size = Panel.Root->GetDesiredSize();
		Panel.Size.X = FMath::Max(Panel.Size.X, Slots[Index].MinW);
		Panel.bLeft = Slots[Index].bLeft;
		Visible.Add(Index);
	}

	// 2. 한쪽 열이 안전 영역을 넘으면 그 열을 반대쪽으로 넘긴다
	bool bOverR = false, bOverL = false;
	for (int32 Index : Visible)
	{
		const FSlotDef& Def = Slots[Index];
		const float W = Panels[Index].Size.X;
		bOverR |= !Def.bLeft && Center.X + Def.In * K + W > Right1;
		bOverL |= Def.bLeft && Center.X + Def.In * K - W < SafeL;
	}
	if (bOverR != bOverL)
	{
		for (int32 Index : Visible)
		{
			Panels[Index].bLeft = bOverR;
		}
	}

	for (const bool bLeftColumn : { true, false })
	{
		TArray<int32, TInlineAllocator<5>> Column;
		for (int32 Index : Visible)
		{
			if (Panels[Index].bLeft == bLeftColumn)
			{
				Column.Add(Index);	// Visible 이 이미 이름→정보 순서
			}
		}
		if (Column.Num() == 0)
		{
			continue;
		}

		for (int32 Index : Column)
		{
			FPanel& Panel = Panels[Index];
			const float InX = FMath::Abs(Slots[Index].In) * K;
			Panel.Pos.X = bLeftColumn ? Center.X - InX - Panel.Size.X : Center.X + InX;
			Panel.Pos.Y = Center.Y + Slots[Index].Cy * K - Panel.Size.Y * .5f;
		}
		// 3. 순서를 지킨 채 세로 겹침을 PANEL_GAP 으로 밀어내고, 아래가 넘치면 열 전체를 위로
		for (int32 Row = 1; Row < Column.Num(); ++Row)
		{
			const FPanel& Above = Panels[Column[Row - 1]];
			Panels[Column[Row]].Pos.Y = FMath::Max(Panels[Column[Row]].Pos.Y, Above.Pos.Y + Above.Size.Y + PanelGap);
		}
		const FPanel& Last = Panels[Column.Last()];
		const float Push = FMath::Max(0.f, Last.Pos.Y + Last.Size.Y - Bottom);
		for (int32 Index : Column)
		{
			FPanel& Panel = Panels[Index];
			Panel.Pos.Y = FMath::Max(Panel.Pos.Y - Push, SafeT);
			Panel.Pos.X = FMath::Clamp(Panel.Pos.X, SafeL, FMath::Max(SafeL, Right1 - Panel.Size.X));
		}

		// 4. 선 끝은 세로 순으로 정렬해 위 패널부터 배정(교차 방지). 넘어간 패널은 x 좌우 반전
		TArray<FVector2D, TInlineAllocator<5>> Anchors;
		for (int32 Index : Column)
		{
			const FSlotDef& Def = Slots[Index];
			Anchors.Add(FVector2D((Def.bLeft == bLeftColumn ? Def.A.X : -Def.A.X) * K, Def.A.Y * K));
		}
		Anchors.Sort([](const FVector2D& A, const FVector2D& B) { return A.Y < B.Y; });

		for (int32 Row = 0; Row < Column.Num(); ++Row)
		{
			const int32 Index = Column[Row];
			FPanel& Panel = Panels[Index];
			const FVector2D End = Center + Anchors[Row];
			// 선: 패널 안쪽 변(Ex 높이) → 수평 run → 사선. run 은 남은 가로 거리 안으로 제한
			const float Dir = bLeftColumn ? 1.f : -1.f;
			const FVector2D Start(bLeftColumn ? Panel.Pos.X + Panel.Size.X + 4.f : Panel.Pos.X - 4.f, Panel.Pos.Y + Panel.Size.Y * Panel.ExitRatio);
			const float Run = FMath::Min(Slots[Index].Run * FMath::Min(K, 1.2f), FMath::Max(0.f, (End.X - Start.X) * Dir - 12.f));
			Panel.Line = { Start, FVector2D(Start.X + Dir * Run, Start.Y), End };
		}
	}

	// 등장 이동(바깥쪽 8px → 0)은 패널에만 — 선 시작점은 그대로
	for (int32 Index : Visible)
	{
		FPanel& Panel = Panels[Index];
		Panel.Pos.X += (Panel.bLeft ? -EnterShift : EnterShift) * Panel.Shift;
		Panel.Slot->SetPosition(Panel.Pos);
		Panel.Root->SetRenderOpacity(Panel.Alpha);
	}
	return true;
}

void UItemInspectWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	APawn* Pawn = GetOwningPlayerPawn();
	if (Pawn != BoundPawn.Get())
	{
		BindPawn(Pawn);
	}

	AItemActorBase* Candidate = ResolveCandidate();
	switch (Phase)
	{
	case EPhase::Hidden:
		if (Candidate)
		{
			Enter(Candidate);
		}
		break;
	case EPhase::Showing:
	{
		AItemActorBase* Item = DisplayItem.Get();
		if (!IsStillValid(Item))
		{
			// 주워짐·파괴·사망·정산 — 유예 없이 퇴장
			Leave(.1f, EPhase::Leaving);
		}
		else if (Candidate == Item)
		{
			GraceLeft = -1.f;
		}
		else if (Candidate)
		{
			NextItem = Candidate;
			Leave(.08f, EPhase::Switching);
		}
		else
		{
			if (GraceLeft < 0.f)
			{
				GraceLeft = ReleaseGraceSeconds;
			}
			GraceLeft -= InDeltaTime;
			if (GraceLeft <= 0.f)
			{
				Leave(.1f, EPhase::Leaving);
			}
		}
		break;
	}
	case EPhase::Leaving:
		if (Candidate)
		{
			Enter(Candidate);
		}
		break;
	case EPhase::Switching:
		if (Candidate)
		{
			NextItem = Candidate;
		}
		break;
	}

	Animator.Tick(InDeltaTime);

	const AItemActorBase* Item = DisplayItem.Get();
	if (Phase != EPhase::Hidden && !Item)
	{
		// 대상이 사라짐 — 그릴 위치가 없다
		Animator.CancelAll();
		Phase = EPhase::Hidden;
	}
	// 표시 중 파손 OnRep — 텍스트만 즉시 교체(재등장 없음)
	if (Phase == EPhase::Showing && Item && Item->GetBreakCount() != ShownBreakCount)
	{
		RefreshDynamic(Item);
		Panels[(int32)EPanel::Info].Root->ForceLayoutPrepass();
	}

	bOnScreen = Phase != EPhase::Hidden && LayoutPanels(MyGeometry.GetLocalSize());
	Canvas->SetRenderOpacity(RootAlpha);
	Canvas->SetVisibility(bOnScreen ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

int32 UItemInspectWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (!bOnScreen || RootAlpha <= 0.f)
	{
		return Layer;
	}
	++Layer;
	const FPaintGeometry Geo = AllottedGeometry.ToPaintGeometry();
	auto Faded = [](FLinearColor Color, float Alpha) { Color.A *= Alpha; return Color; };

	for (int32 Index = 0; Index < (int32)EPanel::Count; ++Index)
	{
		const FPanel& Panel = Panels[Index];
		const float A = Panel.Alpha * RootAlpha;
		if (!Panel.bVisible || A <= 0.f)
		{
			continue;
		}
		const FVector2D Min = Panel.Pos;
		const FVector2D Max = Panel.Pos + Panel.Size;

		// 바깥 글로우 18px(box-shadow 0 0 18px) — 2px 링을 erfc 세기로
		for (float D = 1.f; D < 18.f; D += 2.f)
		{
			Polyline(OutDrawElements, Layer, Geo, RectLoop(Min - D, Max + D), Faded(PanelGlow(), A * Erfc(D / (9.f * UE_SQRT_2))), 2.f);
		}
		// 얇은 외곽선(inset 1px)
		Polyline(OutDrawElements, Layer, Geo, RectLoop(Min + .5f, Max - .5f), Faded(Edge(), A), 1.f);
		// 아이콘 박스 둥근 외곽선
		const FVector2D IconMin(Min.X + PadL, Min.Y + (Panel.Size.Y - Panel.IconSize) * .5f);
		Polyline(OutDrawElements, Layer, Geo, RoundedLoop(IconMin + .5f, IconMin + Panel.IconSize - .5f, 5.f), Faded(Edge(), A), 1.f);

		// 모서리 브래킷: 가로 22 → 45° 모따기 5 → 세로 12, 외곽선 3px 바깥
		const float X0 = Min.X - 3.f, Y0 = Min.Y - 3.f, X1 = Max.X + 3.f, Y1 = Max.Y + 3.f;
		constexpr float Arm = 22.f, Cut = 5.f, Leg = 12.f;
		const TArray<FVector2f> Corners[] = {
			{ FVector2f(X0 + Arm, Y0), FVector2f(X0 + Cut, Y0), FVector2f(X0, Y0 + Cut), FVector2f(X0, Y0 + Leg) },
			{ FVector2f(X1 - Arm, Y0), FVector2f(X1 - Cut, Y0), FVector2f(X1, Y0 + Cut), FVector2f(X1, Y0 + Leg) },
			{ FVector2f(X0 + Arm, Y1), FVector2f(X0 + Cut, Y1), FVector2f(X0, Y1 - Cut), FVector2f(X0, Y1 - Leg) },
			{ FVector2f(X1 - Arm, Y1), FVector2f(X1 - Cut, Y1), FVector2f(X1, Y1 - Cut), FVector2f(X1, Y1 - Leg) } };
		// 연결 틱: 선이 붙는 변의 브래킷 줄 위 2px × 14
		const float TickX = Panel.bLeft ? X1 : X0;
		const float TickY = Min.Y + Panel.Size.Y * Panel.ExitRatio;
		const TArray<FVector2f> Tick = { FVector2f(TickX, TickY - 7.f), FVector2f(TickX, TickY + 7.f) };
		for (const TArray<FVector2f>& Corner : Corners)
		{
			Polyline(OutDrawElements, Layer, Geo, Corner, Faded(Glow(), A * .3f), 7.f);
		}
		Polyline(OutDrawElements, Layer, Geo, Tick, Faded(Glow(), A * .3f), 6.f);
		for (const TArray<FVector2f>& Corner : Corners)
		{
			Polyline(OutDrawElements, Layer, Geo, Corner, Faded(Bracket(), A), 3.f);
		}
		Polyline(OutDrawElements, Layer, Geo, Tick, Faded(Bracket(), A), 2.f);
	}

	// 리더 라인 1.6px 90% + 글로우
	for (const FPanel& Panel : Panels)
	{
		if (!Panel.bVisible || Panel.LineProgress <= 0.f)
		{
			continue;
		}
		const TArray<FVector2f> Line = ClipPolyline(Panel.Line, Panel.LineProgress);
		Polyline(OutDrawElements, Layer, Geo, Line, Faded(Glow(), RootAlpha * .35f), 5.f);
		Polyline(OutDrawElements, Layer, Geo, Line, Faded(Bracket(), RootAlpha * .9f), 1.6f);
	}

	// 레티클: 원 r25 + 원을 가로지르는 틱 4개(16~36) + 중심점
	const float RA = ReticleAlpha * RootAlpha;
	if (RA > 0.f)
	{
		const float S = ReticleScale;
		const FVector2f C(Anchor);
		const TArray<FVector2f> Ticks[] = {
			{ C + FVector2f(0.f, -36.f * S), C + FVector2f(0.f, -16.f * S) },
			{ C + FVector2f(0.f, 16.f * S), C + FVector2f(0.f, 36.f * S) },
			{ C + FVector2f(-36.f * S, 0.f), C + FVector2f(-16.f * S, 0.f) },
			{ C + FVector2f(16.f * S, 0.f), C + FVector2f(36.f * S, 0.f) } };
		const TArray<FVector2f> Ring = Circle(Anchor, 25.f * S);
		const TArray<FVector2f> Dot = Circle(Anchor, 1.f * S, 12);
		for (const float Pass : { 0.f, 1.f })
		{
			const FLinearColor Color = Pass == 0.f ? Faded(Glow(), RA * .35f) : Faded(ReticleInk(), RA);
			const float Extra = Pass == 0.f ? 5.f : 0.f;
			Polyline(OutDrawElements, Layer, Geo, Ring, Color, 2.2f + Extra);
			for (const TArray<FVector2f>& Tick : Ticks)
			{
				Polyline(OutDrawElements, Layer, Geo, Tick, Color, 2.2f + Extra);
			}
			Polyline(OutDrawElements, Layer, Geo, Dot, Color, 2.f + Extra);
		}
	}
	return Layer;
}

#undef LOCTEXT_NAMESPACE
