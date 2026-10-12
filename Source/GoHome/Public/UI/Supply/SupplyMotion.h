#pragma once

#include "CoreMinimal.h"

/**
 * CSS cubic-bezier(x1,y1,x2,y2) 이징. UMG 커브 키로는 근사밖에 안 되는 시안 곡선을 그대로 재현한다.
 */
struct GOHOME_API FSupplyEase
{
	float X1 = 0.f;
	float Y1 = 0.f;
	float X2 = 1.f;
	float Y2 = 1.f;

	static FSupplyEase Bezier(float InX1, float InY1, float InX2, float InY2);
	static FSupplyEase Linear() { return Bezier(0.f, 0.f, 1.f, 1.f); }
	static FSupplyEase Snap() { return Bezier(.16f, .8f, .22f, 1.f); }		// --snap
	static FSupplyEase Page() { return Bezier(.18f, .78f, .22f, 1.f); }		// 사선 스윕·오프닝·전표
	static FSupplyEase EaseOut() { return Bezier(0.f, 0.f, .58f, 1.f); }
	static FSupplyEase EaseIn() { return Bezier(.42f, 0.f, 1.f, 1.f); }

	float Eval(float T) const;
};

/** CSS @keyframes 한 트랙. timing-function은 키 구간마다 다시 적용된다(CSS 규칙). */
struct GOHOME_API FSupplyKey
{
	float Time = 0.f;	// 0..1
	float Value = 0.f;
};

GOHOME_API float SampleSupplyKeys(TConstArrayView<FSupplyKey> Keys, float T, const FSupplyEase& Ease);

/**
 * 보급 UI 장식 모션 재생기. 위젯 NativeTick이 Tick을 부른다.
 * 같은 Id로 다시 Play하면 이전 재생을 대체한다(시안: "새 이벤트는 이전 연출을 중단하고 최신 값으로 재시작").
 * Apply(T)는 지연 구간에도 T=0으로 불린다(CSS fill-mode both). 모션 축소면 즉시 T=1로 끝낸다.
 */
class GOHOME_API FSupplyAnimator
{
public:
	using FApply = TFunction<void(float /*T*/)>;
	using FDone = TFunction<void()>;

	void Play(FName Id, float DelaySeconds, float DurationSeconds, FApply Apply, FDone OnDone = nullptr);

	// 최종 상태 적용 + 완료 콜백
	void Finish(FName Id);
	void FinishAll();

	// 적용 없이 제거(완료 콜백도 안 부름)
	void Cancel(FName Id);
	void CancelAll();

	bool IsPlaying(FName Id) const;
	void Tick(float DeltaSeconds);

	bool bReducedMotion = false;

private:
	struct FTrack
	{
		FName Id;
		float Delay = 0.f;
		float Duration = 0.f;
		float Elapsed = 0.f;
		FApply Apply;
		FDone OnDone;
	};

	void Complete(FTrack Track);

	TArray<FTrack> Tracks;
};
