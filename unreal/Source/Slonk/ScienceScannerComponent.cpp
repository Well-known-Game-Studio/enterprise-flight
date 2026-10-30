// Slonk - science sensors.

#include "ScienceScannerComponent.h"
#include "PlanetBody.h"
#include "StarBody.h"
#include "StarSystem.h"
#include "BeltField.h"
#include "Nebula.h"
#include "SpaceVessel.h"
#include "BorgCube.h"
#include "StarshipPawn.h"
#include "WarpDriveComponent.h"
#include "StarshipMovementComponent.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/Crc.h"

namespace
{
	const TCHAR* StarClassName(EStarClass C)
	{
		switch (C)
		{
		case EStarClass::M: return TEXT("M, red dwarf");
		case EStarClass::K: return TEXT("K, orange dwarf");
		case EStarClass::G: return TEXT("G, yellow dwarf");
		case EStarClass::F: return TEXT("F, yellow-white");
		case EStarClass::A: return TEXT("A, blue-white");
		case EStarClass::WhiteDwarf: return TEXT("D, white dwarf");
		default: return TEXT("unknown");
		}
	}

	int32 StarTemperatureK(EStarClass C)
	{
		switch (C)
		{
		case EStarClass::M: return 3300;
		case EStarClass::K: return 4800;
		case EStarClass::G: return 5700;
		case EStarClass::F: return 6800;
		case EStarClass::A: return 9000;
		case EStarClass::WhiteDwarf: return 15000;
		default: return 5000;
		}
	}

	/** Mean density by archetype, g/cc, for the gravity estimate. */
	float DensityFor(const APlanetBody* P)
	{
		switch (P->ResolvedArchetype)
		{
		case EPlanetArchetype::GasGiant: return 1.3f;
		case EPlanetArchetype::IceGiant: return 1.6f;
		case EPlanetArchetype::Ice: return 3.0f;
		case EPlanetArchetype::Mars: return 3.9f;
		case EPlanetArchetype::Desert: return 4.5f;
		case EPlanetArchetype::Ocean: return 5.0f;
		case EPlanetArchetype::Lava: return 5.0f;
		case EPlanetArchetype::Venus: return 5.2f;
		case EPlanetArchetype::Moon:
			switch (P->ResolvedMoonKind)
			{
			case EMoonKind::Icy: return 1.9f;
			case EMoonKind::Lava: return 3.5f;
			default: return 3.3f;
			}
		default: return 5.5f;
		}
	}

	/** Surface gravity in g from radius and mean density. */
	double GravityG(double RadiusKm, float DensityGcc)
	{
		const double G = 6.674e-11, Rho = DensityGcc * 1000.0, R = RadiusKm * 1000.0;
		return (4.0 / 3.0) * PI * G * Rho * R / 9.81;
	}

	/** A Starfleet planetary class and short description for a world. */
	FString WorldClass(const APlanetBody* P)
	{
		const FPlanetStyle& S = P->Style;
		switch (P->ResolvedArchetype)
		{
		case EPlanetArchetype::GasGiant: return TEXT("Class J gas giant");
		case EPlanetArchetype::IceGiant: return TEXT("Class I ice giant");
		case EPlanetArchetype::Ocean: return TEXT("Class O ocean world");
		case EPlanetArchetype::Desert: return TEXT("Class H desert world");
		case EPlanetArchetype::Ice: return TEXT("Class P glaciated world");
		case EPlanetArchetype::Lava: return TEXT("Class Y volcanic world");
		case EPlanetArchetype::Venus: return TEXT("Class N greenhouse world");
		case EPlanetArchetype::Mars: return TEXT("Class K marginal world");
		case EPlanetArchetype::Moon:
			switch (P->ResolvedMoonKind)
			{
			case EMoonKind::Icy: return TEXT("Class P ice moon");
			case EMoonKind::Lava: return TEXT("Class Y volcanic moon");
			default: return TEXT("Class D rocky moon");
			}
		default:
			return (S.bAtmosphere && S.SeaLevel > 0.1f) ? TEXT("Class M terrestrial world") : TEXT("Class L terrestrial world");
		}
	}

	FString Km(double Cm)
	{
		const double KmV = Cm / 1.0e5;
		if (KmV >= 1.0e6) { return FString::Printf(TEXT("%.2f million km"), KmV / 1.0e6); }
		if (KmV >= 1000.0) { return FString::Printf(TEXT("%.0f km"), KmV); }
		return FString::Printf(TEXT("%.1f km"), KmV);
	}
}

UScienceScannerComponent::UScienceScannerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Star, Planet, Moon, Station, Ship, Hostile, Belt, Nebula, Surface.
	SecondsByKind = { 15.f, 12.f, 8.f, 6.f, 5.f, 5.f, 8.f, 12.f, 10.f };
}

float UScienceScannerComponent::Duration() const
{
	const int32 I = (int32)Kind;
	const float S = SecondsByKind.IsValidIndex(I) ? SecondsByKind[I] : 10.f;
	return FMath::Max(S / FMath::Max(SpeedFactor, 0.05f), 0.5f);
}

float UScienceScannerComponent::Roll(const FString& Key, int32 Salt)
{
	const uint32 H = FCrc::StrCrc32(*Key) ^ (0x9E3779B9u * (uint32)(Salt + 1));
	return (float)((H * 2654435761u) >> 8) / 16777216.f;
}

bool UScienceScannerComponent::ClassifyTarget(const AActor* Actor, EScanKind& OutKind)
{
	if (!Actor) { return false; }
	if (const APlanetBody* P = Cast<APlanetBody>(Actor)) { OutKind = P->IsMoon() ? EScanKind::Moon : EScanKind::Planet; return true; }
	if (Actor->IsA<AStarBody>()) { OutKind = EScanKind::Star; return true; }
	if (Actor->IsA<ABorgCube>()) { OutKind = EScanKind::Hostile; return true; }
	if (Actor->IsA<ASpaceStation>()) { OutKind = EScanKind::Station; return true; }
	if (Actor->IsA<ASpaceVessel>() || Actor->IsA<AStarshipPawn>()) { OutKind = EScanKind::Ship; return true; }
	if (Actor->IsA<ABeltField>()) { OutKind = EScanKind::Belt; return true; }
	if (Actor->IsA<ANebula>()) { OutKind = EScanKind::Nebula; return true; }
	return false;
}

FString UScienceScannerComponent::SystemNameAt(const UWorld* World, const FVector& Location)
{
	if (const AStarSystem* Sys = AStarSystem::Nearest(const_cast<UWorld*>(World), Location))
	{
		if (Sys->DistanceFrom(Location) <= Sys->RadiusCm * 4.0) { return Sys->SystemName; }
	}
	return TEXT("Deep space");
}

FString UScienceScannerComponent::KeyFor(const AActor* Actor, const FString& Label)
{
	return UScienceLogSubsystem::MakeKey(SystemNameAt(Actor->GetWorld(), Actor->GetActorLocation()), Label);
}

void UScienceScannerComponent::SurveyKeys(const UWorld* World, const FVector& Location, FString& OutSystem, TArray<FString>& OutKeys)
{
	OutKeys.Reset();
	OutSystem = SystemNameAt(World, Location);
	const AStarSystem* Sys = AStarSystem::Nearest(const_cast<UWorld*>(World), Location);
	if (!Sys || OutSystem == TEXT("Deep space")) { return; }
	auto StarLabel = [](const AStarBody* S) { return S->StarName.IsEmpty() ? S->GetActorNameOrLabel() : S->StarName; };
	if (Sys->Star) { OutKeys.Add(UScienceLogSubsystem::MakeKey(OutSystem, StarLabel(Sys->Star))); }
	if (Sys->Companion) { OutKeys.Add(UScienceLogSubsystem::MakeKey(OutSystem, StarLabel(Sys->Companion))); }
	for (const APlanetBody* P : Sys->Planets)
	{
		if (P && !P->IsMoon()) { OutKeys.Add(UScienceLogSubsystem::MakeKey(OutSystem, P->GetDisplayName())); }
	}
}

bool UScienceScannerComponent::InRange(const AActor* Actor, EScanKind K, double& OutDistCm, double& OutRangeCm) const
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Actor) { return false; }
	const FVector ShipLoc = Owner->GetActorLocation();
	const FVector To = Actor->GetActorLocation() - ShipLoc;
	switch (K)
	{
	case EScanKind::Planet:
	case EScanKind::Moon:
	case EScanKind::Surface:
	{
		const APlanetBody* P = CastChecked<APlanetBody>(Actor);
		OutDistCm = To.Size() - P->GetRadiusCm();
		OutRangeCm = FMath::Max((double)WorldRangeRadii * P->GetRadiusCm(), WorldMinRangeCm);
		break;
	}
	case EScanKind::Star:
	{
		const AStarBody* S = CastChecked<AStarBody>(Actor);
		OutDistCm = To.Size() - S->GetRadiusCm();
		OutRangeCm = StarRangeAu * AStarSystem::AuCm;
		break;
	}
	case EScanKind::Belt:
	{
		// Distance from the ring's centre line, in the belt's own plane.
		const ABeltField* B = CastChecked<ABeltField>(Actor);
		const FVector Rel = B->GetActorTransform().InverseTransformPosition(ShipLoc);
		const double Radial = FMath::Abs(FVector2D(Rel.X, Rel.Y).Size() - B->BeltRadiusCm);
		OutDistCm = FMath::Sqrt(Radial * Radial + Rel.Z * Rel.Z);
		OutRangeCm = BeltRangeHalfWidths * B->BeltHalfWidthCm;
		break;
	}
	case EScanKind::Nebula:
	{
		const ANebula* N = CastChecked<ANebula>(Actor);
		OutDistCm = To.Size();
		OutRangeCm = NebulaRangeRadii * N->GetRadiusCm();
		break;
	}
	default:
		OutDistCm = To.Size();
		OutRangeCm = VesselRangeCm;
		break;
	}
	return OutDistCm <= OutRangeCm;
}

bool UScienceScannerComponent::BeginScan(AActor* NewTarget, const FString& Label, FString& OutReason)
{
	EScanKind K;
	if (!ClassifyTarget(NewTarget, K)) { OutReason = TEXT("nothing to scan: pick a nav target or lock a target"); return false; }
	if (NewTarget == GetOwner()) { OutReason = TEXT("sensors cannot scan this ship"); return false; }
	double Dist, Range;
	if (!InRange(NewTarget, K, Dist, Range))
	{
		OutReason = FString::Printf(TEXT("%s is out of sensor range: %s away, scan within %s"), *Label, *Km(Dist), *Km(Range));
		return false;
	}
	if (const AStarshipPawn* Ship = Cast<AStarshipPawn>(GetOwner()))
	{
		if (Ship->WarpDrive && Ship->WarpDrive->IsWarpEngaged()) { OutReason = TEXT("sensors are blind at warp"); return false; }
	}
	Target = NewTarget;
	Kind = K;
	TargetLabel = Label;
	Progress = 0.f;
	State = EScanState::Scanning;
	Status = TEXT("scanning");
	UE_LOG(LogTemp, Log, TEXT("Science: scanning %s (%s) at %s, %.0f s"), *Label, UScienceLogSubsystem::KindName(K), *Km(Dist), Duration());
	return true;
}

bool UScienceScannerComponent::BeginSurfaceSurvey(APlanetBody* Body, FString& OutReason)
{
	if (!Body || !GetOwner()) { OutReason = TEXT("no ground under the craft"); return false; }
	Target = Body;
	Kind = EScanKind::Surface;
	TargetLabel = Body->GetDisplayName() + TEXT(" surface");
	SurfacePoint = GetOwner()->GetActorLocation();
	Progress = 0.f;
	State = EScanState::Scanning;
	Status = TEXT("sampling");
	UE_LOG(LogTemp, Log, TEXT("Science: surface survey on %s, %.0f s"), *Body->GetDisplayName(), Duration());
	return true;
}

void UScienceScannerComponent::Cancel()
{
	if (IsBusy()) { Finish(TEXT("scan cancelled")); }
}

void UScienceScannerComponent::Finish(const FString& Why)
{
	if (AStarshipPawn* Ship = Cast<AStarshipPawn>(GetOwner()))
	{
		Ship->ScreenMessage(FString::Printf(TEXT("Science: %s"), *Why), FColor::Orange);
	}
	UE_LOG(LogTemp, Log, TEXT("Science: %s"), *Why);
	State = EScanState::Idle;
	Target = nullptr;
	Progress = 0.f;
	Status.Reset();
}

void UScienceScannerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (State == EScanState::Idle) { return; }
	AActor* T = Target.Get();
	const AStarshipPawn* Ship = Cast<AStarshipPawn>(GetOwner());
	if (!T || !Ship) { Finish(TEXT("scan lost: target gone")); return; }
	if (Ship->WarpDrive && Ship->WarpDrive->IsWarpEngaged()) { Finish(TEXT("scan aborted: sensors are blind at warp")); return; }

	if (Kind == EScanKind::Surface)
	{
		// The craft has to stay on the ground (world positions are no use here: the floating origin and the body's
		// spin both move a landed craft). The sample point is read at completion.
		if (!Ship->Movement || !Ship->Movement->bLanded) { Finish(TEXT("survey aborted: the craft lifted off")); return; }
		SurfacePoint = Ship->GetActorLocation();
		State = EScanState::Scanning;
	}
	else
	{
		double Dist, Range;
		if (!InRange(T, Kind, Dist, Range)) { Finish(FString::Printf(TEXT("scan lost: %s left sensor range"), *TargetLabel)); return; }
		const FVector Dir = (T->GetActorLocation() - Ship->GetActorLocation()).GetSafeNormal();
		const double Cos = FVector::DotProduct(Dir, Ship->GetActorForwardVector());
		const bool bInCone = Cos >= FMath::Cos(FMath::DegreesToRadians(ConeDeg));
		if (!bInCone)
		{
			State = EScanState::Held;
			Status = TEXT("bring the target ahead");
			return;
		}
		State = EScanState::Scanning;
		Status = TEXT("scanning");
	}

	Progress += DeltaTime / Duration();
	if (Progress >= 1.f)
	{
		Progress = 1.f;
		Complete();
	}
}

void UScienceScannerComponent::Complete()
{
	AStarshipPawn* Ship = Cast<AStarshipPawn>(GetOwner());
	UScienceLogSubsystem* Log = GetWorld() && GetWorld()->GetGameInstance() ? GetWorld()->GetGameInstance()->GetSubsystem<UScienceLogSubsystem>() : nullptr;
	AActor* T = Target.Get();
	if (!Ship || !Log || !T) { Finish(TEXT("scan failed")); return; }

	FScanRecord R;
	R.Kind = Kind;
	R.Label = TargetLabel;
	R.System = SystemNameAt(GetWorld(), T->GetActorLocation());
	R.Key = UScienceLogSubsystem::MakeKey(R.System, TargetLabel);
	const bool bRepeat = Log->IsScanned(R.Key);
	BuildRecord(R);
	const int32 Earned = Log->Add(R);

	// The system survey: every star and planet scanned pays once.
	int32 SurveyBonus = 0;
	FString System; TArray<FString> Keys;
	SurveyKeys(GetWorld(), T->GetActorLocation(), System, Keys);
	if (Keys.Num() > 0 && !Log->IsSystemSurveyed(System) && Log->CountScanned(Keys) == Keys.Num())
	{
		SurveyBonus = Log->MarkSystemSurveyed(System);
	}

	// Report: newest message on top, so push in reverse.
	TArray<FString> Lines;
	Lines.Add(bRepeat ? FString::Printf(TEXT("%s: already catalogued, readings refreshed"), *R.Label)
					 : FString::Printf(TEXT("%s catalogued: +%d science  (%d total, %s)"), *R.Label, Earned, Log->TotalPoints(), *Log->RatingTitle()));
	for (const FString& F : R.Facts) { Lines.Add(TEXT("   ") + F); }
	if (!R.Discovery.IsEmpty()) { Lines.Add(TEXT("   Discovery: ") + R.Discovery); }
	for (const FString& N : R.BonusNotes) { Lines.Add(TEXT("   ") + N); }
	if (SurveyBonus > 0) { Lines.Add(FString::Printf(TEXT("%s survey complete: +%d science"), *System, SurveyBonus)); }
	for (int32 i = Lines.Num() - 1; i >= 0; --i)
	{
		Ship->ScreenMessage(Lines[i], i == 0 || Lines[i].StartsWith(System) ? FColor::Cyan : FColor::White, 12.f);
	}
	UE_LOG(LogTemp, Log, TEXT("Science: %s"), *FString::Join(Lines, TEXT(" | ")));

	State = EScanState::Idle;
	Target = nullptr;
	Progress = 0.f;
	Status.Reset();
}

void UScienceScannerComponent::BuildRecord(FScanRecord& R) const
{
	const AActor* T = Target.Get();
	switch (Kind)
	{
	case EScanKind::Planet:
	case EScanKind::Moon: FactsForWorld(CastChecked<APlanetBody>(T), R); break;
	case EScanKind::Surface: FactsForSurface(CastChecked<APlanetBody>(T), R); break;
	case EScanKind::Star: FactsForStar(T, R); break;
	case EScanKind::Belt: FactsForBelt(T, R); break;
	case EScanKind::Nebula: FactsForNebula(T, R); break;
	default: FactsForVessel(T, R); break;
	}
}

void UScienceScannerComponent::FactsForWorld(const APlanetBody* P, FScanRecord& R) const
{
	const FPlanetStyle& S = P->Style;
	const bool bGiant = P->ResolvedArchetype == EPlanetArchetype::GasGiant || P->ResolvedArchetype == EPlanetArchetype::IceGiant;
	R.Facts.Add(WorldClass(P));
	const double Gravity = GravityG(P->RadiusKm, DensityFor(P));
	R.Facts.Add(FString::Printf(TEXT("Diameter %s, surface gravity %.2f g (est.)"), *Km(2.0 * P->GetRadiusCm()), Gravity));
	if (P->SpinDegPerSec > 1.0e-6f)
	{
		const double Hours = 360.0 / P->SpinDegPerSec / 3600.0;
		R.Facts.Add(Hours < 48.0 ? FString::Printf(TEXT("Rotation period %.1f h"), Hours) : FString::Printf(TEXT("Rotation period %.1f days"), Hours / 24.0));
	}
	// Orbit and temperature band from the system's star and its class (the hand-made home worlds have no Star set).
	const AStarSystem* Sys = AStarSystem::Nearest(P->GetWorld(), P->GetActorLocation());
	const AActor* Star = P->Star.Get() ? P->Star.Get() : (Sys ? Sys->Star.Get() : nullptr);
	if (Star)
	{
		const double OrbitAu = FVector::Dist(P->GetActorLocation(), Star->GetActorLocation()) / AStarSystem::AuCm;
		const double LumScale = Sys ? AStarSystem::LuminosityScale(Sys->StarClass) : 1.0;
		const double Band = OrbitAu / FMath::Max(LumScale, 0.01);
		const TCHAR* Zone = Band < 0.6 ? TEXT("hot zone") : Band < 1.8 ? TEXT("temperate zone") : TEXT("cold zone");
		if (P->IsMoon())
		{
			// A moon's orbit is about its planet: report the planet instead.
			const APlanetBody* Primary = nullptr; double Best = TNumericLimits<double>::Max();
			for (TActorIterator<APlanetBody> It(P->GetWorld()); It; ++It)
			{
				if (*It == P || It->IsMoon()) { continue; }
				const double D = FVector::Dist(It->GetActorLocation(), P->GetActorLocation());
				if (D < Best) { Best = D; Primary = *It; }
			}
			if (Primary && Best < 400.0 * Primary->GetRadiusCm())
			{
				R.Facts.Add(FString::Printf(TEXT("Orbits %s at %s, %s"), *Primary->GetDisplayName(), *Km(Best), Zone));
			}
			else { R.Facts.Add(FString::Printf(TEXT("%.2f AU from the star, %s"), OrbitAu, Zone)); }
		}
		else
		{
			R.Facts.Add(FString::Printf(TEXT("Orbit %.2f AU, %s"), OrbitAu, Zone));
			int32 Moons = 0;
			for (TActorIterator<APlanetBody> It(P->GetWorld()); It; ++It)
			{
				if (*It != P && It->IsMoon() && FVector::Dist(It->GetActorLocation(), P->GetActorLocation()) < 400.0 * P->GetRadiusCm()) { ++Moons; }
			}
			if (Moons > 0) { R.Facts.Add(FString::Printf(TEXT("%d moon%s"), Moons, Moons == 1 ? TEXT("") : TEXT("s"))); }
		}
	}
	if (bGiant)
	{
		R.Facts.Add(TEXT("Banded hydrogen-helium atmosphere, no solid surface"));
	}
	else if (S.bAtmosphere)
	{
		const TCHAR* Thick = S.CloudDensity > 1.5f ? TEXT("dense") : S.CloudDensity < 0.5f ? TEXT("thin") : TEXT("breathable-density");
		FString A = FString::Printf(TEXT("Atmosphere: %s"), Thick);
		if (S.bClouds && S.CloudCoverage > 0.02f) { A += FString::Printf(TEXT(", %d%% cloud cover"), FMath::RoundToInt(S.CloudCoverage * 100.f)); }
		R.Facts.Add(A);
	}
	else
	{
		R.Facts.Add(TEXT("No appreciable atmosphere"));
	}
	if (!bGiant)
	{
		TArray<FString> Surface;
		if (S.SeaLevel > 0.95f) { Surface.Add(TEXT("global ocean")); }
		else if (S.SeaLevel > 0.05f) { Surface.Add(FString::Printf(TEXT("oceans over ~%d%%"), FMath::RoundToInt(S.SeaLevel * 100.f))); }
		if (S.IceCapStart < 1.5f) { Surface.Add(TEXT("polar ice caps")); }
		if (S.LavaEmissive > 0.01f) { Surface.Add(TEXT("active volcanism")); }
		if (S.CraterStrength > 0.3f) { Surface.Add(TEXT("heavily cratered")); }
		if (S.MariaStrength > 0.3f) { Surface.Add(TEXT("basaltic maria")); }
		if (S.Lineae > 0.05f) { Surface.Add(TEXT("surface lineae")); }
		if (Surface.Num() > 0) { R.Facts.Add(TEXT("Surface: ") + FString::Join(Surface, TEXT(", "))); }
	}
	if (S.bRings) { R.Facts.Add(FString::Printf(TEXT("Ring system from %.1f to %.1f radii"), S.RingInner, S.RingOuter)); }
	if (S.NightLightIntensity > 0.001f) { R.Facts.Add(TEXT("Artificial lighting on the night side: inhabited")); }

	// Discoveries: certain ones from what the world is, otherwise a seeded roll.
	struct FFind { const TCHAR* Text; int32 Points; };
	if (S.NightLightIntensity > 0.001f) { R.Discovery = TEXT("Industrial civilisation, pre-warp"); R.Bonus += 200; }
	else if (S.Lineae > 0.05f) { R.Discovery = TEXT("Subsurface ocean under the ice"); R.Bonus += 200; }
	else if (Roll(R.Key, 1) < 0.4f)
	{
		static const FFind Rocky[] = {
			{ TEXT("Dilithium deposits in the crust"), 150 }, { TEXT("Microbial life in the regolith"), 250 },
			{ TEXT("Ruins of an unknown culture"), 300 }, { TEXT("Strong magnetic anomaly"), 100 },
			{ TEXT("Rich tritanium ore"), 120 }, { TEXT("Crystalline formations of unknown origin"), 180 },
		};
		static const FFind Giant[] = {
			{ TEXT("Deuterium-rich upper atmosphere"), 120 }, { TEXT("Atmospheric life forms"), 300 },
			{ TEXT("A storm older than the colony records"), 100 }, { TEXT("Metallic hydrogen core resonance"), 150 },
		};
		const FFind* Table = bGiant ? Giant : Rocky;
		const int32 N = bGiant ? UE_ARRAY_COUNT(Giant) : UE_ARRAY_COUNT(Rocky);
		const FFind& F = Table[FMath::Clamp((int32)(Roll(R.Key, 2) * N), 0, N - 1)];
		R.Discovery = F.Text; R.Bonus += F.Points;
	}
	if (R.Bonus > 0) { R.BonusNotes.Add(FString::Printf(TEXT("discovery +%d"), R.Bonus)); }
}

void UScienceScannerComponent::FactsForSurface(const APlanetBody* P, FScanRecord& R) const
{
	const FVector Centre = P->GetActorLocation();
	const FVector Up = (SurfacePoint - Centre).GetSafeNormal();
	const double SurfR = P->GetSurfaceRadiusCm(SurfacePoint);
	const double Elev = (SurfR - P->GetRadiusCm()) / 100.0;
	// Slope from central differences 4 m out along two tangents.
	FVector T1 = FVector::CrossProduct(Up, FMath::Abs(Up.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal();
	FVector T2 = FVector::CrossProduct(Up, T1);
	const double Step = 400.0;
	const double DX = (P->GetSurfaceRadiusCm(Centre + (Up * SurfR + T1 * Step)) - P->GetSurfaceRadiusCm(Centre + (Up * SurfR - T1 * Step))) / (2.0 * Step);
	const double DY = (P->GetSurfaceRadiusCm(Centre + (Up * SurfR + T2 * Step)) - P->GetSurfaceRadiusCm(Centre + (Up * SurfR - T2 * Step))) / (2.0 * Step);
	const double SlopeDeg = FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(DX * DX + DY * DY)));
	R.Facts.Add(FString::Printf(TEXT("Landing site %.0f m %s sea level, slope %.1f deg"), FMath::Abs(Elev), Elev >= 0 ? TEXT("above") : TEXT("below"), SlopeDeg));
	R.Facts.Add(FString::Printf(TEXT("Local gravity %.2f g"), GravityG(P->RadiusKm, DensityFor(P))));
	const FPlanetStyle& S = P->Style;
	const TCHAR* Ground;
	switch (P->ResolvedArchetype)
	{
	case EPlanetArchetype::Ice: Ground = TEXT("water ice over silicate bedrock"); break;
	case EPlanetArchetype::Lava: Ground = TEXT("fresh basalt, still warm"); break;
	case EPlanetArchetype::Desert: Ground = TEXT("silicate sand and wind-cut rock"); break;
	case EPlanetArchetype::Mars: Ground = TEXT("iron-oxide regolith"); break;
	case EPlanetArchetype::Venus: Ground = TEXT("basalt plains under a crushing atmosphere"); break;
	case EPlanetArchetype::Moon:
		Ground = P->ResolvedMoonKind == EMoonKind::Icy ? TEXT("fractured water ice") : P->ResolvedMoonKind == EMoonKind::Lava ? TEXT("sulphur-crusted basalt") : TEXT("anorthosite regolith"); break;
	default: Ground = S.SeaLevel > 0.5f ? TEXT("sedimentary rock and soil") : TEXT("granite and weathered soil"); break;
	}
	R.Facts.Add(FString::Printf(TEXT("Core sample: %s"), Ground));
	if (S.bAtmosphere && P->ResolvedArchetype != EPlanetArchetype::Venus && P->ResolvedArchetype != EPlanetArchetype::Lava)
	{
		R.Facts.Add(S.SeaLevel > 0.1f && S.CloudDensity < 1.5f ? TEXT("Air sample: oxygen-nitrogen, breathable") : TEXT("Air sample: not breathable, suits required"));
	}
	struct FFind { const TCHAR* Text; int32 Points; };
	static const FFind Finds[] = {
		{ TEXT("Mineral sample carries dilithium traces"), 200 }, { TEXT("Fossilised microorganisms in the core sample"), 300 },
		{ TEXT("An artefact of unknown manufacture"), 400 }, { TEXT("Ice core with a trapped ancient atmosphere"), 150 },
		{ TEXT("Seismic readings: a hollow beneath the site"), 180 }, { TEXT("Lichen-like growth on the shaded rock"), 250 },
	};
	if (Roll(R.Key, 3) < 0.5f)
	{
		const FFind& F = Finds[FMath::Clamp((int32)(Roll(R.Key, 4) * UE_ARRAY_COUNT(Finds)), 0, (int32)UE_ARRAY_COUNT(Finds) - 1)];
		R.Discovery = F.Text; R.Bonus += F.Points;
		R.BonusNotes.Add(FString::Printf(TEXT("discovery +%d"), F.Points));
	}
}

void UScienceScannerComponent::FactsForStar(const AActor* A, FScanRecord& R) const
{
	const AStarBody* S = CastChecked<AStarBody>(A);
	const AStarSystem* Sys = AStarSystem::Nearest(A->GetWorld(), A->GetActorLocation());
	EStarClass Class = Sys ? Sys->StarClass : EStarClass::G;
	if (Sys && Sys->Companion == S)
	{
		// The companion's class is not stored: tell it from its size.
		Class = S->RadiusKm < 20000.f ? EStarClass::WhiteDwarf : S->RadiusKm < 500000.f ? EStarClass::M : EStarClass::K;
	}
	R.Facts.Add(FString::Printf(TEXT("Spectral class %s"), StarClassName(Class)));
	R.Facts.Add(FString::Printf(TEXT("Diameter %.2f solar, photosphere ~%d K"), S->RadiusKm / 695700.f, StarTemperatureK(Class)));
	if (Sys)
	{
		int32 Planets = 0;
		for (const APlanetBody* P : Sys->Planets) { if (P && !P->IsMoon()) { ++Planets; } }
		FString Line = FString::Printf(TEXT("%d planet%s"), Planets, Planets == 1 ? TEXT("") : TEXT("s"));
		if (Sys->Companion && Sys->Star) { Line += FString::Printf(TEXT(", binary with %s"), Sys->Companion == S ? *Sys->Star->StarName : *Sys->Companion->StarName); }
		R.Facts.Add(Line);
	}
	struct FFind { const TCHAR* Text; int32 Points; };
	static const FFind Finds[] = {
		{ TEXT("Coronal mass ejection in progress"), 150 }, { TEXT("Unusually strong flare activity"), 100 },
		{ TEXT("Anomalous neutrino flux from the core"), 200 }, { TEXT("A large sunspot group, cycle maximum"), 80 },
	};
	if (Roll(R.Key, 5) < 0.4f)
	{
		const FFind& F = Finds[FMath::Clamp((int32)(Roll(R.Key, 6) * UE_ARRAY_COUNT(Finds)), 0, (int32)UE_ARRAY_COUNT(Finds) - 1)];
		R.Discovery = F.Text; R.Bonus += F.Points;
		R.BonusNotes.Add(FString::Printf(TEXT("discovery +%d"), F.Points));
	}
}

void UScienceScannerComponent::FactsForVessel(const AActor* A, FScanRecord& R) const
{
	if (const ABorgCube* Cube = Cast<ABorgCube>(A))
	{
		R.Facts.Add(FString::Printf(TEXT("Borg cube, %.0f m on a side"), Cube->SizeCm / 100.f));
		R.Facts.Add(FString::Printf(TEXT("Cutting beam range %s, hull %d%%"), *Km(Cube->BeamRangeCm), FMath::RoundToInt(100.f * Cube->Health / FMath::Max(Cube->MaxHealth, 1.f))));
		R.Facts.Add(TEXT("Threat assessment: extreme"));
		R.Discovery = TEXT("Collective subspace signal recorded"); R.Bonus += 200;
		R.BonusNotes.Add(TEXT("discovery +200"));
		return;
	}
	if (const ASpaceStation* St = Cast<ASpaceStation>(A))
	{
		const TCHAR* Style = TEXT("Starfleet");
		switch (St->Style)
		{
		case EStationStyle::Spacedock: Style = TEXT("Spacedock-type"); break;
		case EStationStyle::K7: Style = TEXT("K-type deep space"); break;
		case EStationStyle::Regula: Style = TEXT("Regula-type research"); break;
		case EStationStyle::Ring: Style = TEXT("ring-type"); break;
		default: break;
		}
		R.Facts.Add(FString::Printf(TEXT("%s station, %.0f m across"), Style, St->SizeCm / 100.f));
		const int32 Crew = 60 + (int32)(Roll(R.Key, 7) * 1200.f);
		R.Facts.Add(FString::Printf(TEXT("Hull %d%%, crew complement ~%d"), FMath::RoundToInt(100.f * St->Health / FMath::Max(St->MaxHealth, 1.f)), Crew));
		static const TCHAR* Finds[] = { TEXT("Trade manifests acquired"), TEXT("Starfleet science archive synchronised"), TEXT("Unregistered cargo in bay three") };
		if (Roll(R.Key, 8) < 0.5f) { R.Discovery = Finds[FMath::Clamp((int32)(Roll(R.Key, 9) * 3), 0, 2)]; R.Bonus += 60; R.BonusNotes.Add(TEXT("discovery +60")); }
		return;
	}
	if (const AProceduralStarship* Sh = Cast<AProceduralStarship>(A))
	{
		const TCHAR* Class = TEXT("Starfleet");
		switch (Sh->GetBuiltClass())
		{
		case EStarshipClass::Constitution: Class = TEXT("Constitution-class"); break;
		case EStarshipClass::Miranda: Class = TEXT("Miranda-class"); break;
		case EStarshipClass::Excelsior: Class = TEXT("Excelsior-class"); break;
		default: break;
		}
		R.Facts.Add(FString::Printf(TEXT("%s starship, %.0f m"), Class, Sh->SizeCm / 100.f));
		R.Facts.Add(FString::Printf(TEXT("Hull %d%%, %s"), FMath::RoundToInt(100.f * Sh->Health / FMath::Max(Sh->MaxHealth, 1.f)), Sh->IsInWarp() ? TEXT("at warp") : Sh->Velocity.Size() > 100.0 ? TEXT("under impulse") : TEXT("station keeping")));
		if (Roll(R.Key, 10) < 0.35f) { R.Discovery = TEXT("Registry and crew roster cross-referenced"); R.Bonus += 40; R.BonusNotes.Add(TEXT("discovery +40")); }
		return;
	}
	if (const AStarshipPawn* Craft = Cast<AStarshipPawn>(A))
	{
		R.Facts.Add(FString::Printf(TEXT("%s, %.0f m"), *Craft->GetCraftName(), Craft->ShipLengthMeters));
		R.Facts.Add(FString::Printf(TEXT("Hull %d%%, shields %d%%"), FMath::RoundToInt(100.f * Craft->GetHullFraction()), FMath::RoundToInt(100.f * Craft->GetShieldFraction())));
		return;
	}
	if (const ASpaceVessel* V = Cast<ASpaceVessel>(A))
	{
		R.Facts.Add(FString::Printf(TEXT("Vessel %.0f m, hull %d%%"), V->SizeCm / 100.f, FMath::RoundToInt(100.f * V->Health / FMath::Max(V->MaxHealth, 1.f))));
	}
}

void UScienceScannerComponent::FactsForBelt(const AActor* A, FScanRecord& R) const
{
	const ABeltField* B = CastChecked<ABeltField>(A);
	R.Facts.Add(FString::Printf(TEXT("Orbit %.2f AU, %s wide"), B->BeltRadiusCm / AStarSystem::AuCm, *Km(2.0 * B->BeltHalfWidthCm)));
	R.Facts.Add(FString::Printf(TEXT("Bodies from %.0f m to %s across"), B->MinRockRadiusCm / 50.0, *Km(2.0 * B->MaxRockRadiusCm)));
	static const TCHAR* Kinds[] = { TEXT("nickel-iron"), TEXT("carbonaceous chondrite"), TEXT("silicate, ice-rich"), TEXT("mixed rubble piles") };
	R.Facts.Add(FString::Printf(TEXT("Composition: mostly %s"), Kinds[FMath::Clamp((int32)(Roll(R.Key, 11) * 4), 0, 3)]));
	if (Roll(R.Key, 12) < 0.4f) { R.Discovery = TEXT("Dilithium-bearing asteroids"); R.Bonus += 150; R.BonusNotes.Add(TEXT("discovery +150")); }
}

void UScienceScannerComponent::FactsForNebula(const AActor* A, FScanRecord& R) const
{
	const ANebula* N = CastChecked<ANebula>(A);
	R.Facts.Add(FString::Printf(TEXT("Diameter %s"), *Km(2.0 * N->GetRadiusCm())));
	R.Facts.Add(FString::Printf(TEXT("Optical depth %.2f, emission %.2f"), N->Density, N->Emission));
	static const TCHAR* Gas[] = { TEXT("hydrogen and ionised oxygen"), TEXT("metreon gas"), TEXT("sirillium and hydrogen"), TEXT("dust and molecular hydrogen") };
	R.Facts.Add(FString::Printf(TEXT("Composition: %s"), Gas[FMath::Clamp((int32)(Roll(R.Key, 13) * 4), 0, 3)]));
	struct FFind { const TCHAR* Text; int32 Points; };
	static const FFind Finds[] = { { TEXT("A protostar forming in the core"), 250 }, { TEXT("Pockets of sirillium gas"), 150 }, { TEXT("Subspace eddies in the filaments"), 120 } };
	if (Roll(R.Key, 14) < 0.5f)
	{
		const FFind& F = Finds[FMath::Clamp((int32)(Roll(R.Key, 15) * 3), 0, 2)];
		R.Discovery = F.Text; R.Bonus += F.Points; R.BonusNotes.Add(FString::Printf(TEXT("discovery +%d"), F.Points));
	}
}
