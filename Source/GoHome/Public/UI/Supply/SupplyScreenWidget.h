#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UObject/WeakInterfacePtr.h"
#include "Core/SupplyScreenBackend.h"
#include "UI/Supply/SupplyMotion.h"
#include "SupplyScreenWidget.generated.h"

class UOverlay;
class USupplyOpeningOverlay;
class USupplyPageBase;
class USupplyPageWipe;

UENUM()
enum class ESupplyPage : uint8
{
	None,
	Hub,
	Shop,
	Locker
};

/**
 * 보급 화면 Modal 루트(로비 메인 분기·보급 상점·팀 보관함). 생성·입력 모드·제거는 오너 PlayerController(ISupplyScreenBackend).
 * 페이지 간 이동은 같은 Modal 안의 교체 + 사선 스윕이고, 오브젝트로 열 때만 Sophia 오프닝을 재생한다.
 * Slate 키보드 포커스는 항상 이 위젯이 갖고, 키·D-pad·A/B를 정규화해 현재 페이지에 넘긴다(Enter/Esc 자동 반복 무시).
 * 위젯 트리는 C++이 만든다 — 시안 수치·곡선을 그대로 쓰기 위해(supply-ui 결정). WBP 없이 이 클래스를 바로 생성해도 된다.
 */
UCLASS()
class GOHOME_API USupplyScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void NavigateTo(ESupplyPage Page, bool bBack, FName FocusHint = NAME_None);

	// 최상위에서 닫기(140ms 퇴장 후 백엔드에 제거 요청)
	void RequestClose();

	FSupplyAnimator& GetAnimator() { return Animator; }
	ISupplyScreenBackend* GetBackend() const { return Backend.Get(); }
	bool IsOpeningPlaying() const;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	// 모션 축소(시안 prefers-reduced-motion). 접근성 설정 연결은 supply-ui-5.
	UPROPERTY(EditAnywhere, Category = "Supply")
	bool bReducedMotion = false;

private:
	void Start(ESupplyEntry Entry);
	void ShowPage(ESupplyPage Page, bool bBack, FName FocusHint, bool bWithWipe);
	USupplyPageBase* GetPage(ESupplyPage Page) const;
	USupplyPageBase* GetCurrentPage() const { return GetPage(CurrentPage); }

	// 진행 중인 장식(스윕)을 끝낸다 — 다음 입력은 장식을 기다리지 않는다.
	void SkipDecorations();
	void HandleGhostReleased(UWidget* Ghost);
	void PlayArrive(USupplyPageBase* Page, bool bBack);

	UPROPERTY(Transient)
	TObjectPtr<UOverlay> PageLayer;

	UPROPERTY(Transient)
	TObjectPtr<USupplyPageWipe> Wipe;

	UPROPERTY(Transient)
	TObjectPtr<USupplyOpeningOverlay> Opening;

	UPROPERTY(Transient)
	TObjectPtr<USupplyPageBase> HubPage;

	UPROPERTY(Transient)
	TObjectPtr<USupplyPageBase> ShopPage;

	UPROPERTY(Transient)
	TObjectPtr<USupplyPageBase> LockerPage;

	FSupplyAnimator Animator;
	TWeakInterfacePtr<ISupplyScreenBackend> Backend;
	ESupplyPage CurrentPage = ESupplyPage::None;
	bool bClosing = false;
	FVector2D LastViewSize = FVector2D::ZeroVector;
};
