#include "UI/Supply/SupplyScreenWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "HAL/IConsoleManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "UI/Supply/SupplyHubPage.h"
#include "UI/Supply/SupplyLockerPage.h"
#include "UI/Supply/SupplyOpeningOverlay.h"
#include "UI/Supply/SupplyPageWipe.h"
#include "UI/Supply/SupplyShopPage.h"
#include "UI/Supply/SupplyUIStyle.h"

DEFINE_LOG_CATEGORY_STATIC(LogSupplyUI, Log, All);

#if !UE_BUILD_SHIPPING
// 활성 프레임 캡처용: 장식 모션 속도 배율(0.05 = 20배 느리게)
static TAutoConsoleVariable<float> CVarSupplyMotionScale(
	TEXT("GoHome.Supply.MotionScale"),
	1.f,
	TEXT("보급 화면 장식 모션 재생 속도 배율(개발용)."),
	ECVF_Cheat);

// 압축 변형 시험용: 페이지가 받는 뷰포트 논리 높이를 이 값으로 바꾸고 페이지 영역도 위에서부터 그 높이로 자른다(0 = 끔).
// UE 기본 DPI 규칙(ShortestSide)에선 가로 화면 논리 높이가 항상 ≈1080이라 실제 창 크기로는 850 이하를 만들 수 없다.
static TAutoConsoleVariable<float> CVarSupplyViewHeight(
	TEXT("GoHome.Supply.ViewHeight"),
	0.f,
	TEXT("보급 화면 페이지 논리 높이 강제(개발용, 0 = 실제 크기)."),
	ECVF_Cheat);
#endif

namespace
{
	const FName ArriveTrack(TEXT("Page.Arrive"));
	const FName CloseTrack(TEXT("Page.Close"));

	template <typename TPage>
	TPage* MakePage(USupplyScreenWidget* Host, UOverlay* Layer)
	{
		TPage* Page = CreateWidget<TPage>(Host, TPage::StaticClass());
		if (Page)
		{
			Page->InitPage(Host);
			SupplyUI::AddFill(Layer, Page);
			Page->SetVisibility(ESlateVisibility::Collapsed);
		}
		return Page;
	}
}

void USupplyScreenWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	// UserWidget 기본값은 SelfHitTestInvisible — 페이지는 전부 HitTestInvisible이라 호스트가 직접 클릭을 받아야 한다.
	SetVisibility(ESlateVisibility::Visible);

	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	const SupplyUI::FBuilder B(WidgetTree);
	UOverlay* Root = B.Overlay();
	Root->SetClipping(EWidgetClipping::ClipToBounds);
	WidgetTree->RootWidget = Root;

	Backdrop = B.Fill(SupplyUI::Ink());
	SupplyUI::AddFill(Root, Backdrop);

	PageLayer = B.Overlay();
	SupplyUI::AddFill(Root, PageLayer);

	HubPage = MakePage<USupplyHubPage>(this, PageLayer);
	ShopPage = MakePage<USupplyShopPage>(this, PageLayer);
	LockerPage = MakePage<USupplyLockerPage>(this, PageLayer);

	Wipe = CreateWidget<USupplyPageWipe>(this, USupplyPageWipe::StaticClass());
	SupplyUI::AddFill(Root, Wipe);

	Opening = CreateWidget<USupplyOpeningOverlay>(this, USupplyOpeningOverlay::StaticClass());
	SupplyUI::AddFill(Root, Opening);
}

void USupplyScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();

	Backend = TWeakInterfacePtr<ISupplyScreenBackend>(GetOwningPlayer());
	bClosing = false;

	// 압축 여부를 오프닝 전에 정한다 — 첫 틱에서 트리를 다시 만들면 오프닝·등장 모션이 옛 위젯에 걸린 채 새 트리는 바로 보인다.
	// 이 화면은 뷰포트를 꽉 채우므로 뷰포트 논리 크기 = 첫 틱의 MyGeometry 크기.
	const float ViewportScale = UWidgetLayoutLibrary::GetViewportScale(this);
	if (ViewportScale > KINDA_SMALL_NUMBER)
	{
		ApplyViewSize(UWidgetLayoutLibrary::GetViewportSize(this) / ViewportScale);
	}
	Start(Backend.IsValid() ? Backend->GetSupplyEntry() : ESupplyEntry::LobbyHub);
}

void USupplyScreenWidget::NativeDestruct()
{
	Animator.CancelAll();
	Super::NativeDestruct();
}

void USupplyScreenWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	ApplyViewSize(MyGeometry.GetLocalSize());

	float MotionDelta = InDeltaTime;
#if !UE_BUILD_SHIPPING
	MotionDelta *= FMath::Max(0.f, CVarSupplyMotionScale.GetValueOnGameThread());
#endif
	Animator.Tick(MotionDelta);
}

void USupplyScreenWidget::ApplyViewSize(const FVector2D& RawSize)
{
	FVector2D ViewSize = RawSize;
	float ClipBottom = 0.f;
#if !UE_BUILD_SHIPPING
	const float ForcedHeight = CVarSupplyViewHeight.GetValueOnGameThread();
	if (ForcedHeight > 1.f && ForcedHeight < ViewSize.Y)
	{
		ClipBottom = ViewSize.Y - ForcedHeight;
		ViewSize.Y = ForcedHeight;
	}
#endif
	if (!ViewSize.Equals(LastViewSize, 0.5f) && ViewSize.X > 1.f)
	{
		LastViewSize = ViewSize;
		if (UOverlaySlot* LayerSlot = PageLayer ? Cast<UOverlaySlot>(PageLayer->Slot) : nullptr)
		{
			LayerSlot->SetPadding(FMargin(0.f, 0.f, 0.f, ClipBottom));
		}
		for (USupplyPageBase* Page : { HubPage.Get(), ShopPage.Get(), LockerPage.Get() })
		{
			if (Page)
			{
				Page->ApplyViewSize(ViewSize);
			}
		}
	}
}

void USupplyScreenWidget::Start(ESupplyEntry Entry)
{
	Animator.CancelAll();
	Animator.bReducedMotion = bReducedMotion;
	if (Backdrop)
	{
		Backdrop->SetRenderOpacity(1.f);
	}

	ESupplyPage FirstPage = Entry == ESupplyEntry::ExpeditionLocker ? ESupplyPage::Locker : ESupplyPage::Hub;
	if (!GetPage(FirstPage))
	{
		UE_LOG(LogSupplyUI, Warning, TEXT("Supply page %d not available yet — falling back to hub"), static_cast<int32>(FirstPage));
		FirstPage = ESupplyPage::Hub;
	}

	CurrentPage = ESupplyPage::None;
	ShowPage(FirstPage, false, NAME_None, false);

	if (Opening)
	{
		Opening->Play(Animator);
		if (Opening->IsPlaying())
		{
			if (USupplyPageBase* Page = GetCurrentPage())
			{
				Page->PlayOpeningLift();
			}
		}
	}
}

bool USupplyScreenWidget::IsOpeningPlaying() const
{
	return Opening && Opening->IsPlaying();
}

USupplyPageBase* USupplyScreenWidget::GetPage(ESupplyPage Page) const
{
	switch (Page)
	{
	case ESupplyPage::Hub: return HubPage;
	case ESupplyPage::Shop: return ShopPage;
	case ESupplyPage::Locker: return LockerPage;
	default: return nullptr;
	}
}

void USupplyScreenWidget::NavigateTo(ESupplyPage Page, bool bBack, FName FocusHint)
{
	if (bClosing)
	{
		return;
	}
	if (!GetPage(Page))
	{
		UE_LOG(LogSupplyUI, Warning, TEXT("Supply page %d not available yet"), static_cast<int32>(Page));
		return;
	}
	// 오프닝 중 내부 이동은 스윕 없이 바로(시안: sophia-opening 동안 page motion 생략)
	ShowPage(Page, bBack, FocusHint, !IsOpeningPlaying());
}

void USupplyScreenWidget::ShowPage(ESupplyPage Page, bool bBack, FName FocusHint, bool bWithWipe)
{
	SkipDecorations();

	USupplyPageBase* Previous = GetCurrentPage();
	USupplyPageBase* Next = GetPage(Page);
	if (!Next)
	{
		return;
	}

	if (Previous && Previous != Next)
	{
		Previous->OnPageHidden();
	}

	CurrentPage = Page;
	Next->SetRenderOpacity(1.f);
	Next->SetRenderTranslation(FVector2D::ZeroVector);
	Next->SetVisibility(ESlateVisibility::HitTestInvisible);
	Next->OnPageShown(bBack, FocusHint);

	if (!Previous || Previous == Next)
	{
		return;
	}

	if (bWithWipe && Wipe && !Animator.bReducedMotion)
	{
		Next->ForceLayoutPrepass();
		float Top = 0.f;
		float Bottom = 0.f;
		Next->GetWipeBand(Top, Bottom);
		Wipe->Play(Animator, Top, Bottom, bBack, Previous,
			USupplyPageWipe::FOnGhostReleased::CreateUObject(this, &USupplyScreenWidget::HandleGhostReleased));
		PlayArrive(Next, bBack);
	}
	else
	{
		Previous->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void USupplyScreenWidget::PlayArrive(USupplyPageBase* Page, bool bBack)
{
	// page-content-arrive: opacity .35→1, X ±46→0, 260ms
	const float From = bBack ? -46.f : 46.f;
	TWeakObjectPtr<USupplyPageBase> Weak = Page;
	Animator.Play(ArriveTrack, 0.f, .26f, [Weak, From](float T)
	{
		if (USupplyPageBase* Target = Weak.Get())
		{
			const float E = FSupplyEase::Page().Eval(T);
			Target->SetRenderOpacity(FMath::Lerp(.35f, 1.f, E));
			Target->SetRenderTranslation(FVector2D(From * (1.f - E), 0.f));
		}
	});
}

void USupplyScreenWidget::HandleGhostReleased(UWidget* Ghost)
{
	if (!Ghost || !PageLayer)
	{
		return;
	}
	SupplyUI::AddFill(PageLayer, Ghost);
	if (Ghost != GetCurrentPage())
	{
		Ghost->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void USupplyScreenWidget::SkipDecorations()
{
	if (Wipe && Wipe->IsPlaying())
	{
		Wipe->Stop(Animator);
	}
	Animator.Finish(ArriveTrack);
}

void USupplyScreenWidget::RequestClose()
{
	if (bClosing)
	{
		return;
	}
	bClosing = true;

	if (Opening)
	{
		Opening->Stop(Animator);
	}
	SkipDecorations();

	TWeakObjectPtr<USupplyPageBase> Weak = GetCurrentPage();
	TWeakObjectPtr<UWidget> WeakBackdrop = Backdrop.Get();
	TWeakObjectPtr<USupplyScreenWidget> WeakThis = this;
	// .exit: Y 0→8, opacity 1→0, 140ms ease-in — 닫을 때만 바탕도 같이 걷혀 게임 화면으로 돌아간다
	Animator.Play(CloseTrack, 0.f, .14f, [Weak, WeakBackdrop](float T)
	{
		const float E = FSupplyEase::EaseIn().Eval(T);
		if (USupplyPageBase* Page = Weak.Get())
		{
			Page->SetRenderOpacity(1.f - E);
			Page->SetRenderTranslation(FVector2D(0.f, 8.f * E));
		}
		if (UWidget* Fade = WeakBackdrop.Get())
		{
			Fade->SetRenderOpacity(1.f - E);
		}
	}, [WeakThis]()
	{
		USupplyScreenWidget* Screen = WeakThis.Get();
		if (!Screen)
		{
			return;
		}
		if (ISupplyScreenBackend* ScreenBackend = Screen->GetBackend())
		{
			ScreenBackend->RequestCloseSupplyScreen();
		}
		else
		{
			Screen->RemoveFromParent();
		}
	});
}

FReply USupplyScreenWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (bClosing)
	{
		return FReply::Handled();
	}

	const FKey Key = InKeyEvent.GetKey();
	const bool bBack = Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right;
	const bool bAccept = Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom;

	TOptional<ESupplyInput> Input;
	if (bAccept)
	{
		Input = ESupplyInput::Accept;
	}
	// 방향은 화살표·WASD·패드 모두 같은 입력
	else if (Key == EKeys::Up || Key == EKeys::W || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_LeftStick_Up)
	{
		Input = ESupplyInput::Up;
	}
	else if (Key == EKeys::Down || Key == EKeys::S || Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_LeftStick_Down)
	{
		Input = ESupplyInput::Down;
	}
	else if (Key == EKeys::Left || Key == EKeys::A || Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_LeftStick_Left)
	{
		Input = ESupplyInput::Left;
	}
	else if (Key == EKeys::Right || Key == EKeys::D || Key == EKeys::Gamepad_DPad_Right || Key == EKeys::Gamepad_LeftStick_Right)
	{
		Input = ESupplyInput::Right;
	}
	else if (Key == EKeys::Tab)
	{
		Input = InKeyEvent.IsShiftDown() ? ESupplyInput::TabPrev : ESupplyInput::TabNext;
	}

	if (!bBack && !Input.IsSet())
	{
		return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
	}

	// Enter/Esc 자동 반복은 계층을 관통하지 않게 버린다. 방향 반복은 그대로.
	if ((bBack || bAccept) && InKeyEvent.IsRepeat())
	{
		return FReply::Handled();
	}

	SkipDecorations();

	if (USupplyPageBase* Page = GetCurrentPage())
	{
		if (bBack)
		{
			Page->HandleBack();
		}
		else
		{
			Page->HandleInput(Input.GetValue());
		}
	}
	return FReply::Handled();
}

FReply USupplyScreenWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bClosing)
	{
		return FReply::Handled();
	}
	SkipDecorations();

	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (USupplyPageBase* Page = GetCurrentPage())
		{
			const bool bHit = Page->HandleClick(InMouseEvent.GetScreenSpacePosition());
			UE_LOG(LogSupplyUI, Verbose, TEXT("Click at %s → %s"), *InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()).ToString(), bHit ? TEXT("hit") : TEXT("miss"));
		}
	}
	return FReply::Handled();
}

#if !UE_BUILD_SHIPPING
namespace
{
	void DumpTexts(UWidget* Widget, const FGeometry& HostGeometry, int32 Depth)
	{
		if (!Widget || !Widget->IsVisible())
		{
			return;
		}
		if (UTextBlock* Text = Cast<UTextBlock>(Widget))
		{
			const FGeometry& Geometry = Text->GetCachedGeometry();
			const FVector2D TopLeft = HostGeometry.AbsoluteToLocal(Geometry.GetAbsolutePosition());
			const FVector2D Size = Geometry.GetLocalSize();
			const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			const FSlateFontInfo FontInfo = Text->GetFont();
			UE_LOG(LogSupplyUI, Display, TEXT("TEXT [%.1f, %.1f, %.1f, %.1f] h=%u base=%d \"%s\""), TopLeft.X, TopLeft.Y, Size.X, Size.Y,
				Measure->GetMaxCharacterHeight(FontInfo), Measure->GetBaseline(FontInfo), *Text->GetText().ToString().Replace(TEXT("\n"), TEXT("|")));
		}
		if (UUserWidget* UserWidget = Cast<UUserWidget>(Widget))
		{
			if (UserWidget->WidgetTree)
			{
				DumpTexts(UserWidget->WidgetTree->RootWidget, HostGeometry, Depth + 1);
			}
			return;
		}
		if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
			{
				DumpTexts(Panel->GetChildAt(Index), HostGeometry, Depth + 1);
			}
		}
	}
}

// 시안 비교용: 보이는 TextBlock의 위치·크기(화면 논리 px)를 로그로
static FAutoConsoleCommand GSupplyDumpCommand(
	TEXT("GoHome.Supply.Dump"),
	TEXT("열린 보급 화면의 보이는 텍스트 위치·크기를 LogSupplyUI로 출력한다."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		for (TObjectIterator<USupplyScreenWidget> It; It; ++It)
		{
			if (It->IsInViewport() && It->WidgetTree)
			{
				const FGeometry& HostGeometry = It->GetCachedGeometry();
				UE_LOG(LogSupplyUI, Display, TEXT("HOST size %.1f x %.1f"), HostGeometry.GetLocalSize().X, HostGeometry.GetLocalSize().Y);
				DumpTexts(It->WidgetTree->RootWidget, HostGeometry, 0);
			}
		}
	}));
// 실제 Slate 키 경로 검증용: GoHome.Supply.Key <키 이름> [repeat] — 포커스된 위젯으로 KeyDown/KeyUp을 보낸다.
static FAutoConsoleCommand GSupplyKeyCommand(
	TEXT("GoHome.Supply.Key"),
	TEXT("Slate에 키 입력을 주입한다. 예: GoHome.Supply.Key Down / Enter / Escape / Gamepad_FaceButton_Bottom. 두 번째 인자 repeat이면 자동 반복 이벤트."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() == 0 || !FSlateApplication::IsInitialized())
		{
			return;
		}
		const FKey Key(*Args[0]);
		if (!Key.IsValid())
		{
			UE_LOG(LogSupplyUI, Warning, TEXT("Unknown key %s"), *Args[0]);
			return;
		}
		const bool bRepeat = Args.Num() > 1 && Args[1].Equals(TEXT("repeat"), ESearchCase::IgnoreCase);
		FSlateApplication& App = FSlateApplication::Get();
		const FKeyEvent Down(Key, App.GetModifierKeys(), 0, bRepeat, 0, 0);
		App.ProcessKeyDownEvent(Down);
		const FKeyEvent Up(Key, App.GetModifierKeys(), 0, false, 0, 0);
		App.ProcessKeyUpEvent(Up);
	}));
// 실제 Slate 마우스 경로 검증용: GoHome.Supply.Click <x> <y> — 열린 보급 화면 기준 논리 좌표를 왼쪽 클릭한다.
static FAutoConsoleCommand GSupplyClickCommand(
	TEXT("GoHome.Supply.Click"),
	TEXT("보급 화면 논리 좌표(x y)를 왼쪽 클릭한다(Slate 경로)."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() < 2 || !FSlateApplication::IsInitialized())
		{
			return;
		}
		for (TObjectIterator<USupplyScreenWidget> It; It; ++It)
		{
			if (!It->IsInViewport())
			{
				continue;
			}
			const FVector2D Local(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]));
			const FVector2D Screen = It->GetCachedGeometry().LocalToAbsolute(Local);
			FSlateApplication& App = FSlateApplication::Get();
			const TSet<FKey> NoButtons;
			const FPointerEvent Down(0, Screen, Screen, NoButtons, EKeys::LeftMouseButton, 0.f, App.GetModifierKeys());
			const bool bHandled = App.ProcessMouseButtonDownEvent(nullptr, Down);
			UE_LOG(LogSupplyUI, Verbose, TEXT("Inject click local %s → screen %s handled=%d"), *Local.ToString(), *Screen.ToString(), bHandled ? 1 : 0);
			const FPointerEvent Up(0, Screen, Screen, NoButtons, EKeys::LeftMouseButton, 0.f, App.GetModifierKeys());
			App.ProcessMouseButtonUpEvent(Up);
			return;
		}
	}));
#endif
