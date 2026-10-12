#include "UI/Supply/SupplyMotion.h"

FSupplyEase FSupplyEase::Bezier(float InX1, float InY1, float InX2, float InY2)
{
	FSupplyEase Ease;
	Ease.X1 = InX1;
	Ease.Y1 = InY1;
	Ease.X2 = InX2;
	Ease.Y2 = InY2;
	return Ease;
}

namespace
{
	float BezierAxis(float A1, float A2, float S)
	{
		const float Inv = 1.f - S;
		return 3.f * Inv * Inv * S * A1 + 3.f * Inv * S * S * A2 + S * S * S;
	}

	float BezierAxisSlope(float A1, float A2, float S)
	{
		const float Inv = 1.f - S;
		return 3.f * Inv * Inv * A1 + 6.f * Inv * S * (A2 - A1) + 3.f * S * S * (1.f - A2);
	}
}

float FSupplyEase::Eval(float T) const
{
	T = FMath::Clamp(T, 0.f, 1.f);
	if (T <= 0.f || T >= 1.f || (X1 == Y1 && X2 == Y2))
	{
		return T;
	}

	// x(s) = T 를 뉴턴으로 풀고, 기울기가 작으면 이분법으로 마무리.
	float S = T;
	for (int32 Iteration = 0; Iteration < 8; ++Iteration)
	{
		const float Error = BezierAxis(X1, X2, S) - T;
		if (FMath::Abs(Error) < 1e-5f)
		{
			return BezierAxis(Y1, Y2, S);
		}
		const float Slope = BezierAxisSlope(X1, X2, S);
		if (FMath::Abs(Slope) < 1e-6f)
		{
			break;
		}
		S -= Error / Slope;
	}

	float Low = 0.f;
	float High = 1.f;
	S = T;
	for (int32 Iteration = 0; Iteration < 32; ++Iteration)
	{
		const float X = BezierAxis(X1, X2, S);
		if (FMath::Abs(X - T) < 1e-5f)
		{
			break;
		}
		if (X < T)
		{
			Low = S;
		}
		else
		{
			High = S;
		}
		S = 0.5f * (Low + High);
	}
	return BezierAxis(Y1, Y2, S);
}

float SampleSupplyKeys(TConstArrayView<FSupplyKey> Keys, float T, const FSupplyEase& Ease)
{
	if (Keys.Num() == 0)
	{
		return 0.f;
	}
	if (T <= Keys[0].Time)
	{
		return Keys[0].Value;
	}
	for (int32 Index = 1; Index < Keys.Num(); ++Index)
	{
		const FSupplyKey& From = Keys[Index - 1];
		const FSupplyKey& To = Keys[Index];
		if (T <= To.Time)
		{
			const float Span = To.Time - From.Time;
			const float Local = Span > KINDA_SMALL_NUMBER ? (T - From.Time) / Span : 1.f;
			return FMath::Lerp(From.Value, To.Value, Ease.Eval(Local));
		}
	}
	return Keys.Last().Value;
}

void FSupplyAnimator::Play(FName Id, float DelaySeconds, float DurationSeconds, FApply Apply, FDone OnDone)
{
	Cancel(Id);

	FTrack Track;
	Track.Id = Id;
	Track.Delay = FMath::Max(0.f, DelaySeconds);
	Track.Duration = FMath::Max(0.f, DurationSeconds);
	Track.Apply = MoveTemp(Apply);
	Track.OnDone = MoveTemp(OnDone);

	if (bReducedMotion || Track.Duration <= 0.f)
	{
		Complete(MoveTemp(Track));
		return;
	}

	if (Track.Apply)
	{
		Track.Apply(0.f);
	}
	Tracks.Add(MoveTemp(Track));
}

void FSupplyAnimator::Complete(FTrack Track)
{
	if (Track.Apply)
	{
		Track.Apply(1.f);
	}
	if (Track.OnDone)
	{
		Track.OnDone();
	}
}

void FSupplyAnimator::Finish(FName Id)
{
	const int32 Index = Tracks.IndexOfByPredicate([Id](const FTrack& Track) { return Track.Id == Id; });
	if (Index != INDEX_NONE)
	{
		FTrack Track = MoveTemp(Tracks[Index]);
		Tracks.RemoveAt(Index);
		Complete(MoveTemp(Track));
	}
}

void FSupplyAnimator::FinishAll()
{
	// 완료 콜백이 새 트랙을 걸 수 있으니 한 번에 하나씩 뽑는다.
	int32 Guard = 64;
	while (Tracks.Num() > 0 && Guard-- > 0)
	{
		FTrack Track = MoveTemp(Tracks[0]);
		Tracks.RemoveAt(0);
		Complete(MoveTemp(Track));
	}
}

void FSupplyAnimator::Cancel(FName Id)
{
	Tracks.RemoveAll([Id](const FTrack& Track) { return Track.Id == Id; });
}

void FSupplyAnimator::CancelAll()
{
	Tracks.Reset();
}

bool FSupplyAnimator::IsPlaying(FName Id) const
{
	return Tracks.ContainsByPredicate([Id](const FTrack& Track) { return Track.Id == Id; });
}

void FSupplyAnimator::Tick(float DeltaSeconds)
{
	TArray<FName> Finished;
	// Apply 안에서 Play/Cancel이 불려도 안전하도록 Id로 다시 찾는다.
	TArray<FName> Ids;
	for (const FTrack& Track : Tracks)
	{
		Ids.Add(Track.Id);
	}

	for (const FName& Id : Ids)
	{
		FTrack* Track = Tracks.FindByPredicate([Id](const FTrack& Candidate) { return Candidate.Id == Id; });
		if (!Track)
		{
			continue;
		}
		Track->Elapsed += DeltaSeconds;
		const float Active = Track->Elapsed - Track->Delay;
		if (Active < 0.f)
		{
			continue;
		}
		if (Active >= Track->Duration)
		{
			Finished.Add(Id);
			continue;
		}
		if (Track->Apply)
		{
			FApply Apply = Track->Apply;
			Apply(Active / Track->Duration);
		}
	}

	for (const FName& Id : Finished)
	{
		Finish(Id);
	}
}
