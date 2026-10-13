#include "UI/HUD/AnnouncerWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Core/AnnouncerBackend.h"
#include "Engine/FontFace.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "UI/Common/StrokeGlyphWidget.h"
#include "UI/Supply/SupplyUIStyle.h"

#define LOCTEXT_NAMESPACE "Announcer"

using namespace SupplyUI;

namespace
{
	// study.css .announcer — 1920×1080 논리 px
	constexpr float CardW = 660.f;
	constexpr float CardH = 184.f;
	constexpr float CardRight = 24.f;
	constexpr float CardTop = 48.f;
	constexpr float PortraitBox = 184.f;
	constexpr float CopyMinH = 142.f;
	constexpr float CopyPadTop = 13.f;
	constexpr float CopyPadBottom = 18.f;
	constexpr float CopyPadLeft = 22.f;
	constexpr float CopyWidth = CardW - 202.f;	// padding-right = portrait overlap 184 + 18 → 내용 끝 x=458
	constexpr float MotionSeconds = .18f;
	constexpr float EnterShift = 8.f;

	// CSS 값은 sRGB 0..1 로 다룬다(정점 색은 sRGB 바이트로 넘긴다)
	struct FCss
	{
		float R = 0.f, G = 0.f, B = 0.f, A = 0.f;
		static FCss Rgb(int32 R8, int32 G8, int32 B8, float A) { return { R8 / 255.f, G8 / 255.f, B8 / 255.f, A }; }
		static FCss FromLinear(const FLinearColor& Color, float A = 1.f)
		{
			const FColor S = Color.ToFColorSRGB();
			return Rgb(S.R, S.G, S.B, A);
		}
		FCss WithAlpha(float InA) const { return { R, G, B, InA }; }
	};

	FCss Lerp(const FCss& X, const FCss& Y, float T)
	{
		return { FMath::Lerp(X.R, Y.R, T), FMath::Lerp(X.G, Y.G, T), FMath::Lerp(X.B, Y.B, T), FMath::Lerp(X.A, Y.A, T) };
	}

	// color-mix(in srgb, A P%, B) — 알파는 premultiplied 로 섞는다
	FCss Mix(const FCss& X, float P, const FCss& Y)
	{
		const float A = X.A * P + Y.A * (1.f - P);
		if (A <= 0.f)
		{
			return {};
		}
		return { (X.R * X.A * P + Y.R * Y.A * (1.f - P)) / A, (X.G * X.A * P + Y.G * Y.A * (1.f - P)) / A,
			(X.B * X.A * P + Y.B * Y.A * (1.f - P)) / A, A };
	}

	struct FStop
	{
		float T;
		FCss C;
	};

	// CSS 그라데이션(색 정지점 사이 선형). transparent 끝점은 앞 색을 유지한 채 알파만 0(premultiplied 보간 결과)
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

	/** 정점 색 메시 — 위젯 스타일 불투명도를 직접 곱한다(커스텀 정점은 틴트가 자동 적용되지 않는다). */
	struct FMesh
	{
		const FSlateRenderTransform& Transform;
		FVector2f Origin;
		float Opacity;
		TArray<FSlateVertex> Verts;
		TArray<SlateIndex> Indices;

		FMesh(const FSlateRenderTransform& InTransform, const FVector2f& InOrigin, float InOpacity)
			: Transform(InTransform), Origin(InOrigin), Opacity(InOpacity) {}

		SlateIndex Add(const FVector2f& P, const FCss& C)
		{
			const FColor Color(FMath::RoundToInt(FMath::Clamp(C.R, 0.f, 1.f) * 255.f), FMath::RoundToInt(FMath::Clamp(C.G, 0.f, 1.f) * 255.f),
				FMath::RoundToInt(FMath::Clamp(C.B, 0.f, 1.f) * 255.f), FMath::RoundToInt(FMath::Clamp(C.A * Opacity, 0.f, 1.f) * 255.f));
			Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform, Origin + P, FVector2f(.5f, .5f), Color));
			return (SlateIndex)(Verts.Num() - 1);
		}

		void Tri(SlateIndex A, SlateIndex B, SlateIndex C) { Indices.Append({ A, B, C }); }

		void Quad(const FVector2f& P0, const FCss& C0, const FVector2f& P1, const FCss& C1,
			const FVector2f& P2, const FCss& C2, const FVector2f& P3, const FCss& C3)
		{
			const SlateIndex A = Add(P0, C0), B = Add(P1, C1), C = Add(P2, C2), D = Add(P3, C3);
			Tri(A, B, C);
			Tri(A, C, D);
		}

		// 볼록 다각형 단색 채움 + 바깥 1px 페더(정점 메시는 AA 가 없어 사선이 계단지는 것을 감춘다)
		void Convex(TConstArrayView<FVector2f> Points, const FCss& C, bool bFeather)
		{
			FVector2f Center(0.f, 0.f);
			for (const FVector2f& P : Points)
			{
				Center += P;
			}
			Center /= (float)Points.Num();
			const SlateIndex Mid = Add(Center, C);
			TArray<SlateIndex> Ring;
			for (const FVector2f& P : Points)
			{
				Ring.Add(Add(P, C));
			}
			for (int32 Index = 0; Index < Ring.Num(); ++Index)
			{
				Tri(Mid, Ring[Index], Ring[(Index + 1) % Ring.Num()]);
			}
			if (!bFeather)
			{
				return;
			}
			for (int32 Index = 0; Index < Points.Num(); ++Index)
			{
				const FVector2f A = Points[Index], B = Points[(Index + 1) % Points.Num()];
				FVector2f Normal(B.Y - A.Y, A.X - B.X);
				if (Normal.SizeSquared() < KINDA_SMALL_NUMBER)
				{
					continue;
				}
				Normal.Normalize();
				if (FVector2f::DotProduct(Normal, (A + B) * .5f - Center) < 0.f)
				{
					Normal = -Normal;
				}
				Quad(A, C, B, C, B + Normal, C.WithAlpha(0.f), A + Normal, C.WithAlpha(0.f));
			}
		}

		// 동심 고리 [R0,R1] — 반지름별 색 함수
		void Annulus(const FVector2f& Center, float R0, float R1, TFunctionRef<FCss(const FVector2f&, float)> ColorAt, int32 Segments = 96)
		{
			const SlateIndex Base = (SlateIndex)Verts.Num();
			for (int32 Step = 0; Step <= Segments; ++Step)
			{
				const float Angle = 2.f * PI * Step / Segments;
				const FVector2f Dir(FMath::Cos(Angle), FMath::Sin(Angle));
				Add(Center + Dir * R0, ColorAt(Center + Dir * R0, R0));
				Add(Center + Dir * R1, ColorAt(Center + Dir * R1, R1));
			}
			for (int32 Step = 0; Step < Segments; ++Step)
			{
				const SlateIndex I = Base + Step * 2;
				Tri(I, I + 1, I + 3);
				Tri(I, I + 3, I + 2);
			}
		}

		void Flush(FSlateWindowElementList& Out, int32 Layer, const FSlateResourceHandle& Handle)
		{
			if (Indices.Num() > 0)
			{
				FSlateDrawElement::MakeCustomVerts(Out, Layer, Handle, Verts, Indices, nullptr, 0, 0);
			}
			Verts.Reset();
			Indices.Reset();
		}
	};

	// 시안 --mono 는 "JetBrains Mono"(900/750/700). 보급 UI 의 Mono(Inconsolata 단일 굵기)로는 굵기가 안 맞아 Bold 면을 직접 쓴다
	FSlateFontInfo MonoBold(float CssPx, float LetterSpacingEm)
	{
		static TSharedPtr<const FCompositeFont> Composite;
		if (!Composite.IsValid())
		{
			TSharedRef<FCompositeFont> Built = MakeShared<FCompositeFont>();
			if (UFontFace* Face = LoadObject<UFontFace>(nullptr, TEXT("/Game/GoHome/UI/Style/Fonts/FF_JBMono_Bold.FF_JBMono_Bold")))
			{
				Face->AddToRoot();
				Built->DefaultTypeface.Fonts.Emplace_GetRef(TEXT("Default")).Font = FFontData(Face);
			}
			Composite = Built;
		}
		FSlateFontInfo Info = Font(EFace::Mono, CssPx, LetterSpacingEm);
		Info.CompositeFont = Composite;
		Info.FontObject = nullptr;
		return Info;
	}

	// 시안 화면은 JetBrains Mono 미설치 시 Consolas(advance .55em)로 그려진다 — JetBrains(.60em) 폭을 자간으로 맞춘다
	constexpr float MonoAdvanceDelta = .05f;

	// Noto Sans KR(시안이 실제로 그린 글꼴) hhea ascent 1.160 / descent .288 — CSS 줄 상자 안 첫 기준선
	float NotoBaseline(float CssPx, float LineHeightPx)
	{
		return (LineHeightPx - 1.448f * CssPx) * .5f + 1.16f * CssPx;
	}

	// SVG 원호를 선분으로(도 단위, 시계 방향 증가 = y 아래)
	TArray<FVector2D> Arc(const FVector2D& Center, float Radius, float FromDeg, float ToDeg, int32 Steps = 10)
	{
		TArray<FVector2D> Points;
		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			const float Angle = FMath::DegreesToRadians(FMath::Lerp(FromDeg, ToDeg, (float)Step / Steps));
			Points.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
		}
		return Points;
	}

	// study.js announcerIcons — viewBox 40, stroke 2. 산소는 글자(O₂)라 위젯이 텍스트로 대신 그린다
	TArray<TArray<FVector2D>> IconPaths(EAnnouncerIcon Icon)
	{
		using V = FVector2D;
		switch (Icon)
		{
		case EAnnouncerIcon::Signal:
			return { Arc(V(20, 20), 3, 0, 360, 16), Arc(V(19.55, 20), 11, 133.3f, 226.7f), Arc(V(20.45, 20), 11, -46.7f, 46.7f),
				Arc(V(19.45, 20), 18, 133.8f, 226.2f), Arc(V(20.55, 20), 18, -46.2f, 46.2f) };
		case EAnnouncerIcon::Return:
			return { { V(8, 32), V(8, 9), V(23, 9), V(23, 32) }, { V(5, 32), V(35, 32) }, { V(16, 21), V(17, 21) },
				{ V(27, 14), V(35, 14), V(35, 25) }, { V(31, 10), V(35, 14), V(31, 18) } };
		case EAnnouncerIcon::Clock:
			return { Arc(V(20, 22), 13, 0, 360, 32), { V(20, 14), V(20, 23), V(26, 27) }, { V(15, 5), V(25, 5) }, { V(20, 5), V(20, 9) } };
		case EAnnouncerIcon::Salvage:
			return { { V(20, 5), V(34, 20), V(20, 35), V(6, 20), V(20, 5) }, { V(20, 13), V(27, 20), V(20, 27), V(13, 20), V(20, 13) } };
		case EAnnouncerIcon::Hazard:
			return { { V(20, 6), V(35, 32), V(5, 32), V(20, 6) }, { V(20, 15), V(20, 23) }, { V(20, 27), V(20, 28) } };
		default:
			return {};
		}
	}
}

// ---------------------------------------------------------------- 밑판

void UAnnouncerPlate::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	WhiteTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Game/GoHome/UI/Announcer/T_Announcer_White.T_Announcer_White"));
	if (WhiteTexture)
	{
		WhiteBrush.SetResourceObject(WhiteTexture);
		WhiteBrush.ImageSize = FVector2D(8.f, 8.f);
	}
}

int32 UAnnouncerPlate::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (!WhiteHandle.IsValid() && WhiteTexture && FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer())
	{
		WhiteHandle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(WhiteBrush);
	}
	const FSlateResourceHandle& Handle = WhiteHandle;
	const float Opacity = InWidgetStyle.GetColorAndOpacityTint().A;
	if (!Handle.IsValid() || Opacity <= 0.f)
	{
		return Layer;
	}

	const FSlateRenderTransform& Transform = AllottedGeometry.GetAccumulatedRenderTransform();
	const FCss SignalCss = FCss::FromLinear(Signal);
	const float W = CardW;
	const float H = PanelHeight;

	// --- .ann-copy:before — 패싯 패널 ---
	{
		FMesh Mesh(Transform, FVector2f(0.f, PanelTop), Opacity);

		// clip-path: polygon(12px 0, 100%-19px 0, 100% 18px, 100% 100%-19px, 100%-15px 100%, 14px 100%, 0 100%-14px, 0 12px)
		auto TopAt = [W](float X) { return X < 12.f ? 12.f - X : (X > W - 19.f ? (X - (W - 19.f)) * 18.f / 19.f : 0.f); };
		auto BottomAt = [W, H](float X) { return X < 14.f ? H - 14.f + X : (X > W - 15.f ? H - (X - (W - 15.f)) * 19.f / 15.f : H); };

		// background: linear-gradient(110deg, ...) — 각 정점 위치를 110° 그라데이션 축에 투영
		const float Glass_ = FMath::Clamp(Glass, .2f, .8f);
		const FStop Background[] = {
			{ 0.f, FCss::Rgb(8, 25, 34, Glass_ + .22f) },
			{ .64f, FCss::Rgb(7, 17, 24, Glass_ + .15f) },
			{ .82f, FCss::Rgb(6, 17, 23, .28f) },
			{ 1.f, FCss::Rgb(6, 17, 23, 0.f) } };
		// mask-image: linear-gradient(90deg, #000 0, #000 58%, rgba(0,0,0,.72) 74%, transparent 100%)
		const FStop Mask[] = { { 0.f, FCss::Rgb(0, 0, 0, 1.f) }, { .58f, FCss::Rgb(0, 0, 0, 1.f) }, { .74f, FCss::Rgb(0, 0, 0, .72f) }, { 1.f, FCss::Rgb(0, 0, 0, 0.f) } };
		const float Sin = FMath::Sin(FMath::DegreesToRadians(110.f));
		const float Cos = FMath::Cos(FMath::DegreesToRadians(110.f));
		const float AxisLength = FMath::Abs(W * Sin) + FMath::Abs(H * Cos);
		auto PanelColor = [&](float X, float Y)
		{
			const float T = ((X - W * .5f) * Sin + (Y - H * .5f) * -Cos) / AxisLength + .5f;
			const FCss Color = Sample(Background, T);
			return Color.WithAlpha(Color.A * Sample(Mask, X / W).A);
		};

		TArray<float> Xs = { 0.f, 12.f, 14.f, W * .58f, W * .64f, W * .74f, W * .82f, W - 19.f, W - 15.f, W };
		for (float X = 22.f; X < W; X += 22.f)
		{
			Xs.Add(X);
		}
		Xs.Sort();
		for (int32 Index = 0; Index + 1 < Xs.Num(); ++Index)
		{
			const float X0 = Xs[Index], X1 = Xs[Index + 1];
			if (X1 - X0 < .01f)
			{
				continue;
			}
			const float T0 = TopAt(X0), T1 = TopAt(X1), B0 = BottomAt(X0), B1 = BottomAt(X1);
			Mesh.Quad(FVector2f(X0, T0), PanelColor(X0, T0), FVector2f(X1, T1), PanelColor(X1, T1),
				FVector2f(X1, B1), PanelColor(X1, B1), FVector2f(X0, B0), PanelColor(X0, B0));
		}

		// border 1px color-mix(signal 45%, #4a6670) — 위·왼쪽은 transparent, 오른쪽은 마스크 끝(0)이라 하단선만 보인다
		const FCss BorderColor = Mix(SignalCss, .45f, FCss::Rgb(0x4a, 0x66, 0x70, 1.f));
		for (int32 Index = 0; Index + 1 < Xs.Num(); ++Index)
		{
			const float X0 = FMath::Max(Xs[Index], 14.f), X1 = FMath::Min(Xs[Index + 1], W - 15.f);
			if (X1 - X0 < .01f)
			{
				continue;
			}
			const FCss C0 = BorderColor.WithAlpha(Sample(Mask, X0 / W).A), C1 = BorderColor.WithAlpha(Sample(Mask, X1 / W).A);
			Mesh.Quad(FVector2f(X0, H - 1.f), C0, FVector2f(X1, H - 1.f), C1, FVector2f(X1, H), C1, FVector2f(X0, H), C0);
		}

		// .ann-copy:after — 하단 신호선 left 16 right 24 bottom 8 height 4, clip-path 아래 변 2% 안쪽
		{
			const float L = 16.f, R = W - 24.f, Bw = R - L, Y0 = H - 12.f, Y1 = H - 8.f;
			const FStop Bar[] = { { 0.f, SignalCss }, { .68f, SignalCss.WithAlpha(.45f) }, { 1.f, SignalCss.WithAlpha(0.f) } };
			const float Us[] = { 0.f, .02f, .17f, .34f, .51f, .68f, .84f, .98f, 1.f };
			for (int32 Index = 0; Index + 1 < UE_ARRAY_COUNT(Us); ++Index)
			{
				const float Xa = L + Bw * Us[Index], Xb = L + Bw * Us[Index + 1];
				const float Ba = FMath::Clamp(Xa, L + Bw * .02f, R - Bw * .02f), Bb = FMath::Clamp(Xb, L + Bw * .02f, R - Bw * .02f);
				const FCss Ca = Sample(Bar, Us[Index]), Cb = Sample(Bar, Us[Index + 1]);
				const FCss Da = Sample(Bar, (Ba - L) / Bw), Db = Sample(Bar, (Bb - L) / Bw);
				Mesh.Quad(FVector2f(Xa, Y0), Ca, FVector2f(Xb, Y0), Cb, FVector2f(Bb, Y1), Db, FVector2f(Ba, Y1), Da);
			}
		}
		Mesh.Flush(OutDrawElements, ++Layer, Handle);
	}

	// --- .ann-kicker strong — BUI 탭 ---
	if (const UWidget* TabWidget = Tab.Get())
	{
		const FGeometry& TabGeometry = TabWidget->GetCachedGeometry();
		const FVector2f Size = FVector2f(TabGeometry.GetLocalSize());
		if (Size.X > 0.f)
		{
			const FVector2f Pos = FVector2f(AllottedGeometry.AbsoluteToLocal(TabGeometry.GetAbsolutePosition()));
			FMesh Mesh(Transform, Pos, Opacity);
			const float Tw = Size.X, Th = Size.Y;
			// clip-path: polygon(10px 0, 100% 0, 100%-12px 100%, 0 100%, 0 10px), 배경 #203c4a
			const FVector2f Shape[] = { FVector2f(10.f, 0.f), FVector2f(Tw, 0.f), FVector2f(Tw - 12.f, Th), FVector2f(0.f, Th), FVector2f(0.f, 10.f) };
			Mesh.Convex(Shape, FCss::Rgb(0x20, 0x3c, 0x4a, 1.f), true);
			// linear-gradient(130deg, signal 0 10px, ...) — 130° 축 위치 .766x+.643y < 10 인 사선 띠
			const float Dx = 10.f / FMath::Sin(FMath::DegreesToRadians(130.f));
			const float Dy = 10.f / -FMath::Cos(FMath::DegreesToRadians(130.f));
			const FVector2f Sliver[] = { FVector2f(10.f, 0.f), FVector2f(Dx, 0.f), FVector2f(0.f, Dy), FVector2f(0.f, 10.f) };
			Mesh.Convex(Sliver, SignalCss, false);
			Mesh.Flush(OutDrawElements, ++Layer, Handle);
		}
	}

	// --- .ann-portrait — 계기창(184 정사각, overflow hidden) ---
	const FVector2f PortraitOrigin(W - PortraitBox, 0.f);
	OutDrawElements.PushClip(FSlateClippingZone(AllottedGeometry.ToPaintGeometry(FVector2f(PortraitBox, PortraitBox), FSlateLayoutTransform(PortraitOrigin))));
	{
		FMesh Mesh(Transform, PortraitOrigin, Opacity);
		// :before inset 7px 0 3px 10px → 174 정사각, border 5px, border-radius 50%
		const FVector2f Center(10.f + 87.f, 7.f + 87.f);
		const float Inner = 82.f, Outer = 87.f;

		// radial-gradient(circle at 52% 45%, mix(signal 12%, rgba(8,23,30,.82)), rgba(4,12,18,.72) 74%) — 패딩 상자(164) 기준
		const FVector2f GradientCenter(15.f + 164.f * .52f, 12.f + 164.f * .45f);
		const float Farthest = FMath::Sqrt(FMath::Square(15.f + 164.f - GradientCenter.X) + FMath::Square(12.f + 164.f - GradientCenter.Y));
		const FCss Start = Mix(SignalCss, .12f, FCss::Rgb(8, 23, 30, .82f));
		const FCss End = FCss::Rgb(4, 12, 18, .72f);
		auto FillAt = [&](const FVector2f& P, float Radius)
		{
			const float T = FMath::Clamp(FVector2f::Distance(P, GradientCenter) / (Farthest * .74f), 0.f, 1.f);
			const FCss Fill = Lerp(Start, End, T);
			// inset 0 0 16px rgba(0,0,0,.32) — 안쪽 가장자리에서 16px 에 걸쳐 어두워짐
			const float Shade = .32f * .5f * FMath::Clamp(1.f - (Inner - Radius) / 16.f, 0.f, 1.f);
			const float A = Fill.A + Shade * (1.f - Fill.A);
			const float K = A > 0.f ? Fill.A * (1.f - Shade) / A : 0.f;
			return FCss{ Fill.R * K, Fill.G * K, Fill.B * K, A };
		};
		const float Radii[] = { 0.f, 16.f, 32.f, 48.f, 60.f, 66.f, 72.f, 76.f, 79.f, Inner };
		for (int32 Index = 0; Index + 1 < UE_ARRAY_COUNT(Radii); ++Index)
		{
			Mesh.Annulus(Center, Radii[Index], Radii[Index + 1], FillAt);
		}

		// border 5px color-mix(signal 82%, #183541) + box-shadow 0 0 0 2px rgba(5,14,19,.85) + 1px 페더
		const FCss Ring = Mix(SignalCss, .82f, FCss::Rgb(0x18, 0x35, 0x41, 1.f));
		const FCss Halo = FCss::Rgb(5, 14, 19, .85f);
		Mesh.Annulus(Center, Inner - .5f, Inner, [&](const FVector2f& P, float Radius) { return Radius < Inner ? FillAt(P, Radius).WithAlpha(0.f) : Ring; });
		Mesh.Annulus(Center, Inner, Outer, [&](const FVector2f&, float) { return Ring; });
		Mesh.Annulus(Center, Outer, Outer + 2.f, [&](const FVector2f&, float) { return Halo; });
		Mesh.Annulus(Center, Outer + 2.f, Outer + 3.f, [&](const FVector2f&, float Radius) { return Halo.WithAlpha(Radius > Outer + 2.5f ? 0.f : Halo.A); });
		Mesh.Flush(OutDrawElements, ++Layer, Handle);

		if (bHasPortrait)
		{
			// img: filter drop-shadow(0 5px 8px #000b) — 9탭 근사(σ≈4) 후 본 이미지
			const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
			const float TapAlpha = 1.f - FMath::Pow(1.f - .73f, 1.f / 9.f);
			const FVector2f Offsets[] = { FVector2f(0.f, 0.f), FVector2f(4.f, 0.f), FVector2f(-4.f, 0.f), FVector2f(0.f, 4.f), FVector2f(0.f, -4.f),
				FVector2f(2.8f, 2.8f), FVector2f(-2.8f, 2.8f), FVector2f(2.8f, -2.8f), FVector2f(-2.8f, -2.8f) };
			++Layer;
			for (const FVector2f& Offset : Offsets)
			{
				FSlateDrawElement::MakeBox(OutDrawElements, Layer,
					AllottedGeometry.ToPaintGeometry(FVector2f(PortraitSize), FSlateLayoutTransform(PortraitOrigin + FVector2f(PortraitPos) + FVector2f(0.f, 5.f) + Offset)),
					&PortraitBrush, ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, TapAlpha * Tint.A));
			}
			FSlateDrawElement::MakeBox(OutDrawElements, ++Layer,
				AllottedGeometry.ToPaintGeometry(FVector2f(PortraitSize), FSlateLayoutTransform(PortraitOrigin + FVector2f(PortraitPos))),
				&PortraitBrush, ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, Tint.A));
		}
	}
	OutDrawElements.PopClip();
	return Layer;
}

// ---------------------------------------------------------------- 위젯

void UAnnouncerWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetVisibility(ESlateVisibility::HitTestInvisible);
	Ease = FSupplyEase::Bezier(.25f, .1f, .25f, 1.f);	// CSS transition 기본 ease
	BuildTree();
	ApplyMotion(0.f);
}

void UAnnouncerWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BindBackend();
}

void UAnnouncerWidget::NativeDestruct()
{
	if (Backend && BackendObject.IsValid())
	{
		Backend->OnAnnouncerDisplayChanged().RemoveAll(this);
	}
	Backend = nullptr;
	BackendObject.Reset();
	Super::NativeDestruct();
}

void UAnnouncerWidget::BindBackend()
{
	if (Backend)
	{
		return;
	}
	APlayerController* PC = GetOwningPlayer();
	UActorComponent* Component = PC ? PC->FindComponentByInterface(UAnnouncerBackend::StaticClass()) : nullptr;
	Backend = Cast<IAnnouncerBackend>(Component);
	if (Backend)
	{
		BackendObject = Component;
		Backend->OnAnnouncerDisplayChanged().AddUObject(this, &UAnnouncerWidget::HandleDisplayChanged);
		HandleDisplayChanged();
	}
}

void UAnnouncerWidget::BuildTree()
{
	const FBuilder B(WidgetTree);

	UCanvasPanel* Root = B.Canvas();
	WidgetTree->RootWidget = Root;

	Card = B.Canvas();
	UCanvasPanelSlot* CardSlot = Root->AddChildToCanvas(Card);
	CardSlot->SetAnchors(FAnchors(1.f, 0.f));
	CardSlot->SetAlignment(FVector2D(1.f, 0.f));
	CardSlot->SetPosition(FVector2D(-CardRight, CardTop));
	CardSlot->SetSize(FVector2D(CardW, CardH));

	Plate = WidgetTree->ConstructWidget<UAnnouncerPlate>(UAnnouncerPlate::StaticClass());
	Plate->Glass = BackgroundOpacity;
	AddCanvas(Card, Plate, FVector2D::ZeroVector, FVector2D(CardW, CardH));

	// --- 왼쪽 문구(.ann-copy 내용) — x=0 에서 시작(탭이 padding 22 를 margin -22 로 상쇄), 나머지 줄은 22 안쪽 ---
	UVerticalBox* Copy = B.VBox();
	USizeBox* CopySize = B.Size(CopyWidth);
	CopySize->SetContent(Copy);
	CopyBox = CopySize;
	CopySlot = Card->AddChildToCanvas(CopySize);
	CopySlot->SetAutoSize(true);
	CopySlot->SetPosition(FVector2D(0.f, (CardH - CopyMinH) * .5f + CopyPadTop));

	// .ann-kicker: [BUI 탭][탐사 보조 AI] ... [아이콘 코드]
	UHorizontalBox* Kicker = B.HBox();
	AddV(Copy, Kicker);

	USizeBox* Tab = B.Size(-1.f, 24.f);
	Tab->SetMinDesiredWidth(52.f);
	UBorder* TabPad = B.Box(FMargin(20.f, 0.f, 14.f, 0.f));
	TabPad->SetHorizontalAlignment(HAlign_Center);
	TabPad->SetVerticalAlignment(VAlign_Center);
	UTextBlock* Bui = B.Text(FText::FromString(TEXT("BUI")), MonoBold(13.f, .12f - MonoAdvanceDelta), Hex(0xf3fbfb));
	TabPad->SetContent(Bui);
	Tab->SetContent(TabPad);
	TabBox = Tab;
	AddH(Kicker, Tab, FMargin(0.f), VAlign_Center);

	UTextBlock* Small = B.Text(LOCTEXT("Role", "탐사 보조 AI"), Font(EFace::Regular, 11.f), Hex(0xaec3c4));
	AddH(Kicker, Small, FMargin(8.f, 0.f, 0.f, 0.f), VAlign_Center);
	AddH(Kicker, B.Spacer(0.f, 0.f), FMargin(0.f), VAlign_Center, 1.f);

	// .ann-event: svg 15×15 + gap 6 + 코드(750 10px mono .08em)
	USizeBox* IconBox = B.Size(15.f, 15.f);
	UCanvasPanel* IconCanvas = B.Canvas();
	IconBox->SetContent(IconCanvas);
	CodeIcon = WidgetTree->ConstructWidget<UStrokeGlyphWidget>(UStrokeGlyphWidget::StaticClass());
	AddCanvas(IconCanvas, CodeIcon, FVector2D::ZeroVector, FVector2D(15.f, 15.f));
	// 산소 아이콘은 <text x=4 y=28 font 700 21px Arial>O₂ — viewBox 40 → 15px 배율 .375. 한글 글꼴엔 ₂ 가 없어 JetBrains Mono 로 그린다
	UTextBlock* OxygenGlyph = B.Text(FText::FromString(TEXT("O₂")), MonoBold(10.f, -MonoAdvanceDelta), Hex(0x27d9d2));
	CodeOxygen = OxygenGlyph;
	OxygenTexts = { OxygenGlyph };
	UCanvasPanelSlot* OxygenSlot = IconCanvas->AddChildToCanvas(OxygenGlyph);
	OxygenSlot->SetAutoSize(true);
	OxygenSlot->SetPosition(FVector2D(4.f * .375f, 0.f));	// 시안 화면에선 코드 글자(10px)와 같은 크기로 보인다
	AddH(Kicker, IconBox, FMargin(0.f), VAlign_Center);
	CodeText = B.Text(FText::GetEmpty(), MonoBold(10.f, .08f - MonoAdvanceDelta), Hex(0x27d9d2));
	AddH(Kicker, CodeText, FMargin(6.f, 0.f, 0.f, 0.f), VAlign_Center);

	// .ann-headline: margin-top 3, 900 35px/1.05, -.05em, 그림자 0 2px 10px rgba(0,0,0,.7)
	Headline = B.Text(FText::GetEmpty(), Font(EFace::Black, 35.f, -.05f), Hex(0x27d9d2));
	Headline->SetShadowOffset(FVector2D(0.f, 2.f));
	Headline->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, .38f));	// 10px 블러의 겹친 농도 근사
	Typeset(Headline, ERef::Malgun, 35.f * 1.05f, NotoBaseline(35.f, 35.f * 1.05f));
	AddV(Copy, Headline, FMargin(CopyPadLeft, 3.f, 0.f, 0.f));

	// .ann-line: margin 4 0 0, 650 18px/1.35, -.02em, 흰색, 그림자 0 2px 5px #000, keep-all 줄바꿈
	LineText = B.Text(FText::GetEmpty(), Font(EFace::Bold, 18.f, -.02f), FLinearColor::White);
	LineText->SetShadowOffset(FVector2D(0.f, 2.f));
	LineText->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, .6f));
	LineText->SetAutoWrapText(true);
	Typeset(LineText, ERef::Malgun, 18.f * 1.35f, NotoBaseline(18.f, 18.f * 1.35f));
	AddV(Copy, LineText, FMargin(CopyPadLeft, 4.f, 0.f, 0.f));

	// --- 오른쪽 초상 계기창 위 전경: 대체 얼굴, FIELD SUPPORT 라벨 ---
	const float PortraitX = CardW - PortraitBox;

	// .missing-portrait: 88×88, margin-bottom 20, 가운데 — rgba(10,26,30,.92), 1px signal
	UBorder* FallbackFace = B.Fill(FLinearColor(FColor(10, 26, 30, 235)));
	FallbackFace->SetHorizontalAlignment(HAlign_Center);
	FallbackFace->SetVerticalAlignment(VAlign_Center);
	FallbackText = B.Text(FText::FromString(TEXT("● ◡ ●")), Font(EFace::Regular, 22.f), Hex(0x27d9d2));
	FallbackFace->SetContent(FallbackText);
	Fallback = FallbackFace;
	AddCanvas(Card, FallbackFace, FVector2D(PortraitX + (PortraitBox - 88.f) * .5f, PortraitBox - 20.f - 88.f), FVector2D(88.f, 88.f));
	Fallback->SetVisibility(ESlateVisibility::Collapsed);

	// .ann-portrait-label: left 4 bottom 11, padding 3 6, border-left 2px signal, rgba(4,12,16,.9), 700 9px mono .09em
	UHorizontalBox* Label = B.HBox();
	USizeBox* LabelEdge = B.Size(2.f);
	UBorder* LabelEdgeFill = B.Fill(Hex(0x27d9d2));
	LabelEdge->SetContent(LabelEdgeFill);
	LabelBar = LabelEdgeFill;
	AddH(Label, LabelEdge);
	UBorder* LabelFace = B.Fill(FLinearColor(FColor(4, 12, 16, 230)), FMargin(6.f, 3.f));
	LabelText = B.Text(FText::FromString(TEXT("FIELD SUPPORT")), MonoBold(9.f, .09f - MonoAdvanceDelta), Hex(0x27d9d2));
	LabelFace->SetContent(LabelText);
	AddH(Label, LabelFace);
	UCanvasPanelSlot* LabelSlot = Card->AddChildToCanvas(Label);
	LabelSlot->SetAutoSize(true);
	LabelSlot->SetAlignment(FVector2D(0.f, 1.f));
	LabelSlot->SetPosition(FVector2D(PortraitX + 4.f, PortraitBox - 11.f));

	// 초상: img 188×188 object-fit contain, place-items end center, translateY(8) — 밑판이 그린다(그림자·클립 포함)
	PortraitTexture = Portrait.LoadSynchronous();
	Plate->Tab = TabBox.Get();
	Plate->bHasPortrait = PortraitTexture != nullptr && PortraitTexture->GetSizeX() > 0;
	if (Plate->bHasPortrait)
	{
		const float Tw = PortraitTexture->GetSizeX(), Th = PortraitTexture->GetSizeY();
		const float Scale = FMath::Min(188.f / Tw, 188.f / Th);
		const FVector2D Drawn(Tw * Scale, Th * Scale);
		Plate->PortraitBrush.SetResourceObject(PortraitTexture);
		Plate->PortraitBrush.ImageSize = Drawn;
		Plate->PortraitBrush.DrawAs = ESlateBrushDrawType::Image;
		Plate->PortraitSize = Drawn;
		Plate->PortraitPos = FVector2D((PortraitBox - 188.f) * .5f + (188.f - Drawn.X) * .5f, PortraitBox - 188.f + 8.f + (188.f - Drawn.Y) * .5f);
	}
	else
	{
		Fallback->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UAnnouncerWidget::HandleDisplayChanged()
{
	if (!Backend)
	{
		return;
	}
	const FAnnouncerDisplay& Display = Backend->GetAnnouncerDisplay();
	bTargetVisible = Display.bVisible;
	// 표시 중 교체는 즉시(초상 재생성 없음), 퇴장 중이면 내용을 유지한 채 사라진다
	if (Display.bVisible && Display.Serial != ShownSerial)
	{
		ShownSerial = Display.Serial;
		ApplyContent(Display);
	}
}

void UAnnouncerWidget::ApplyContent(const FAnnouncerDisplay& Display)
{
	const FLinearColor Signal = Display.Signal;
	Headline->SetText(Display.Title);
	Headline->SetColorAndOpacity(Signal);
	LineText->SetText(Display.Line);
	CodeText->SetText(Display.Code);
	CodeText->SetColorAndOpacity(Signal);
	LabelText->SetColorAndOpacity(Signal);
	if (UBorder* Bar = Cast<UBorder>(LabelBar))
	{
		Bar->SetBrushColor(Signal);
	}
	FallbackText->SetColorAndOpacity(Signal);

	const bool bOxygen = Display.Icon == EAnnouncerIcon::Oxygen;
	CodeOxygen->SetVisibility(bOxygen ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	for (UTextBlock* Glyph : OxygenTexts)
	{
		Glyph->SetColorAndOpacity(Signal);
	}
	CodeIcon->SetVisibility(bOxygen ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	CodeIcon->SetGlyph(FVector2D(40.f, 40.f), IconPaths(Display.Icon), 2.f);
	CodeIcon->SetStrokeColor(Signal);

	Plate->Signal = Signal;
}

void UAnnouncerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!Backend)
	{
		BindBackend();
	}

	// 패널 높이 = max(142, padding 13 + 내용 + 18), 184 카드 안 세로 가운데(align-items:center)
	const float ContentH = CopyBox ? CopyBox->GetDesiredSize().Y : 0.f;
	const float PanelH = FMath::Max(CopyMinH, CopyPadTop + ContentH + CopyPadBottom);
	const float PanelTop = (CardH - PanelH) * .5f;
	Plate->PanelHeight = PanelH;
	Plate->PanelTop = PanelTop;
	Plate->Glass = BackgroundOpacity;
	if (CopySlot && !FMath::IsNearlyEqual(CopySlot->GetPosition().Y, PanelTop + CopyPadTop))
	{
		CopySlot->SetPosition(FVector2D(0.f, PanelTop + CopyPadTop));
	}

	const float Target = bTargetVisible ? 1.f : 0.f;
	if (Progress != Target)
	{
		const float Step = InDeltaTime / MotionSeconds;
		Progress = Target > Progress ? FMath::Min(Target, Progress + Step) : FMath::Max(Target, Progress - Step);
		ApplyMotion(Ease.Eval(Progress));
	}
}

void UAnnouncerWidget::ApplyMotion(float Eased)
{
	// 진입 opacity 0→1 · Y -8→0, 퇴장은 그 역(180ms ease) — 밑판·문구·초상이 함께 움직인다
	SetRenderOpacity(Eased);
	SetRenderTranslation(FVector2D(0.f, -EnterShift * (1.f - Eased)));
	if (Card)
	{
		Card->SetVisibility(Eased > 0.f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

#undef LOCTEXT_NAMESPACE
