#include "ShipSystemsComponent.h"
#include "ShipCompartments.h"
#include "EngineUtils.h"
#include "ScienceScannerComponent.h"
#include "SpaceVessel.h"
#include "StarshipMovementComponent.h"
#include "StarshipPawn.h"
#include "StarshipWeaponsComponent.h"
#include "WarpDriveComponent.h"

UShipSystemsComponent::UShipSystemsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	Systems.SetNum((int32)EShipSystem::Count);
	Officers = {
		{ TEXT("Lt. Cmdr."), TEXT("Rhys Dahl"), EStation::Engineering, TEXT("Ensign Mara Quill") },
		{ TEXT("Lt."), TEXT("Sorek"), EStation::Tactical, TEXT("Ensign Dax Mbeki") },
		{ TEXT("Cmdr."), TEXT("T'Lara"), EStation::Science, TEXT("Ensign Hollis Rane") },
		{ TEXT("Lt."), TEXT("Imani Okonkwo"), EStation::Helm, TEXT("Ensign Petra Lind") },
		{ TEXT("Lt."), TEXT("Pell Haskins"), EStation::Operations, TEXT("Ensign Tomas Reyes") },
		{ TEXT("Dr."), TEXT("Elena Vasquez"), EStation::Medical, TEXT("Dr. Anand Kessler") },
		{ TEXT("Lt."), TEXT("Vreen"), EStation::Security, TEXT("Ensign Kira Solvay") },
	};
}

FString FOfficer::Speaker() const
{
	return OnDuty() ? FString::Printf(TEXT("%s %s"), *Rank, *Name) : FString::Printf(TEXT("%s (acting)"), *Acting);
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
	const FString Speaker = O ? O->Speaker() : StationName(From);
	FBridgeLogEntry E; E.Time = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f; E.Station = From; E.Speaker = Speaker; E.Text = Text; E.Color = Color;
	Log.Add(E);
	++LogRevision;
	UE_LOG(LogTemp, Log, TEXT("Bridge log: %s: %s"), *Speaker, *Text);
	if (Log.Num() > 200) { Log.RemoveAt(0, Log.Num() - 200); }
	if (AStarshipPawn* S = Ship()) { S->ScreenMessage(FString::Printf(TEXT("%s: %s"), *Speaker, *Text), Color.ToFColor(true), 5.f); }
}

void UShipSystemsComponent::BeginPlay()
{
	Super::BeginPlay();
	CacheBase();
	// The magazine is ours to keep.
	if (AStarshipPawn* S = Ship()) { if (S->Weapons) { S->Weapons->bUnlimitedTorpedoes = false; S->Weapons->TorpedoesCarried = FMath::Min(S->Weapons->TorpedoesCarried, Stores.TorpedoCapacity); } }
	BuildCompartments();
}

void UShipSystemsComponent::CacheBase()
{
	AStarshipPawn* S = Ship();
	if (!S || bBaseCached) { return; }
	BaseMaxShield = S->MaxShieldStrength; BaseShieldRecharge = S->ShieldRechargePerSecond;
	if (S->Weapons) { BasePhaserDps = S->Weapons->PhaserDamagePerSecond; BasePhaserRecharge = S->Weapons->PhaserRechargeSeconds; BaseTorpedoCooldown = S->Weapons->TorpedoCooldown; }
	if (S->WarpDrive) { BaseMaxWarp = S->WarpDrive->MaxWarpFactor; }
	if (S->Movement) { BaseImpulse = S->Movement->FullImpulseSpeed; }
	bBaseCached = true;
}

float UShipSystemsComponent::CrewFactor() const
{
	return Crew.Complement > 0 ? FMath::Clamp(Crew.Fit / Crew.Complement, 0.3f, 1.f) : 1.f;
}

float UShipSystemsComponent::Effectiveness(EShipSystem System) const
{
	const FShipSystemState& St = Systems[(int32)System];
	// Power helps a working system more than a broken one; nothing runs on no power. Short-handed crews run it worse.
	float E = St.Integrity * FMath::Clamp(0.4f + 0.6f * St.Power, 0.f, 1.5f) * (0.7f + 0.3f * CrewFactor());
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
	// Reinforcing a facing gives it half again at the cost of the other three; the total stays the same.
	for (int32 i = 0; i < 4; ++i) { S->ShieldFacingMax[i] = S->MaxShieldStrength * (ReinforcedFacing < 0 ? 1.f : i == ReinforcedFacing ? 1.5f : 5.f / 6.f); S->ShieldFacing[i] = FMath::Min(S->ShieldFacing[i], S->ShieldFacingMax[i]); }
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
		// The selected factor drives the cruise speed directly: pull it down to the new limit.
		if (S->WarpDrive->WarpFactor > S->WarpDrive->MaxWarpFactor) { S->WarpDrive->SetWarpFactor(S->WarpDrive->MaxWarpFactor); }
	}
	if (S->Movement) { S->Movement->FullImpulseSpeed = BaseImpulse * FMath::Max(Effectiveness(EShipSystem::Impulse), 0.1f); }
	if (S->Scanner) { S->Scanner->RangeScale = FMath::Max(Effectiveness(EShipSystem::Sensors), 0.15f); }
}

void UShipSystemsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	TickRepairs(DeltaTime);
	if (bCrewModel) { TickLifeSupport(DeltaTime); TickCrew(DeltaTime); }
	TickResupply(DeltaTime);
	ApplyEffects();
	for (float& C : ReportCooldown) { C = FMath::Max(0.f, C - DeltaTime); }
	MedicalReportCooldown = FMath::Max(0.f, MedicalReportCooldown - DeltaTime);
	LifeSupportReportCooldown = FMath::Max(0.f, LifeSupportReportCooldown - DeltaTime);
}

void UShipSystemsComponent::TickLifeSupport(float DeltaTime)
{
	// Life support at half or better keeps the air; below that the decks slowly lose it, and people with it.
	const float Target = AtmosphereTarget();
	const float Rate = Target > Atmosphere ? 0.05f : 0.03f;
	Atmosphere = FMath::Clamp(FMath::FInterpConstantTo(Atmosphere, Target, DeltaTime, Rate), 0.f, 1.f);
	if (Atmosphere < 0.6f)
	{
		const float Hurt = FMath::Min(Crew.Fit, (0.6f - Atmosphere) * 2.f * DeltaTime);
		Crew.Fit -= Hurt; Crew.Injured += Hurt;
		if (Atmosphere < 0.3f)
		{
			const float Die = FMath::Min(Crew.Injured + Crew.Critical, (0.3f - Atmosphere) * 1.f * DeltaTime);
			const float FromCrit = FMath::Min(Crew.Critical, Die);
			Crew.Critical -= FromCrit; Crew.Injured -= (Die - FromCrit); Crew.Dead += Die;
		}
		if (LifeSupportReportCooldown <= 0.f)
		{
			LifeSupportReportCooldown = 15.f; bAirWarned = true;
			Report(EStation::Operations, FString::Printf(TEXT("Life support failing. Atmosphere at %d percent, crew are going down."), FMath::RoundToInt(Atmosphere * 100.f)), FLinearColor(1.f, 0.5f, 0.4f));
		}
	}
	else if (bAirWarned && Atmosphere > 0.9f)
	{
		bAirWarned = false; LifeSupportReportCooldown = 0.f;
		Report(EStation::Operations, TEXT("Atmosphere restored on all decks."), FLinearColor(0.6f, 1.f, 0.6f));
	}
}

float UShipSystemsComponent::AtmosphereTarget() const
{
	const float LS = Effectiveness(EShipSystem::LifeSupport);
	return LS >= 0.5f ? 1.f : LS / 0.5f;
}

float UShipSystemsComponent::TreatRatePerSecond() const
{
	// No supplies, no treatment: sickbay keeps people alive and nothing more.
	if (Stores.MedicalSupplies <= 0.f) { return 0.f; }
	const FOfficer* CMO = OfficerAt(EStation::Medical);
	float R = BaseTreatRate;
	if (CMO && !CMO->OnDuty()) { R *= 0.6f; }
	if (Atmosphere < 0.6f) { R *= 0.5f; }
	return R;
}

void UShipSystemsComponent::TickCrew(float DeltaTime)
{
	// Sickbay: the critical are stabilised into the wounded, the wounded are returned to duty. Triage splits the staff.
	const float R = TreatRatePerSecond() * DeltaTime;
	const float CritShare = Triage == ETriage::CriticalFirst ? 0.7f : 0.3f;
	float Stab = FMath::Min(Crew.Critical, R * CritShare);
	float Heal = FMath::Min(Crew.Injured, R * (1.f - CritShare));
	// Idle staff on one side help the other.
	if (Stab < R * CritShare) { Heal = FMath::Min(Crew.Injured, Heal + (R * CritShare - Stab)); }
	else if (Heal < R * (1.f - CritShare)) { Stab = FMath::Min(Crew.Critical, Stab + (R * (1.f - CritShare) - Heal)); }
	Crew.Critical -= Stab; Crew.Injured += Stab - Heal; Crew.Fit += Heal;
	Stores.MedicalSupplies = FMath::Max(0.f, Stores.MedicalSupplies - Stab * 0.008f - Heal * 0.003f);
	if (Stores.MedicalSupplies <= 0.f && !bSuppliesOutReported) { bSuppliesOutReported = true; Report(EStation::Medical, TEXT("Sickbay is out of supplies. I can keep people alive, not much more."), FLinearColor(1.f, 0.6f, 0.4f)); }
	// The critical beyond the beds, or without supplies, slip away.
	const float Unbedded = FMath::Max(0.f, Crew.Critical - Crew.SickbayBeds) + (Stores.MedicalSupplies <= 0.f ? Crew.Critical * 0.5f : 0.f);
	const float Lost = FMath::Min(Crew.Critical, Unbedded * 0.004f * DeltaTime);
	Crew.Critical -= Lost; Crew.Dead += Lost; PendingDead += Lost;
	// Officers heal in sickbay.
	for (FOfficer& O : Officers)
	{
		if (O.bInjured && !O.bLost)
		{
			O.RecoverSeconds -= DeltaTime * (Stores.MedicalSupplies > 0.f ? 1.f : 0.5f);
			if (O.RecoverSeconds <= 0.f) { O.bInjured = false; Report(O.Station, TEXT("Back on duty, Captain."), FLinearColor(0.6f, 1.f, 0.6f)); }
		}
	}
	// The doctor's report, batched.
	if (MedicalReportCooldown <= 0.f && (PendingWounded >= 3.f || PendingDead >= 1.f))
	{
		MedicalReportCooldown = 8.f;
		FString T;
		if (PendingWounded >= 3.f)
		{
			T = FString::Printf(TEXT("Casualties coming in: %d wounded, %d critical"), FMath::RoundToInt(Crew.Injured), FMath::RoundToInt(Crew.Critical));
			if (Crew.Dead >= 1.f) { T += FString::Printf(TEXT(", %d dead"), FMath::RoundToInt(Crew.Dead)); }
		}
		else
		{
			// Nobody new came in: these are the critical we could not hold.
			T = FString::Printf(TEXT("We've lost %d in sickbay. %d critical still waiting"), FMath::RoundToInt(PendingDead), FMath::RoundToInt(Crew.Critical));
		}
		if (Crew.Critical > Crew.SickbayBeds) { T += TEXT(". Sickbay is full"); }
		Report(EStation::Medical, T + TEXT("."), FLinearColor(1.f, 0.6f, 0.5f));
		PendingWounded = 0.f; PendingDead = 0.f;
	}
}

void UShipSystemsComponent::ApplyCasualties(float ThroughShields)
{
	if (!bCrewModel || ThroughShields <= 0.f || Crew.Fit <= 0.f) { return; }
	const float Brace = Alert == EAlertState::Red ? 0.6f : 1.f;
	// The dead come off the fit pool first, so a harder hit never kills fewer; the wounded take what is left.
	const float Dead = FMath::Min(Crew.Fit, FMath::Max(0.f, ThroughShields - 30.f) * 0.04f * Brace);
	const float Wounded = FMath::Min(Crew.Fit - Dead, ThroughShields * WoundedPerHullPoint * Brace);
	const float Crit = Wounded * 0.33f;
	Crew.Fit -= Wounded + Dead; Crew.Injured += Wounded - Crit; Crew.Critical += Crit; Crew.Dead += Dead;
	PendingWounded += Wounded; PendingDead += Dead;
}

bool UShipSystemsComponent::CanResupply(FString& OutWhy, AActor** OutStation) const
{
	AStarshipPawn* S = Ship();
	if (!S) { OutWhy = TEXT("no ship"); return false; }
	AActor* Best = nullptr; double BestD = ResupplyRangeKm * 1e5;
	for (TActorIterator<ASpaceStation> It(S->GetWorld()); It; ++It)
	{
		const double D = FVector::Dist(It->GetActorLocation(), S->GetActorLocation());
		if (D < BestD) { BestD = D; Best = *It; }
	}
	if (!Best) { OutWhy = FString::Printf(TEXT("no station within %.0f km"), ResupplyRangeKm); return false; }
	if (!CanResupplyAt(Best, OutWhy)) { return false; }
	if (OutStation) { *OutStation = Best; }
	return true;
}

bool UShipSystemsComponent::CanResupplyAt(const AActor* Station, FString& OutWhy) const
{
	AStarshipPawn* S = Ship();
	if (!S || !Station) { OutWhy = TEXT("no station"); return false; }
	if (FVector::Dist(Station->GetActorLocation(), S->GetActorLocation()) > ResupplyRangeKm * 1e5) { OutWhy = FString::Printf(TEXT("we've pulled away from the station, more than %.0f km out"), ResupplyRangeKm); return false; }
	if (S->Movement && S->Movement->GetSpeed() > 2e5) { OutWhy = TEXT("too fast to take on stores, come to all stop"); return false; }
	return true;
}

bool UShipSystemsComponent::BeginResupply()
{
	if (IsResupplying()) { return true; }
	FString Why; AActor* Station = nullptr;
	if (!CanResupply(Why, &Station))
	{
		Report(EStation::Operations, FString::Printf(TEXT("Can't resupply: %s."), *Why), FLinearColor(1.f, 0.7f, 0.5f));
		return false;
	}
	ResupplyLeft = ResupplySeconds; ResupplyStation = Station;
	const ASpaceVessel* V = Cast<ASpaceVessel>(Station);
	Report(EStation::Operations, FString::Printf(TEXT("%s has us. Taking on torpedoes, parts and supplies, their repair teams are coming aboard, wounded going across. About %.0f seconds."), V ? *V->GetDisplayName() : TEXT("The station"), ResupplySeconds));
	return true;
}

void UShipSystemsComponent::TickResupply(float DeltaTime)
{
	if (ResupplyLeft <= 0.f) { return; }
	AStarshipPawn* S = Ship();
	FString Why;
	// The same range and speed that let it start keep it going, against the station that has us.
	if (!S || !CanResupplyAt(ResupplyStation.Get(), Why))
	{
		ResupplyLeft = 0.f;
		Report(EStation::Operations, FString::Printf(TEXT("Resupply broken off: %s."), *Why), FLinearColor(1.f, 0.7f, 0.5f));
		return;
	}
	// Station teams on top of our own: every system four times the team's pace, the hull 1.5 points a second.
	for (FShipSystemState& St : Systems) { St.Integrity = FMath::Min(1.f, St.Integrity + RepairPerSecond * 4.f * DeltaTime); }
	S->HullIntegrity = FMath::Min(100.f, S->HullIntegrity + 1.5f * DeltaTime);
	ResupplyLeft -= DeltaTime;
	if (ResupplyLeft <= 0.f) { FinishResupply(); }
}

void UShipSystemsComponent::FinishResupply()
{
	AStarshipPawn* S = Ship();
	const int32 Replacements = FMath::RoundToInt(Crew.Dead);
	const int32 Treated = FMath::RoundToInt(Crew.Injured + Crew.Critical);
	Crew.Fit = Crew.Complement; Crew.Injured = 0.f; Crew.Critical = 0.f; Crew.Dead = 0.f;
	Stores.SpareParts = 1.f; Stores.MedicalSupplies = 1.f; bPartsOutReported = false; bSuppliesOutReported = false;
	for (FShipSystemState& St : Systems) { St.Integrity = 1.f; }
	for (FOfficer& O : Officers) { O.bInjured = false; O.RecoverSeconds = 0.f; }
	if (S)
	{
		S->HullIntegrity = 100.f;
		if (S->Weapons) { S->Weapons->TorpedoesCarried = Stores.TorpedoCapacity; }
	}
	Report(EStation::Operations, FString::Printf(TEXT("Resupply complete. Full torpedo load, lockers full, all systems at nominal%s%s."),
		Treated ? *FString::Printf(TEXT(", %d wounded transferred to the station's care"), Treated) : TEXT(""),
		Replacements ? *FString::Printf(TEXT(", %d replacements aboard"), Replacements) : TEXT("")), FLinearColor(0.6f, 1.f, 0.6f));
}

void UShipSystemsComponent::SetTriage(ETriage NewTriage)
{
	Triage = NewTriage;
	Report(EStation::Medical, NewTriage == ETriage::CriticalFirst ? TEXT("Critical cases first. The walking wounded will wait.") : TEXT("Patching up whoever can go back to their post. The critical will have to hold on."));
}

void UShipSystemsComponent::TickRepairs(float DeltaTime)
{
	for (FCompartment& K : Compartments) { K.Damage = FMath::Max(0.f, K.Damage - RepairPerSecond * 0.6f * CrewFactor() * DeltaTime); }
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
				const float PartsK = Stores.SpareParts > 0.f ? 1.f : 0.25f;
				const float Before = S->HullIntegrity;
				S->HullIntegrity = FMath::Min(100.f, S->HullIntegrity + RepairPerSecond * 25.f * CrewFactor() * PartsK * DeltaTime);
				Stores.SpareParts = FMath::Max(0.f, Stores.SpareParts - (S->HullIntegrity - Before) / 100.f * 0.12f);
				if (Stores.SpareParts <= 0.f && !bPartsOutReported) { bPartsOutReported = true; Report(EStation::Engineering, TEXT("We're out of spare parts. I'm stripping what I can from the shuttles and the lounge."), FLinearColor(1.f, 0.6f, 0.4f)); }
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
			// Repairs use spare parts; with the lockers empty the team cannibalises, at a quarter of the pace.
			const float PartsK = Stores.SpareParts > 0.f ? 1.f : 0.25f;
			St.Integrity = FMath::Min(1.f, St.Integrity + RepairPerSecond * (bTeam ? 1.f : 0.33f) * CrewFactor() * PartsK * DeltaTime);
			Stores.SpareParts = FMath::Max(0.f, Stores.SpareParts - (St.Integrity - Before) * 0.12f);
			if (Stores.SpareParts <= 0.f && !bPartsOutReported) { bPartsOutReported = true; Report(EStation::Engineering, TEXT("We're out of spare parts. I'm stripping what I can from the shuttles and the lounge."), FLinearColor(1.f, 0.6f, 0.4f)); }
			if (Before < 1.f && St.Integrity >= 1.f)
			{
				Report(EStation::Engineering, FString::Printf(TEXT("%s back to full."), *SystemName((EShipSystem)i)), FLinearColor(0.6f, 1.f, 0.6f));
			}
		}
	}
}

int32 UShipSystemsComponent::PickRandomSystem() const
{
	// By weight; the big systems are big targets.
	static const float Weights[(int32)EShipSystem::Count] = { 2.f, 2.f, 1.f, 1.5f, 1.5f, 1.f, 0.7f };
	float Total = 0.f; for (float W : Weights) { Total += W; }
	float Pick = FMath::FRand() * Total;
	for (int32 i = 0; i < (int32)EShipSystem::Count; ++i) { Pick -= Weights[i]; if (Pick <= 0.f) { return i; } }
	return 0;
}

void UShipSystemsComponent::DamageSystem(int32 Hit, float Loss, const FString& Where)
{
	FShipSystemState& St = Systems[Hit];
	const float Before = St.Integrity;
	St.Integrity = FMath::Max(0.f, St.Integrity - Loss);
	if (Before - St.Integrity > 0.02f && ReportCooldown[Hit] <= 0.f)
	{
		ReportCooldown[Hit] = 6.f;
		const int32 Pct = FMath::RoundToInt(St.Integrity * 100.f);
		const FString Lead = Where.IsEmpty() ? FString() : FString::Printf(TEXT("Hit in %s: "), *Where.ToLower());
		Report(EStation::Engineering, Lead + (St.Integrity <= 0.f ? FString::Printf(TEXT("%s are out!"), *SystemName((EShipSystem)Hit))
			: FString::Printf(TEXT("%s at %d percent."), *SystemName((EShipSystem)Hit), Pct)), FLinearColor(1.f, 0.7f, 0.5f));
	}
}

void UShipSystemsComponent::OnHit(float Applied, float ThroughShields)
{
	// No position (crush, scripted): the old spread.
	if (ThroughShields <= 0.f) { return; }
	ApplyCasualties(ThroughShields);
	InjureOfficer(ThroughShields, nullptr);
	DamageSystem(PickRandomSystem(), ThroughShields * DamagePerHullPoint * (Alert == EAlertState::Red ? 0.5f : 1.f), FString());
}

void UShipSystemsComponent::OnHitAt(float Applied, float ThroughShields, const FVector& LocalPoint, const FVector& LocalDir)
{
	if (ThroughShields <= 0.f) { return; }
	AStarshipPawn* S = Ship();
	const int32 Ki = SlonkCompartments::Hit(Compartments, LocalPoint, LocalDir, S ? S->GetShipLengthCm() : 30500.f);
	if (Ki == INDEX_NONE) { OnHit(Applied, ThroughShields); return; }
	FCompartment& K = Compartments[Ki];
	const float Loss = ThroughShields * DamagePerHullPoint * (Alert == EAlertState::Red ? 0.5f : 1.f);
	K.Damage = FMath::Min(1.f, K.Damage + Loss * 2.5f);
	K.LastHitTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	// Most of the harm goes to what lives there; some always finds a conduit somewhere else.
	if (K.System != EShipSystem::Count) { DamageSystem((int32)K.System, Loss * 0.7f, K.Name); DamageSystem(PickRandomSystem(), Loss * 0.3f, FString()); }
	else { DamageSystem(PickRandomSystem(), Loss * 0.4f, K.Name); }
	// Casualties follow the people: a hit on the quarters hurts more than one on a nacelle.
	const float CrewK = FMath::Clamp(0.4f + K.CrewShare * 10.f, 0.4f, 2.f);
	const float SavedWounded = WoundedPerHullPoint;
	WoundedPerHullPoint *= CrewK;
	ApplyCasualties(ThroughShields * (K.CrewShare > 0.01f ? 1.f : 0.5f));
	WoundedPerHullPoint = SavedWounded;
	InjureOfficer(ThroughShields, &K);
}

void UShipSystemsComponent::InjureOfficer(float ThroughShields, const FCompartment* Where)
{
	// A heavy hit can put a senior officer in sickbay, or worse: only the ones whose post is where it landed.
	// Craft without the crew model (the shuttle) keep their borrowed staff out of it.
	if (!bCrewModel || ThroughShields <= 40.f || FMath::FRand() >= ThroughShields / 400.f) { return; }
	TArray<int32> Up;
	for (int32 i = 0; i < Officers.Num(); ++i)
	{
		if (!Officers[i].OnDuty()) { continue; }
		if (Where)
		{
			const EStation St = Officers[i].Station;
			const bool bBridgePost = St == EStation::Tactical || St == EStation::Helm || St == EStation::Operations || St == EStation::Science;
			if (!(Where->Station == St || (Where->bBridge && bBridgePost))) { continue; }
		}
		Up.Add(i);
	}
	if (Up.Num() == 0) { return; }
	FOfficer& O = Officers[Up[FMath::RandHelper(Up.Num())]];
	const FString Who = FString::Printf(TEXT("%s %s"), *O.Rank, *O.Name);
	if (ThroughShields > 100.f && FMath::FRand() < 0.15f)
	{
		O.bLost = true;
		Report(EStation::Medical, FString::Printf(TEXT("%s is dead, Captain. %s has %s."), *Who, *O.Acting, *StationName(O.Station).ToLower()), FLinearColor(1.f, 0.4f, 0.4f));
	}
	else
	{
		O.bInjured = true; O.RecoverSeconds = 90.f + ThroughShields;
		Report(EStation::Medical, FString::Printf(TEXT("%s is down, we have them in sickbay. %s has %s."), *Who, *O.Acting, *StationName(O.Station).ToLower()), FLinearColor(1.f, 0.6f, 0.5f));
	}
}

void UShipSystemsComponent::BuildCompartments()
{
	AStarshipPawn* S = Ship();
	if (!S) { return; }
	SlonkCompartments::BuildRefit(Compartments, S->GetShipLengthCm(), S->TorpedoTubeCentre, S->ShuttleBayOffset, S->BridgeCameraOffset);
}

FString UShipSystemsComponent::FacingName(int32 F)
{
	return F == 0 ? TEXT("forward") : F == 1 ? TEXT("aft") : F == 2 ? TEXT("port") : F == 3 ? TEXT("starboard") : TEXT("even");
}

void UShipSystemsComponent::ReinforceShields(int32 Facing)
{
	const int32 Was = ReinforcedFacing;
	ReinforcedFacing = FMath::Clamp(Facing, -1, 3);
	// The grid moves its charge, it does not throw it away: the same total, shared out in the new facings' proportions
	// (the four limits always sum to four times the ship's figure, so the share fits under each one).
	if (AStarshipPawn* S = Ship(); S && ReinforcedFacing != Was)
	{
		const float Total = S->ShieldFacing[0] + S->ShieldFacing[1] + S->ShieldFacing[2] + S->ShieldFacing[3];
		ApplyEffects();
		const float SumMax = S->ShieldFacingMax[0] + S->ShieldFacingMax[1] + S->ShieldFacingMax[2] + S->ShieldFacingMax[3];
		if (SumMax > 0.f) { for (int32 i = 0; i < 4; ++i) { S->ShieldFacing[i] = FMath::Min(S->ShieldFacingMax[i], Total * S->ShieldFacingMax[i] / SumMax); } }
		S->ShieldStrength = (S->ShieldFacing[0] + S->ShieldFacing[1] + S->ShieldFacing[2] + S->ShieldFacing[3]) * 0.25f;
	}
	Report(EStation::Tactical, ReinforcedFacing < 0 ? TEXT("Shields balanced on all facings.") : FString::Printf(TEXT("Reinforcing the %s shield."), *FacingName(ReinforcedFacing)));
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
		Report(EStation::Engineering, R + FString::Printf(TEXT("Parts %d%%."), FMath::RoundToInt(Stores.SpareParts * 100.f)));
		Report(EStation::Medical, FString::Printf(TEXT("%d fit for duty, %d wounded, %d critical, %d dead. Sickbay %d of %d beds, supplies %d%%. Atmosphere %d%%."),
			FMath::RoundToInt(Crew.Fit), FMath::RoundToInt(Crew.Injured), FMath::RoundToInt(Crew.Critical), FMath::RoundToInt(Crew.Dead), SickbayInUse(), Crew.SickbayBeds, FMath::RoundToInt(Stores.MedicalSupplies * 100.f), FMath::RoundToInt(Atmosphere * 100.f)));
		return true;
	}
	if (What == TEXT("triage"))
	{
		if (Arg.StartsWith(TEXT("crit"))) { SetTriage(ETriage::CriticalFirst); return true; }
		if (Arg.StartsWith(TEXT("walk")) || Arg.StartsWith(TEXT("duty"))) { SetTriage(ETriage::WalkingWounded); return true; }
		OutReply = TEXT("triage critical|walking"); return false;
	}
	if (What == TEXT("shields") || What == TEXT("reinforce"))
	{
		if (Arg.StartsWith(TEXT("fore")) || Arg.StartsWith(TEXT("forw")) || Arg.StartsWith(TEXT("front"))) { ReinforceShields(0); return true; }
		if (Arg.StartsWith(TEXT("aft")) || Arg.StartsWith(TEXT("rear"))) { ReinforceShields(1); return true; }
		if (Arg.StartsWith(TEXT("port"))) { ReinforceShields(2); return true; }
		if (Arg.StartsWith(TEXT("star"))) { ReinforceShields(3); return true; }
		if (Arg.StartsWith(TEXT("even")) || Arg.StartsWith(TEXT("bal")) || Arg.IsEmpty()) { ReinforceShields(-1); return true; }
		OutReply = TEXT("shields fore|aft|port|starboard|even"); return false;
	}
	if (What == TEXT("resupply"))
	{
		if (BeginResupply()) { return true; }
		CanResupply(OutReply);
		OutReply = TEXT("resupply: ") + OutReply;
		return false;
	}
	OutReply = TEXT("order alert|power|repair|shields|report|triage|resupply");
	return false;
}
