#include "ShipSystemsComponent.h"
#include "ScienceScannerComponent.h"
#include "StarshipMovementComponent.h"
#include "StarshipPawn.h"
#include "StarshipWeaponsComponent.h"
#include "WarpDriveComponent.h"

UShipSystemsComponent::UShipSystemsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	Systems.SetNum((int32)EShipSystem::Count);
	Officers = {
		{ TEXT("Lt. Cmdr."), TEXT("Rhys Dahl"), EStation::Engineering },
		{ TEXT("Lt."), TEXT("Sorek"), EStation::Tactical },
		{ TEXT("Cmdr."), TEXT("T'Lara"), EStation::Science },
		{ TEXT("Lt."), TEXT("Imani Okonkwo"), EStation::Helm },
		{ TEXT("Lt."), TEXT("Pell Haskins"), EStation::Operations },
		{ TEXT("Dr."), TEXT("Elena Vasquez"), EStation::Medical },
		{ TEXT("Lt."), TEXT("Vreen"), EStation::Security },
	};
}

AStarshipPawn* UShipSystemsComponent::Ship() const { return Cast<AStarshipPawn>(GetOwner()); }

FString UShipSystemsComponent::SystemName(EShipSystem S)
{
	switch (S)
	{
	case EShipSystem::Shields: return TEXT("Shields");
	case EShipSystem::Phasers: return TEXT("Phasers");
	case EShipSystem::Torpedoes: return TEXT("Torpedo launchers");
	case EShipSystem::Warp: return TEXT("Warp drive");
	case EShipSystem::Impulse: return TEXT("Impulse engines");
	case EShipSystem::Sensors: return TEXT("Sensors");
	case EShipSystem::LifeSupport: return TEXT("Life support");
	default: return TEXT("?");
	}
}

FString UShipSystemsComponent::StationName(EStation S)
{
	switch (S)
	{
	case EStation::Engineering: return TEXT("Engineering");
	case EStation::Tactical: return TEXT("Tactical");
	case EStation::Science: return TEXT("Science");
	case EStation::Helm: return TEXT("Helm");
	case EStation::Operations: return TEXT("Operations");
	case EStation::Medical: return TEXT("Sickbay");
	case EStation::Security: return TEXT("Security");
	default: return TEXT("?");
	}
}

FString UShipSystemsComponent::AlertName(EAlertState A)
{
	return A == EAlertState::Red ? TEXT("RED ALERT") : A == EAlertState::Yellow ? TEXT("YELLOW ALERT") : TEXT("CONDITION GREEN");
}

bool UShipSystemsComponent::ParseSystem(const FString& Word, EShipSystem& Out)
{
	const FString W = Word.ToLower();
	if (W.StartsWith(TEXT("shield"))) { Out = EShipSystem::Shields; return true; }
	if (W.StartsWith(TEXT("phaser")) || W == TEXT("weapons")) { Out = EShipSystem::Phasers; return true; }
	if (W.StartsWith(TEXT("torp")) || W.StartsWith(TEXT("launcher"))) { Out = EShipSystem::Torpedoes; return true; }
	if (W.StartsWith(TEXT("warp"))) { Out = EShipSystem::Warp; return true; }
	if (W.StartsWith(TEXT("impulse")) || W == TEXT("engines")) { Out = EShipSystem::Impulse; return true; }
	if (W.StartsWith(TEXT("sensor"))) { Out = EShipSystem::Sensors; return true; }
	if (W.StartsWith(TEXT("life"))) { Out = EShipSystem::LifeSupport; return true; }
	return false;
}

const FOfficer* UShipSystemsComponent::OfficerAt(EStation Station) const
{
	for (const FOfficer& O : Officers) { if (O.Station == Station) { return &O; } }
	return nullptr;
}

void UShipSystemsComponent::Report(EStation From, const FString& Text, const FLinearColor& Color)
{
	const FOfficer* O = OfficerAt(From);
	const FString Speaker = O ? FString::Printf(TEXT("%s %s"), *O->Rank, *O->Name) : StationName(From);
	FBridgeLogEntry E; E.Time = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f; E.Speaker = Speaker; E.Text = Text; E.Color = Color;
	Log.Add(E);
	if (Log.Num() > 200) { Log.RemoveAt(0, Log.Num() - 200); }
	if (AStarshipPawn* S = Ship()) { S->ScreenMessage(FString::Printf(TEXT("%s: %s"), *Speaker, *Text), Color.ToFColor(true), 5.f); }
}

void UShipSystemsComponent::BeginPlay()
{
	Super::BeginPlay();
	CacheBase();
}

void UShipSystemsComponent::CacheBase()
{
	AStarshipPawn* S = Ship();
	if (!S || bBaseCached) { return; }
	BaseMaxShield = S->MaxShieldStrength; BaseShieldRecharge = S->ShieldRechargePerSecond;
	if (S->Weapons) { BasePhaserDps = S->Weapons->PhaserDamagePerSecond; BasePhaserRecharge = S->Weapons->PhaserRechargeSeconds; BaseTorpedoCooldown = S->Weapons->TorpedoCooldown; }
	if (S->WarpDrive) { BaseMaxWarp = S->WarpDrive->MaxWarpFactor; }
	if (S->Movement) { BaseImpulse = S->Movement->FullImpulseSpeed; }
	if (S->Scanner) { BaseScanRadii = S->Scanner->WorldRangeRadii; }
	bBaseCached = true;
}

float UShipSystemsComponent::Effectiveness(EShipSystem System) const
{
	const FShipSystemState& St = Systems[(int32)System];
	// Power helps a working system more than a broken one; nothing runs on no power.
	float E = St.Integrity * FMath::Clamp(0.4f + 0.6f * St.Power, 0.f, 1.5f);
	if (System == EShipSystem::Shields) { E *= Alert == EAlertState::Red ? 1.15f : Alert == EAlertState::Yellow ? 1.f : 0.85f; }
	return E;
}

void UShipSystemsComponent::ApplyEffects()
{
	AStarshipPawn* S = Ship();
	if (!S) { return; }
	CacheBase();
	const float Sh = Effectiveness(EShipSystem::Shields);
	S->MaxShieldStrength = BaseMaxShield * FMath::Max(Sh, 0.05f);
	S->ShieldStrength = FMath::Min(S->ShieldStrength, S->MaxShieldStrength);
	S->ShieldRechargePerSecond = BaseShieldRecharge * Sh;
	if (S->Weapons)
	{
		const float Ph = Effectiveness(EShipSystem::Phasers);
		S->Weapons->PhaserDamagePerSecond = BasePhaserDps * Ph;
		S->Weapons->PhaserRechargeSeconds = BasePhaserRecharge / FMath::Max(Ph, 0.1f);
		S->Weapons->TorpedoCooldown = BaseTorpedoCooldown / FMath::Max(Effectiveness(EShipSystem::Torpedoes), 0.1f);
	}
	if (S->WarpDrive)
	{
		// Warp holds its top factor until the drive is well hurt, then falls off fast: 9.99 down to 1 at 30 % integrity.
		const float W = Systems[(int32)EShipSystem::Warp].Integrity;
		S->WarpDrive->MaxWarpFactor = FMath::Clamp(FMath::Lerp(1.f, BaseMaxWarp, FMath::Clamp((W - 0.3f) / 0.5f, 0.f, 1.f)) * FMath::Clamp(0.6f + 0.4f * Systems[(int32)EShipSystem::Warp].Power, 0.5f, 1.f), 1.f, BaseMaxWarp);
	}
	if (S->Movement) { S->Movement->FullImpulseSpeed = BaseImpulse * FMath::Max(Effectiveness(EShipSystem::Impulse), 0.1f); }
	if (S->Scanner) { S->Scanner->WorldRangeRadii = BaseScanRadii * FMath::Max(Effectiveness(EShipSystem::Sensors), 0.15f); }
}

void UShipSystemsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	TickRepairs(DeltaTime);
	ApplyEffects();
	for (float& C : ReportCooldown) { C = FMath::Max(0.f, C - DeltaTime); }
}

void UShipSystemsComponent::TickRepairs(float DeltaTime)
{
	// The chief's pick: the worst system, shields and warp first when tied.
	EShipSystem Target = RepairPriority;
	if (Target == EShipSystem::Count || Systems[(int32)Target].Integrity >= 1.f)
	{
		float Worst = 1.f;
		for (int32 i = 0; i < (int32)EShipSystem::Count; ++i)
		{
			if (Systems[i].Integrity < Worst - 1e-3f) { Worst = Systems[i].Integrity; Target = (EShipSystem)i; }
		}
		if (Worst >= 1.f) { Target = EShipSystem::Count; }
	}
	// Nothing broken: the team patches the hull, slowly.
	if (Target == EShipSystem::Count)
	{
		if (AStarshipPawn* S = Ship())
		{
			if (S->HullIntegrity < 100.f)
			{
				S->HullIntegrity = FMath::Min(100.f, S->HullIntegrity + RepairPerSecond * 25.f * DeltaTime);
				if (S->HullIntegrity >= 100.f) { Report(EStation::Engineering, TEXT("Hull patched. She's whole again."), FLinearColor(0.6f, 1.f, 0.6f)); }
			}
		}
	}
	for (int32 i = 0; i < (int32)EShipSystem::Count; ++i)
	{
		FShipSystemState& St = Systems[i];
		const bool bTeam = (EShipSystem)i == Target;
		St.bRepairing = bTeam && St.Integrity < 1.f;
		if (St.Integrity < 1.f)
		{
			const float Before = St.Integrity;
			St.Integrity = FMath::Min(1.f, St.Integrity + RepairPerSecond * (bTeam ? 1.f : 0.33f) * DeltaTime);
			if (Before < 1.f && St.Integrity >= 1.f)
			{
				Report(EStation::Engineering, FString::Printf(TEXT("%s back to full."), *SystemName((EShipSystem)i)), FLinearColor(0.6f, 1.f, 0.6f));
			}
		}
	}
}

void UShipSystemsComponent::OnHit(float Applied, float ThroughShields)
{
	if (ThroughShields <= 0.f) { return; }
	// A hit through the shields breaks something: which, by weight; the big systems are big targets.
	static const float Weights[(int32)EShipSystem::Count] = { 2.f, 2.f, 1.f, 1.5f, 1.5f, 1.f, 0.7f };
	float Total = 0.f; for (float W : Weights) { Total += W; }
	float Pick = FMath::FRand() * Total;
	int32 Hit = 0;
	for (int32 i = 0; i < (int32)EShipSystem::Count; ++i) { Pick -= Weights[i]; if (Pick <= 0.f) { Hit = i; break; } }
	FShipSystemState& St = Systems[Hit];
	const float Loss = ThroughShields * DamagePerHullPoint * (Alert == EAlertState::Red ? 0.75f : 1.f);
	const float Before = St.Integrity;
	St.Integrity = FMath::Max(0.f, St.Integrity - Loss);
	if (Before - St.Integrity > 0.02f && ReportCooldown[Hit] <= 0.f)
	{
		ReportCooldown[Hit] = 6.f;
		const int32 Pct = FMath::RoundToInt(St.Integrity * 100.f);
		Report(EStation::Engineering, St.Integrity <= 0.f ? FString::Printf(TEXT("%s are out!"), *SystemName((EShipSystem)Hit))
			: FString::Printf(TEXT("%s at %d percent."), *SystemName((EShipSystem)Hit), Pct), FLinearColor(1.f, 0.7f, 0.5f));
	}
}

void UShipSystemsComponent::SetAlert(EAlertState NewAlert)
{
	if (Alert == NewAlert) { return; }
	Alert = NewAlert;
	switch (Alert)
	{
	case EAlertState::Red: Report(EStation::Tactical, TEXT("Red alert. Shields to full, phasers charging, all decks brace."), FLinearColor(1.f, 0.35f, 0.3f)); break;
	case EAlertState::Yellow: Report(EStation::Tactical, TEXT("Yellow alert. Shields up."), FLinearColor(1.f, 0.85f, 0.4f)); break;
	default: Report(EStation::Tactical, TEXT("Standing down. Condition green."), FLinearColor(0.6f, 1.f, 0.6f)); break;
	}
}

void UShipSystemsComponent::DivertPower(EShipSystem To)
{
	// 150 % to the one, the rest share the remainder of the budget (Count systems at 100 % each).
	const int32 N = (int32)EShipSystem::Count;
	const float Rest = (N - 1.5f) / (N - 1);
	for (int32 i = 0; i < N; ++i) { Systems[i].Power = (EShipSystem)i == To ? 1.5f : Rest; }
	Report(EStation::Engineering, FString::Printf(TEXT("Diverting power to %s."), *SystemName(To).ToLower()));
}

void UShipSystemsComponent::BalancePower()
{
	for (FShipSystemState& St : Systems) { St.Power = 1.f; }
	Report(EStation::Engineering, TEXT("Power balanced across all systems."));
}

void UShipSystemsComponent::SetRepairPriority(EShipSystem System)
{
	RepairPriority = System;
	if (System == EShipSystem::Count) { Report(EStation::Engineering, TEXT("Damage control on the worst of it, as I see fit.")); }
	else { Report(EStation::Engineering, FString::Printf(TEXT("Damage control team to %s."), *SystemName(System).ToLower())); }
}

bool UShipSystemsComponent::HandleOrder(const TArray<FString>& Args, FString& OutReply)
{
	// Args[0] is "order"; the rest is the order.
	if (Args.Num() < 2) { OutReply = TEXT("order alert red|yellow|green | power <system>|balance | repair <system>|auto"); return false; }
	const FString What = Args[1].ToLower();
	const FString Arg = Args.Num() > 2 ? Args[2].ToLower() : FString();
	EShipSystem Sys;
	if (What == TEXT("alert") || What == TEXT("red") || What == TEXT("yellow") || What == TEXT("green"))
	{
		const FString Level = What == TEXT("alert") ? Arg : What;
		if (Level.StartsWith(TEXT("red"))) { SetAlert(EAlertState::Red); }
		else if (Level.StartsWith(TEXT("yel"))) { SetAlert(EAlertState::Yellow); }
		else if (Level.StartsWith(TEXT("gre")) || Level.StartsWith(TEXT("stand"))) { SetAlert(EAlertState::Green); }
		else { OutReply = TEXT("alert red|yellow|green"); return false; }
		return true;
	}
	if (What == TEXT("power"))
	{
		if (Arg == TEXT("balance") || Arg == TEXT("normal")) { BalancePower(); return true; }
		if (ParseSystem(Arg, Sys)) { DivertPower(Sys); return true; }
		OutReply = TEXT("power shields|phasers|torpedoes|warp|impulse|sensors|life|balance"); return false;
	}
	if (What == TEXT("balance")) { BalancePower(); return true; }
	if (What == TEXT("repair"))
	{
		if (Arg == TEXT("auto") || Arg.IsEmpty()) { SetRepairPriority(EShipSystem::Count); return true; }
		if (ParseSystem(Arg, Sys)) { SetRepairPriority(Sys); return true; }
		OutReply = TEXT("repair <system>|auto"); return false;
	}
	if (What == TEXT("report") || What == TEXT("status"))
	{
		FString R;
		for (int32 i = 0; i < (int32)EShipSystem::Count; ++i)
		{
			R += FString::Printf(TEXT("%s %d%% (%d%% power)%s  "), *SystemName((EShipSystem)i), FMath::RoundToInt(Systems[i].Integrity * 100.f), FMath::RoundToInt(Systems[i].Power * 100.f), Systems[i].bRepairing ? TEXT(" repairing") : TEXT(""));
		}
		Report(EStation::Engineering, R);
		return true;
	}
	OutReply = TEXT("order alert|power|repair|report");
	return false;
}
