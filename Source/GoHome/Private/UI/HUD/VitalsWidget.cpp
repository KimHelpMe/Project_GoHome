#include "UI/HUD/VitalsWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Engine/FontFace.h"
#include "Engine/Texture2D.h"
#include "Fonts/CompositeFont.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Pawn.h"
#include "Player/HealthComponent.h"
#include "Player/OxygenComponent.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "UI/Supply/SupplyMotion.h"
#include "UI/Supply/SupplyUIStyle.h"
#include "Upgrade/EquipmentUpgradeDataAsset.h"
#include <cmath>

#define LOCTEXT_NAMESPACE "Vitals"

using namespace SupplyUI;

namespace
{
	// study.css .instrument(B안 separate-hp) — 계기 원점(화면 left 24 / top 612 @1080p) 기준 CSS px. 위치는 시안을 브라우저로 측정한 값
	constexpr float InstW = 264.f;
	constexpr float InstH = 444.f;
	constexpr float InstLeft = 24.f;
	constexpr float InstBottom = 24.f;

	// .dial-svg: gauge-window(232, border 1) 안 230 정사각에 viewBox 220
	constexpr float SvgX = 17.f;
	constexpr float SvgY = 110.f;
	constexpr float SvgK = 230.f / 220.f;

	constexpr float FadeSeconds = .25f;		// 사망 시 개인 HUD opacity 1→0
	constexpr float HeartPeriod = 1.25f;
	constexpr float WavePeriod = 2.2f;

	// CSS 값은 sRGB 0..1 로 다룬다(정점 색은 sRGB 바이트로 넘긴다 — 아나운서 밑판과 같은 규칙)
	struct FCss
	{
		float R = 0.f, G = 0.f, B = 0.f, A = 0.f;
		static FCss Hex(uint32 RGB, float InA = 1.f) { return { ((RGB >> 16) & 0xff) / 255.f, ((RGB >> 8) & 0xff) / 255.f, (RGB & 0xff) / 255.f, InA }; }
		static FCss From(const FLinearColor& Color, float InA = 1.f)
		{
			const FColor S = Color.ToFColorSRGB();
			return { S.R / 255.f, S.G / 255.f, S.B / 255.f, InA };
		}
		FCss WithAlpha(float InA) const { return { R, G, B, InA }; }
		FCss Scaled(float K) const { return { R, G, B, A * K }; }
	};

	FCss Lerp(const FCss& X, const FCss& Y, float T)
	{
		return { FMath::Lerp(X.R, Y.R, T), FMath::Lerp(X.G, Y.G, T), FMath::Lerp(X.B, Y.B, T), FMath::Lerp(X.A, Y.A, T) };
	}

	// color-mix(in srgb, X P%, Y)
	FCss Mix(const FCss& X, float P, const FCss& Y)
	{
		const float A = X.A * P + Y.A * (1.f - P);
		if (A <= 0.f)
		{
			return {};
		}
		return { (X.R * X.A * P + Y.R * Y.A * (1.f - P)) / A, (X.G * X.A * P + Y.G * Y.A * (1.f - P)) / A, (X.B * X.A * P + Y.B * Y.A * (1.f - P)) / A, A };
	}

	FLinearColor ToLinear(const FCss& C)
	{
		FLinearColor Color = FLinearColor::FromSRGBColor(FColor(FMath::RoundToInt(C.R * 255.f), FMath::RoundToInt(C.G * 255.f), FMath::RoundToInt(C.B * 255.f)));
		Color.A = C.A;
		return Color;
	}

	struct FStop
	{
		float T;
		FCss C;
	};

	FCss Sample(TConstArrayView<FStop> Stops, float T)
	{
		if (T <= Stops[0].T)
		{
			return Stops[0].C;
		}
		for (int32 Index = 1; Index < Stops.Num(); ++Index)
		{
			if (T <= Stops[Index].T)
			{
				const float Span = Stops[Index].T - Stops[Index - 1].T;
				return Lerp(Stops[Index - 1].C, Stops[Index].C, Span > 0.f ? (T - Stops[Index - 1].T) / Span : 1.f);
			}
		}
		return Stops.Last().C;
	}

	// 가우시안 누적분포 — CSS 블러(σ = blur/2)의 가장자리 농도
	float Phi(float X)
	{
		return .5f * (1.f + (float)std::erf(X * .70710678f));
	}

	// CSS linear-gradient(Deg) 의 위치 t(그라데이션 박스 기준)
	float LinearT(const FVector2f& P, const FVector4f& Box, float Deg)
	{
		const float Sin = FMath::Sin(FMath::DegreesToRadians(Deg));
		const float Cos = FMath::Cos(FMath::DegreesToRadians(Deg));
		const float Length = FMath::Abs(Box.Z * Sin) + FMath::Abs(Box.W * Cos);
		const FVector2f Center(Box.X + Box.Z * .5f, Box.Y + Box.W * .5f);
		return ((P.X - Center.X) * Sin - (P.Y - Center.Y) * Cos) / Length + .5f;
	}

	/** 둥근 사각형(border-radius 모서리별). 원은 W=H=2R. */
	struct FRound
	{
		float X, Y, W, H;
		float R[4];	// TL, TR, BR, BL

		static FRound Box(float X, float Y, float W, float H, float Radius) { return { X, Y, W, H, { Radius, Radius, Radius, Radius } }; }
		static FRound Circle(const FVector2f& C, float Radius) { return Box(C.X - Radius, C.Y - Radius, Radius * 2.f, Radius * 2.f, Radius); }

		// 바깥쪽으로 D 만큼 오프셋한 외곽선(D<0 안쪽). 점 개수는 D 와 무관하게 같아 링끼리 띠로 잇는다
		TArray<FVector2f> Ring(float D, int32 ArcSteps = 14, int32 EdgeSteps = 8) const
		{
			const FVector2f Corners[] = { { X, Y }, { X + W, Y }, { X + W, Y + H }, { X, Y + H } };
			const FVector2f Inward[] = { { 1.f, 1.f }, { -1.f, 1.f }, { -1.f, -1.f }, { 1.f, -1.f } };
			const float StartDeg[] = { 180.f, 270.f, 0.f, 90.f };
			TArray<FVector2f> Points;
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				const float Inset = FMath::Max(R[Corner], -D);
				const float Radius = FMath::Max(R[Corner] + D, 0.f);
				const FVector2f Center = Corners[Corner] + Inward[Corner] * Inset;
				const int32 First = Points.Num();
				for (int32 Step = 0; Step <= ArcSteps; ++Step)
				{
					const float Angle = FMath::DegreesToRadians(StartDeg[Corner] + 90.f * Step / ArcSteps);
					Points.Add(Center + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
				}
				// 다음 모서리까지 직선 변 — 그라데이션을 정점 색으로 담으려고 쪼갠다
				const int32 NextCorner = (Corner + 1) % 4;
				const float NextInset = FMath::Max(R[NextCorner], -D);
				const float NextRadius = FMath::Max(R[NextCorner] + D, 0.f);
				const float NextAngle = FMath::DegreesToRadians(StartDeg[NextCorner]);
				const FVector2f NextStart = Corners[NextCorner] + Inward[NextCorner] * NextInset + FVector2f(FMath::Cos(NextAngle), FMath::Sin(NextAngle)) * NextRadius;
				const FVector2f EdgeStart = Points.Last();
				for (int32 Step = 1; Step < EdgeSteps; ++Step)
				{
					Points.Add(FMath::Lerp(EdgeStart, NextStart, (float)Step / EdgeSteps));
				}
				(void)First;
			}
			return Points;
		}

		FVector2f Center() const { return { X + W * .5f, Y + H * .5f }; }
		float HalfMin() const { return FMath::Min(W, H) * .5f; }
	};

	/** 정점 색 메시(로컬 좌표). 그릴 때 위젯 변환·불투명도를 곱한다. */
	struct FMesh
	{
		TArray<FVector2f> P;
		TArray<FColor> C;
		TArray<SlateIndex> I;

		SlateIndex Add(const FVector2f& Point, const FCss& Color)
		{
			P.Add(Point);
			C.Add(FColor(FMath::RoundToInt(FMath::Clamp(Color.R, 0.f, 1.f) * 255.f), FMath::RoundToInt(FMath::Clamp(Color.G, 0.f, 1.f) * 255.f),
				FMath::RoundToInt(FMath::Clamp(Color.B, 0.f, 1.f) * 255.f), FMath::RoundToInt(FMath::Clamp(Color.A, 0.f, 1.f) * 255.f)));
			return (SlateIndex)(P.Num() - 1);
		}

		void Tri(SlateIndex A, SlateIndex B, SlateIndex D) { I.Append({ A, B, D }); }

		// 같은 개수의 닫힌 두 링을 띠로
		void Band(TConstArrayView<FVector2f> RingA, TFunctionRef<FCss(const FVector2f&)> ColorA, TConstArrayView<FVector2f> RingB, TFunctionRef<FCss(const FVector2f&)> ColorB)
		{
			const SlateIndex BaseA = (SlateIndex)P.Num();
			for (const FVector2f& Point : RingA)
			{
				Add(Point, ColorA(Point));
			}
			const SlateIndex BaseB = (SlateIndex)P.Num();
			for (const FVector2f& Point : RingB)
			{
				Add(Point, ColorB(Point));
			}
			const int32 Count = RingA.Num();
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const int32 Next = (Index + 1) % Count;
				Tri(BaseA + Index, BaseA + Next, BaseB + Next);
				Tri(BaseA + Index, BaseB + Next, BaseB + Index);
			}
		}

		// 채움: 가장자리 1px AA(+0.5 투명 → -0.5 불투명) 후 안쪽으로 링을 좁혀 가며 정점 색을 샘플링, 마지막은 중심 부채꼴
		void Fill(const FRound& Shape, TFunctionRef<FCss(const FVector2f&)> ColorAt, TConstArrayView<float> Insets = {})
		{
			static const float DefaultInsets[] = { 3.f, 7.f, 12.f, 18.f, 26.f, 36.f, 48.f, 62.f, 78.f, 96.f, 116.f, 138.f, 162.f };
			TConstArrayView<float> Steps = Insets.Num() > 0 ? Insets : TConstArrayView<float>(DefaultInsets);
			auto Clear = [&ColorAt](const FVector2f& Point) { return ColorAt(Point).WithAlpha(0.f); };
			TArray<FVector2f> Outer = Shape.Ring(.5f);
			TArray<FVector2f> Inner = Shape.Ring(-.5f);
			Band(Outer, Clear, Inner, ColorAt);
			const float Limit = Shape.HalfMin();
			for (float Inset : Steps)
			{
				if (Inset >= Limit - .5f)
				{
					break;
				}
				TArray<FVector2f> Next = Shape.Ring(-Inset);
				Band(Inner, ColorAt, Next, ColorAt);
				Inner = MoveTemp(Next);
			}
			const SlateIndex Mid = Add(Shape.Center(), ColorAt(Shape.Center()));
			const SlateIndex Base = (SlateIndex)P.Num();
			for (const FVector2f& Point : Inner)
			{
				Add(Point, ColorAt(Point));
			}
			for (int32 Index = 0; Index < Inner.Num(); ++Index)
			{
				Tri(Mid, Base + Index, Base + (Index + 1) % Inner.Num());
			}
		}

		// 오프셋 [D0, D1] 단색 고리(테두리·inset spread) + 양쪽 1px AA
		void Border(const FRound& Shape, float D0, float D1, const FCss& Color)
		{
			auto Solid = [&Color](const FVector2f&) { return Color; };
			auto Clear = [&Color](const FVector2f&) { return Color.WithAlpha(0.f); };
			const TArray<FVector2f> A = Shape.Ring(D0 - .5f), B = Shape.Ring(D0 + .5f), Cc = Shape.Ring(D1 - .5f), D = Shape.Ring(D1 + .5f);
			Band(A, Clear, B, Solid);
			Band(B, Solid, Cc, Solid);
			Band(Cc, Solid, D, Clear);
		}

		// box-shadow 바깥 그림자: 모양을 Offset 만큼 옮기고 가장자리 거리 s 에서 농도 = A·(1-Φ(s/σ))
		void Shadow(FRound Shape, const FVector2f& Offset, float Sigma, const FCss& Color, bool bInsideToo)
		{
			Shape.X += Offset.X;
			Shape.Y += Offset.Y;
			auto At = [&](float S) { return Color.Scaled(1.f - Phi(S / Sigma)); };
			const float Start = bInsideToo ? -FMath::Min(3.f * Sigma, Shape.HalfMin() - 1.f) : 0.f;
			const float End = 3.f * Sigma;
			const float Step = Sigma * .4f;
			TArray<FVector2f> Prev = Shape.Ring(Start);
			FCss PrevColor = At(Start);
			for (float S = Start + Step; S <= End + .01f; S += Step)
			{
				TArray<FVector2f> Next = Shape.Ring(S);
				const FCss NextColor = At(S);
				Band(Prev, [&PrevColor](const FVector2f&) { return PrevColor; }, Next, [&NextColor](const FVector2f&) { return NextColor; });
				Prev = MoveTemp(Next);
				PrevColor = NextColor;
			}
			if (bInsideToo)
			{
				const FCss Core = At(Start);
				TArray<FVector2f> Inner = Shape.Ring(Start);
				const SlateIndex Mid = Add(Shape.Center(), Core);
				const SlateIndex Base = (SlateIndex)P.Num();
				for (const FVector2f& Point : Inner)
				{
					Add(Point, Core);
				}
				for (int32 Index = 0; Index < Inner.Num(); ++Index)
				{
					Tri(Mid, Base + Index, Base + (Index + 1) % Inner.Num());
				}
			}
		}

		// 열린 선(마이터 이음, SVG 기본 butt 끝) — Profile: (선 중심에서 법선 방향 거리, 농도 배율) 오름차순
		void Polyline(TConstArrayView<FVector2f> Points, TConstArrayView<FVector2f> Profile, const FCss& Color)
		{
			const int32 Count = Points.Num();
			if (Count < 2)
			{
				return;
			}
			TArray<FVector2f> Miters;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const FVector2f In = Index > 0 ? (Points[Index] - Points[Index - 1]).GetSafeNormal() : (Points[1] - Points[0]).GetSafeNormal();
				const FVector2f Out = Index + 1 < Count ? (Points[Index + 1] - Points[Index]).GetSafeNormal() : In;
				const FVector2f N0(-In.Y, In.X), N1(-Out.Y, Out.X);
				const float Dot = FVector2f::DotProduct(N0, N1);
				Miters.Add((N0 + N1) / FMath::Max(1.f + Dot, .125f));	// 마이터 한도 4 (SVG 기본)
			}
			TArray<SlateIndex> Rows;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				for (const FVector2f& Sample : Profile)
				{
					Rows.Add(Add(Points[Index] + Miters[Index] * Sample.X, Color.Scaled(Sample.Y)));
				}
			}
			const int32 Width = Profile.Num();
			for (int32 Index = 0; Index + 1 < Count; ++Index)
			{
				for (int32 Lane = 0; Lane + 1 < Width; ++Lane)
				{
					const SlateIndex A = Rows[Index * Width + Lane], B = Rows[Index * Width + Lane + 1];
					const SlateIndex D = Rows[(Index + 1) * Width + Lane], E = Rows[(Index + 1) * Width + Lane + 1];
					Tri(A, B, E);
					Tri(A, E, D);
				}
			}
		}

		void Line(const FVector2f& A, const FVector2f& B, float Width, const FCss& Color)
		{
			const FVector2f Points[] = { A, B };
			const float Half = Width * .5f, Inner = FMath::Max(Half - .5f, 0.f), Cover = FMath::Min(Width, 1.f);
			const FVector2f Profile[] = { { -Half - .5f, 0.f }, { -Inner, Cover }, { Inner, Cover }, { Half + .5f, 0.f } };
			Polyline(Points, Profile, Color);
		}

		// 원 둘레 선(stroke) — 반지름 Radius, 두께 Width
		void CircleStroke(const FVector2f& Center, float Radius, float Width, const FCss& Color)
		{
			const float Half = Width * .5f, Inner = FMath::Max(Half - .5f, 0.f), Cover = FMath::Min(Width, 1.f);
			auto Solid = [&Color, Cover](const FVector2f&) { return Color.Scaled(Cover); };
			auto Clear = [&Color](const FVector2f&) { return Color.WithAlpha(0.f); };
			const TArray<FVector2f> A = FRound::Circle(Center, Radius - Half - .5f).Ring(0.f, 40, 1), B = FRound::Circle(Center, Radius - Inner).Ring(0.f, 40, 1);
			const TArray<FVector2f> Cc = FRound::Circle(Center, Radius + Inner).Ring(0.f, 40, 1), D = FRound::Circle(Center, Radius + Half + .5f).Ring(0.f, 40, 1);
			Band(A, Clear, B, Solid);
			Band(B, Solid, Cc, Solid);
			Band(Cc, Solid, D, Clear);
		}

		// 중심에서 보이는(별 모양) 닫힌 다각형 채움 + 바깥 1px 페더 + 선택적 바깥 블러(text-shadow 0 0 b)
		void Star(TConstArrayView<FVector2f> Points, const FVector2f& Center, const FCss& Color, float GlowSigma = 0.f, const FCss& Glow = {})
		{
			const int32 Count = Points.Num();
			TArray<FVector2f> Normals;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const FVector2f Tangent = (Points[(Index + 1) % Count] - Points[(Index + Count - 1) % Count]).GetSafeNormal();
				FVector2f Normal(Tangent.Y, -Tangent.X);
				if (FVector2f::DotProduct(Normal, Points[Index] - Center) < 0.f)
				{
					Normal = -Normal;
				}
				Normals.Add(Normal);
			}
			auto Offset = [&](float D)
			{
				TArray<FVector2f> Ring;
				for (int32 Index = 0; Index < Count; ++Index)
				{
					Ring.Add(Points[Index] + Normals[Index] * D);
				}
				return Ring;
			};
			if (GlowSigma > 0.f)
			{
				// 글리프 아래 그림자 — 가장자리 안쪽은 글리프가 덮는다
				const float Step = GlowSigma * .5f;
				TArray<FVector2f> Prev = Offset(-.5f);
				FCss PrevColor = Glow.Scaled(1.f - Phi(-.5f / GlowSigma));
				for (float S = Step; S <= GlowSigma * 3.f + .01f; S += Step)
				{
					TArray<FVector2f> Next = Offset(S);
					const FCss NextColor = Glow.Scaled(1.f - Phi(S / GlowSigma));
					Band(Prev, [&PrevColor](const FVector2f&) { return PrevColor; }, Next, [&NextColor](const FVector2f&) { return NextColor; });
					Prev = MoveTemp(Next);
					PrevColor = NextColor;
				}
			}
			const TArray<FVector2f> Inner = Offset(-.5f);
			const SlateIndex Mid = Add(Center, Color);
			const SlateIndex Base = (SlateIndex)P.Num();
			for (const FVector2f& Point : Inner)
			{
				Add(Point, Color);
			}
			for (int32 Index = 0; Index < Count; ++Index)
			{
				Tri(Mid, Base + Index, Base + (Index + 1) % Count);
			}
			Band(Inner, [&Color](const FVector2f&) { return Color; }, Offset(.5f), [&Color](const FVector2f&) { return Color.WithAlpha(0.f); });
		}

		// 볼록 다각형 + 1px 페더(바늘)
		void Convex(TConstArrayView<FVector2f> Points, const FCss& Color)
		{
			FVector2f Center(0.f, 0.f);
			for (const FVector2f& Point : Points)
			{
				Center += Point;
			}
			Star(Points, Center / (float)Points.Num(), Color);
		}

		void Emit(FSlateWindowElementList& Out, int32 Layer, const FSlateResourceHandle& Handle, const FSlateRenderTransform& Transform, float Opacity) const
		{
			if (I.Num() == 0)
			{
				return;
			}
			TArray<FSlateVertex> Verts;
			Verts.Reserve(P.Num());
			for (int32 Index = 0; Index < P.Num(); ++Index)
			{
				FColor Color = C[Index];
				Color.A = (uint8)FMath::RoundToInt(Color.A * FMath::Clamp(Opacity, 0.f, 1.f));
				Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform, P[Index], FVector2f(.5f, .5f), Color));
			}
			TArray<SlateIndex> Indices = I;
			FSlateDrawElement::MakeCustomVerts(Out, Layer, Handle, Verts, Indices, nullptr, 0, 0);
		}
	};

	// 가우시안 블러된 선(폭 Width, σ)의 단면 농도 — filter: drop-shadow 근사
	TArray<FVector2f> BlurProfile(float Width, float Sigma, float Scale)
	{
		TArray<FVector2f> Profile;
		const float Reach = Width * .5f + Sigma * 3.f;
		for (int32 Step = -8; Step <= 8; ++Step)
		{
			const float X = Reach * Step / 8.f;
			Profile.Add({ X, Scale * (Phi((X + Width * .5f) / Sigma) - Phi((X - Width * .5f) / Sigma)) });
		}
		return Profile;
	}

	TArray<FVector2f> SolidProfile(float Width)
	{
		const float Half = Width * .5f, Inner = FMath::Max(Half - .5f, 0.f), Cover = FMath::Min(Width, 1.f);
		return { { -Half - .5f, 0.f }, { -Inner, Cover }, { Inner, Cover }, { Half + .5f, 0.f } };
	}

	FVector2f SvgPoint(float X, float Y) { return { SvgX + X * SvgK, SvgY + Y * SvgK }; }

	FVector2f DialPoint(float Deg, float Radius)
	{
		const float A = FMath::DegreesToRadians(Deg);
		return SvgPoint(110.f + FMath::Cos(A) * Radius, 110.f + FMath::Sin(A) * Radius);
	}

	// .hp-wave path "M0 18H15L20 13 24 20 31 4 38 28 43 18H59L63 14 67 18H80" — viewBox 80×32, svg 115×36 meet(배율 1.125, 가로 12.5 여백)
	constexpr float WaveX = 129.f + 12.5f;
	constexpr float WaveY = 387.f;
	constexpr float WaveK = 1.125f;
	const FVector2f WavePath[] = { { 0, 18 }, { 15, 18 }, { 20, 13 }, { 24, 20 }, { 31, 4 }, { 38, 28 }, { 43, 18 }, { 59, 18 }, { 63, 14 }, { 67, 18 }, { 80, 18 } };

	// Noto Sans KR ♥(U+2665, wght 400) 외곽선 — 폰트 단위(UPM 1000, y 위), 23px 글리프 원점 (20, 414.91)
	const FIntPoint HeartOutline[] = {
		{712, 796}, {740, 794}, {767, 788}, {792, 778}, {816, 764}, {839, 747}, {859, 726}, {877, 703}, {892, 678}, {905, 649}, {914, 620}, {919, 588}, {921, 555}, {920, 528}, {916, 502}, {910, 476}, {902, 452}, {891, 428}, {879, 406}, {866, 384}, {852, 364}, {837, 344}, {821, 325}, {805, 307}, {788, 290}, {772, 273}, {756, 258}, {741, 243}, {727, 229}, {704, 207}, {682, 184}, {660, 160}, {637, 136}, {615, 111}, {594, 86}, {575, 62}, {556, 39}, {540, 17}, {525, -3}, {513, -21}, {502, -37}, {501, -38}, {500, -39}, {499, -38}, {498, -37}, {488, -21}, {476, -3}, {461, 17}, {444, 39}, {425, 62}, {406, 86}, {385, 111}, {364, 136}, {341, 160}, {319, 184}, {296, 207}, {273, 229}, {259, 243}, {244, 258}, {228, 273}, {212, 290}, {195, 307}, {179, 325}, {163, 344}, {148, 364}, {134, 384}, {121, 406}, {109, 428}, {98, 452}, {90, 476}, {84, 502}, {80, 528}, {79, 555}, {81, 588}, {86, 620}, {95, 649}, {108, 678}, {123, 703}, {141, 726}, {161, 747}, {184, 764}, {208, 778}, {233, 788}, {260, 794}, {288, 796}, {323, 794}, {356, 786}, {387, 774}, {416, 758}, {442, 735}, {464, 708}, {482, 675}, {495, 636}, {496, 633}, {497, 631}, {499, 629}, {500, 629}, {501, 629}, {503, 631}, {504, 633}, {505, 636}, {518, 675}, {536, 708}, {558, 735}, {584, 758}, {614, 774}, {645, 786}, {678, 794} };
	const FVector2f HeartOrigin(20.f, 414.91f);
	constexpr float HeartEm = 23.f / 1000.f;
	const FVector2f HeartPivot(31.5f, 405.f);	// .heart inline-block 23×32.19 의 중심(transform-origin center)

	// 시안 --mono 는 "JetBrains Mono" 지만 미설치 브라우저(한국어 Windows)는 GulimChe(.5em, 굵게는 합성 .52em)로 그렸다.
	// JetBrains Mono(.6em)로 그리되 글자 간격을 측정한 시안 글자 폭에 맞춘다(Slate 는 글자 폭·자간을 정수 px 로 자른다).
	// 600/700 은 합성 굵기라 획이 얇다 — Bold 대신 Medium 이 시안 획 두께에 가깝다.
	FSlateFontInfo Mono(bool bBold, float CssPx, float TargetAdvancePx)
	{
		static TSharedPtr<const FCompositeFont> Composites[2];
		TSharedPtr<const FCompositeFont>& Composite = Composites[bBold ? 1 : 0];
		if (!Composite.IsValid())
		{
			TSharedRef<FCompositeFont> Built = MakeShared<FCompositeFont>();
			const TCHAR* Path = bBold ? TEXT("/Game/GoHome/UI/Style/Fonts/FF_JBMono_Medium.FF_JBMono_Medium") : TEXT("/Game/GoHome/UI/Style/Fonts/FF_JBMono_Regular.FF_JBMono_Regular");
			if (UFontFace* Face = LoadObject<UFontFace>(nullptr, Path))
			{
				Face->AddToRoot();
				Built->DefaultTypeface.Fonts.Emplace_GetRef(TEXT("Default")).Font = FFontData(Face);
			}
			Composite = Built;
		}
		const float SizePt = CssPx * .75f;
		FSlateFontInfo Info(Composite, SizePt);
		const int32 Spacing = FMath::RoundToInt(TargetAdvancePx - FMath::RoundToFloat(CssPx * .6f));
		if (Spacing != 0)
		{
			Info.LetterSpacing = FMath::RoundToInt((Spacing + (Spacing > 0 ? .5f : -.5f)) * 1000.f / SizePt);
		}
		return Info;
	}

	// 상태 색(study.js vitalCluster)
	const FLinearColor OxygenNormal = Hex(0x19e3ee);
	const FLinearColor OxygenWarn = Hex(0xffb547);
	const FLinearColor OxygenCrit = Hex(0xff4d4d);
	const FLinearColor HealthNormal = Hex(0x5ef0b0);
	const FLinearColor HealthLow = Hex(0xff6559);
	const FLinearColor StatusIdle = Hex(0x71979d);

	// 시안 글자 폭(브라우저 측정, 자간 포함)
	constexpr float OxygenAdvance = 16.55f;	// 600 36px −.06em
	constexpr float LevelAdvance = 19.75f;	// 700 38px
	constexpr float LevelRight = 240.f;		// .integrated-tier 오른쪽 padding 끝
	constexpr float LevelGap = 7.f;
	constexpr float LevelLabelW = 27.f;		// "LV." 15px
}

// ---------------------------------------------------------------- 도형

void UVitalsPlate::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	WhiteTexture = UTexture2D::CreateTransient(4, 4, PF_B8G8R8A8);
	if (WhiteTexture && WhiteTexture->GetPlatformData() && !WhiteTexture->GetPlatformData()->Mips.IsEmpty())
	{
		FTexture2DMipMap& Mip = WhiteTexture->GetPlatformData()->Mips[0];
		FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		for (int32 Index = 0; Index < 16; ++Index)
		{
			Pixels[Index] = FColor::White;
		}
		Mip.BulkData.Unlock();
		WhiteTexture->UpdateResource();
		WhiteBrush.SetResourceObject(WhiteTexture);
		WhiteBrush.ImageSize = FVector2D(4.f, 4.f);
	}
}

void UVitalsPlate::BuildStatic() const
{
	FMesh Mesh;
	const FCss GradeCss = FCss::From(Grade);

	if (!bFront)
	{
		// --- .vital-console (0,31) 264×328, radius 24 ---
		const FRound Console = FRound::Box(0.f, 31.f, 264.f, 328.f, 24.f);
		// box-shadow 0 12px 30px #0006
		Mesh.Shadow(Console, FVector2f(0.f, 12.f), 15.f, FCss::Hex(0x000000, .4f), true);
		// background: linear-gradient(140deg, #294147, #0c191e 32%, #091114 70%, #1f3338) — 패딩 상자 기준
		const FStop ConsoleStops[] = { { 0.f, FCss::Hex(0x294147) }, { .32f, FCss::Hex(0x0c191e) }, { .70f, FCss::Hex(0x091114) }, { 1.f, FCss::Hex(0x1f3338) } };
		const FVector4f ConsoleBox(1.f, 32.f, 262.f, 326.f);
		Mesh.Fill(Console, [&](const FVector2f& Point) { return Sample(ConsoleStops, LinearT(Point, ConsoleBox, 140.f)); });
		// inset 0 0 0 6px #3c555a 위에 inset 0 0 0 5px #13252a(먼저 적은 그림자가 위)
		Mesh.Border(Console, -7.f, -1.f, FCss::Hex(0x3c555a));
		Mesh.Border(Console, -6.f, -1.f, FCss::Hex(0x13252a));
		Mesh.Border(Console, -1.f, 0.f, FCss::Hex(0x53686c));

		// :before — inset 7px(패딩 상자) border 2px 등급색 radius 18, box-shadow 0 0 12px 등급색 14%
		const FRound Frame = FRound::Box(8.f, 39.f, 248.f, 312.f, 18.f);
		Mesh.Shadow(Frame, FVector2f(0.f, 0.f), 6.f, GradeCss.WithAlpha(.14f), false);
		Mesh.Border(Frame, -2.f, 0.f, GradeCss);

		// .integrated-tier border-bottom 1px color-mix(grade 45%, #253b40)
		{
			// 페더 선으로 — 1px 채움 막대는 DPI 배율 < 1 에서 픽셀 중심 사이에 끼면 통째로 사라진다
			Mesh.Line({ 16.f, 98.5f }, { 248.f, 98.5f }, 1.f, Mix(GradeCss, .45f, FCss::Hex(0x253b40)));
		}

		// --- .gauge-window (16,109) 232 원 ---
		{
			const FVector2f Center(132.f, 225.f);
			const FRound Window = FRound::Circle(Center, 116.f);
			// radial-gradient(ellipse at 30% 15%, #20353a, #051013 65%) — 패딩 상자 230: 중심 (86,144.5), farthest-corner 타원 rx 422.9 / ry 211.4
			// + inset 0 4px 14px #000c(σ 7, 아래로 4 옮긴 원 바깥 농도)
			const FStop Radial[] = { { 0.f, FCss::Hex(0x20353a) }, { .65f, FCss::Hex(0x051013) } };
			const FVector2f GradientCenter(86.f, 144.5f);
			const float Rx = 422.9f, Ry = 211.4f;
			auto WindowColor = [&](const FVector2f& Point)
			{
				const FCss Base = Sample(Radial, FMath::Sqrt(FMath::Square((Point.X - GradientCenter.X) / Rx) + FMath::Square((Point.Y - GradientCenter.Y) / Ry)));
				const float Shade = .8f * Phi((FVector2f::Distance(Point, Center + FVector2f(0.f, 4.f)) - 115.f) / 7.f);
				return FCss{ Base.R * (1.f - Shade), Base.G * (1.f - Shade), Base.B * (1.f - Shade), 1.f };
			};
			const float Insets[] = { 1.5f, 3.f, 5.f, 7.5f, 10.5f, 14.f, 18.f, 23.f, 29.f, 36.f, 44.f, 54.f, 66.f, 80.f, 96.f, 108.f };
			Mesh.Fill(Window, WindowColor, Insets);
			Mesh.Border(Window, -1.f, 0.f, FCss::Hex(0x263f43));

			// svg circle r101 stroke #38525a
			Mesh.CircleStroke(Center, 101.f * SvgK, SvgK, FCss::Hex(0x38525a));
			// 눈금 41개: 135°부터 6.75° 간격, 10칸마다 굵고 길게. 0~4 적색, ~10 황색
			for (int32 Tick = 0; Tick <= 40; ++Tick)
			{
				const float Angle = 135.f + Tick * 6.75f;
				const bool bMajor = Tick % 10 == 0;
				const FCss Color = FCss::Hex(Tick <= 4 ? 0xff6559 : Tick <= 10 ? 0xd9a152 : 0x7a9da4);
				Mesh.Line(DialPoint(Angle, 96.f), DialPoint(Angle, bMajor ? 83.f : 90.f), (bMajor ? 2.f : 1.f) * SvgK, Color);
			}
		}

		// --- .health-module (2,367) 260×76, radius 10 10 18 18 ---
		{
			const FRound Module = { 2.f, 367.f, 260.f, 76.f, { 10.f, 10.f, 18.f, 18.f } };
			const FStop ModuleStops[] = { { 0.f, FCss::Hex(0x15282d) }, { 1.f, FCss::Hex(0x071316) } };
			const FVector4f ModuleBox(3.f, 368.f, 258.f, 74.f);
			Mesh.Fill(Module, [&](const FVector2f& Point) { return Sample(ModuleStops, LinearT(Point, ModuleBox, 120.f)); });
			Mesh.Border(Module, -5.f, -1.f, FCss::Hex(0x0b181c));
			Mesh.Border(Module, -1.f, 0.f, FCss::Hex(0x3e5358));
		}
	}
	else
	{
		// svg hub — circle r8 #506d73, r4 #d4e1de (바늘 위)
		// 바늘은 동적이라 NativePaint 에서 그 아래에 그린다
		const FVector2f Hub = SvgPoint(110.f, 110.f);
		Mesh.Fill(FRound::Circle(Hub, 8.f * SvgK), [](const FVector2f&) { return FCss::Hex(0x506d73); });
		Mesh.Fill(FRound::Circle(Hub, 4.f * SvgK), [](const FVector2f&) { return FCss::Hex(0xd4e1de); });
	}

	StaticPositions = MoveTemp(Mesh.P);
	StaticColors = MoveTemp(Mesh.C);
	StaticIndices = MoveTemp(Mesh.I);
	bStaticDirty = false;
}

int32 UVitalsPlate::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (!WhiteHandle.IsValid() && WhiteTexture && FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer())
	{
		WhiteHandle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(WhiteBrush);
	}
	const float Opacity = InWidgetStyle.GetColorAndOpacityTint().A;
	if (!WhiteHandle.IsValid() || Opacity <= 0.f)
	{
		return Layer;
	}
	const FSlateRenderTransform& Transform = AllottedGeometry.GetAccumulatedRenderTransform();

	if (bStaticDirty)
	{
		BuildStatic();
	}
	FMesh Static;
	Static.P = StaticPositions;
	Static.C = StaticColors;
	Static.I = StaticIndices;

	FMesh Dynamic;
	if (!bFront)
	{
		Static.Emit(OutDrawElements, ++Layer, WhiteHandle, Transform, Opacity);

		// .vital-status i — 5×5 원, 상태색
		Dynamic.Fill(FRound::Circle(FVector2f(2.5f, 11.47f), 2.5f), [this](const FVector2f&) { return FCss::From(Status); });

		// .heart — 심장 글리프, text-shadow 0 0 6px color-mix(hp 65%, transparent), 박동 scale(transform-origin center)
		const FCss HealthCss = FCss::From(Health);
		{
			TArray<FVector2f> Heart;
			FVector2f Center(0.f, 0.f);
			for (const FIntPoint& Point : HeartOutline)
			{
				const FVector2f Glyph = HeartOrigin + FVector2f(Point.X * HeartEm, -Point.Y * HeartEm);
				Heart.Add(HeartPivot + (Glyph - HeartPivot) * HeartScale);
			}
			Center = HeartPivot + (HeartOrigin + FVector2f(500.f * HeartEm, -420.f * HeartEm) - HeartPivot) * HeartScale;
			Dynamic.Star(Heart, Center, HealthCss, 3.f * HeartScale, HealthCss.WithAlpha(.65f));
		}

		// .health-module svg — 파형. svg 전체 drop-shadow(0 0 4px hp 45%), 바탕선 .72, 스윕은 dash 15/85 + 자체 drop-shadow(0 0 3px hp)
		{
			TArray<FVector2f> Wave;
			for (const FVector2f& Point : WavePath)
			{
				Wave.Add(FVector2f(WaveX, WaveY) + Point * WaveK);
			}
			// pathLength 100 기준 스윕 구간 [SweepStart, SweepStart+15] ∩ [0,100]
			TArray<float> Lengths = { 0.f };
			for (int32 Index = 1; Index < Wave.Num(); ++Index)
			{
				Lengths.Add(Lengths.Last() + FVector2f::Distance(Wave[Index - 1], Wave[Index]));
			}
			const float Total = Lengths.Last();
			const float From = FMath::Clamp(SweepStart, 0.f, 100.f) * Total / 100.f;
			const float To = FMath::Clamp(SweepStart + 15.f, 0.f, 100.f) * Total / 100.f;
			auto At = [&](float Distance)
			{
				for (int32 Index = 1; Index < Wave.Num(); ++Index)
				{
					if (Distance <= Lengths[Index])
					{
						const float Span = Lengths[Index] - Lengths[Index - 1];
						return FMath::Lerp(Wave[Index - 1], Wave[Index], Span > 0.f ? (Distance - Lengths[Index - 1]) / Span : 0.f);
					}
				}
				return Wave.Last();
			};
			TArray<FVector2f> Sweep;
			if (To - From > .01f)
			{
				Sweep.Add(At(From));
				for (int32 Index = 1; Index + 1 < Wave.Num(); ++Index)
				{
					if (Lengths[Index] > From && Lengths[Index] < To)
					{
						Sweep.Add(Wave[Index]);
					}
				}
				Sweep.Add(At(To));
			}

			const float BaseWidth = 1.6f * WaveK;
			const float SweepWidth = 2.3f * WaveK;
			// svg 필터 그림자(σ 2) — 바탕선(.72)과 스윕을 함께 번지게
			Dynamic.Polyline(Wave, BlurProfile(BaseWidth, 2.f, .45f * .72f), HealthCss);
			if (Sweep.Num() >= 2)
			{
				Dynamic.Polyline(Sweep, BlurProfile(SweepWidth, 2.f, .45f), HealthCss);
			}
			Dynamic.Polyline(Wave, SolidProfile(BaseWidth), HealthCss.Scaled(.72f));
			if (Sweep.Num() >= 2)
			{
				Dynamic.Polyline(Sweep, BlurProfile(SweepWidth, 1.5f, 1.f), HealthCss);
				const FCss Core = Mix(HealthCss, .35f, FCss::Hex(0xffffff));
				Dynamic.Polyline(Sweep, SolidProfile(SweepWidth), Core);
				// stroke-linecap: round
				for (const FVector2f& Cap : { Sweep[0], Sweep.Last() })
				{
					Dynamic.Fill(FRound::Circle(Cap, SweepWidth * .5f), [&Core](const FVector2f&) { return Core; });
				}
			}
		}
		Dynamic.Emit(OutDrawElements, ++Layer, WhiteHandle, Transform, Opacity);
	}
	else
	{
		// 바늘: path M91 108 174 107 186 110 174 113 91 112Z, rotate(135 + 270·O₂) 중심 (110,110)
		const float Angle = FMath::DegreesToRadians(135.f + 270.f * FMath::Clamp(OxygenRatio, 0.f, 1.f));
		const float Cos = FMath::Cos(Angle), Sin = FMath::Sin(Angle);
		const FVector2f Shape[] = { { 91, 108 }, { 174, 107 }, { 186, 110 }, { 174, 113 }, { 91, 112 } };
		TArray<FVector2f> Needle;
		for (const FVector2f& Point : Shape)
		{
			const FVector2f D = Point - FVector2f(110.f, 110.f);
			Needle.Add(SvgPoint(110.f + D.X * Cos - D.Y * Sin, 110.f + D.X * Sin + D.Y * Cos));
		}
		Dynamic.Convex(Needle, FCss::From(Oxygen));
		Dynamic.Emit(OutDrawElements, ++Layer, WhiteHandle, Transform, Opacity);
		Static.Emit(OutDrawElements, ++Layer, WhiteHandle, Transform, Opacity);
	}
	return Layer;
}

// ---------------------------------------------------------------- 위젯

void UVitalsWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetVisibility(ESlateVisibility::HitTestInvisible);
	if (GradeColors.IsEmpty())
	{
		// 시안 previewTiers — Lv.1 청록, Lv.2 초록, Lv.3 파랑, Lv.4 보라
		GradeColors = { Hex(0x19e3ee), Hex(0x4ce36b), Hex(0x4f7dff), Hex(0xc064ff) };
	}
	BuildTree();
}

void UVitalsWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BindToPawn(GetOwningPlayerPawn());
}

void UVitalsWidget::NativeDestruct()
{
	Unbind();
	Super::NativeDestruct();
}

void UVitalsWidget::BuildTree()
{
	const FBuilder B(WidgetTree);
	UCanvasPanel* Root = B.Canvas();
	WidgetTree->RootWidget = Root;

	Instrument = B.Canvas();
	UCanvasPanelSlot* InstrumentSlot = Root->AddChildToCanvas(Instrument);
	InstrumentSlot->SetAnchors(FAnchors(0.f, 1.f));
	InstrumentSlot->SetAlignment(FVector2D(0.f, 1.f));
	InstrumentSlot->SetPosition(FVector2D(InstLeft, -InstBottom));
	InstrumentSlot->SetSize(FVector2D(InstW, InstH));

	auto Plate = [&](bool bFront)
	{
		UVitalsPlate* Widget = WidgetTree->ConstructWidget<UVitalsPlate>(UVitalsPlate::StaticClass());
		Widget->bFront = bFront;
		AddCanvas(Instrument, Widget, FVector2D::ZeroVector, FVector2D(InstW, InstH));
		return Widget;
	};

	// 텍스트를 기준선(Baseline, 줄 상자 위에서부터가 아니라 계기 좌표)에 맞춰 놓는다. AlignX 0 왼쪽 / .5 가운데 / 1 오른쪽
	auto Place = [&](UTextBlock* Text, float CssPx, float X, float Baseline, float AlignX)
	{
		const float LineBox = CssPx * 1.5f;
		const float Ascent = CssPx * 1.2f;
		Typeset(Text, ERef::Malgun, LineBox, Ascent);
		UCanvasPanelSlot* TextSlot = Instrument->AddChildToCanvas(Text);
		TextSlot->SetAutoSize(true);
		TextSlot->SetAlignment(FVector2D(AlignX, 0.f));
		TextSlot->SetPosition(FVector2D(X, Baseline - Ascent));
		return TextSlot;
	};
	auto Static = [&](const TCHAR* Value, const FSlateFontInfo& Font, const FLinearColor& Color, float CssPx, float X, float Baseline, float AlignX = 0.f)
	{
		UTextBlock* Text = B.Text(FText::FromString(Value), Font, Color);
		Place(Text, CssPx, X, Baseline, AlignX);
		return Text;
	};

	Back = Plate(false);

	// svg 눈금 숫자(9px·뷰박스 배율, text-anchor middle, y+3 기준선) — 바늘 아래
	{
		const float Px = 9.f * SvgK;
		for (int32 Major = 0; Major <= 4; ++Major)
		{
			const FVector2f Point = DialPoint(135.f + Major * 67.5f, 73.f);
			Static(*FString::FromInt(Major * 25), Mono(false, Px, 4.8f), Hex(0x9bb0b5), Px, Point.X, Point.Y + 3.f * SvgK, .5f);
		}
	}

	Front = Plate(true);

	// .vital-status: 600 13px Noto, 점 5 + 간격 7
	StatusText = B.Text(FText::GetEmpty(), Font(EFace::SemiBold, 13.f), StatusIdle);
	Place(StatusText, 13.f, 12.f, 15.f, 0.f);

	// .integrated-tier — O₂(700 23px mono, 등급색) / 산소 장비(10px) / LV.(600 15px, 등급색) 큰 레벨(700 38px)
	// ₂ 는 시안 글꼴에 없어 대체 글꼴의 전각 칸에 그려졌다 — 같은 칸 왼쪽에 둔다
	GradeTexts.Add(Static(TEXT("O"), Mono(true, 23.f, 13.f), GradeColors[0], 23.f, 24.f, 68.f));
	GradeTexts.Add(Static(TEXT("₂"), Mono(true, 23.f, 13.f), GradeColors[0], 23.f, 37.f, 68.f));
	Static(TEXT("산소 장비"), Font(EFace::Regular, 10.f), Hex(0xbfd1d4), 10.f, 24.f, 86.f);
	UTextBlock* LevelLabel = B.Text(FText::FromString(TEXT("LV.")), Mono(true, 15.f, 9.f), GradeColors[0]);
	GradeTexts.Add(LevelLabel);
	LevelLabelSlot = Place(LevelLabel, 15.f, LevelRight - LevelAdvance - LevelGap, 82.f, 1.f);
	LevelText = B.Text(FText::GetEmpty(), Mono(true, 38.f, LevelAdvance), Hex(0xf2faf7));
	LevelSlot = Place(LevelText, 38.f, LevelRight, 82.f, 1.f);

	// .dial-label "O₂ / RESERVE" 600 10px .12em #9bb9bf — 가운데 정렬 결과 위치
	{
		const FSlateFontInfo LabelFont = Mono(true, 10.f, 6.4f);
		Static(TEXT("O"), LabelFont, Hex(0x9bb9bf), 10.f, 91.31f, 179.f);
		Static(TEXT("₂"), LabelFont, Hex(0x9bb9bf), 10.f, 97.7f, 179.f);
		Static(TEXT("/ RESERVE"), LabelFont, Hex(0x9bb9bf), 10.f, 115.3f, 179.f);
	}

	// .dial-value: 600 36px −.06em 산소색 + small "%" 13px #9ab6ba(margin-left 3) / em 9px .13em #749096
	OxygenText = B.Text(FText::GetEmpty(), Mono(true, 36.f, OxygenAdvance), OxygenNormal);
	OxygenSlot = Place(OxygenText, 36.f, 0.f, 285.f, 0.f);
	UTextBlock* Percent = B.Text(FText::FromString(TEXT("%")), Mono(true, 13.f, 8.f), Hex(0x9ab6ba));
	PercentSlot = Place(Percent, 13.f, 0.f, 285.f, 0.f);
	OxygenStateText = B.Text(FText::GetEmpty(), Mono(false, 9.f, 5.67f), Hex(0x749096));
	Place(OxygenStateText, 9.f, 132.f, 304.19f, .5f);

	// .hp-value: 600 30px HP색 / small 9px .1em color-mix(hp 55%, #90a7ad)
	HealthText = B.Text(FText::GetEmpty(), Mono(true, 30.f, 15.6f), HealthNormal);
	Place(HealthText, 30.f, 55.f, 411.f, 0.f);
	HealthMaxText = B.Text(FText::GetEmpty(), Mono(false, 9.f, 5.4f), HealthNormal);
	Place(HealthMaxText, 9.f, 55.f, 425.f, 0.f);

	Instrument->SetVisibility(ESlateVisibility::Collapsed);
}

void UVitalsWidget::BindToPawn(APawn* Pawn)
{
	Unbind();
	BoundPawn = Pawn;
	DeathFade = 1.f;

	UOxygenComponent* Oxygen = Pawn ? Pawn->FindComponentByClass<UOxygenComponent>() : nullptr;
	UHealthComponent* Health = Pawn ? Pawn->FindComponentByClass<UHealthComponent>() : nullptr;
	if (!Oxygen || !Health)
	{
		Instrument->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	BoundOxygen = Oxygen;
	BoundHealth = Health;
	Oxygen->OnOxygenChanged.AddUniqueDynamic(this, &UVitalsWidget::HandleOxygenChanged);
	Health->OnHPChanged.AddUniqueDynamic(this, &UVitalsWidget::HandleHPChanged);
	Instrument->SetVisibility(ESlateVisibility::HitTestInvisible);
	Instrument->SetRenderOpacity(Health->IsDead() ? 0.f : 1.f);
	DeathFade = Health->IsDead() ? 0.f : 1.f;
	Refresh();
}

void UVitalsWidget::Unbind()
{
	if (UOxygenComponent* Oxygen = BoundOxygen.Get())
	{
		Oxygen->OnOxygenChanged.RemoveDynamic(this, &UVitalsWidget::HandleOxygenChanged);
	}
	if (UHealthComponent* Health = BoundHealth.Get())
	{
		Health->OnHPChanged.RemoveDynamic(this, &UVitalsWidget::HandleHPChanged);
	}
	BoundOxygen.Reset();
	BoundHealth.Reset();
	BoundPawn.Reset();
}

void UVitalsWidget::HandleOxygenChanged(float CurrentOxygen, float MaxOxygen)
{
	Refresh();
}

void UVitalsWidget::HandleHPChanged(float CurrentHP, float MaxHP)
{
	Refresh();
}

void UVitalsWidget::Refresh()
{
	const UOxygenComponent* Oxygen = BoundOxygen.Get();
	const UHealthComponent* Health = BoundHealth.Get();
	if (!Oxygen || !Health)
	{
		return;
	}

	// 산소: 현재/최대 비율(최대 0 은 빈 상태)
	const float MaxOxygen = Oxygen->GetMaxOxygen();
	const float Ratio = MaxOxygen > 0.f ? FMath::Clamp(Oxygen->GetOxygen() / MaxOxygen, 0.f, 1.f) : 0.f;
	const bool bCritical = Ratio <= OxygenCriticalRatio;
	const bool bWarning = Ratio <= OxygenWarningRatio;
	const FLinearColor OxygenColor = bCritical ? OxygenCrit : bWarning ? OxygenWarn : OxygenNormal;

	const float MaxHP = Health->GetMaxHP();
	const float HP = FMath::Max(Health->GetHP(), 0.f);
	const bool bHealthLow = MaxHP > 0.f && HP / MaxHP <= HealthLowRatio;
	const FLinearColor HealthColor = bHealthLow ? HealthLow : HealthNormal;

	const FLinearColor StatusColor = bWarning ? OxygenColor : bHealthLow ? HealthColor : StatusIdle;
	StatusText->SetText(bCritical ? LOCTEXT("StatusO2Crit", "산소 위험 · 즉시 복귀")
		: bWarning ? LOCTEXT("StatusO2Low", "산소 부족 · 복귀 준비")
		: bHealthLow ? LOCTEXT("StatusHPLow", "체력 위험 · 주의")
		: LOCTEXT("StatusOk", "장비 연결 양호"));
	StatusText->SetColorAndOpacity(StatusColor);

	// 강화 등급: 큰 LV 숫자(흰색)와 외장 테두리·O₂·LV. 등급색 — 산소 경고와 독립
	const int32 Level = ComputeUpgradeLevel(Oxygen->GetMaxOxygenBonus());
	const FLinearColor GradeColor = GradeColors.IsEmpty() ? OxygenNormal : GradeColors[FMath::Clamp(Level - 1, 0, GradeColors.Num() - 1)];
	if (Level != ShownLevel)
	{
		ShownLevel = Level;
		const FString Digits = FString::FromInt(Level);
		LevelText->SetText(FText::FromString(Digits));
		if (Digits.Len() != ShownLevelDigits)
		{
			ShownLevelDigits = Digits.Len();
			LevelLabelSlot->SetPosition(FVector2D(LevelRight - Digits.Len() * LevelAdvance - LevelGap, LevelLabelSlot->GetPosition().Y));
		}
		for (UTextBlock* Text : GradeTexts)
		{
			Text->SetColorAndOpacity(GradeColor);
		}
		Back->Grade = GradeColor;
		Back->MarkStaticDirty();
	}

	// 잔량 숫자 + "%" 는 한 줄 가운데 정렬(gauge 중심 x 132)
	const FString OxygenDigits = FString::FromInt(FMath::RoundToInt(Ratio * 100.f));
	OxygenText->SetText(FText::FromString(OxygenDigits));
	OxygenText->SetColorAndOpacity(OxygenColor);
	if (OxygenDigits.Len() != ShownOxygenDigits)
	{
		ShownOxygenDigits = OxygenDigits.Len();
		const float DigitsW = OxygenDigits.Len() * OxygenAdvance;
		const float Left = 132.f - (DigitsW + 3.f + 8.f) * .5f;
		OxygenSlot->SetPosition(FVector2D(Left, OxygenSlot->GetPosition().Y));
		PercentSlot->SetPosition(FVector2D(Left + DigitsW + 3.f, PercentSlot->GetPosition().Y));
	}
	OxygenStateText->SetText(FText::FromString(bCritical ? TEXT("CRITICAL") : bWarning ? TEXT("LOW RESERVE") : TEXT("OXYGEN")));

	HealthText->SetText(FText::AsNumber(FMath::CeilToInt(HP)));
	HealthText->SetColorAndOpacity(HealthColor);
	HealthMaxText->SetText(FText::FromString(FString::Printf(TEXT("HP / %d"), FMath::RoundToInt(MaxHP))));
	HealthMaxText->SetColorAndOpacity(ToLinear(Mix(FCss::From(HealthColor), .55f, FCss::Hex(0x90a7ad))));

	Back->Health = HealthColor;
	Back->Status = StatusColor;
	Front->Oxygen = OxygenColor;
	Front->OxygenRatio = Ratio;
}

int32 UVitalsWidget::ComputeUpgradeLevel(float Bonus) const
{
	if (Bonus <= KINDA_SMALL_NUMBER)
	{
		return 1;
	}
	const UEquipmentUpgradeDataAsset* Data = UpgradeData.LoadSynchronous();
	if (!Data)
	{
		return 2;
	}
	// 복제된 보너스 값으로 레벨 역산: 보너스가 커지는 레벨마다 한 등급
	int32 Level = 1;
	float Previous = 0.f;
	for (int32 DataLevel = 2; DataLevel <= Data->GetMaxLevel(); ++DataLevel)
	{
		const float LevelBonus = Data->GetBonusValueAtLevel(DataLevel);
		if (LevelBonus <= Previous + KINDA_SMALL_NUMBER || LevelBonus > Bonus + KINDA_SMALL_NUMBER)
		{
			continue;
		}
		++Level;
		Previous = LevelBonus;
	}
	return FMath::Max(Level, 2);
}

void UVitalsWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 폰 교체(사망→관전, 로비 재생성) — 위젯은 뷰라 폰 스코프 데이터를 따라간다
	// 같은 폰이라도 컴포넌트가 다시 만들어지면(재구성) 다시 바인딩한다
	APawn* Pawn = GetOwningPlayerPawn();
	if (Pawn != BoundPawn.Get() || (Pawn && (!BoundOxygen.IsValid() || !BoundHealth.IsValid())))
	{
		BindToPawn(Pawn);
	}
	if (!BoundHealth.IsValid())
	{
		return;
	}

	// 사망: 개인 HUD opacity 1→0, 250ms ease
	const float Target = BoundHealth->IsDead() ? 0.f : 1.f;
	if (DeathFade != Target)
	{
		DeathFade = Target < DeathFade ? FMath::Max(Target, DeathFade - InDeltaTime / FadeSeconds) : FMath::Min(Target, DeathFade + InDeltaTime / FadeSeconds);
		static const FSupplyEase Ease = FSupplyEase::Bezier(.25f, .1f, .25f, 1.f);
		Instrument->SetRenderOpacity(1.f - Ease.Eval(1.f - DeathFade));
	}

	// 심장 박동 1.25s(scale 1 → 1.14 → 1 → 1.06 → 1, 구간마다 ease-in-out) · 파형 스윕 2.2s linear(dashoffset 100→0)
	Clock += InDeltaTime;
	static const FSupplyKey Beat[] = { { 0.f, 1.f }, { .09f, 1.14f }, { .18f, 1.f }, { .27f, 1.06f }, { .36f, 1.f }, { 1.f, 1.f } };
	static const FSupplyEase BeatEase = FSupplyEase::Bezier(.42f, 0.f, .58f, 1.f);
	Back->HeartScale = SampleSupplyKeys(Beat, FMath::Fmod(Clock, HeartPeriod) / HeartPeriod, BeatEase);
	Back->SweepStart = 100.f * FMath::Fmod(Clock, WavePeriod) / WavePeriod;
}

#undef LOCTEXT_NAMESPACE
