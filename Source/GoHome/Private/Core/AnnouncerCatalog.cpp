#include "Core/AnnouncerCatalog.h"

#define LOCTEXT_NAMESPACE "Announcer"

namespace
{
	// 시안 상태 신호색(sRGB)
	FLinearColor Teal() { return FLinearColor(FColor(0x27, 0xd9, 0xd2)); }		// 정상 통신
	FLinearColor Gold() { return FLinearColor(FColor(0xf0, 0xb9, 0x4e)); }		// 귀환·산소 주의
	FLinearColor Red() { return FLinearColor(FColor(0xef, 0x51, 0x53)); }		// 산소 위험·피해
	FLinearColor Crimson() { return FLinearColor(FColor(0xf0, 0x44, 0x68)); }	// 시간 임박·즉시 회피

	FAnnouncerLineDef Def(const FText& Title, const FText& Line, const TCHAR* Code, const FLinearColor& Signal, EAnnouncerIcon Icon,
		int32 Priority, EAnnouncerScope Scope, float Ttl, float Cooldown = 0.f)
	{
		FAnnouncerLineDef Out;
		Out.Title = Title;
		Out.Line = Line;
		Out.Code = FText::FromString(Code);
		Out.Signal = Signal;
		Out.Icon = Icon;
		Out.Priority = Priority;
		Out.Scope = Scope;
		Out.TtlSeconds = Ttl;
		Out.ServerCooldownSeconds = Cooldown;
		return Out;
	}

	struct FCatalog
	{
		FAnnouncerLineDef Lines[(int32)EAnnouncerEvent::Count];
		FAnnouncerLineDef ReturnTime[3];	// 60/30/10 — 귀환 절차 중 변형
		FAnnouncerLineDef Empty;

		FCatalog()
		{
			using E = EAnnouncerEvent;
			using S = EAnnouncerScope;
			using I = EAnnouncerIcon;

			Lines[(int32)E::ExpeditionStart] = Def(LOCTEXT("StartTitle", "통신 양호!"), LOCTEXT("StartLine", "준비됐으면 탐사를 시작하자."),
				TEXT("LINK / CLEAR"), Teal(), I::Signal, 60, S::Survivor, 10.f);
			Lines[(int32)E::ReturnStarted] = Def(LOCTEXT("ReturnTitle", "귀환 절차"), LOCTEXT("ReturnLine", "시작됐어. 잠수정으로 복귀해."),
				TEXT("RETURN / HOLD"), Gold(), I::Return, 80, S::Survivor, 6.f);

			Lines[(int32)E::TimeWarning60] = Def(LOCTEXT("Time60Title", "제한 시간 1분"), LOCTEXT("Time60Line", "이제 돌아갈 준비를 해."),
				TEXT("TIME / 01:00"), Gold(), I::Clock, 60, S::Team, 8.f);
			Lines[(int32)E::TimeWarning30] = Def(LOCTEXT("Time30Title", "제한 시간 30초"), LOCTEXT("Time30Line", "시간이 얼마 안 남았어! 어서 복귀해."),
				TEXT("TIME / 00:30"), Crimson(), I::Clock, 80, S::Team, 6.f);
			Lines[(int32)E::TimeWarning10] = Def(LOCTEXT("Time10Title", "제한 시간 10초"), LOCTEXT("Time10Line", "지금 잠수정으로 복귀해!"),
				TEXT("TIME / 00:10"), Crimson(), I::Clock, 100, S::Team, 4.f);

			// 귀환 버튼을 눌렀어도 시간 만료는 실패다 — 안심시키는 문구 대신 진행 중임만 알린다
			ReturnTime[0] = Lines[(int32)E::TimeWarning60];
			ReturnTime[0].Line = LOCTEXT("Time60ReturnLine", "귀환 절차 진행 중. 잠수정 안에서 기다려.");
			ReturnTime[1] = Lines[(int32)E::TimeWarning30];
			ReturnTime[1].Line = LOCTEXT("Time30ReturnLine", "귀환 절차 진행 중. 잠수정 안에 있어야 해.");
			ReturnTime[2] = Lines[(int32)E::TimeWarning10];
			ReturnTime[2].Line = LOCTEXT("Time10ReturnLine", "귀환 절차 진행 중. 제한 시간이 얼마 없어!");

			Lines[(int32)E::OxygenLow] = Def(LOCTEXT("O2LowTitle", "산소 잔량 낮음"), LOCTEXT("O2LowLine", "귀환 경로를 확인해."),
				TEXT("O₂ / RESERVE"), Gold(), I::Oxygen, 80, S::Personal, 5.f);
			Lines[(int32)E::OxygenCritical] = Def(LOCTEXT("O2CritTitle", "산소 위험"), LOCTEXT("O2CritLine", "거의 바닥이야. 지금 복귀해!"),
				TEXT("O₂ / CRITICAL"), Red(), I::Oxygen, 100, S::Personal, 5.f);

			Lines[(int32)E::ArtifactFirst] = Def(LOCTEXT("FirstArtifactTitle", "첫 유물 확보!"), LOCTEXT("FirstArtifactLine", "좋아, 무사히 가져가자."),
				TEXT("SALVAGE / 01"), Teal(), I::Salvage, 20, S::Personal, 4.f);
			Lines[(int32)E::Artifact] = Def(LOCTEXT("ArtifactTitle", "유물 확보"), LOCTEXT("ArtifactLine", "조심히 가져가자."),
				TEXT("SALVAGE / OK"), Teal(), I::Salvage, 20, S::Personal, 4.f, 30.f);

			Lines[(int32)E::Attacked] = Def(LOCTEXT("AttackedTitle", "공격받고 있어!"), LOCTEXT("AttackedLine", "거리를 벌려!"),
				TEXT("HULL / IMPACT"), Red(), I::Hazard, 80, S::Personal, 3.f, 15.f);
			Lines[(int32)E::Damaged] = Def(LOCTEXT("DamagedTitle", "피해 발생"), LOCTEXT("DamagedLine", "주변을 확인해!"),
				TEXT("HULL / DAMAGE"), Red(), I::Hazard, 80, S::Personal, 3.f, 15.f);

			// 열수 예고는 0.8초 — 짧은 문구를 즉시 띄우고 기존 월드 VFX 를 그대로 둔다
			Lines[(int32)E::VentWarning] = Def(LOCTEXT("VentTitle", "분출 경고"), LOCTEXT("VentLine", "분출구에서 떨어져!"),
				TEXT("VENT / ERUPT"), Crimson(), I::Hazard, 100, S::Personal, 2.f, 8.f);
			Lines[(int32)E::MineArmed] = Def(LOCTEXT("MineTitle", "기뢰 작동!"), LOCTEXT("MineLine", "거리를 벌려!"),
				TEXT("MINE / ARMED"), Crimson(), I::Hazard, 100, S::Personal, 3.f);
		}
	};

	const FCatalog& Catalog()
	{
		static const FCatalog Instance;
		return Instance;
	}
}

const FAnnouncerLineDef& AnnouncerCatalog::Get(EAnnouncerEvent Event, bool bReturnInProgress)
{
	const FCatalog& C = Catalog();
	if (bReturnInProgress)
	{
		switch (Event)
		{
		case EAnnouncerEvent::TimeWarning60: return C.ReturnTime[0];
		case EAnnouncerEvent::TimeWarning30: return C.ReturnTime[1];
		case EAnnouncerEvent::TimeWarning10: return C.ReturnTime[2];
		default: break;
		}
	}
	const int32 Index = (int32)Event;
	return Index > 0 && Index < (int32)EAnnouncerEvent::Count ? C.Lines[Index] : C.Empty;
}

EAnnouncerEvent AnnouncerCatalog::FromName(const FString& Name)
{
	static const TPair<const TCHAR*, EAnnouncerEvent> Names[] = {
		{ TEXT("Start"), EAnnouncerEvent::ExpeditionStart },
		{ TEXT("Return"), EAnnouncerEvent::ReturnStarted },
		{ TEXT("Time60"), EAnnouncerEvent::TimeWarning60 },
		{ TEXT("Time30"), EAnnouncerEvent::TimeWarning30 },
		{ TEXT("Time10"), EAnnouncerEvent::TimeWarning10 },
		{ TEXT("O2Low"), EAnnouncerEvent::OxygenLow },
		{ TEXT("O2Crit"), EAnnouncerEvent::OxygenCritical },
		{ TEXT("FirstArtifact"), EAnnouncerEvent::ArtifactFirst },
		{ TEXT("Artifact"), EAnnouncerEvent::Artifact },
		{ TEXT("Attacked"), EAnnouncerEvent::Attacked },
		{ TEXT("Damaged"), EAnnouncerEvent::Damaged },
		{ TEXT("Vent"), EAnnouncerEvent::VentWarning },
		{ TEXT("Mine"), EAnnouncerEvent::MineArmed },
	};
	for (const TPair<const TCHAR*, EAnnouncerEvent>& Pair : Names)
	{
		if (Name.Equals(Pair.Key, ESearchCase::IgnoreCase))
		{
			return Pair.Value;
		}
	}
	return EAnnouncerEvent::None;
}

#undef LOCTEXT_NAMESPACE
