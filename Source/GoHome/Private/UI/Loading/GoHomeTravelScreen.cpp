#include "UI/Loading/GoHomeTravelScreen.h"

#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "UI/Supply/SupplyUIStyle.h"

#define LOCTEXT_NAMESPACE "GoHomeTravel"

namespace TravelCanvas
{
	constexpr uint32 Ink = 0x080f20, Navy = 0x112547, Blue = 0x2747d8, Paper = 0xf0f1de, Lime = 0xdafa65;
	float Mod(float X, float N) { return FMath::Fmod(FMath::Fmod(X, N) + N, N); }
	float Ease(float T) { return 1.f - FMath::Pow(1.f - FMath::Clamp(T, 0.f, 1.f), 3.f); }

	struct FState
	{
		FVector2f X = FVector2f(1, 0), Y = FVector2f(0, 1), O = FVector2f(0, 0);
		float Alpha = 1.f;
		FVector2f Point(float A, float B) const { return O + X * A + Y * B; }
	};

	/** Canvas 순서를 보존한다. 텍스트 앞에서 메쉬를 flush하고 이후 도형을 위에 그린다. */
	class FCanvas
	{
	public:
		FCanvas(const FGeometry& InGeometry, FSlateWindowElementList& InOut, int32 InLayer, const FSlateBrush& Brush)
			: Geometry(InGeometry), Out(InOut), Layer(InLayer), Handle(FSlateApplication::Get().GetRenderer()->GetResourceHandle(Brush)) {}
		~FCanvas() { Flush(); }
		FState State;
		TArray<FState> Stack;
		const FGeometry& Geometry;
		FSlateWindowElementList& Out;
		int32 Layer;
		FSlateResourceHandle Handle;
		TArray<FSlateVertex> Verts;
		TArray<SlateIndex> Indices;
		void Save() { Stack.Add(State); }
		void Restore() { State = Stack.Pop(); }
		void Translate(float X, float Y) { State.O = State.Point(X, Y); }
		void Scale(float X, float Y) { State.X *= X; State.Y *= Y; }
		void Rotate(float A)
		{
			const FVector2f X = State.X, Y = State.Y;
			State.X = X * FMath::Cos(A) + Y * FMath::Sin(A);
			State.Y = Y * FMath::Cos(A) - X * FMath::Sin(A);
		}
		void Shear(float X) { State.Y += State.X * X; }
		void Flush()
		{
			if (Indices.Num())
			{
				FSlateDrawElement::MakeCustomVerts(Out, Layer++, Handle, Verts, Indices, nullptr, 0, 0);
				Verts.Reset(); Indices.Reset();
			}
		}
		SlateIndex Vertex(FVector2f P, uint32 RGB, float Alpha)
		{
			const FColor Color((RGB >> 16) & 255, (RGB >> 8) & 255, RGB & 255,
				FMath::RoundToInt(255.f * FMath::Clamp(Alpha * State.Alpha, 0.f, 1.f)));
			Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Geometry.GetAccumulatedRenderTransform(), P, FVector2f(.5f, .5f), Color));
			return static_cast<SlateIndex>(Verts.Num() - 1);
		}
		void Triangle(SlateIndex A, SlateIndex B, SlateIndex C) { Indices.Append({A, B, C}); }
		void Poly(std::initializer_list<FVector2f> Points, uint32 Color) { Polygon(TArray<FVector2f>(Points), Color); }
		void Polygon(const TArray<FVector2f>& Points, uint32 Color)
		{
			// Ear clipping also handles the sub's concave tail; a centroid fan would fill outside it.
			TArray<FVector2f> P; TArray<int32> Remaining;
			float Area = 0.f;
			for (int32 I = 0; I < Points.Num(); ++I) { P.Add(State.Point(Points[I].X, Points[I].Y)); Remaining.Add(I); }
			for (int32 I = 0; I < P.Num(); ++I) Area += FVector2f::CrossProduct(P[I], P[(I + 1) % P.Num()]);
			const float Sign = Area >= 0.f ? 1.f : -1.f;
			const SlateIndex Base = static_cast<SlateIndex>(Verts.Num());
			for (const auto& V : P) Vertex(V, Color, 1.f);
			while (Remaining.Num() > 2)
			{
				bool bClipped = false;
				for (int32 I = 0; I < Remaining.Num(); ++I)
				{
					const int32 A = Remaining[(I + Remaining.Num() - 1) % Remaining.Num()], B = Remaining[I], C = Remaining[(I + 1) % Remaining.Num()];
					if (Sign * FVector2f::CrossProduct(P[B] - P[A], P[C] - P[B]) <= .0001f) continue;
					bool bInside = false;
					for (int32 J : Remaining)
					{
						if (J == A || J == B || J == C) continue;
						if (Sign * FVector2f::CrossProduct(P[B] - P[A], P[J] - P[A]) >= 0.f &&
							Sign * FVector2f::CrossProduct(P[C] - P[B], P[J] - P[B]) >= 0.f &&
							Sign * FVector2f::CrossProduct(P[A] - P[C], P[J] - P[C]) >= 0.f) { bInside = true; break; }
					}
					if (!bInside) { Triangle(Base + A, Base + B, Base + C); Remaining.RemoveAt(I); bClipped = true; break; }
				}
				if (!bClipped) break;
			}
			// One physical pixel AA fringe, centred on the original Canvas edge.
			const float Feather = .5f / FMath::Max(Geometry.GetAccumulatedLayoutTransform().GetScale(), .01f);
			for (int32 I = 0; I < P.Num(); ++I)
			{
				const FVector2f A = P[I], B = P[(I + 1) % P.Num()];
				const FVector2f N = FVector2f(B.Y - A.Y, A.X - B.X).GetSafeNormal() * Sign * Feather;
				const SlateIndex V = Vertex(A, Color, 1.f); Vertex(B, Color, 1.f); Vertex(B + N, Color, 0.f); Vertex(A + N, Color, 0.f);
				Triangle(V, V + 1, V + 2); Triangle(V, V + 2, V + 3);
			}
		}
		void Rect(float X, float Y, float W, float H, uint32 C) { Poly({{X,Y},{X+W,Y},{X+W,Y+H},{X,Y+H}}, C); }
		void Line(float X, float Y, float X2, float Y2, uint32 C, float Width = 1.f)
		{
			const FVector2f N = FVector2f(Y2-Y, X-X2).GetSafeNormal() * Width * .5f;
			Poly({FVector2f(X,Y)+N,FVector2f(X2,Y2)+N,FVector2f(X2,Y2)-N,FVector2f(X,Y)-N}, C);
		}
		void Ellipse(float X, float Y, float RX, float RY, uint32 C)
		{
			TArray<FVector2f> P;
			for (int32 I = 0; I < 48; ++I) { const float A = 2.f * PI * I / 48.f; P.Add(FVector2f(X + RX * FMath::Cos(A), Y + RY * FMath::Sin(A))); }
			Polygon(P, C);
		}
		void Arrow(float X, float Y, float ScaleFactor, int32 D, uint32 C)
		{
			Save(); Translate(X,Y); Scale(ScaleFactor,ScaleFactor*D);
			Line(0,-25,0,25,C,5); Line(-15,10,0,25,C,5); Line(15,10,0,25,C,5); Restore();
		}
		float Text(const FText& Value, float X, float Baseline, const FSlateFontInfo& Font, uint32 C)
		{
			Flush();
			const auto Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			const FVector2D Size = Measure->Measure(Value, Font);
			// HTML의 Malgun Bold는 한글 advance=1em, space=720/2048em이다.
			// 배포 가능한 Noto 대체의 advance 차이를 이 화면의 run 폭에서 보정한다.
			float ReferenceWidth=0.f;
			bool bKoreanRun=true;
			for (TCHAR Ch : Value.ToString())
			{
				if (Ch>=0xac00 && Ch<=0xd7af) ReferenceWidth+=Font.Size/.75f;
				else if (Ch==TEXT(' ')) ReferenceWidth+=Font.Size/.75f*(720.f/2048.f);
				else { bKoreanRun=false; break; }
			}
			const float RunScale=bKoreanRun && Size.X>0.f?ReferenceWidth/Size.X:1.f;
			const float Top = Baseline - Measure->GetMaxCharacterHeight(Font) - Measure->GetBaseline(Font);
			const FVector2f O = State.Point(X, Top);
			const FSlateRenderTransform Transform(FMatrix2x2(State.X.X*RunScale,State.X.Y*RunScale,State.Y.X,State.Y.Y), O);
			const FGeometry TextGeometry = Geometry.MakeChild(Size, FSlateLayoutTransform(), Transform, FVector2D::ZeroVector);
			FLinearColor Color = SupplyUI::Hex(C, State.Alpha);
			FSlateDrawElement::MakeText(Out, Layer++, TextGeometry.ToPaintGeometry(), Value, Font, ESlateDrawEffect::None, Color);
			return bKoreanRun?ReferenceWidth:Size.X;
		}
	};

	void Sub(FCanvas& C, float X, float Y, float Angle)
	{
		C.Save(); C.Translate(X,Y); C.Rotate(Angle);
		C.Poly({{-222,27},{-278,64},{-292,55},{-266,-4},{-226,-12}},Paper);
		C.Rect(-280,-48,22,91,Ink); C.Rect(-274,-51,9,38,Lime); C.Rect(-274,8,9,40,Lime);
		C.Poly({{-86,-75},{-66,-131},{36,-131},{77,-68}},Ink);
		C.Poly({{-72,-80},{-57,-118},{26,-118},{56,-80}},Paper);
		C.Rect(-16,-170,12,45,Ink); C.Rect(-18,-174,59,12,Ink); C.Rect(32,-174,12,24,Lime);
		C.Poly({{-231,-51},{-182,-85},{137,-85},{218,-41},{244,18},{207,71},{-175,71},{-228,40}},Ink);
		C.Poly({{-212,-42},{-177,-68},{131,-68},{204,-28},{225,17},{194,54},{-169,54},{-212,29}},Paper);
		C.Poly({{-204,24},{216,10},{194,54},{-169,54},{-204,34}},0xbcc6c4);
		C.Poly({{-179,54},{192,54},{176,76},{-145,76}},Ink);
		C.Poly({{-180,-66},{-104,-66},{-104,52},{-169,52},{-211,27},{-211,-39}},Blue);
		C.Line(-94,-56,-94,43,Ink,3); C.Line(107,-51,131,42,Ink,3);
		for(int32 I=0; I<3; ++I) { const float PX=-48+I*60; C.Ellipse(PX,-17,24,25,Ink); C.Ellipse(PX,-17,16,17,Blue); C.Ellipse(PX-4,-22,7,7,Lime); C.Line(PX-9,-6,PX+8,-25,Paper,2); }
		C.Poly({{139,-43},{188,-20},{205,11},{152,11}},Ink); C.Poly({{150,-30},{179,-15},{188,0},{157,0}},Lime);
		C.Rect(-64,23,116,7,Ink); C.Rect(-64,34,74,3,Ink);
		C.Rect(-143,72,24,18,Ink); C.Rect(118,72,24,18,Ink); C.Rect(-169,87,347,9,Paper);
		for (float PX : {-172.f,-117.f,106.f,169.f}) C.Ellipse(PX,-56,2.3f,2.3f,Ink);
		C.Ellipse(219,22,7,9,Lime); C.Restore();
	}
}

void SGoHomeTravelScreen::Construct(const FArguments& Args)
{
	Presentation = Args._Presentation;
	WhiteBrush.SetResourceObject(Args._WhiteTexture);
	WhiteBrush.ImageSize = FVector2D(1,1);
	WhiteBrush.DrawAs = ESlateBrushDrawType::Image;
	// Malgun 700–900 maps to a single bold face in the HTML; use the existing matching SemiBold face.
	TitleDownFont = SupplyUI::Font(SupplyUI::EFace::SemiBold,62);
	TitleUpFont = SupplyUI::Font(SupplyUI::EFace::SemiBold,48);
	StatusFont = SupplyUI::Font(SupplyUI::EFace::SemiBold,19);
	SetCanTick(false);
	SetVisibility(EVisibility::Visible);
}

int32 SGoHomeTravelScreen::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle&, bool) const
{
	using namespace TravelCanvas;
	if (!Presentation) return Layer;
	TravelCanvas::FCanvas Back(G,Out,Layer,WhiteBrush); Back.Rect(0,0,G.GetLocalSize().X,G.GetLocalSize().Y,TravelCanvas::Ink); Back.Flush();
	const float ScaleFactor = FMath::Min(G.GetLocalSize().X/1600.f,G.GetLocalSize().Y/900.f);
	const FVector2D Offset = (G.GetLocalSize()-FVector2D(1600,900)*ScaleFactor)*.5;
	const FGeometry Stage = G.MakeChild(FVector2D(1600,900),FSlateLayoutTransform(ScaleFactor,Offset));
	Out.PushClip(FSlateClippingZone(Stage));
	TravelCanvas::FCanvas C(Stage,Out,Back.Layer,WhiteBrush);
	const FTravelPresentation& F=*Presentation;
	const float T=F.Elapsed, M=F.MotionTime; const int32 D=F.Direction;
	C.Rect(0,0,1600,900,TravelCanvas::Ink);
	if (F.Phase == ETravelPresentationPhase::Covered) { C.Flush(); Presentation->bCoverPainted=true; Out.PopClip(); return C.Layer; }
	C.Poly({{905,0},{1480,0},{1250,900},{675,900}},TravelCanvas::Navy);
	C.Poly({{1068,0},{1410,0},{1168,900},{826,900}},TravelCanvas::Blue);
	C.Poly({{1376,0},{1440,0},{1200,900},{1170,900}},0x3859e3);
	C.State.Alpha=.09f; for(int32 I=0; I<22; ++I) C.Line(688+I*38,0,453+I*38,900,TravelCanvas::Paper); C.State.Alpha=1;
	const float Drift=M*D;
	for(int32 I=0; I<19; ++I) { const float Y=Mod(I*62-Drift*67,1150)-120; C.Line(1347,Y,1373+(I%4==0?26:0),Y,0x6980bb,I%4==0?2:1); }
	for(int32 I=0; I<39; ++I) { const float X=735+Mod(I*137,703),Y=Mod(I*191-Drift*(39+(I%4)*18),1080)-90; C.State.Alpha=.2f+(I%3)*.12f; C.Rect(X,Y,I%3==0?3:2,I%5==0?32:5,TravelCanvas::Paper); } C.State.Alpha=1;
	const float BeamY=438+FMath::Sin(M*1.2f)*5;
	C.State.Alpha=.09f; C.Poly({{1195,BeamY},{1570,BeamY-45},{1570,BeamY+110}},TravelCanvas::Lime); C.State.Alpha=1;
	C.Save(); C.State.Alpha=F.bReducedMotion?1:Ease((T-.13f)/.54f); C.Translate(-42*(1-C.State.Alpha),0);
	for(int32 I=0;I<3;++I) C.Poly({{72.f+I*19,307},{84.f+I*19,307},{76.f+I*19,323},{64.f+I*19,323}},I==2?TravelCanvas::Lime:TravelCanvas::Blue);
	C.Line(137,315,540,315,0x344867);
	C.Save(); C.Translate(73,397); C.Shear(-.12f);
	const FText Lead=D==1?LOCTEXT("DownLead","미지의 "):LOCTEXT("UpLead","탐사를 마치고 ");
	const FText Verb=D==1?LOCTEXT("DownVerb","심해로"):LOCTEXT("UpVerb","귀환");
	const FText Full=D==1?LOCTEXT("DownTitle","미지의 심해로"):LOCTEXT("UpTitle","탐사를 마치고 귀환");
	const FSlateFontInfo& Font=D==1?TitleDownFont:TitleUpFont;
	C.Text(Full,3,5,Font,TravelCanvas::Blue);
	const float LeadWidth=C.Text(Lead,0,0,Font,TravelCanvas::Paper),TitleWidth=LeadWidth+C.Text(Verb,LeadWidth,0,Font,TravelCanvas::Lime);
	C.Line(0,19,TitleWidth,19,0x526687);
	C.Poly({{LeadWidth,16},{TitleWidth+4,16},{TitleWidth+1,22},{LeadWidth-3,22}},TravelCanvas::Lime); C.Restore();
	C.Poly({{72,440},{124,440},{111,502},{59,502}},TravelCanvas::Blue); C.Arrow(92,471,.58f,D,TravelCanvas::Paper); C.Restore();
	float SubY=450;
	if(!F.bReducedMotion) { SubY+=D*(-460*(1-Ease(T/.94f))+FMath::Sin(M*1.35f)*8); if(F.Phase==ETravelPresentationPhase::Exit) SubY+=D*760*FMath::Square(FMath::Clamp((T-F.ExitStartedAt)/FTravelPresentation::ExitSeconds,0.f,1.f)); }
	Sub(C,1002,SubY,F.bReducedMotion?0:D*.055f+FMath::Sin(M*.9f)*.008f);
	if(!F.bReducedMotion && F.Phase!=ETravelPresentationPhase::Error) { for(int32 I=0;I<3;++I) { C.State.Alpha=.25f+I*.23f; C.Arrow(1045,611+I*48+D*Mod(M*24,48),.62f,D,TravelCanvas::Lime); } C.State.Alpha=1; } else C.Arrow(1045,660,.7f,D,TravelCanvas::Lime);
	const bool bError=F.Phase==ETravelPresentationPhase::Error; const uint32 Accent=bError?0xffb89c:TravelCanvas::Lime;
	C.Poly({{69,763},{569,763},{549,862},{49,862}},TravelCanvas::Navy); C.Poly({{69,763},{76,763},{56,862},{49,862}},TravelCanvas::Blue);
	C.Poly({{93,786},{101,786},{97,796},{89,796}},Accent);
	C.Text(bError?LOCTEXT("Error","연결 오류"):F.bReady?LOCTEXT("Ready","도착 준비 중"):LOCTEXT("Loading","이동 중"),113,800,StatusFont,bError?Accent:TravelCanvas::Paper);
	C.Line(90,814,521,814,0x42577c);
	const int32 Head=F.bReducedMotion?7:static_cast<int32>(Mod(FMath::FloorToFloat(M*8)*D,16));
	for(int32 I=0;I<16;++I) { const float X=94+I*27,Trail=Mod((Head-I)*D,16); const uint32 Color=bError?(I==0?Accent:0x443b3d):Trail==0?TravelCanvas::Paper:Trail<3?Accent:Trail<5?TravelCanvas::Blue:0x293d60; C.Poly({{X,824},{X+20,824},{X+16,840},{X-4,840}},Color); }
	C.Line(532,822,528,842,Accent,2);
	if(F.Phase==ETravelPresentationPhase::Enter) { const float K=FMath::Clamp(T/(F.bReducedMotion?.1f:FTravelPresentation::EnterSeconds),0.f,1.f); if(F.bReducedMotion) { C.State.Alpha=1-K; C.Rect(0,0,1600,900,TravelCanvas::Ink); C.State.Alpha=1; } else if(K<1) { const float O=D*Ease(K)*1950; C.Poly({{-140,O-260},{1740,O-660},{1740,O+1050},{-140,O+1450}},TravelCanvas::Blue); C.Poly({{-140,O-300},{1740,O-700},{1740,O+1000},{-140,O+1400}},TravelCanvas::Ink); } }
	if(F.Phase==ETravelPresentationPhase::Exit) { const float P=FMath::Clamp((T-F.ExitStartedAt)/(F.bReducedMotion?.1f:FTravelPresentation::ExitSeconds),0.f,1.f); C.State.Alpha=F.bReducedMotion?P:FMath::Clamp((P-.65f)/.35f,0.f,1.f); C.Rect(0,0,1600,900,TravelCanvas::Ink); C.State.Alpha=1; }
	C.Flush(); Out.PopClip(); return C.Layer;
}

#undef LOCTEXT_NAMESPACE
