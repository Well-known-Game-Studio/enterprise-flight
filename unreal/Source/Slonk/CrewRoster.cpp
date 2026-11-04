// Slonk - the crew roster: generation, the ship's day, the people's days, the captain's hand, and the save.

#include "CrewRoster.h"
#include "Kismet/GameplayStatics.h"

const TCHAR* UCrewRosterSubsystem::SlotName = TEXT("Crew");

namespace
{
	// ---- name tables (the TMP-era mix) ----
	const TCHAR* HumanFirst[] = { TEXT("Amara"), TEXT("Benjamin"), TEXT("Chidi"), TEXT("Dana"), TEXT("Elias"), TEXT("Farah"), TEXT("Gideon"), TEXT("Hana"), TEXT("Ivan"), TEXT("Jonah"), TEXT("Keiko"), TEXT("Lucas"), TEXT("Mei"), TEXT("Nikolai"), TEXT("Oona"), TEXT("Priya"), TEXT("Quentin"), TEXT("Rosa"), TEXT("Samir"), TEXT("Tomas"), TEXT("Uma"), TEXT("Viktor"), TEXT("Wren"), TEXT("Ximena"), TEXT("Yusuf"), TEXT("Zara"), TEXT("Arjun"), TEXT("Beatriz"), TEXT("Callum"), TEXT("Delphine"), TEXT("Emeka"), TEXT("Freya"), TEXT("Gabriel"), TEXT("Hiro"), TEXT("Ingrid"), TEXT("Jamal"), TEXT("Kirsten"), TEXT("Leandro"), TEXT("Maren"), TEXT("Nadia"), TEXT("Oscar"), TEXT("Paulo"), TEXT("Rhiannon"), TEXT("Sanjay"), TEXT("Tamsin"), TEXT("Ulysses"), TEXT("Valentina"), TEXT("Wesley"), TEXT("Yara"), TEXT("Zoran") };
	const TCHAR* HumanLast[] = { TEXT("Adeyemi"), TEXT("Brandt"), TEXT("Castellano"), TEXT("Duval"), TEXT("Eriksen"), TEXT("Fontaine"), TEXT("Grieve"), TEXT("Hartmann"), TEXT("Ibarra"), TEXT("Jensen"), TEXT("Kowalski"), TEXT("Lindqvist"), TEXT("Mbeki"), TEXT("Nakamura"), TEXT("Okafor"), TEXT("Park"), TEXT("Quinlan"), TEXT("Reyes"), TEXT("Sato"), TEXT("Tremblay"), TEXT("Underhill"), TEXT("Varga"), TEXT("Whitlock"), TEXT("Yamada"), TEXT("Zielinski"), TEXT("Achterberg"), TEXT("Bakshi"), TEXT("Calloway"), TEXT("Dimitriou"), TEXT("Esposito"), TEXT("Farouk"), TEXT("Gallagher"), TEXT("Holm"), TEXT("Iqbal"), TEXT("Jafari"), TEXT("Kimura"), TEXT("Lorenz"), TEXT("Moreau"), TEXT("Nwosu"), TEXT("Olsen"), TEXT("Petrov"), TEXT("Rahimi"), TEXT("Singh"), TEXT("Tanaka"), TEXT("Uribe"), TEXT("Vance"), TEXT("Weiss"), TEXT("Xu"), TEXT("Young"), TEXT("Zhou") };
	const TCHAR* VulcanNames[] = { TEXT("Sarel"), TEXT("T'Pau"), TEXT("Stavek"), TEXT("T'Mir"), TEXT("Sovar"), TEXT("T'Vran"), TEXT("Selek"), TEXT("T'Lin"), TEXT("Savik"), TEXT("T'Rea"), TEXT("Sutok"), TEXT("T'Ven"), TEXT("Soval"), TEXT("T'Kel") };
	const TCHAR* AndorianNames[] = { TEXT("Thelin"), TEXT("Shran"), TEXT("Tarah"), TEXT("Thy'lek"), TEXT("Jhamel"), TEXT("Keval"), TEXT("Talas"), TEXT("Thon"), TEXT("Shras"), TEXT("Lissan") };
	const TCHAR* TellariteNames[] = { TEXT("Gral"), TEXT("Naarg"), TEXT("Tevrin"), TEXT("Brell"), TEXT("Gorv"), TEXT("Skalaar"), TEXT("Mor"), TEXT("Tholvik") };
	const TCHAR* CaitianNames[] = { TEXT("M'Ress"), TEXT("R'Shal"), TEXT("K'Vara"), TEXT("S'Rel"), TEXT("M'Tar"), TEXT("T'Shen") };
	const TCHAR* BolianNames[] = { TEXT("Chell"), TEXT("Zim"), TEXT("Mot"), TEXT("Rixx"), TEXT("Boq"), TEXT("Hars") };
	const TCHAR* DeltanNames[] = { TEXT("Ilia"), TEXT("Tavo"), TEXT("Rhaan"), TEXT("Sela"), TEXT("Dovan") };
	const TCHAR* BetazoidNames[] = { TEXT("Lwaxana"), TEXT("Tam"), TEXT("Devinoni"), TEXT("Kestra"), TEXT("Rael") };
	const TCHAR* RigelianNames[] = { TEXT("Vaal"), TEXT("Orin"), TEXT("Kesh"), TEXT("Tarn") };
	const TCHAR* SaurianNames[] = { TEXT("Sssk"), TEXT("Rrash"), TEXT("Lssar") };
	const TCHAR* Homes[] = { TEXT("Lagos, Earth"), TEXT("San Francisco, Earth"), TEXT("Nairobi, Earth"), TEXT("Kyoto, Earth"), TEXT("Montreal, Earth"), TEXT("Mumbai, Earth"), TEXT("Sao Paulo, Earth"), TEXT("Reykjavik, Earth"), TEXT("Luna, Tycho City"), TEXT("Mars, Utopia Planitia"), TEXT("Alpha Centauri"), TEXT("Deneva"), TEXT("Tarsus IV"), TEXT("Cestus III"), TEXT("Vega Colony"), TEXT("Rigel II"), TEXT("Starbase 11"), TEXT("the Bradbury colony") };
	const TCHAR* Classes[] = { TEXT("Academy class of '74"), TEXT("Academy class of '77"), TEXT("Academy class of '79"), TEXT("Academy class of '81"), TEXT("enlisted at Mars"), TEXT("enlisted at Earth"), TEXT("transferred from the Lexington"), TEXT("transferred from the Yorktown"), TEXT("transferred from Starbase 11"), TEXT("transferred from the Hood") };
	const TCHAR* Hobbies[] = { TEXT("chess"), TEXT("the rec deck's handball court"), TEXT("Andorian flute"), TEXT("old films"), TEXT("the hydroponics bay"), TEXT("cards"), TEXT("tri-dimensional chess"), TEXT("sketching the nebulae"), TEXT("the ship's choir"), TEXT("fencing") };

	struct FTraitDef { const TCHAR* Tag; const TCHAR* Text; float Weight; };
	const FTraitDef TraitDefs[] = {
		{ TEXT("Veteran"), TEXT("Veteran: steady under fire, restless on idle days"), 0.8f },
		{ TEXT("Homesick"), TEXT("Homesick: letters and leave matter"), 1.0f },
		{ TEXT("TransporterShy"), TEXT("Transporter-shy: dreads the pad"), 0.5f },
		{ TEXT("Pacifist"), TEXT("Pacifist: a fight costs, mending suits"), 0.5f },
		{ TEXT("ThrillSeeker"), TEXT("Thrill-seeker: wants the away missions"), 0.8f },
		{ TEXT("Devout"), TEXT("Devout: keeps the observances"), 0.5f },
		{ TEXT("Gambler"), TEXT("Gambler: cards after watch"), 0.6f },
		{ TEXT("ChessPlayer"), TEXT("Chess player: finds the other players"), 0.9f },
		{ TEXT("WoundedPride"), TEXT("Wounded pride: a reprimand lingers"), 0.6f },
		{ TEXT("Bookish"), TEXT("Bookish: the library over the rec deck"), 0.8f },
		{ TEXT("Musician"), TEXT("Musician: plays in the mess"), 0.7f },
		{ TEXT("Loner"), TEXT("Loner: company costs, quiet restores"), 0.5f },
		{ TEXT("Joker"), TEXT("Joker: lifts the watch"), 0.7f },
		{ TEXT("Perfectionist"), TEXT("Perfectionist: slow, sure, hard on the careless"), 0.6f },
	};
	struct FWantDef { const TCHAR* Tag; const TCHAR* Text; };
	const FWantDef DesireDefs[] = {
		{ TEXT("Promotion"), TEXT("a promotion") }, { TEXT("AwayMission"), TEXT("a place on an away mission") }, { TEXT("ShoreLeave"), TEXT("shore leave at a starbase") },
		{ TEXT("Letter"), TEXT("a letter from home") }, { TEXT("BridgePosting"), TEXT("a posting on the bridge") }, { TEXT("SeeNebula"), TEXT("a close look at a nebula") },
		{ TEXT("SeeGiant"), TEXT("a flight past a gas giant") }, { TEXT("Commendation"), TEXT("a commendation") }, { TEXT("Teach"), TEXT("someone to train up") },
	};
	const FWantDef FearDefs[] = {
		{ TEXT("Vacuum"), TEXT("death in the dark between the stars") }, { TEXT("Klingons"), TEXT("the Klingons") }, { TEXT("Transporter"), TEXT("the transporter") },
		{ TEXT("Disgrace"), TEXT("disgrace before the crew") }, { TEXT("LosingFriend"), TEXT("losing a friend") }, { TEXT("Fire"), TEXT("fire in a sealed compartment") },
		{ TEXT("Borg"), TEXT("the thing that took the Reliant's sister ship") },
	};

	template <int N> const TCHAR* Pick(FRandomStream& R, const TCHAR* const (&Table)[N]) { return Table[R.RandRange(0, N - 1)]; }
	float Facetish(FRandomStream& R) { return (R.FRandRange(0.f, 100.f) + R.FRandRange(0.f, 100.f)) * 0.5f; }
	ECrewRank RankFromIndex(FRandomStream& R, int32 Index, int32 Count)
	{
		// Within a department: the first after the head and second are the officers, then the chiefs, the rest the ranks.
		const float F = (float)Index / FMath::Max(Count, 1);
		if (F < 0.06f) { return R.FRand() < 0.5f ? ECrewRank::Lieutenant : ECrewRank::LieutenantJG; }
		if (F < 0.15f) { return ECrewRank::Ensign; }
		if (F < 0.22f) { return ECrewRank::Chief; }
		if (F < 0.40f) { return ECrewRank::PettyOfficer; }
		if (F < 0.70f) { return ECrewRank::AbleCrewman; }
		return ECrewRank::Crewman;
	}
	bool IsOfficer(ECrewRank R) { return R >= ECrewRank::Ensign; }
}

// ---- names ----

FString UCrewRosterSubsystem::DepartmentName(ECrewDepartment D)
{
	switch (D)
	{
	case ECrewDepartment::Command: return TEXT("Command");
	case ECrewDepartment::Helm: return TEXT("Helm");
	case ECrewDepartment::Operations: return TEXT("Operations");
	case ECrewDepartment::Engineering: return TEXT("Engineering");
	case ECrewDepartment::Science: return TEXT("Science");
	case ECrewDepartment::Medical: return TEXT("Medical");
	case ECrewDepartment::Tactical: return TEXT("Tactical");
	case ECrewDepartment::Security: return TEXT("Security");
	case ECrewDepartment::Services: return TEXT("Services");
	default: return TEXT("-");
	}
}

FString UCrewRosterSubsystem::RankName(ECrewRank R)
{
	switch (R)
	{
	case ECrewRank::Crewman: return TEXT("Crewman");
	case ECrewRank::AbleCrewman: return TEXT("Able Crewman");
	case ECrewRank::PettyOfficer: return TEXT("Petty Officer");
	case ECrewRank::Chief: return TEXT("Chief Petty Officer");
	case ECrewRank::Ensign: return TEXT("Ensign");
	case ECrewRank::LieutenantJG: return TEXT("Lieutenant j.g.");
	case ECrewRank::Lieutenant: return TEXT("Lieutenant");
	case ECrewRank::LtCommander: return TEXT("Lieutenant Commander");
	case ECrewRank::Commander: return TEXT("Commander");
	case ECrewRank::Captain: return TEXT("Captain");
	default: return TEXT("-");
	}
}

FString UCrewRosterSubsystem::RankShort(ECrewRank R)
{
	switch (R)
	{
	case ECrewRank::Crewman: return TEXT("Crewman");
	case ECrewRank::AbleCrewman: return TEXT("Crewman");
	case ECrewRank::PettyOfficer: return TEXT("PO");
	case ECrewRank::Chief: return TEXT("Chief");
	case ECrewRank::Ensign: return TEXT("Ensign");
	case ECrewRank::LieutenantJG: return TEXT("Lt. j.g.");
	case ECrewRank::Lieutenant: return TEXT("Lt.");
	case ECrewRank::LtCommander: return TEXT("Lt. Cmdr.");
	case ECrewRank::Commander: return TEXT("Cmdr.");
	case ECrewRank::Captain: return TEXT("Capt.");
	default: return TEXT("");
	}
}

FString UCrewRosterSubsystem::SkillName(ECrewSkill S)
{
	static const TCHAR* N[] = { TEXT("Command"), TEXT("Helm"), TEXT("Operations"), TEXT("Engineering"), TEXT("Science"), TEXT("Medicine"), TEXT("Tactical"), TEXT("Security"), TEXT("Diplomacy"), TEXT("Trade") };
	return (int32)S < (int32)ECrewSkill::Count ? N[(int32)S] : TEXT("-");
}
FString UCrewRosterSubsystem::FacetName(ECrewFacet F)
{
	static const TCHAR* N[] = { TEXT("Resolve"), TEXT("Empathy"), TEXT("Curiosity"), TEXT("Discipline"), TEXT("Courage"), TEXT("Sociability"), TEXT("Ambition") };
	return (int32)F < (int32)ECrewFacet::Count ? N[(int32)F] : TEXT("-");
}
FString UCrewRosterSubsystem::NeedName(ECrewNeed N)
{
	static const TCHAR* S[] = { TEXT("Rest"), TEXT("Health"), TEXT("Safety"), TEXT("Belonging"), TEXT("Purpose"), TEXT("Recreation"), TEXT("Recognition") };
	return (int32)N < (int32)ECrewNeed::Count ? S[(int32)N] : TEXT("-");
}
FString UCrewRosterSubsystem::WatchName(ECrewWatch W) { static const TCHAR* S[] = { TEXT("Alpha"), TEXT("Beta"), TEXT("Gamma") }; return (int32)W < 3 ? S[(int32)W] : TEXT("-"); }
FString UCrewRosterSubsystem::ConditionName(ECrewCondition C)
{
	static const TCHAR* S[] = { TEXT("Fit"), TEXT("Wounded"), TEXT("Critical"), TEXT("Dead"), TEXT("On leave"), TEXT("Away"), TEXT("Transferred") };
	return (int32)C < (int32)ECrewCondition::Count ? S[(int32)C] : TEXT("-");
}
FString UCrewRosterSubsystem::ActivityName(ECrewActivity A)
{
	static const TCHAR* S[] = { TEXT("On watch"), TEXT("Asleep"), TEXT("In the mess"), TEXT("Off watch"), TEXT("In sickbay"), TEXT("On leave"), TEXT("Away") };
	return (int32)A < (int32)ECrewActivity::Count ? S[(int32)A] : TEXT("-");
}
FString UCrewRosterSubsystem::RelationName(ECrewRelation R)
{
	static const TCHAR* S[] = { TEXT("acquaintance"), TEXT("friend"), TEXT("close friend"), TEXT("rival"), TEXT("mentor"), TEXT("protege"), TEXT("partner"), TEXT("family") };
	return (int32)R < (int32)ECrewRelation::Count ? S[(int32)R] : TEXT("-");
}
FString UCrewRosterSubsystem::TraitText(FName Trait)
{
	for (const FTraitDef& T : TraitDefs) { if (Trait == FName(T.Tag)) { return T.Text; } }
	if (Trait == TEXT("Survivor")) { return TEXT("Survivor: walked out of a compartment that killed others"); }
	return Trait.ToString();
}
bool UCrewRosterSubsystem::ParseDepartment(const FString& Word, ECrewDepartment& Out)
{
	const FString W = Word.ToLower();
	for (int32 i = 0; i < (int32)ECrewDepartment::Count; ++i)
	{
		if (DepartmentName((ECrewDepartment)i).ToLower().StartsWith(W) && W.Len() >= 3) { Out = (ECrewDepartment)i; return true; }
	}
	if (W == TEXT("eng")) { Out = ECrewDepartment::Engineering; return true; }
	if (W == TEXT("sci")) { Out = ECrewDepartment::Science; return true; }
	if (W == TEXT("med")) { Out = ECrewDepartment::Medical; return true; }
	if (W == TEXT("ops")) { Out = ECrewDepartment::Operations; return true; }
	if (W == TEXT("tac")) { Out = ECrewDepartment::Tactical; return true; }
	if (W == TEXT("sec")) { Out = ECrewDepartment::Security; return true; }
	return false;
}
bool UCrewRosterSubsystem::ParseSkill(const FString& Word, ECrewSkill& Out)
{
	const FString W = Word.ToLower();
	for (int32 i = 0; i < (int32)ECrewSkill::Count; ++i)
	{
		if (SkillName((ECrewSkill)i).ToLower().StartsWith(W) && W.Len() >= 3) { Out = (ECrewSkill)i; return true; }
	}
	return false;
}
ECrewSkill UCrewRosterSubsystem::SkillOf(ECrewDepartment D)
{
	switch (D)
	{
	case ECrewDepartment::Command: return ECrewSkill::Command;
	case ECrewDepartment::Helm: return ECrewSkill::Helm;
	case ECrewDepartment::Operations: return ECrewSkill::Operations;
	case ECrewDepartment::Engineering: return ECrewSkill::Engineering;
	case ECrewDepartment::Science: return ECrewSkill::Science;
	case ECrewDepartment::Medical: return ECrewSkill::Medicine;
	case ECrewDepartment::Tactical: return ECrewSkill::Tactical;
	case ECrewDepartment::Security: return ECrewSkill::Security;
	default: return ECrewSkill::Trade;
	}
}
ECrewDepartment UCrewRosterSubsystem::DepartmentOf(EStation Station)
{
	switch (Station)
	{
	case EStation::Engineering: return ECrewDepartment::Engineering;
	case EStation::Tactical: return ECrewDepartment::Tactical;
	case EStation::Science: return ECrewDepartment::Science;
	case EStation::Helm: return ECrewDepartment::Helm;
	case EStation::Operations: return ECrewDepartment::Operations;
	case EStation::Medical: return ECrewDepartment::Medical;
	case EStation::Security: return ECrewDepartment::Security;
	default: return ECrewDepartment::Command;
	}
}
EStation UCrewRosterSubsystem::StationOf(ECrewDepartment D)
{
	switch (D)
	{
	case ECrewDepartment::Engineering: return EStation::Engineering;
	case ECrewDepartment::Tactical: return EStation::Tactical;
	case ECrewDepartment::Science: return EStation::Science;
	case ECrewDepartment::Helm: return EStation::Helm;
	case ECrewDepartment::Operations: return EStation::Operations;
	case ECrewDepartment::Medical: return EStation::Medical;
	case ECrewDepartment::Security: return EStation::Security;
	default: return EStation::Count;
	}
}

FString FCrewMember::Styled() const
{
	if (Department == ECrewDepartment::Medical && Rank >= ECrewRank::Lieutenant) { return TEXT("Dr. ") + FullName(); }
	const FString R = UCrewRosterSubsystem::RankShort(Rank);
	return R.IsEmpty() ? FullName() : R + TEXT(" ") + FullName();
}

const FCrewRelationship* FCrewMember::RelationWith(int32 Other) const
{
	for (const FCrewRelationship& R : Relationships) { if (R.Other == Other) { return &R; } }
	return nullptr;
}

// ---- lifetime ----

void UCrewRosterSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Load();
	if (Roster.Num() == 0) { NewComplement(0); }
	Rng.Initialize(Seed ^ (int32)(Hours * 7.0));
}

FString UCrewRosterSubsystem::StardateText() const
{
	return FString::Printf(TEXT("%.1f"), GetStardate());
}

void UCrewRosterSubsystem::Load()
{
	Roster.Reset(); Lost.Reset();
	if (UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		if (const USlonkCrewSaveGame* Save = Cast<USlonkCrewSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0)))
		{
			Roster = Save->Roster; Lost = Save->Lost; Hours = Save->Hours; Seed = Save->Seed; NextId = Save->NextId; ShipName = Save->ShipName;
			UE_LOG(LogTemp, Log, TEXT("Crew: loaded %d aboard, %d lost, stardate %s"), Roster.Num(), Lost.Num(), *StardateText());
		}
	}
	Dirty();
}

void UCrewRosterSubsystem::Save() const
{
	USlonkCrewSaveGame* Save = Cast<USlonkCrewSaveGame>(UGameplayStatics::CreateSaveGameObject(USlonkCrewSaveGame::StaticClass()));
	if (!Save) { return; }
	Save->Roster = Roster; Save->Lost = Lost; Save->Hours = Hours; Save->Seed = Seed; Save->NextId = NextId; Save->ShipName = ShipName;
	UGameplayStatics::SaveGameToSlot(Save, SlotName, 0);
}

void UCrewRosterSubsystem::Reset()
{
	UGameplayStatics::DeleteGameInSlot(SlotName, 0);
	Roster.Reset(); Lost.Reset(); Recruits.Reset(); Hours = 0.0; NextId = 0; MourningDays = 0;
	NewComplement(0);
}

// ---- generation ----

void UCrewRosterSubsystem::MakeNamed(FRandomStream& R, FCrewMember& M, const TCHAR* First, const TCHAR* Last, const TCHAR* Species, const TCHAR* Pronoun)
{
	M.FirstName = First; M.LastName = Last; M.Species = Species; M.Pronoun = Pronoun;
}

FString UCrewRosterSubsystem::PostingFor(FRandomStream& R, ECrewDepartment Dept, int32 Index) const
{
	switch (Dept)
	{
	case ECrewDepartment::Command: return TEXT("Main bridge");
	case ECrewDepartment::Helm: return Index < 6 ? TEXT("Main bridge") : TEXT("Shuttle bay");
	case ECrewDepartment::Operations: { static const TCHAR* P[] = { TEXT("Main bridge"), TEXT("Computer core"), TEXT("Cargo bays"), TEXT("Shuttle bay"), TEXT("Environmental control") }; return Index < 6 ? P[0] : P[1 + (Index % 4)]; }
	case ECrewDepartment::Engineering: { static const TCHAR* P[] = { TEXT("Main engineering"), TEXT("Reactor"), TEXT("Impulse engineering"), TEXT("Shield generators"), TEXT("Environmental control"), TEXT("Deflector"), TEXT("Port nacelle"), TEXT("Starboard nacelle"), TEXT("Main engineering") }; return P[Index % 9]; }
	case ECrewDepartment::Science: { static const TCHAR* P[] = { TEXT("Computer core"), TEXT("Sensor bay"), TEXT("Computer core") }; return Index < 3 ? TEXT("Main bridge") : P[Index % 3]; }
	case ECrewDepartment::Medical: return TEXT("Sickbay");
	case ECrewDepartment::Tactical: { static const TCHAR* P[] = { TEXT("Phaser control"), TEXT("Torpedo bay") }; return Index < 3 ? TEXT("Main bridge") : P[Index % 2]; }
	case ECrewDepartment::Security: { static const TCHAR* P[] = { TEXT("Security and armoury"), TEXT("Cargo bays"), TEXT("Shuttle bay"), TEXT("Main bridge"), TEXT("Security and armoury") }; return P[Index % 5]; }
	default: { static const TCHAR* P[] = { TEXT("Crew quarters"), TEXT("Officers' quarters"), TEXT("Cargo bays") }; return P[Index % 3]; }
	}
}

FCrewMember UCrewRosterSubsystem::MakePerson(FRandomStream& R, ECrewDepartment Dept, ECrewRank Rank, int32 Index)
{
	FCrewMember M;
	M.Id = NextId++;
	// Species: three in four human, the rest the Federation's neighbours.
	const float S = R.FRand();
	const bool bShe = R.FRand() < 0.5f;
	if (S < 0.74f) { MakeNamed(R, M, Pick(R, HumanFirst), Pick(R, HumanLast), TEXT("Human"), bShe ? TEXT("she") : TEXT("he")); }
	else if (S < 0.82f) { MakeNamed(R, M, Pick(R, VulcanNames), TEXT(""), TEXT("Vulcan"), bShe ? TEXT("she") : TEXT("he")); }
	else if (S < 0.87f) { MakeNamed(R, M, Pick(R, AndorianNames), TEXT(""), TEXT("Andorian"), bShe ? TEXT("she") : TEXT("he")); }
	else if (S < 0.90f) { MakeNamed(R, M, Pick(R, TellariteNames), TEXT(""), TEXT("Tellarite"), bShe ? TEXT("she") : TEXT("he")); }
	else if (S < 0.93f) { MakeNamed(R, M, Pick(R, CaitianNames), TEXT(""), TEXT("Caitian"), bShe ? TEXT("she") : TEXT("he")); }
	else if (S < 0.95f) { MakeNamed(R, M, Pick(R, BolianNames), TEXT(""), TEXT("Bolian"), bShe ? TEXT("she") : TEXT("he")); }
	else if (S < 0.97f) { MakeNamed(R, M, Pick(R, DeltanNames), TEXT(""), TEXT("Deltan"), bShe ? TEXT("she") : TEXT("he")); }
	else if (S < 0.985f) { MakeNamed(R, M, Pick(R, BetazoidNames), TEXT(""), TEXT("Betazoid"), bShe ? TEXT("she") : TEXT("he")); }
	else if (S < 0.995f) { MakeNamed(R, M, Pick(R, RigelianNames), TEXT(""), TEXT("Rigelian"), TEXT("they")); }
	else { MakeNamed(R, M, Pick(R, SaurianNames), TEXT(""), TEXT("Saurian"), TEXT("they")); }
	M.Rank = Rank;
	M.Department = Dept;
	M.Age = (int32)FMath::Clamp(22 + (int32)Rank * 3 + R.RandRange(-3, 9), 19, 64);
	M.Home = M.Species == TEXT("Human") ? Pick(R, Homes) : (M.Species == TEXT("Vulcan") ? TEXT("Vulcan") : M.Species == TEXT("Andorian") ? TEXT("Andoria") : M.Species == TEXT("Tellarite") ? TEXT("Tellar Prime") : M.Species == TEXT("Caitian") ? TEXT("Cait") : M.Species == TEXT("Bolian") ? TEXT("Bolarus IX") : M.Species == TEXT("Deltan") ? TEXT("Delta IV") : M.Species == TEXT("Betazoid") ? TEXT("Betazed") : M.Species == TEXT("Rigelian") ? TEXT("Rigel V") : TEXT("Sauria"));
	M.Backstory = FString::Printf(TEXT("%s; %s"), Pick(R, Classes), Pick(R, Hobbies));
	M.Birthday = R.RandRange(0, 364);
	M.Watch = (ECrewWatch)(Index % 3);
	M.Posting = PostingFor(R, Dept, Index);
	// Skills: the department's by rank, the rest a little.
	M.Skills.SetNumZeroed((int32)ECrewSkill::Count); M.SkillXP.SetNumZeroed((int32)ECrewSkill::Count);
	static const float Base[] = { 18.f, 28.f, 38.f, 52.f, 38.f, 48.f, 58.f, 68.f, 78.f, 86.f };
	for (int32 i = 0; i < (int32)ECrewSkill::Count; ++i) { M.Skills[i] = R.FRandRange(4.f, 26.f); }
	M.Skills[(int32)SkillOf(Dept)] = FMath::Clamp(Base[(int32)Rank] + R.FRandRange(-6.f, 10.f), 5.f, 97.f);
	if (IsOfficer(Rank)) { M.Skills[(int32)ECrewSkill::Command] = FMath::Max(M.Skills[(int32)ECrewSkill::Command], Base[(int32)Rank] * 0.6f + R.FRandRange(0.f, 12.f)); }
	if (Dept == ECrewDepartment::Command) { M.Skills[(int32)ECrewSkill::Diplomacy] = FMath::Max(M.Skills[(int32)ECrewSkill::Diplomacy], Base[(int32)Rank] * 0.7f); }
	// Facets, with a little species colour.
	M.Facets.SetNumZeroed((int32)ECrewFacet::Count);
	for (int32 i = 0; i < (int32)ECrewFacet::Count; ++i) { M.Facets[i] = Facetish(R); }
	if (M.Species == TEXT("Vulcan")) { M.Facets[(int32)ECrewFacet::Discipline] = FMath::Min(M.Facets[(int32)ECrewFacet::Discipline] + 25.f, 100.f); M.Facets[(int32)ECrewFacet::Resolve] += 15.f; M.Facets[(int32)ECrewFacet::Sociability] -= 20.f; }
	if (M.Species == TEXT("Andorian")) { M.Facets[(int32)ECrewFacet::Courage] += 15.f; }
	if (M.Species == TEXT("Tellarite")) { M.Facets[(int32)ECrewFacet::Empathy] -= 15.f; M.Facets[(int32)ECrewFacet::Sociability] += 10.f; }
	if (M.Species == TEXT("Betazoid") || M.Species == TEXT("Deltan")) { M.Facets[(int32)ECrewFacet::Empathy] += 25.f; }
	for (float& F : M.Facets) { F = FMath::Clamp(F, 0.f, 100.f); }
	// Traits: one to three, by weight.
	const int32 NT = R.FRand() < 0.2f ? 1 : (R.FRand() < 0.6f ? 2 : 3);
	float Total = 0.f; for (const FTraitDef& T : TraitDefs) { Total += T.Weight; }
	for (int32 n = 0; n < NT; ++n)
	{
		float X = R.FRandRange(0.f, Total);
		for (const FTraitDef& T : TraitDefs) { X -= T.Weight; if (X <= 0.f) { M.Traits.AddUnique(FName(T.Tag)); break; } }
	}
	// Needs, mood.
	M.Needs.SetNumZeroed((int32)ECrewNeed::Count);
	for (float& N : M.Needs) { N = R.FRandRange(0.6f, 0.95f); }
	M.Needs[(int32)ECrewNeed::Health] = 1.f;
	// Wants: desires from the facets and traits, one fear.
	auto Desire = [&](const TCHAR* Tag) { for (const FWantDef& D : DesireDefs) { if (FCString::Strcmp(D.Tag, Tag) == 0) { FCrewWant W; W.Tag = Tag; W.Text = D.Text; W.SinceDay = GetDay(); M.Wants.Add(W); } } };
	if (M.Facet(ECrewFacet::Ambition) > 60.f && Rank < ECrewRank::Commander) { Desire(TEXT("Promotion")); }
	if (M.Facet(ECrewFacet::Curiosity) > 60.f) { Desire(R.FRand() < 0.5f ? TEXT("SeeNebula") : TEXT("SeeGiant")); }
	if (M.HasTrait(TEXT("ThrillSeeker")) || (M.Facet(ECrewFacet::Courage) > 70.f && R.FRand() < 0.5f)) { Desire(TEXT("AwayMission")); }
	if (M.HasTrait(TEXT("Homesick"))) { Desire(TEXT("Letter")); }
	if (M.Facet(ECrewFacet::Sociability) > 65.f && R.FRand() < 0.5f) { Desire(TEXT("ShoreLeave")); }
	if (M.Wants.Num() == 0) { Desire(R.FRand() < 0.5f ? TEXT("Commendation") : TEXT("ShoreLeave")); }
	{
		const FWantDef& F = M.HasTrait(TEXT("TransporterShy")) ? FearDefs[2] : FearDefs[R.RandRange(0, UE_ARRAY_COUNT(FearDefs) - 1)];
		FCrewWant W; W.Tag = F.Tag; W.Text = F.Text; W.bFear = true; W.SinceDay = GetDay(); M.Wants.Add(W);
	}
	M.Service.JoinedStardate = GetStardate();
	M.Condition = ECrewCondition::Fit;
	UpdateMood(M);
	return M;
}

void UCrewRosterSubsystem::NewComplement(int32 InSeed, int32 Complement)
{
	Seed = InSeed != 0 ? InSeed : (int32)(FDateTime::Now().GetTicks() % 1000003);
	FRandomStream R(Seed);
	Rng.Initialize(Seed);
	Roster.Reset(); Lost.Reset(); Recruits.Reset(); NextId = 0; Hours = 0.0; HourAccumulator = 0.0; DeadBase = 0; MourningDays = 0;
	// Department strengths for a refit's 502.
	const int32 Sizes[(int32)ECrewDepartment::Count] = { 6, 24, 60, 150, 70, 30, 36, 60, 66 };
	int32 Scale = FMath::Max(Complement, 20);
	// The senior staff by name: the ship's own, then a first officer and a chief of the boat.
	struct FNamed { ECrewDepartment Dept; const TCHAR* First; const TCHAR* Last; const TCHAR* Species; const TCHAR* Pronoun; ECrewRank Rank; };
	const FNamed Heads[] = {
		{ ECrewDepartment::Command, TEXT("Ilse"), TEXT("Varga"), TEXT("Human"), TEXT("she"), ECrewRank::Commander },
		{ ECrewDepartment::Engineering, TEXT("Rhys"), TEXT("Dahl"), TEXT("Human"), TEXT("he"), ECrewRank::LtCommander },
		{ ECrewDepartment::Tactical, TEXT("Sorek"), TEXT(""), TEXT("Vulcan"), TEXT("he"), ECrewRank::Lieutenant },
		{ ECrewDepartment::Science, TEXT("T'Lara"), TEXT(""), TEXT("Vulcan"), TEXT("she"), ECrewRank::Commander },
		{ ECrewDepartment::Helm, TEXT("Imani"), TEXT("Okonkwo"), TEXT("Human"), TEXT("she"), ECrewRank::Lieutenant },
		{ ECrewDepartment::Operations, TEXT("Pell"), TEXT("Haskins"), TEXT("Human"), TEXT("he"), ECrewRank::Lieutenant },
		{ ECrewDepartment::Medical, TEXT("Elena"), TEXT("Vasquez"), TEXT("Human"), TEXT("she"), ECrewRank::Commander },
		{ ECrewDepartment::Security, TEXT("Vreen"), TEXT(""), TEXT("Andorian"), TEXT("he"), ECrewRank::Lieutenant },
		{ ECrewDepartment::Services, TEXT("Marguerite"), TEXT("Okafor"), TEXT("Human"), TEXT("she"), ECrewRank::Chief },
	};
	const FNamed Seconds[] = {
		{ ECrewDepartment::Engineering, TEXT("Mara"), TEXT("Quill"), TEXT("Human"), TEXT("she"), ECrewRank::Ensign },
		{ ECrewDepartment::Medical, TEXT("Anand"), TEXT("Kessler"), TEXT("Human"), TEXT("he"), ECrewRank::Lieutenant },
	};
	for (int32 d = 0; d < (int32)ECrewDepartment::Count; ++d)
	{
		const ECrewDepartment Dept = (ECrewDepartment)d;
		const int32 Count = FMath::Max(2, (int32)FMath::RoundToInt(Sizes[d] * (float)Scale / 502.f));
		for (int32 i = 0; i < Count; ++i)
		{
			ECrewRank Rank = i == 0 ? ECrewRank::LtCommander : (i == 1 ? ECrewRank::Lieutenant : RankFromIndex(R, i - 2, Count - 2));
			FCrewMember M = MakePerson(R, Dept, Rank, i);
			if (i == 0)
			{
				for (const FNamed& H : Heads) { if (H.Dept == Dept) { MakeNamed(R, M, H.First, H.Last, H.Species, H.Pronoun); M.Rank = H.Rank; M.Home = M.Species == TEXT("Vulcan") ? TEXT("Vulcan") : M.Species == TEXT("Andorian") ? TEXT("Andoria") : Pick(R, Homes); } }
				M.bHead = true;
				M.Skills[(int32)SkillOf(Dept)] = FMath::Max(M.Skills[(int32)SkillOf(Dept)], 72.f + R.FRandRange(0.f, 20.f));
				M.Skills[(int32)ECrewSkill::Command] = FMath::Max(M.Skills[(int32)ECrewSkill::Command], 55.f + R.FRandRange(0.f, 25.f));
				M.Watch = ECrewWatch::Alpha;
				M.Age = FMath::Max(M.Age, 34);
			}
			else if (i == 1)
			{
				for (const FNamed& H : Seconds) { if (H.Dept == Dept) { MakeNamed(R, M, H.First, H.Last, H.Species, H.Pronoun); M.Rank = H.Rank; M.Home = Pick(R, Homes); } }
				M.bSecond = true;
				M.Skills[(int32)SkillOf(Dept)] = FMath::Max(M.Skills[(int32)SkillOf(Dept)], 52.f + R.FRandRange(0.f, 16.f));
				M.Watch = ECrewWatch::Beta;
			}
			Record(M, ECrewEventKind::Joined, FString::Printf(TEXT("Joined the %s's %s department; %s."), *ShipName, *DepartmentName(Dept).ToLower(), *M.Backstory), true);
			Roster.Add(M);
		}
	}
	// Bonds: a few friends within the department and watch, the odd rival, the head a mentor to the second.
	for (int32 i = 0; i < Roster.Num(); ++i)
	{
		FCrewMember& A = Roster[i];
		const int32 Friends = R.RandRange(1, 3);
		for (int32 n = 0; n < Friends; ++n)
		{
			const int32 j = R.RandRange(0, Roster.Num() - 1);
			if (j == i) { continue; }
			FCrewMember& B = Roster[j];
			if (B.Department != A.Department && R.FRand() < 0.7f) { continue; }
			Bond(A, B, R.FRandRange(0.25f, 0.55f), ECrewRelation::Friend);
		}
		if (R.FRand() < 0.1f)
		{
			const int32 j = R.RandRange(0, Roster.Num() - 1);
			if (j != i) { Bond(A, Roster[j], -R.FRandRange(0.2f, 0.5f), ECrewRelation::Rival); }
		}
	}
	for (int32 d = 0; d < (int32)ECrewDepartment::Count; ++d)
	{
		FCrewMember* H = const_cast<FCrewMember*>(Head((ECrewDepartment)d)); FCrewMember* S2 = const_cast<FCrewMember*>(Second((ECrewDepartment)d));
		if (H && S2) { Bond(*H, *S2, 0.4f, ECrewRelation::Mentor); Bond(*S2, *H, 0.4f, ECrewRelation::Protege); }
	}
	for (FCrewMember& M : Roster) { Place(M); UpdateMood(M); }
	UE_LOG(LogTemp, Log, TEXT("Crew: new complement of %d from seed %d"), Roster.Num(), Seed);
	Dirty();
	Save();
}

void UCrewRosterSubsystem::MakeRecruits(const FString& StationName)
{
	// A pool for this station and day: the vacancies' departments first, then a spread.
	Recruits.Reset();
	RecruitStation = StationName; RecruitDay = GetDay();
	FRandomStream R(GetTypeHash(StationName) ^ (RecruitDay * 2654435761u) ^ Seed);
	int32 Counts[(int32)ECrewDepartment::Count] = { 0 };
	for (const FCrewMember& M : Roster) { if (M.Condition != ECrewCondition::Transferred) { ++Counts[(int32)M.Department]; } }
	const int32 Sizes[(int32)ECrewDepartment::Count] = { 6, 24, 60, 150, 70, 30, 36, 60, 66 };
	TArray<ECrewDepartment> Pool;
	for (int32 d = 0; d < (int32)ECrewDepartment::Count; ++d) { for (int32 k = Counts[d]; k < Sizes[d]; ++k) { Pool.Add((ECrewDepartment)d); } }
	const int32 SavedNext = NextId;
	for (int32 i = 0; i < 8; ++i)
	{
		const ECrewDepartment Dept = i < Pool.Num() ? Pool[i] : (ECrewDepartment)R.RandRange(1, (int32)ECrewDepartment::Count - 1);
		NextId = 100000 + RecruitDay * 100 + i;   // provisional ids; a real one on signing
		FCrewMember M = MakePerson(R, Dept, RankFromIndex(R, R.RandRange(0, 9), 10), R.RandRange(0, 20));
		M.Backstory = FString::Printf(TEXT("%s; met at %s"), Pick(R, Classes), *StationName);
		Recruits.Add(M);
	}
	NextId = SavedNext;
	Dirty();
}

// ---- queries ----

const FCrewMember* UCrewRosterSubsystem::Find(int32 Id) const
{
	for (const FCrewMember& M : Roster) { if (M.Id == Id) { return &M; } }
	return nullptr;
}
FCrewMember* UCrewRosterSubsystem::FindMutable(int32 Id)
{
	for (FCrewMember& M : Roster) { if (M.Id == Id) { return &M; } }
	return nullptr;
}
const FCrewMember* UCrewRosterSubsystem::Head(ECrewDepartment Dept) const
{
	for (const FCrewMember& M : Roster) { if (M.bHead && M.Department == Dept && M.Condition != ECrewCondition::Dead && M.Condition != ECrewCondition::Transferred) { return &M; } }
	return nullptr;
}
const FCrewMember* UCrewRosterSubsystem::Second(ECrewDepartment Dept) const
{
	for (const FCrewMember& M : Roster) { if (M.bSecond && M.Department == Dept && M.Condition != ECrewCondition::Dead && M.Condition != ECrewCondition::Transferred) { return &M; } }
	return nullptr;
}
const FCrewMember* UCrewRosterSubsystem::FindByName(const FString& Name) const
{
	const FString N = Name.ToLower();
	if (N.IsEmpty()) { return nullptr; }
	for (const FCrewMember& M : Roster) { if (M.FullName().ToLower() == N) { return &M; } }
	for (const FCrewMember& M : Roster) { if (M.LastName.ToLower() == N || M.FirstName.ToLower() == N) { return &M; } }
	for (const FCrewMember& M : Roster) { if (M.FullName().ToLower().Contains(N)) { return &M; } }
	return nullptr;
}
int32 UCrewRosterSubsystem::CountAboard() const
{
	int32 N = 0; for (const FCrewMember& M : Roster) { if (M.IsAboard()) { ++N; } } return N;
}
int32 UCrewRosterSubsystem::CountCondition(ECrewCondition C) const
{
	int32 N = 0; for (const FCrewMember& M : Roster) { if (M.Condition == C) { ++N; } } return N;
}
float UCrewRosterSubsystem::Morale() const
{
	float Sum = 0.f; int32 N = 0;
	for (const FCrewMember& M : Roster) { if (M.IsAboard()) { Sum += M.Mood; ++N; } }
	return N > 0 ? Sum / N : 0.5f;
}
TArray<int32> UCrewRosterSubsystem::InTrouble() const
{
	TArray<TPair<float, int32>> T;
	for (const FCrewMember& M : Roster)
	{
		if (!M.IsAboard()) { continue; }
		float Worst = 1.f; for (float N : M.Needs) { Worst = FMath::Min(Worst, N); }
		const float Score = FMath::Min(Worst, 1.f - M.Stress) - (M.Condition != ECrewCondition::Fit ? 0.3f : 0.f);
		if (Worst < 0.25f || M.Stress > 0.7f || M.Condition != ECrewCondition::Fit) { T.Add({ Score, M.Id }); }
	}
	T.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; });
	TArray<int32> Out; for (const TPair<float, int32>& P : T) { Out.Add(P.Value); } return Out;
}
TArray<int32> UCrewRosterSubsystem::PresentIn(const FString& Compartment, bool bFitOnly) const
{
	TArray<int32> Out;
	for (const FCrewMember& M : Roster) { if (M.IsAboard() && M.Where == Compartment && (!bFitOnly || M.IsFit())) { Out.Add(M.Id); } }
	return Out;
}
TArray<int32> UCrewRosterSubsystem::Candidates(const FString& Compartment) const
{
	// Those in the compartment hit (within the last ship's hour), else anyone fit aboard.
	TArray<int32> Out = (!Compartment.IsEmpty() && Hours - LastHitHours < 1.0) ? PresentIn(Compartment, true) : TArray<int32>();
	if (Out.Num() < 4) { for (const FCrewMember& M : Roster) { if (M.IsFit() && M.IsAboard() && !Out.Contains(M.Id)) { Out.Add(M.Id); } } }
	return Out;
}

// ---- the record ----

void UCrewRosterSubsystem::Record(FCrewMember& M, ECrewEventKind Kind, const FString& Text, bool bMilestone, const TArray<int32>& Others)
{
	FCrewEvent E; E.Stardate = GetStardate(); E.Kind = Kind; E.Text = Text; E.bMilestone = bMilestone; E.Others = Others;
	M.History.Add(E);
	if (M.History.Num() > 80)
	{
		for (int32 i = 0; i < M.History.Num(); ++i) { if (!M.History[i].bMilestone) { M.History.RemoveAt(i); break; } }
		if (M.History.Num() > 120) { M.History.RemoveAt(0); }
	}
	Dirty();
}

void UCrewRosterSubsystem::Remember(FCrewMember& M, const FString& Text, float Weight, bool bPermanent)
{
	FCrewMemory Mem; Mem.Text = Text; Mem.Weight = Weight; Mem.Stardate = GetStardate(); Mem.bPermanent = bPermanent;
	M.Memories.Add(Mem);
	if (M.Memories.Num() > 24) { for (int32 i = 0; i < M.Memories.Num(); ++i) { if (!M.Memories[i].bPermanent) { M.Memories.RemoveAt(i); break; } } }
}

void UCrewRosterSubsystem::Bond(FCrewMember& A, FCrewMember& B, float Delta, ECrewRelation KindIfNew)
{
	auto Apply = [&](FCrewMember& From, int32 To)
	{
		for (FCrewRelationship& R : From.Relationships)
		{
			if (R.Other == To)
			{
				R.Affinity = FMath::Clamp(R.Affinity + Delta, -1.f, 1.f);
				if (R.Kind == ECrewRelation::Acquaintance || R.Kind == ECrewRelation::Friend || R.Kind == ECrewRelation::CloseFriend || R.Kind == ECrewRelation::Rival)
				{
					R.Kind = R.Affinity > 0.7f ? ECrewRelation::CloseFriend : R.Affinity > 0.2f ? ECrewRelation::Friend : R.Affinity < -0.2f ? ECrewRelation::Rival : ECrewRelation::Acquaintance;
				}
				return;
			}
		}
		FCrewRelationship R; R.Other = To; R.Affinity = FMath::Clamp(Delta, -1.f, 1.f); R.Kind = KindIfNew;
		From.Relationships.Add(R);
		if (From.Relationships.Num() > 12) { From.Relationships.Sort([](const FCrewRelationship& X, const FCrewRelationship& Y) { return FMath::Abs(X.Affinity) > FMath::Abs(Y.Affinity); }); From.Relationships.SetNum(12); }
	};
	Apply(A, B.Id);
	Apply(B, A.Id);
}

void UCrewRosterSubsystem::Grow(FCrewMember& M, ECrewSkill S, float XP)
{
	const int32 i = (int32)S;
	if (!M.Skills.IsValidIndex(i)) { return; }
	M.SkillXP[i] += XP;
	// Harder with every point: a crewman's first year is quick, a chief's last ten points a career.
	const float Cost = 8.f + M.Skills[i] * 0.9f;
	while (M.SkillXP[i] >= Cost && M.Skills[i] < 100.f) { M.SkillXP[i] -= Cost; M.Skills[i] += 1.f; }
}

void UCrewRosterSubsystem::UpdateMood(FCrewMember& M)
{
	// The needs weighted by what the person is: rest and purpose for the disciplined, belonging for the sociable,
	// safety for the timid, recognition for the ambitious.
	const float W[] = { 1.f, 1.2f, 0.6f + (100.f - M.Facet(ECrewFacet::Courage)) / 100.f, 0.5f + M.Facet(ECrewFacet::Sociability) / 100.f, 0.6f + M.Facet(ECrewFacet::Discipline) / 150.f, 0.8f, 0.4f + M.Facet(ECrewFacet::Ambition) / 100.f };
	float Sum = 0.f, Tot = 0.f;
	for (int32 i = 0; i < (int32)ECrewNeed::Count && i < M.Needs.Num(); ++i) { Sum += M.Needs[i] * W[i]; Tot += W[i]; }
	float Mood = 0.12f + 0.7f * (Tot > 0.f ? Sum / Tot : 0.5f);
	float Mem = 0.f; for (const FCrewMemory& Me : M.Memories) { Mem += Me.Weight; }
	Mood += FMath::Clamp(Mem, -0.45f, 0.35f);
	int32 Friends = 0; for (const FCrewRelationship& R : M.Relationships) { if (R.Affinity > 0.2f) { ++Friends; } }
	Mood += FMath::Min(Friends, 4) * 0.02f;
	Mood -= M.Stress * 0.25f;
	M.Mood = FMath::Clamp(Mood, 0.f, 1.f);
}

void UCrewRosterSubsystem::Place(FCrewMember& M)
{
	switch (M.Condition)
	{
	case ECrewCondition::Critical: M.Where = TEXT("Sickbay"); M.Activity = ECrewActivity::Sickbay; return;
	case ECrewCondition::Injured: M.Where = TEXT("Sickbay"); M.Activity = ECrewActivity::Sickbay; return;
	case ECrewCondition::OnLeave: M.Where = TEXT("Ashore"); M.Activity = ECrewActivity::Leave; return;
	case ECrewCondition::Away: M.Where = TEXT("Away team"); M.Activity = ECrewActivity::Away; return;
	case ECrewCondition::Dead: case ECrewCondition::Transferred: M.Where = TEXT("-"); M.Activity = ECrewActivity::Away; return;
	default: break;
	}
	const int32 Hour = GetHourOfDay();
	const int32 Start = (int32)M.Watch * 8;
	const int32 Since = (Hour - Start + 24) % 24;   // hours since the watch began
	const bool bOfficer = IsOfficer(M.Rank);
	const FString Quarters = bOfficer ? TEXT("Officers' quarters") : TEXT("Crew quarters");
	if (Since < 8) { M.Where = M.Posting; M.Activity = ECrewActivity::OnWatch; }
	else if (Since < 9) { M.Where = Quarters; M.Activity = ECrewActivity::Mess; }
	else if (Since < 16) { M.Where = Quarters; M.Activity = ECrewActivity::Sleeping; }
	else if (Since < 17) { M.Where = Quarters; M.Activity = ECrewActivity::Mess; }
	else { M.Where = M.HasTrait(TEXT("Bookish")) || M.HasTrait(TEXT("Loner")) ? Quarters : TEXT("Crew quarters"); M.Activity = ECrewActivity::Recreation; }
}

// ---- the ship's day ----

void UCrewRosterSubsystem::AdvanceSeconds(float Seconds, const FCrewShipState& Ship)
{
	ReportCooldownSeconds = FMath::Max(ReportCooldownSeconds - Seconds, 0.f);
	HourAccumulator += Seconds / FMath::Max(SecondsPerShipHour, 1.f);
	int32 Guard = 0;
	while (HourAccumulator >= 1.0 && Guard++ < 48)
	{
		HourAccumulator -= 1.0;
		Hours += 1.0;
		LiveHour(Ship);
		if (GetHourOfDay() == 0) { LiveDay(Ship); }
	}
}

void UCrewRosterSubsystem::DevAdvanceHours(int32 HoursToLive, const FCrewShipState& Ship)
{
	for (int32 h = 0; h < HoursToLive; ++h)
	{
		Hours += 1.0;
		LiveHour(Ship);
		if (GetHourOfDay() == 0) { LiveDay(Ship); }
	}
	Save();
}

void UCrewRosterSubsystem::LiveHour(const FCrewShipState& Ship)
{
	const ECrewWatch Duty = WatchOnDuty();
	const bool bRed = Ship.Alert == EAlertState::Red, bYellow = Ship.Alert == EAlertState::Yellow;
	for (FCrewMember& M : Roster)
	{
		if (M.Condition == ECrewCondition::Dead || M.Condition == ECrewCondition::Transferred) { continue; }
		Place(M);
		auto N = [&](ECrewNeed Need) -> float& { return M.Needs[(int32)Need]; };
		const float Disc = M.Facet(ECrewFacet::Discipline) / 100.f, Soc = M.Facet(ECrewFacet::Sociability) / 100.f, Cour = M.Facet(ECrewFacet::Courage) / 100.f, Res = M.Facet(ECrewFacet::Resolve) / 100.f;
		switch (M.Activity)
		{
		case ECrewActivity::OnWatch:
			N(ECrewNeed::Purpose) += 0.05f * (0.5f + Disc); N(ECrewNeed::Rest) -= 0.035f; N(ECrewNeed::Recreation) -= 0.012f; N(ECrewNeed::Belonging) += 0.01f;
			Grow(M, SkillOf(M.Department), 1.f * (0.6f + M.Facet(ECrewFacet::Curiosity) / 200.f));
			if (M.HasTrait(TEXT("Veteran")) && !bRed && !bYellow) { N(ECrewNeed::Purpose) -= 0.02f; }
			break;
		case ECrewActivity::Sleeping: N(ECrewNeed::Rest) += 0.11f; N(ECrewNeed::Belonging) -= 0.008f; break;
		case ECrewActivity::Mess: N(ECrewNeed::Belonging) += 0.06f * (0.5f + Soc); N(ECrewNeed::Rest) += 0.01f; N(ECrewNeed::Recreation) += 0.02f; break;
		case ECrewActivity::Recreation: N(ECrewNeed::Recreation) += 0.07f; N(ECrewNeed::Belonging) += 0.03f * (M.HasTrait(TEXT("Loner")) ? 0.f : Soc + 0.5f); N(ECrewNeed::Purpose) -= 0.01f; if (M.HasTrait(TEXT("Loner"))) { N(ECrewNeed::Rest) += 0.02f; } break;
		case ECrewActivity::Sickbay: N(ECrewNeed::Rest) += 0.05f; N(ECrewNeed::Purpose) -= 0.02f; N(ECrewNeed::Safety) -= 0.01f; break;
		case ECrewActivity::Leave: for (float& V : M.Needs) { V += 0.08f; } break;
		default: break;
		}
		N(ECrewNeed::Recognition) -= 0.004f;
		if (bRed) { N(ECrewNeed::Safety) -= 0.05f * (1.4f - Cour); N(ECrewNeed::Rest) -= 0.015f; }
		else if (bYellow) { N(ECrewNeed::Safety) -= 0.012f; }
		else { N(ECrewNeed::Safety) += 0.03f; }
		if (Ship.bUnderFire) { N(ECrewNeed::Safety) -= 0.08f * (1.4f - Cour); }
		const float HealthTarget = M.Condition == ECrewCondition::Fit ? 1.f : (M.Condition == ECrewCondition::Injured ? 0.45f : 0.15f);
		N(ECrewNeed::Health) = FMath::FInterpConstantTo(N(ECrewNeed::Health), HealthTarget, 1.f, 0.12f);
		for (float& V : M.Needs) { V = FMath::Clamp(V, 0.f, 1.f); }
		// Stress.
		if (bRed) { M.Stress += 0.04f * (1.3f - Cour); }
		if (Ship.bUnderFire) { M.Stress += 0.08f * (1.3f - Cour) * (M.HasTrait(TEXT("Veteran")) ? 0.5f : 1.f); }
		if (!bRed && !Ship.bUnderFire) { M.Stress -= 0.03f * (0.5f + Res); }
		if (M.Condition == ECrewCondition::Critical) { M.Stress += 0.02f; }
		M.Stress = FMath::Clamp(M.Stress, 0.f, 1.f);
		// Memories fade; the wound and leave clocks run.
		for (int32 i = M.Memories.Num() - 1; i >= 0; --i)
		{
			FCrewMemory& Me = M.Memories[i];
			if (Me.bPermanent) { Me.Weight = FMath::FInterpConstantTo(Me.Weight, Me.Weight < 0.f ? -0.04f : 0.04f, 1.f, 0.002f * (MourningDays > 0 ? 3.f : 1.f)); continue; }
			Me.Weight *= MourningDays > 0 && Me.Weight < 0.f ? 0.97f : 0.985f;
			if (FMath::Abs(Me.Weight) < 0.01f) { M.Memories.RemoveAt(i); }
		}
		if (M.Condition == ECrewCondition::OnLeave) { M.ConditionHours -= 1.f; if (M.ConditionHours <= 0.f) { M.Condition = ECrewCondition::Fit; Record(M, ECrewEventKind::Leave, TEXT("Back aboard from leave.")); } }
		else if (M.Condition == ECrewCondition::Injured || M.Condition == ECrewCondition::Critical) { M.ConditionHours = FMath::Max(M.ConditionHours - 1.f, 0.f); }
		UpdateMood(M);
		PersonalMoment(M, Ship);
	}
	Dirty();
}

void UCrewRosterSubsystem::LiveDay(const FCrewShipState& Ship)
{
	const int32 Day = GetDay();
	if (MourningDays > 0) { --MourningDays; }
	int32 Low = 0, Pressed = 0;
	for (FCrewMember& M : Roster)
	{
		if (!M.IsAboard()) { continue; }
		if (M.Mood < 0.35f) { ++Low; }
		for (FCrewWant& W : M.Wants)
		{
			if (W.bFear || W.bGranted) { continue; }
			const int32 Waited = Day - W.SinceDay;
			if (Waited > 20 && Waited % 10 == 0) { Remember(M, FString::Printf(TEXT("Still waiting for %s"), *W.Text), -0.06f); ++Pressed; }
		}
	}
	const FCrewMember* XO = Head(ECrewDepartment::Command);
	if (XO)
	{
		const float Mor = Morale();
		FString Line = FString::Printf(TEXT("Morning report, stardate %s: morale %s (%.0f%%), %d aboard"), *StardateText(), Mor > 0.7f ? TEXT("high") : Mor > 0.5f ? TEXT("fair") : Mor > 0.35f ? TEXT("low") : TEXT("poor"), Mor * 100.f, CountAboard());
		if (Low > 0) { Line += FString::Printf(TEXT(", %d in low spirits"), Low); }
		if (Pressed > 0) { Line += FString::Printf(TEXT(", %d requests outstanding"), Pressed); }
		Line += TEXT(".");
		Report(ECrewDepartment::Command, Line, Mor < 0.35f ? FLinearColor(1.f, 0.6f, 0.4f) : FLinearColor(0.75f, 0.9f, 1.f), true);
	}
	RequestsToday = 0;
	Save();
}

void UCrewRosterSubsystem::PersonalMoment(FCrewMember& M, const FCrewShipState& Ship)
{
	// A small chance an hour of something personal; the kind by traits, needs and company.
	if (Rng.FRand() > 0.014f) { return; }
	const bool bOnWatch = M.Activity == ECrewActivity::OnWatch;
	const bool bRed = Ship.Alert == EAlertState::Red;
	struct FKind { int32 K; float W; };
	TArray<FKind> Kinds;
	Kinds.Add({ 0, 1.0f * (0.5f + M.Facet(ECrewFacet::Sociability) / 100.f) });            // friendship
	Kinds.Add({ 1, 0.5f + (M.Stress > 0.5f ? 0.8f : 0.f) + (M.HasTrait(TEXT("WoundedPride")) ? 0.4f : 0.f) });   // quarrel
	Kinds.Add({ 2, (M.HasTrait(TEXT("Homesick")) ? 3.f : 0.6f) });                           // letter
	if (bOnWatch && M.Department == ECrewDepartment::Science) { Kinds.Add({ 3, 0.6f + M.Facet(ECrewFacet::Curiosity) / 100.f }); }   // breakthrough
	if (bOnWatch && M.Department == ECrewDepartment::Engineering) { Kinds.Add({ 4, 0.6f + M.Facet(ECrewFacet::Discipline) / 100.f }); } // fine work
	if (bRed) { Kinds.Add({ 5, 1.2f * (1.5f - M.Facet(ECrewFacet::Courage) / 100.f) }); }    // a scare
	if (M.Facet(ECrewFacet::Ambition) > 55.f && RequestsToday < 2) { Kinds.Add({ 6, 0.5f }); }   // a request to see the captain, two a day at most
	Kinds.Add({ 7, 0.4f });                                                                   // off colour
	if (!M.bHead) { Kinds.Add({ 8, 0.5f + (M.Facet(ECrewFacet::Curiosity) / 200.f) }); }     // mentoring
	if (M.HasTrait(TEXT("ChessPlayer")) || M.HasTrait(TEXT("Musician")) || M.HasTrait(TEXT("Gambler"))) { Kinds.Add({ 9, 1.2f }); }   // the hobby
	if (M.Stress > 0.8f && M.Mood < 0.35f) { Kinds.Add({ 10, 3.f }); }                       // breakdown
	if (M.Birthday == GetDay() % 365 && M.Activity == ECrewActivity::Mess) { Kinds.Add({ 11, 6.f }); }   // birthday
	float Tot = 0.f; for (const FKind& K : Kinds) { Tot += K.W; }
	float X = Rng.FRandRange(0.f, Tot); int32 Kind = 0;
	for (const FKind& K : Kinds) { X -= K.W; if (X <= 0.f) { Kind = K.K; break; } }
	auto Colleague = [&](bool bSameDept) -> FCrewMember*
	{
		for (int32 Try = 0; Try < 12; ++Try)
		{
			FCrewMember& O = Roster[Rng.RandRange(0, Roster.Num() - 1)];
			if (O.Id != M.Id && O.IsAboard() && (!bSameDept || (O.Department == M.Department && O.Watch == M.Watch))) { return &O; }
		}
		return nullptr;
	};
	switch (Kind)
	{
	case 0: if (FCrewMember* O = Colleague(Rng.FRand() < 0.75f))
		{
			const FString Hobby = Pick(Rng, Hobbies);
			Bond(M, *O, 0.25f, ECrewRelation::Friend);
			Record(M, ECrewEventKind::Moment, FString::Printf(TEXT("Struck up a friendship with %s over %s."), *O->Styled(), *Hobby), false, { O->Id });
			Record(*O, ECrewEventKind::Moment, FString::Printf(TEXT("Struck up a friendship with %s over %s."), *M.Styled(), *Hobby), false, { M.Id });
			Remember(M, TEXT("A new friend"), 0.06f); Remember(*O, TEXT("A new friend"), 0.06f);
		} break;
	case 1: {
			FCrewMember* O = nullptr;
			for (const FCrewRelationship& R : M.Relationships) { if (R.Kind == ECrewRelation::Rival) { O = FindMutable(R.Other); break; } }
			if (!O) { O = Colleague(true); }
			if (O && O->IsAboard())
			{
				Bond(M, *O, -0.15f, ECrewRelation::Rival);
				Record(M, ECrewEventKind::Moment, FString::Printf(TEXT("Words with %s on the %s watch."), *O->Styled(), *WatchName(M.Watch).ToLower()), false, { O->Id });
				Record(*O, ECrewEventKind::Moment, FString::Printf(TEXT("Words with %s."), *M.Styled()), false, { M.Id });
				Remember(M, TEXT("A quarrel"), -0.08f); Remember(*O, TEXT("A quarrel"), -0.06f);
				M.Stress = FMath::Min(M.Stress + 0.05f, 1.f);
			}
		} break;
	case 2: Record(M, ECrewEventKind::Letter, FString::Printf(TEXT("A letter from home, %s."), *M.Home)); Remember(M, TEXT("A letter from home"), M.HasTrait(TEXT("Homesick")) ? 0.2f : 0.1f);
		for (FCrewWant& W : M.Wants) { if (W.Tag == TEXT("Letter") && !W.bGranted) { W.bGranted = true; Remember(M, TEXT("Word from home at last"), 0.15f); } }
		break;
	case 3: Record(M, ECrewEventKind::Discovery, TEXT("A small breakthrough at the station: a pattern in the sensor returns nobody had seen.")); Remember(M, TEXT("A breakthrough"), 0.15f); Grow(M, ECrewSkill::Science, 8.f);
		if (Rng.FRand() < 0.25f) { Report(ECrewDepartment::Science, FString::Printf(TEXT("%s has found something in the sensor returns worth a second look."), *M.Styled())); }
		break;
	case 4: Record(M, ECrewEventKind::Moment, TEXT("A fine piece of work on the watch: a fault found before it showed.")); Remember(M, TEXT("Good work noticed"), 0.1f); Grow(M, ECrewSkill::Engineering, 5.f);
		if (Rng.FRand() < 0.15f) { Report(ECrewDepartment::Engineering, FString::Printf(TEXT("%s caught a fault in the %s before it showed. Good hands."), *M.Styled(), *M.Posting.ToLower())); }
		break;
	case 5: M.Stress = FMath::Min(M.Stress + 0.15f, 1.f); Remember(M, TEXT("A scare at red alert"), -0.12f); Record(M, ECrewEventKind::Moment, TEXT("A bad moment at red alert; it passed."));
		for (FCrewWant& W : M.Wants) { if (W.bFear && M.Facet(ECrewFacet::Courage) < 40.f && Rng.FRand() < 0.3f) { Remember(M, FString::Printf(TEXT("Thought of %s"), *W.Text), -0.1f); } }
		break;
	case 6: {
			const FCrewWant* Want = nullptr; for (const FCrewWant& W : M.Wants) { if (!W.bFear && !W.bGranted) { Want = &W; break; } }
			if (Want) { ++RequestsToday; Report(ECrewDepartment::Command, FString::Printf(TEXT("%s asks for a word with the captain about %s."), *M.Styled(), *Want->Text)); Record(M, ECrewEventKind::Moment, FString::Printf(TEXT("Asked to see the captain about %s."), *Want->Text)); }
		} break;
	case 7: M.Needs[(int32)ECrewNeed::Health] = FMath::Max(M.Needs[(int32)ECrewNeed::Health] - 0.25f, 0.f); Remember(M, TEXT("Off colour"), -0.05f); break;
	case 8: if (const FCrewMember* H = Head(M.Department))
		{
			if (FCrewMember* HM = FindMutable(H->Id)) { Grow(M, SkillOf(M.Department), 6.f); Bond(M, *HM, 0.08f, ECrewRelation::Acquaintance); Record(M, ECrewEventKind::Moment, FString::Printf(TEXT("An hour with %s over the %s procedures."), *HM->Styled(), *DepartmentName(M.Department).ToLower()), false, { HM->Id }); }
		} break;
	case 9: if (FCrewMember* O = Colleague(false))
		{
			const TCHAR* What = M.HasTrait(TEXT("ChessPlayer")) ? TEXT("a game of chess") : M.HasTrait(TEXT("Musician")) ? TEXT("an evening of music in the mess") : TEXT("a hand of cards");
			Bond(M, *O, 0.1f, ECrewRelation::Friend); Remember(M, What, 0.07f); Remember(*O, What, 0.05f);
			Record(M, ECrewEventKind::Moment, FString::Printf(TEXT("%s with %s."), What, *O->Styled()), false, { O->Id });
		} break;
	case 10: M.Stress = FMath::Max(M.Stress - 0.35f, 0.f);
		{
			const bool bMistake = bOnWatch;
			Record(M, ECrewEventKind::Moment, bMistake ? TEXT("Lost the thread on watch: a mistake the station caught, and a dressing down.") : TEXT("An outburst in the mess; walked it off."), true);
			Remember(M, TEXT("Came apart for a moment"), -0.15f);
			Report(bMistake ? M.Department : ECrewDepartment::Command, FString::Printf(TEXT("%s %s; I'd keep an eye on %s."), *M.Styled(), bMistake ? TEXT("fumbled the station on watch") : TEXT("lost their temper off watch"), M.Pronoun == TEXT("they") ? TEXT("them") : (M.Pronoun == TEXT("she") ? TEXT("her") : TEXT("him"))), FLinearColor(1.f, 0.75f, 0.4f));
		} break;
	case 11: Record(M, ECrewEventKind::Moment, TEXT("A birthday in the mess: a cake from the galley and a song.")); Remember(M, TEXT("A birthday aboard"), 0.2f); M.Needs[(int32)ECrewNeed::Belonging] = FMath::Min(M.Needs[(int32)ECrewNeed::Belonging] + 0.25f, 1.f);
		if (Rng.FRand() < 0.3f) { Report(ECrewDepartment::Services, FString::Printf(TEXT("A birthday in the mess for %s; the galley did a cake."), *M.Styled())); }
		break;
	default: break;
	}
}

void UCrewRosterSubsystem::Report(ECrewDepartment From, const FString& Text, const FLinearColor& Color, bool bAlways)
{
	if (!bAlways && ReportCooldownSeconds > 0.f && Reports.Num() > 2) { return; }
	FCrewReport R; R.From = From; R.Text = Text; R.Color = Color;
	Reports.Add(R);
	ReportCooldownSeconds = 12.f;
}

// ---- the ship's events ----

void UCrewRosterSubsystem::NoteAlert(EAlertState NewAlert)
{
	if (NewAlert == Alert) { return; }
	Alert = NewAlert; AlertSinceHours = Hours;
}

void UCrewRosterSubsystem::NoteHitCompartment(const FString& Compartment)
{
	LastHitCompartment = Compartment; LastHitHours = Hours;
}

void UCrewRosterSubsystem::Wound(FCrewMember& M, bool bCritical, const FString& Where)
{
	M.Condition = bCritical ? ECrewCondition::Critical : ECrewCondition::Injured;
	M.ConditionHours = bCritical ? 72.f : 24.f;
	++M.Service.Wounds;
	Record(M, ECrewEventKind::Wounded, FString::Printf(TEXT("%s in %s."), bCritical ? TEXT("Critically wounded") : TEXT("Wounded"), *Where), true);
	Remember(M, bCritical ? TEXT("Nearly died") : TEXT("Wounded"), bCritical ? -0.25f : -0.12f);
	M.Stress = FMath::Min(M.Stress + (bCritical ? 0.3f : 0.15f), 1.f);
	Place(M);
}

void UCrewRosterSubsystem::Kill(FCrewMember& M, const FString& Where)
{
	M.Condition = ECrewCondition::Dead;
	Record(M, ECrewEventKind::Died, FString::Printf(TEXT("Killed in %s, stardate %s."), *Where, *StardateText()), true);
	Grieve(M);
	Place(M);
	// The survivors in the compartment carry it.
	for (int32 Id : PresentIn(Where, false))
	{
		if (FCrewMember* S = FindMutable(Id))
		{
			if (S->Id == M.Id) { continue; }
			if (!S->HasTrait(TEXT("Survivor")) && Rng.FRand() < 0.5f) { S->Traits.Add(TEXT("Survivor")); S->Facets[(int32)ECrewFacet::Resolve] = FMath::Min(S->Facets[(int32)ECrewFacet::Resolve] + 8.f, 100.f); Record(*S, ECrewEventKind::Moment, FString::Printf(TEXT("Walked out of %s when %s did not."), *Where, *M.Styled()), true, { M.Id }); }
			Remember(*S, FString::Printf(TEXT("%s died beside me"), *M.FullName()), -0.2f);
		}
	}
	if (M.bHead) { PromoteToHead(M.Department, FString::Printf(TEXT("%s was killed"), *M.Styled())); }
	Lost.Add(M);
}

void UCrewRosterSubsystem::Grieve(const FCrewMember& Dead)
{
	for (const FCrewRelationship& R : Dead.Relationships)
	{
		if (R.Affinity <= 0.2f) { continue; }
		if (FCrewMember* F = FindMutable(R.Other))
		{
			const float Depth = R.Affinity;
			Remember(*F, FString::Printf(TEXT("%s is gone"), *Dead.FullName()), -0.35f * Depth, Depth > 0.6f);
			Record(*F, ECrewEventKind::Bereaved, FString::Printf(TEXT("Lost %s, %s."), *Dead.Styled(), *RelationName(R.Kind)), true, { Dead.Id });
			F->Stress = FMath::Min(F->Stress + 0.2f * Depth, 1.f);
			if (Depth > 0.6f && Rng.FRand() < 0.4f) { FCrewWant W; W.Tag = TEXT("LosingFriend"); W.Text = TEXT("losing another friend"); W.bFear = true; W.SinceDay = GetDay(); F->Wants.Add(W); }
		}
	}
}

void UCrewRosterSubsystem::Heal(FCrewMember& M)
{
	const bool bWasCritical = M.Condition == ECrewCondition::Critical;
	M.Condition = ECrewCondition::Fit; M.ConditionHours = 0.f;
	Record(M, ECrewEventKind::Recovered, bWasCritical ? TEXT("Out of danger and back on the roster.") : TEXT("Fit for duty again."));
	Place(M);
}

void UCrewRosterSubsystem::PromoteToHead(ECrewDepartment Dept, const FString& Why)
{
	const FCrewMember* S = Second(Dept);
	if (!S)
	{
		// The most skilled fit officer of the department.
		float Best = -1.f; for (const FCrewMember& M : Roster) { if (M.Department == Dept && M.IsAboard() && !M.bHead && M.Skill(SkillOf(Dept)) > Best) { Best = M.Skill(SkillOf(Dept)); S = &M; } }
	}
	if (!S) { return; }
	FCrewMember* New = FindMutable(S->Id);
	for (FCrewMember& M : Roster) { if (M.Department == Dept && M.bHead) { M.bHead = false; } }
	New->bHead = true; New->bSecond = false;
	if (New->Rank < ECrewRank::Lieutenant) { New->Rank = ECrewRank::Lieutenant; ++New->Service.Promotions; }
	New->Posting = PostingFor(Rng, Dept, 0);
	Record(*New, ECrewEventKind::Promoted, FString::Printf(TEXT("Took the %s department when %s."), *DepartmentName(Dept).ToLower(), *Why), true);
	Remember(*New, TEXT("Stepped up"), 0.1f);
	// A new second: the next best.
	float Best = -1.f; FCrewMember* Next = nullptr;
	for (FCrewMember& M : Roster) { if (M.Department == Dept && M.IsAboard() && !M.bHead && M.Skill(SkillOf(Dept)) > Best) { Best = M.Skill(SkillOf(Dept)); Next = &M; } }
	if (Next) { Next->bSecond = true; }
	Report(Dept, FString::Printf(TEXT("%s has the %s department now."), *New->Styled(), *DepartmentName(Dept).ToLower()), FLinearColor(1.f, 0.8f, 0.5f), true);
	Dirty();
}

void UCrewRosterSubsystem::SyncCasualties(int32 Fit, int32 Injured, int32 Critical, int32 Dead, TArray<FString>& OutLines)
{
	// Match the medical model's counts with people: the dead and the newly wounded from the compartment hit,
	// the healed from those longest under treatment.
	const int32 HaveDead = FMath::Max(Lost.Num() - DeadBase, 0), HaveCrit = CountCondition(ECrewCondition::Critical), HaveInj = CountCondition(ECrewCondition::Injured);
	TArray<FString> Killed, Wounded, Healed;
	TArray<int32> Pool = Candidates(LastHitCompartment);
	const FString Where = LastHitCompartment.IsEmpty() ? TEXT("the ship") : LastHitCompartment;
	for (int32 n = HaveDead; n < Dead && Pool.Num() > 0; ++n)
	{
		const int32 k = Rng.RandRange(0, Pool.Num() - 1);
		if (FCrewMember* M = FindMutable(Pool[k])) { Kill(*M, Where); Killed.Add(M->Styled()); }
		Pool.RemoveAtSwap(k);
	}
	for (int32 n = HaveCrit; n < Critical && Pool.Num() > 0; ++n)
	{
		const int32 k = Rng.RandRange(0, Pool.Num() - 1);
		if (FCrewMember* M = FindMutable(Pool[k])) { Wound(*M, true, Where); Wounded.Add(M->Styled()); }
		Pool.RemoveAtSwap(k);
	}
	for (int32 n = Critical; n < HaveCrit; ++n)
	{
		// Stabilised: the critical longest under treatment becomes wounded.
		FCrewMember* Best = nullptr; for (FCrewMember& M : Roster) { if (M.Condition == ECrewCondition::Critical && (!Best || M.ConditionHours < Best->ConditionHours)) { Best = &M; } }
		if (Best) { Best->Condition = ECrewCondition::Injured; Best->ConditionHours = 24.f; Record(*Best, ECrewEventKind::Recovered, TEXT("Out of danger.")); }
	}
	const int32 NowInj = CountCondition(ECrewCondition::Injured);
	for (int32 n = NowInj; n < Injured && Pool.Num() > 0; ++n)
	{
		const int32 k = Rng.RandRange(0, Pool.Num() - 1);
		if (FCrewMember* M = FindMutable(Pool[k])) { Wound(*M, false, Where); Wounded.Add(M->Styled()); }
		Pool.RemoveAtSwap(k);
	}
	for (int32 n = Injured; n < NowInj; ++n)
	{
		FCrewMember* Best = nullptr; for (FCrewMember& M : Roster) { if (M.Condition == ECrewCondition::Injured && (!Best || M.ConditionHours < Best->ConditionHours)) { Best = &M; } }
		if (Best) { Heal(*Best); Healed.Add(Best->Styled()); }
	}
	auto Join = [](const TArray<FString>& Names) { FString S; for (int32 i = 0; i < Names.Num() && i < 3; ++i) { S += (i ? TEXT(", ") : TEXT("")) + Names[i]; } if (Names.Num() > 3) { S += FString::Printf(TEXT(" and %d more"), Names.Num() - 3); } return S; };
	if (Killed.Num()) { OutLines.Add(FString::Printf(TEXT("Lost in %s: %s."), *Where, *Join(Killed))); }
	if (Wounded.Num()) { OutLines.Add(FString::Printf(TEXT("Wounded in %s: %s."), *Where, *Join(Wounded))); }
	if (Killed.Num() || Wounded.Num()) { Dirty(); Save(); }
	if (Healed.Num() && Rng.FRand() < 0.3f) { OutLines.Add(FString::Printf(TEXT("Back on their feet: %s."), *Join(Healed))); }
}

void UCrewRosterSubsystem::NoteHeadHurt(ECrewDepartment Dept, bool bLost, float Seconds)
{
	const FCrewMember* H = Head(Dept);
	if (!H) { return; }
	FCrewMember* M = FindMutable(H->Id);
	const FString Where = LastHitCompartment.IsEmpty() ? TEXT("the ship") : LastHitCompartment;
	if (bLost) { Kill(*M, Where); }
	else if (M->Condition == ECrewCondition::Fit) { Wound(*M, false, Where); M->ConditionHours = Seconds / FMath::Max(SecondsPerShipHour, 1.f); }
	Dirty(); Save();
}

void UCrewRosterSubsystem::NoteHeadRecovered(ECrewDepartment Dept)
{
	if (const FCrewMember* H = Head(Dept)) { if (FCrewMember* M = FindMutable(H->Id)) { if (M->Condition == ECrewCondition::Injured || M->Condition == ECrewCondition::Critical) { Heal(*M); } } }
}

void UCrewRosterSubsystem::NoteRepair(const FString& SystemName, bool bUnderFire)
{
	for (FCrewMember& M : Roster)
	{
		if (M.Department == ECrewDepartment::Engineering && M.Activity == ECrewActivity::OnWatch && M.IsFit())
		{
			Grow(M, ECrewSkill::Engineering, bUnderFire ? 6.f : 3.f);
			if (bUnderFire) { Remember(M, FString::Printf(TEXT("%s back under fire"), *SystemName), 0.08f); }
		}
	}
}

void UCrewRosterSubsystem::NoteScan(const FString& Label, int32 Points, bool bDiscovery)
{
	for (FCrewMember& M : Roster)
	{
		if (M.Department == ECrewDepartment::Science && M.Activity == ECrewActivity::OnWatch && M.IsFit())
		{
			Grow(M, ECrewSkill::Science, FMath::Clamp(Points / 30.f, 1.f, 8.f));
			if (bDiscovery) { Remember(M, FString::Printf(TEXT("Part of the %s discovery"), *Label), 0.12f + M.Facet(ECrewFacet::Curiosity) / 800.f); Record(M, ECrewEventKind::Discovery, FString::Printf(TEXT("On the science watch for the %s discovery."), *Label)); }
		}
	}
	if (bDiscovery) { Dirty(); }
}

void UCrewRosterSubsystem::NoteSight(FName Tag, const FString& Text)
{
	for (FCrewMember& M : Roster)
	{
		if (!M.IsAboard()) { continue; }
		for (FCrewWant& W : M.Wants)
		{
			if (!W.bFear && !W.bGranted && W.Tag == Tag) { W.bGranted = true; Remember(M, Text, 0.2f); Record(M, ECrewEventKind::Moment, FString::Printf(TEXT("%s: %s, as %s had wanted."), *Text, *Text, *M.Pronoun)); }
		}
		if (M.Facet(ECrewFacet::Curiosity) > 70.f) { Remember(M, Text, 0.05f); }
	}
	Dirty();
}

void UCrewRosterSubsystem::NoteStarbase(const FString& StationName)
{
	if (RecruitStation != StationName || RecruitDay != GetDay()) { MakeRecruits(StationName); }
	// Letters come aboard with the station's mail.
	for (FCrewMember& M : Roster)
	{
		if (M.IsAboard() && (M.HasTrait(TEXT("Homesick")) || Rng.FRand() < 0.3f))
		{
			Remember(M, TEXT("Mail from the station"), M.HasTrait(TEXT("Homesick")) ? 0.15f : 0.06f);
			for (FCrewWant& W : M.Wants) { if (W.Tag == TEXT("Letter") && !W.bGranted) { W.bGranted = true; } }
		}
	}
	Dirty();
}

void UCrewRosterSubsystem::NoteResupplyDone(const FString& StationName, TArray<FString>& OutLines)
{
	// The wounded transferred ashore come back fit (the medical model's reading), and the dead are replaced from
	// the station's pool: new names signing on for the vacancies.
	int32 Restored = 0;
	for (FCrewMember& M : Roster) { if (M.Condition == ECrewCondition::Injured || M.Condition == ECrewCondition::Critical) { Heal(M); ++Restored; } }
	int32 Counts[(int32)ECrewDepartment::Count] = { 0 };
	for (const FCrewMember& M : Roster) { if (M.Condition != ECrewCondition::Dead && M.Condition != ECrewCondition::Transferred) { ++Counts[(int32)M.Department]; } }
	const int32 Sizes[(int32)ECrewDepartment::Count] = { 6, 24, 60, 150, 70, 30, 36, 60, 66 };
	FRandomStream R(GetTypeHash(StationName) ^ Seed ^ (int32)Hours);
	TArray<FString> Names;
	for (int32 d = 0; d < (int32)ECrewDepartment::Count; ++d)
	{
		for (int32 k = Counts[d]; k < Sizes[d]; ++k)
		{
			FCrewMember M = MakePerson(R, (ECrewDepartment)d, RankFromIndex(R, R.RandRange(3, 9), 10), R.RandRange(3, 30));
			M.Backstory = FString::Printf(TEXT("%s; signed on at %s"), Pick(R, Classes), *StationName);
			Record(M, ECrewEventKind::Joined, FString::Printf(TEXT("Signed on at %s to fill a vacancy in %s."), *StationName, *DepartmentName((ECrewDepartment)d).ToLower()), true);
			Place(M);
			Roster.Add(M);
			Names.Add(M.Styled());
		}
	}
	for (int32 i = Roster.Num() - 1; i >= 0; --i) { if (Roster[i].Condition == ECrewCondition::Dead) { Roster.RemoveAt(i); } }
	DeadBase = Lost.Num();   // the medical model's count starts again from nothing
	if (Restored > 0) { OutLines.Add(FString::Printf(TEXT("%d wounded treated ashore and back aboard."), Restored)); }
	if (Names.Num() > 0) { OutLines.Add(FString::Printf(TEXT("%d replacements signed on at %s%s."), Names.Num(), *StationName, Names.Num() <= 3 ? *(TEXT(": ") + FString::Join(Names, TEXT(", "))) : TEXT(""))); }
	Dirty(); Save();
}

// ---- the captain's hand ----

bool UCrewRosterSubsystem::Promote(int32 Id, FString& OutReply)
{
	FCrewMember* M = FindMutable(Id);
	if (!M) { OutReply = TEXT("No such crew member"); return false; }
	if (M->Rank >= ECrewRank::Commander) { OutReply = FString::Printf(TEXT("%s holds the highest rank aboard short of yours"), *M->Styled()); return false; }
	M->Rank = (ECrewRank)((int32)M->Rank + 1);
	++M->Service.Promotions;
	Record(*M, ECrewEventKind::Promoted, FString::Printf(TEXT("Promoted to %s by the captain."), *RankName(M->Rank)), true);
	Remember(*M, TEXT("Promoted"), 0.25f + M->Facet(ECrewFacet::Ambition) / 400.f);
	M->Needs[(int32)ECrewNeed::Recognition] = 1.f;
	for (FCrewWant& W : M->Wants) { if (W.Tag == TEXT("Promotion") && !W.bGranted) { W.bGranted = true; Remember(*M, TEXT("The promotion I wanted"), 0.15f, true); } }
	// Rivals resent it.
	for (const FCrewRelationship& R : M->Relationships) { if (R.Kind == ECrewRelation::Rival) { if (FCrewMember* O = FindMutable(R.Other)) { Remember(*O, FString::Printf(TEXT("%s promoted over me"), *M->FullName()), -0.1f); } } }
	OutReply = FString::Printf(TEXT("%s, with the captain's congratulations"), *M->Styled());
	Dirty(); Save();
	return true;
}

bool UCrewRosterSubsystem::Demote(int32 Id, FString& OutReply)
{
	FCrewMember* M = FindMutable(Id);
	if (!M) { OutReply = TEXT("No such crew member"); return false; }
	if (M->Rank == ECrewRank::Crewman) { OutReply = TEXT("Nowhere lower to go"); return false; }
	M->Rank = (ECrewRank)((int32)M->Rank - 1);
	Record(*M, ECrewEventKind::Demoted, FString::Printf(TEXT("Reduced to %s by the captain."), *RankName(M->Rank)), true);
	Remember(*M, TEXT("Demoted"), -0.3f, M->HasTrait(TEXT("WoundedPride")));
	M->Stress = FMath::Min(M->Stress + 0.2f, 1.f);
	OutReply = FString::Printf(TEXT("%s, reduced in rank"), *M->Styled());
	Dirty(); Save();
	return true;
}

bool UCrewRosterSubsystem::Commend(int32 Id, const FString& For, FString& OutReply)
{
	FCrewMember* M = FindMutable(Id);
	if (!M) { OutReply = TEXT("No such crew member"); return false; }
	++M->Service.Commendations;
	Record(*M, ECrewEventKind::Commended, FString::Printf(TEXT("Commended by the captain%s."), For.IsEmpty() ? TEXT("") : *(TEXT(" for ") + For)), true);
	Remember(*M, TEXT("Commended by the captain"), 0.2f);
	M->Needs[(int32)ECrewNeed::Recognition] = FMath::Min(M->Needs[(int32)ECrewNeed::Recognition] + 0.5f, 1.f);
	for (FCrewWant& W : M->Wants) { if (W.Tag == TEXT("Commendation") && !W.bGranted) { W.bGranted = true; } }
	OutReply = FString::Printf(TEXT("%s commended; it goes in the record"), *M->Styled());
	Dirty(); Save();
	return true;
}

bool UCrewRosterSubsystem::Reprimand(int32 Id, const FString& For, FString& OutReply)
{
	FCrewMember* M = FindMutable(Id);
	if (!M) { OutReply = TEXT("No such crew member"); return false; }
	++M->Service.Reprimands;
	Record(*M, ECrewEventKind::Reprimanded, FString::Printf(TEXT("Reprimanded by the captain%s."), For.IsEmpty() ? TEXT("") : *(TEXT(" for ") + For)), true);
	const bool bProud = M->HasTrait(TEXT("WoundedPride"));
	Remember(*M, TEXT("Reprimanded"), bProud ? -0.25f : -0.15f, bProud);
	M->Stress = FMath::Min(M->Stress + 0.15f, 1.f);
	if (M->Facet(ECrewFacet::Discipline) > 60.f) { M->Needs[(int32)ECrewNeed::Purpose] = FMath::Min(M->Needs[(int32)ECrewNeed::Purpose] + 0.1f, 1.f); }
	OutReply = FString::Printf(TEXT("%s reprimanded"), *M->Styled());
	Dirty(); Save();
	return true;
}

bool UCrewRosterSubsystem::SetWatch(int32 Id, ECrewWatch Watch, FString& OutReply)
{
	FCrewMember* M = FindMutable(Id);
	if (!M) { OutReply = TEXT("No such crew member"); return false; }
	M->Watch = Watch;
	Record(*M, ECrewEventKind::Transfer, FString::Printf(TEXT("Moved to the %s watch."), *WatchName(Watch).ToLower()));
	Place(*M);
	OutReply = FString::Printf(TEXT("%s to the %s watch"), *M->Styled(), *WatchName(Watch).ToLower());
	Dirty(); Save();
	return true;
}

bool UCrewRosterSubsystem::SetPosting(int32 Id, ECrewDepartment Dept, const FString& Compartment, FString& OutReply)
{
	FCrewMember* M = FindMutable(Id);
	if (!M) { OutReply = TEXT("No such crew member"); return false; }
	if (M->bHead) { OutReply = TEXT("A department head keeps the department; promote a successor first"); return false; }
	M->Department = Dept;
	M->bSecond = false;
	M->Posting = Compartment.IsEmpty() ? PostingFor(Rng, Dept, 5) : Compartment;
	Record(*M, ECrewEventKind::Transfer, FString::Printf(TEXT("Posted to %s, %s department."), *M->Posting, *DepartmentName(Dept).ToLower()), true);
	for (FCrewWant& W : M->Wants) { if (W.Tag == TEXT("BridgePosting") && !W.bGranted && M->Posting == TEXT("Main bridge")) { W.bGranted = true; Remember(*M, TEXT("A bridge posting at last"), 0.2f, true); } }
	Place(*M);
	OutReply = FString::Printf(TEXT("%s posted to %s"), *M->Styled(), *M->Posting);
	Dirty(); Save();
	return true;
}

bool UCrewRosterSubsystem::GrantLeave(int32 Id, int32 Days, FString& OutReply)
{
	FCrewMember* M = FindMutable(Id);
	if (!M) { OutReply = TEXT("No such crew member"); return false; }
	if (M->Condition != ECrewCondition::Fit) { OutReply = FString::Printf(TEXT("%s is %s"), *M->Styled(), *ConditionName(M->Condition).ToLower()); return false; }
	M->Condition = ECrewCondition::OnLeave; M->ConditionHours = FMath::Max(Days, 1) * 24.f;
	Record(*M, ECrewEventKind::Leave, FString::Printf(TEXT("%d days' leave granted at %s."), Days, RecruitStation.IsEmpty() ? TEXT("the station") : *RecruitStation));
	Remember(*M, TEXT("Shore leave"), 0.2f);
	for (FCrewWant& W : M->Wants) { if (W.Tag == TEXT("ShoreLeave") && !W.bGranted) { W.bGranted = true; Remember(*M, TEXT("The leave I asked for"), 0.1f); } }
	Place(*M);
	OutReply = FString::Printf(TEXT("%s ashore for %d days"), *M->Styled(), Days);
	Dirty(); Save();
	return true;
}

bool UCrewRosterSubsystem::Talk(int32 Id, const FString& How, FString& OutReply)
{
	FCrewMember* M = FindMutable(Id);
	if (!M) { OutReply = TEXT("No such crew member"); return false; }
	const FString H = How.ToLower();
	const FCrewWant* Want = nullptr; for (const FCrewWant& W : M->Wants) { if (!W.bFear && !W.bGranted) { Want = &W; break; } }
	M->Needs[(int32)ECrewNeed::Recognition] = FMath::Min(M->Needs[(int32)ECrewNeed::Recognition] + 0.3f, 1.f);
	if (H == TEXT("listen"))
	{
		M->Stress = FMath::Max(M->Stress - 0.15f * (0.5f + M->Facet(ECrewFacet::Empathy) / 200.f), 0.f);
		Remember(*M, TEXT("The captain listened"), 0.1f);
		OutReply = Want ? FString::Printf(TEXT("%s talks, in the end, about %s"), *M->Styled(), *Want->Text) : FString::Printf(TEXT("%s has nothing pressing, and is glad to be asked"), *M->Styled());
		Record(*M, ECrewEventKind::Moment, TEXT("The captain asked how things were, and listened."));
	}
	else if (H == TEXT("encourage"))
	{
		M->Needs[(int32)ECrewNeed::Purpose] = FMath::Min(M->Needs[(int32)ECrewNeed::Purpose] + 0.2f, 1.f);
		Remember(*M, TEXT("A word from the captain"), 0.12f);
		OutReply = FString::Printf(TEXT("%s stands a little straighter"), *M->Styled());
		Record(*M, ECrewEventKind::Moment, TEXT("A word of encouragement from the captain."));
	}
	else if (H == TEXT("order"))
	{
		M->Needs[(int32)ECrewNeed::Purpose] = FMath::Min(M->Needs[(int32)ECrewNeed::Purpose] + 0.1f, 1.f);
		if (M->Facet(ECrewFacet::Discipline) < 40.f) { Remember(*M, TEXT("Ordered about"), -0.06f); }
		OutReply = FString::Printf(TEXT("%s: \"Aye, Captain.\""), *M->Styled());
		Record(*M, ECrewEventKind::Moment, TEXT("Orders from the captain, in person."));
	}
	else if (H == TEXT("promise"))
	{
		if (Want)
		{
			Remember(*M, FString::Printf(TEXT("The captain promised %s"), *Want->Text), 0.18f);
			OutReply = FString::Printf(TEXT("%s will hold you to %s"), *M->Styled(), *Want->Text);
			Record(*M, ECrewEventKind::Moment, FString::Printf(TEXT("The captain promised %s."), *Want->Text));
		}
		else { OutReply = FString::Printf(TEXT("%s wants for nothing you can promise"), *M->Styled()); }
	}
	else { OutReply = TEXT("talk <name> listen|encourage|order|promise"); return false; }
	UpdateMood(*M);
	Dirty(); Save();
	return true;
}

bool UCrewRosterSubsystem::SignOn(int32 RecruitIndex, FString& OutReply)
{
	if (!Recruits.IsValidIndex(RecruitIndex)) { OutReply = TEXT("No such recruit; ship crew recruits lists them alongside a station"); return false; }
	FCrewMember M = Recruits[RecruitIndex];
	Recruits.RemoveAt(RecruitIndex);
	M.Id = NextId++;
	M.Service.JoinedStardate = GetStardate();
	M.History.Reset();
	Record(M, ECrewEventKind::Joined, FString::Printf(TEXT("Signed on at %s; %s."), *RecruitStation, *M.Backstory), true);
	Place(M);
	Roster.Add(M);
	OutReply = FString::Printf(TEXT("%s signed on, %s department"), *M.Styled(), *DepartmentName(M.Department).ToLower());
	Dirty(); Save();
	return true;
}

bool UCrewRosterSubsystem::Discharge(int32 Id, FString& OutReply)
{
	FCrewMember* M = FindMutable(Id);
	if (!M) { OutReply = TEXT("No such crew member"); return false; }
	if (M->bHead) { OutReply = TEXT("Promote a successor before discharging a department head"); return false; }
	M->Condition = ECrewCondition::Transferred;
	Record(*M, ECrewEventKind::Transfer, FString::Printf(TEXT("Transferred off the ship at %s."), RecruitStation.IsEmpty() ? TEXT("the station") : *RecruitStation), true);
	for (const FCrewRelationship& R : M->Relationships) { if (R.Affinity > 0.4f) { if (FCrewMember* F = FindMutable(R.Other)) { Remember(*F, FString::Printf(TEXT("%s transferred away"), *M->FullName()), -0.1f); } } }
	OutReply = FString::Printf(TEXT("%s transferred ashore"), *M->Styled());
	Place(*M);
	Dirty(); Save();
	return true;
}

FString UCrewRosterSubsystem::Memorial()
{
	MourningDays = 3;
	TArray<FString> Names; for (const FCrewMember& L : Lost) { Names.Add(L.Styled()); }
	for (FCrewMember& M : Roster)
	{
		if (!M.IsAboard()) { continue; }
		bool bBereaved = false; for (const FCrewMemory& Me : M.Memories) { if (Me.bPermanent && Me.Weight < 0.f) { bBereaved = true; } }
		if (bBereaved) { Remember(M, TEXT("The memorial"), 0.12f); M.Stress = FMath::Max(M.Stress - 0.1f, 0.f); }
		M.Needs[(int32)ECrewNeed::Belonging] = FMath::Min(M.Needs[(int32)ECrewNeed::Belonging] + 0.1f, 1.f);
		if (Names.Num() > 0 && Rng.FRand() < 0.1f) { Record(M, ECrewEventKind::Memorial, FString::Printf(TEXT("Stood for the memorial: %s."), *FString::Join(Names, TEXT(", ")))); }
	}
	Dirty(); Save();
	return Names.Num() > 0 ? FString::Printf(TEXT("The ship stands for %s"), *FString::Join(Names, TEXT(", "))) : TEXT("The ship stands a moment for those lost elsewhere");
}

FCrewCheck UCrewRosterSubsystem::Check(ECrewSkill Skill, float Difficulty, const TArray<int32>& InIds, float CaptainBonus)
{
	FCrewCheck Out;
	TArray<int32> Ids = InIds;
	if (Ids.Num() == 0)
	{
		// The department's head and second for this skill.
		ECrewDepartment D = ECrewDepartment::Command;
		for (int32 d = 0; d < (int32)ECrewDepartment::Count; ++d) { if (SkillOf((ECrewDepartment)d) == Skill) { D = (ECrewDepartment)d; break; } }
		if (const FCrewMember* H = Head(D)) { Ids.Add(H->Id); }
		if (const FCrewMember* S = Second(D)) { Ids.Add(S->Id); }
	}
	float Best = -1.f, Worst = 1000.f, Sum = 0.f; int32 N = 0;
	for (int32 Id : Ids)
	{
		const FCrewMember* M = Find(Id);
		if (!M || !M->IsAboard() && M->Condition != ECrewCondition::Away) { continue; }
		float E = M->Skill(Skill) + (M->Facet(ECrewFacet::Discipline) - 50.f) / 10.f - M->Stress * 20.f - (1.f - M->Need(ECrewNeed::Rest)) * 12.f;
		if (M->Condition == ECrewCondition::Injured) { E -= 15.f; }
		if (M->HasTrait(TEXT("Perfectionist"))) { E += 4.f; }
		if (E > Best) { Best = E; Out.BestId = Id; }
		if (E < Worst) { Worst = E; Out.WorstId = Id; }
		Sum += E; ++N;
	}
	if (N == 0) { Out.Summary = TEXT("Nobody to make the attempt"); return Out; }
	const float Support = N > 1 ? (Sum - Best) / (N - 1) * 0.25f : 0.f;
	const float Roll = Rng.FRandRange(-15.f, 15.f);
	const float Effective = Best + Support + CaptainBonus + Roll;
	Out.Margin = Effective - Difficulty;
	Out.bSuccess = Out.Margin >= 0.f;
	const FCrewMember* B = Find(Out.BestId);
	for (int32 Id : Ids) { if (FCrewMember* M = FindMutable(Id)) { Grow(*M, Skill, Id == Out.BestId ? 8.f : 4.f); ++M->Service.Missions; } }
	if (FCrewMember* BM = FindMutable(Out.BestId))
	{
		Record(*BM, ECrewEventKind::Check, FString::Printf(TEXT("%s the %s problem (difficulty %.0f) with %s."), Out.bSuccess ? TEXT("Carried") : TEXT("Could not crack"), *SkillName(Skill).ToLower(), Difficulty, Out.Margin > 20.f ? TEXT("room to spare") : Out.bSuccess ? TEXT("little to spare") : TEXT("nothing to show")), Out.bSuccess && Out.Margin > 20.f);
		Remember(*BM, Out.bSuccess ? TEXT("Carried it") : TEXT("Fell short"), Out.bSuccess ? 0.1f : -0.1f);
	}
	Out.Summary = FString::Printf(TEXT("%s: %s led, effective %.0f against %.0f (margin %+.0f)"), Out.bSuccess ? TEXT("Success") : TEXT("Failure"), B ? *B->Styled() : TEXT("nobody"), Effective, Difficulty, Out.Margin);
	Dirty(); Save();
	return Out;
}
