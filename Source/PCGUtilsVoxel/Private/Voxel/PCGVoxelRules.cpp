// Copyright Max Harris

#include "Voxel/PCGVoxelRules.h"

#include "Math/Matrix.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGVoxelRules)

namespace PCGVoxelRulesPrivate
{
	/** Face directions in mask-bit order: +X, -X, +Y, -Y, +Z, -Z. */
	const FIntVector FaceDir[6] = {
		FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0), FIntVector(0, 0, 1), FIntVector(0, 0, -1) };

	constexpr uint8 BitPosX = 1, BitNegX = 2, BitPosY = 4, BitNegY = 8, BitPosZ = 16, BitNegZ = 32;

	/** The side faces in layout order (+Y, +X, -Y, -X) and their text keys. */
	const uint8 SideBit[4] = { BitPosY, BitPosX, BitNegY, BitNegX };
	const TCHAR* SideKey[4] = { TEXT("+Y"), TEXT("+X"), TEXT("-Y"), TEXT("-X") };

	void AddState(EPCGVoxelFaceState State, uint8 Bit, uint8& Open, uint8& Closed)
	{
		if (State == EPCGVoxelFaceState::Empty)
		{
			Open |= Bit;
		}
		else if (State == EPCGVoxelFaceState::Filled)
		{
			Closed |= Bit;
		}
	}

	const TCHAR* StateText(EPCGVoxelFaceState State)
	{
		return State == EPCGVoxelFaceState::Empty ? TEXT("E") : (State == EPCGVoxelFaceState::Filled ? TEXT("F") : TEXT("*"));
	}

	struct FLayoutName { EPCGVoxelSideLayout Layout; const TCHAR* Name; };
	const FLayoutName LayoutNames[] = {
		{ EPCGVoxelSideLayout::Any, TEXT("Any") }, { EPCGVoxelSideLayout::None, TEXT("None") }, { EPCGVoxelSideLayout::One, TEXT("One") },
		{ EPCGVoxelSideLayout::TwoAdjacent, TEXT("TwoAdjacent") }, { EPCGVoxelSideLayout::TwoOpposite, TEXT("TwoOpposite") },
		{ EPCGVoxelSideLayout::Three, TEXT("Three") }, { EPCGVoxelSideLayout::Four, TEXT("Four") } };

	struct FAxisName { EPCGVoxelThinAxis Axis; const TCHAR* Name; };
	const FAxisName AxisNames[] = {
		{ EPCGVoxelThinAxis::Any, TEXT("Any") }, { EPCGVoxelThinAxis::Z, TEXT("Z") }, { EPCGVoxelThinAxis::X, TEXT("X") },
		{ EPCGVoxelThinAxis::Y, TEXT("Y") }, { EPCGVoxelThinAxis::Sides, TEXT("Sides") } };

	struct FTurnsName { EPCGVoxelRuleTurns Turns; const TCHAR* Name; };
	const FTurnsName TurnsNames[] = {
		{ EPCGVoxelRuleTurns::Exact, TEXT("Exact") }, { EPCGVoxelRuleTurns::AboutUp, TEXT("Up") },
		{ EPCGVoxelRuleTurns::AboutUpAndFlip, TEXT("UpFlip") }, { EPCGVoxelRuleTurns::Any, TEXT("Any") } };

	PCGVoxelRules::FRotation Compose(const PCGVoxelRules::FRotation& A, const PCGVoxelRules::FRotation& B)
	{
		PCGVoxelRules::FRotation Out;
		Out.X = A.Apply(B.X);
		Out.Y = A.Apply(B.Y);
		Out.Z = A.Apply(B.Z);
		return Out;
	}

	PCGVoxelRules::FRotation Make(const FIntVector& X, const FIntVector& Y, const FIntVector& Z)
	{
		PCGVoxelRules::FRotation Out;
		Out.X = X;
		Out.Y = Y;
		Out.Z = Z;
		return Out;
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Rules

void FPCGVoxelFacesRule::GetMasks(uint8& OutOpen, uint8& OutClosed) const
{
	using namespace PCGVoxelRulesPrivate;
	OutOpen = 0;
	OutClosed = 0;
	AddState(Up, BitPosZ, OutOpen, OutClosed);
	AddState(Down, BitNegZ, OutOpen, OutClosed);

	using S = EPCGVoxelFaceState;
	S Side[4] = { S::Any, S::Any, S::Any, S::Any };
	switch (Sides)
	{
	case EPCGVoxelSideLayout::Any: break;
	case EPCGVoxelSideLayout::None: Side[0] = S::Filled; Side[1] = S::Filled; Side[2] = S::Filled; Side[3] = S::Filled; break;
	case EPCGVoxelSideLayout::One: Side[0] = S::Empty; Side[1] = S::Filled; Side[2] = S::Filled; Side[3] = S::Filled; break;
	case EPCGVoxelSideLayout::TwoAdjacent: Side[0] = S::Empty; Side[1] = S::Empty; Side[2] = S::Filled; Side[3] = S::Filled; break;
	case EPCGVoxelSideLayout::TwoOpposite: Side[0] = S::Empty; Side[1] = S::Filled; Side[2] = S::Empty; Side[3] = S::Filled; break;
	case EPCGVoxelSideLayout::Three: Side[0] = S::Empty; Side[1] = S::Empty; Side[2] = S::Empty; Side[3] = S::Filled; break;
	case EPCGVoxelSideLayout::Four: Side[0] = S::Empty; Side[1] = S::Empty; Side[2] = S::Empty; Side[3] = S::Empty; break;
	case EPCGVoxelSideLayout::Custom: Side[0] = PosY; Side[1] = PosX; Side[2] = NegY; Side[3] = NegX; break;
	}
	for (int32 I = 0; I < 4; ++I)
	{
		AddState(Side[I], SideBit[I], OutOpen, OutClosed);
	}
}

bool FPCGVoxelFacesRule::Matches(uint8 OpenMask) const
{
	uint8 Open, Closed;
	GetMasks(Open, Closed);
	return (OpenMask & Open) == Open && (OpenMask & Closed) == 0;
}

FString FPCGVoxelFacesRule::ToText() const
{
	using namespace PCGVoxelRulesPrivate;
	TArray<FString> Tokens;
	if (Up != EPCGVoxelFaceState::Any)
	{
		Tokens.Add(FString::Printf(TEXT("+Z=%s"), StateText(Up)));
	}
	if (Down != EPCGVoxelFaceState::Any)
	{
		Tokens.Add(FString::Printf(TEXT("-Z=%s"), StateText(Down)));
	}
	if (Sides == EPCGVoxelSideLayout::Custom)
	{
		const EPCGVoxelFaceState Side[4] = { PosY, PosX, NegY, NegX };
		for (int32 I = 0; I < 4; ++I)
		{
			if (Side[I] != EPCGVoxelFaceState::Any)
			{
				Tokens.Add(FString::Printf(TEXT("%s=%s"), SideKey[I], StateText(Side[I])));
			}
		}
	}
	else if (Sides != EPCGVoxelSideLayout::Any)
	{
		for (const FLayoutName& Name : LayoutNames)
		{
			if (Name.Layout == Sides)
			{
				Tokens.Add(FString::Printf(TEXT("Sides=%s"), Name.Name));
			}
		}
	}
	return FString::Join(Tokens, TEXT(" "));
}

bool FPCGVoxelThinRule::Matches(uint8 OpenMask) const
{
	using namespace PCGVoxelRulesPrivate;
	const bool bX = (OpenMask & (BitPosX | BitNegX)) == (BitPosX | BitNegX);
	const bool bY = (OpenMask & (BitPosY | BitNegY)) == (BitPosY | BitNegY);
	const bool bZ = (OpenMask & (BitPosZ | BitNegZ)) == (BitPosZ | BitNegZ);
	switch (Axis)
	{
	case EPCGVoxelThinAxis::Z: return bZ;
	case EPCGVoxelThinAxis::X: return bX;
	case EPCGVoxelThinAxis::Y: return bY;
	case EPCGVoxelThinAxis::Sides: return bX || bY;
	default: return bX || bY || bZ;
	}
}

FString FPCGVoxelThinRule::ToText() const
{
	using namespace PCGVoxelRulesPrivate;
	for (const FAxisName& Name : AxisNames)
	{
		if (Name.Axis == Axis)
		{
			return FString::Printf(TEXT("Thin=%s"), Name.Name);
		}
	}
	return TEXT("Thin=Any");
}

bool FPCGVoxelCompoundRule::Test(uint8 OpenMask) const
{
	for (const FInstancedStruct& Instance : Rules)
	{
		const FPCGVoxelRule* Rule = Instance.GetPtr<FPCGVoxelRule>();
		if (Rule && !Rule->Test(OpenMask))
		{
			return false;
		}
	}
	return true;
}

FString FPCGVoxelCompoundRule::ToText() const
{
	using namespace PCGVoxelRulesPrivate;
	TArray<FString> Tokens;
	bool bLooseFacesUsed = false;
	for (const FInstancedStruct& Instance : Rules)
	{
		if (const FPCGVoxelFacesRule* Faces = Instance.GetPtr<FPCGVoxelFacesRule>())
		{
			const FString Body = Faces->ToText();
			if (!Faces->bInvert && !bLooseFacesUsed)
			{
				bLooseFacesUsed = true;
				if (!Body.IsEmpty())
				{
					Tokens.Add(Body);
				}
			}
			else
			{
				Tokens.Add(FString::Printf(TEXT("%s{%s}"), Faces->bInvert ? TEXT("!") : TEXT(""), *Body));
			}
		}
		else if (const FPCGVoxelRule* Rule = Instance.GetPtr<FPCGVoxelRule>())
		{
			Tokens.Add(FString::Printf(TEXT("%s%s"), Rule->bInvert ? TEXT("!") : TEXT(""), *Rule->ToText()));
		}
	}

	FString Settings;
	for (const FTurnsName& Name : TurnsNames)
	{
		if (Name.Turns == Turns)
		{
			Settings = FString::Printf(TEXT("Turns=%s"), Name.Name);
		}
	}
	if (!bPreferUp)
	{
		Settings += TEXT(" PreferUp=No");
	}
	if (Spin == EPCGVoxelRuleSpin::Random)
	{
		Settings += TEXT(" Spin=Random");
	}
	const FString Body = FString::Join(Tokens, TEXT(" "));
	return Body.IsEmpty() ? FString::Printf(TEXT("| %s"), *Settings) : FString::Printf(TEXT("%s | %s"), *Body, *Settings);
}

FString FPCGVoxelRuleEntry::ToText() const
{
	FString Head = FString::FromInt(MeshIndex);
	if (!Name.IsNone())
	{
		FString Word = Name.ToString();
		Word.ReplaceInline(TEXT(" "), TEXT("_"));
		Word.ReplaceInline(TEXT(":"), TEXT("_"));
		Head += TEXT(" ") + Word;
	}
	return FString::Printf(TEXT("%s: %s"), *Head, *Rule.ToText());
}

FString FPCGVoxelRuleMapping::ToText() const
{
	TArray<FString> Lines;
	for (const FPCGVoxelRuleEntry& Entry : Entries)
	{
		Lines.Add(Entry.bEnabled ? Entry.ToText() : TEXT("# (off) ") + Entry.ToText());
	}
	return FString::Join(Lines, TEXT("\n"));
}

// ---------------------------------------------------------------------------------------------------------------
// Rotations and matching

FQuat PCGVoxelRules::FRotation::ToQuat() const
{
	return FMatrix(FVector(X), FVector(Y), FVector(Z), FVector::ZeroVector).ToQuat();
}

const TArray<PCGVoxelRules::FRotation>& PCGVoxelRules::Rotations()
{
	using namespace PCGVoxelRulesPrivate;
	static const TArray<FRotation> All = []()
	{
		const FIntVector PX(1, 0, 0), PY(0, 1, 0), PZ(0, 0, 1);
		const FIntVector NX(-1, 0, 0), NY(0, -1, 0), NZ(0, 0, -1);
		const FRotation Yaw90 = Make(PY, NX, PZ);
		// Rotations taking the mesh's up to each direction: +Z, -Z (flip about X), then the four sides.
		const FRotation Align[6] = {
			Make(PX, PY, PZ),
			Make(PX, NY, NZ),
			Make(NZ, PY, PX),
			Make(PZ, PY, NX),
			Make(PX, NZ, PY),
			Make(PX, PZ, NY) };
		TArray<FRotation> Out;
		for (const FRotation& A : Align)
		{
			FRotation Yaw;
			for (int32 K = 0; K < 4; ++K)
			{
				Out.Add(Compose(A, Yaw));
				Yaw = Compose(Yaw90, Yaw);
			}
		}
		return Out;
	}();
	return All;
}

int32 PCGVoxelRules::NumTurns(EPCGVoxelRuleTurns Turns)
{
	switch (Turns)
	{
	case EPCGVoxelRuleTurns::Exact: return 1;
	case EPCGVoxelRuleTurns::AboutUp: return 4;
	case EPCGVoxelRuleTurns::AboutUpAndFlip: return 8;
	default: return 24;
	}
}

int32 PCGVoxelRules::FaceOf(const FIntVector& Direction)
{
	using namespace PCGVoxelRulesPrivate;
	for (int32 Face = 0; Face < 6; ++Face)
	{
		if (FaceDir[Face] == Direction)
		{
			return Face;
		}
	}
	return INDEX_NONE;
}

uint8 PCGVoxelRules::ToMeshFrame(const FRotation& Rotation, uint8 GridMask)
{
	using namespace PCGVoxelRulesPrivate;
	uint8 Mesh = 0;
	for (int32 Face = 0; Face < 6; ++Face)
	{
		const int32 GridFace = FaceOf(Rotation.Apply(FaceDir[Face]));
		if (GridFace != INDEX_NONE && (GridMask & (1 << GridFace)))
		{
			Mesh |= 1 << Face;
		}
	}
	return Mesh;
}

void PCGVoxelRules::BuildTable(const FPCGVoxelRuleMapping& Mapping, FTable& OutTable)
{
	const TArray<FRotation>& All = Rotations();
	for (int32 Mask = 0; Mask < 64; ++Mask)
	{
		OutTable.Entry[Mask] = INDEX_NONE;
		OutTable.Tied[Mask].Reset();
		for (int32 EntryIndex = 0; EntryIndex < Mapping.Entries.Num(); ++EntryIndex)
		{
			const FPCGVoxelRuleEntry& Entry = Mapping.Entries[EntryIndex];
			if (!Entry.bEnabled)
			{
				continue;
			}
			int32 Best = MIN_int32;
			TArray<uint8> Tied;
			for (int32 R = 0; R < NumTurns(Entry.Rule.Turns); ++R)
			{
				if (!Entry.Rule.Test(ToMeshFrame(All[R], static_cast<uint8>(Mask))))
				{
					continue;
				}
				const int32 Score = Entry.Rule.bPreferUp ? All[R].Z.Z : 0;
				if (Score > Best)
				{
					Best = Score;
					Tied.Reset();
				}
				if (Score == Best)
				{
					Tied.Add(static_cast<uint8>(R));
				}
			}
			if (!Tied.IsEmpty())
			{
				OutTable.Entry[Mask] = EntryIndex;
				OutTable.Tied[Mask] = MoveTemp(Tied);
				break;
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Text form

namespace PCGVoxelRulesPrivate
{
	bool ParseState(const FString& Value, EPCGVoxelFaceState& Out)
	{
		if (Value == TEXT("E") || Value.Equals(TEXT("Empty"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("Open"), ESearchCase::IgnoreCase)) { Out = EPCGVoxelFaceState::Empty; return true; }
		if (Value == TEXT("F") || Value.Equals(TEXT("Filled"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("Closed"), ESearchCase::IgnoreCase)) { Out = EPCGVoxelFaceState::Filled; return true; }
		if (Value == TEXT("*") || Value.Equals(TEXT("Any"), ESearchCase::IgnoreCase)) { Out = EPCGVoxelFaceState::Any; return true; }
		if (Value.Equals(TEXT("e"), ESearchCase::CaseSensitive)) { Out = EPCGVoxelFaceState::Empty; return true; }
		if (Value.Equals(TEXT("f"), ESearchCase::CaseSensitive)) { Out = EPCGVoxelFaceState::Filled; return true; }
		return false;
	}

	/** A Faces rule being parsed, and which of its faces the text has set. */
	struct FFacesBuild
	{
		int32 RuleIndex = INDEX_NONE;
		uint8 SetFaces = 0;
		bool bPreset = false;
		FString PresetToken;
	};

	/** Applies one face token (+Z=E, Sides=One...) to a Faces rule. Returns false with an error when it is not one or clashes. */
	bool ApplyFaceToken(const FString& Key, const FString& Value, const FString& Token, FPCGVoxelFacesRule& Rule, FFacesBuild& Build,
		FString& OutError, bool& bOutIsFaceToken)
	{
		bOutIsFaceToken = true;
		if (Key.Equals(TEXT("Sides"), ESearchCase::IgnoreCase))
		{
			for (const FLayoutName& Name : LayoutNames)
			{
				if (Value.Equals(Name.Name, ESearchCase::IgnoreCase))
				{
					if (Build.bPreset)
					{
						OutError = FString::Printf(TEXT("'%s' and '%s' both set Sides in one rule"), *Build.PresetToken, *Token);
						return false;
					}
					if (Build.SetFaces & (BitPosX | BitNegX | BitPosY | BitNegY))
					{
						OutError = FString::Printf(TEXT("'%s' sets Sides, but one side is already set on its own in this rule; use Sides=... or +X/+Y/-X/-Y tokens, not both"), *Token);
						return false;
					}
					Build.bPreset = true;
					Build.PresetToken = Token;
					Rule.Sides = Name.Layout;
					return true;
				}
			}
			OutError = FString::Printf(TEXT("'%s': Sides is None, One, TwoAdjacent, TwoOpposite, Three, Four or Any"), *Token);
			return false;
		}

		struct FKey { const TCHAR* Name; uint8 Bit; EPCGVoxelFaceState FPCGVoxelFacesRule::* Field; bool bSide; };
		static const FKey Keys[] = {
			{ TEXT("+Z"), BitPosZ, &FPCGVoxelFacesRule::Up, false }, { TEXT("-Z"), BitNegZ, &FPCGVoxelFacesRule::Down, false },
			{ TEXT("+Y"), BitPosY, &FPCGVoxelFacesRule::PosY, true }, { TEXT("+X"), BitPosX, &FPCGVoxelFacesRule::PosX, true },
			{ TEXT("-Y"), BitNegY, &FPCGVoxelFacesRule::NegY, true }, { TEXT("-X"), BitNegX, &FPCGVoxelFacesRule::NegX, true } };
		for (const FKey& K : Keys)
		{
			if (!Key.Equals(K.Name, ESearchCase::IgnoreCase))
			{
				continue;
			}
			EPCGVoxelFaceState State;
			if (!ParseState(Value, State))
			{
				OutError = FString::Printf(TEXT("'%s': a face is E (empty, open), F (filled) or * (any)"), *Token);
				return false;
			}
			if (Build.SetFaces & K.Bit)
			{
				OutError = FString::Printf(TEXT("'%s' sets %s a second time in one rule"), *Token, K.Name);
				return false;
			}
			if (K.bSide && Build.bPreset)
			{
				OutError = FString::Printf(TEXT("'%s' sets a side, but '%s' already sets Sides in this rule; use one or the other"), *Token, *Build.PresetToken);
				return false;
			}
			Build.SetFaces |= K.Bit;
			Rule.*K.Field = State;
			if (K.bSide)
			{
				Rule.Sides = EPCGVoxelSideLayout::Custom;
			}
			return true;
		}
		bOutIsFaceToken = false;
		return true;
	}

	bool ParseSettings(const FString& Text, FPCGVoxelCompoundRule& Rule, FString& OutError)
	{
		TArray<FString> Tokens;
		Text.ParseIntoArrayWS(Tokens);
		for (const FString& Token : Tokens)
		{
			FString Key, Value;
			if (!Token.Split(TEXT("="), &Key, &Value))
			{
				OutError = FString::Printf(TEXT("'%s' is not a setting (Turns=..., PreferUp=..., Spin=...)"), *Token);
				return false;
			}
			if (Key.Equals(TEXT("Turns"), ESearchCase::IgnoreCase))
			{
				bool bFound = false;
				for (const FTurnsName& Name : TurnsNames)
				{
					if (Value.Equals(Name.Name, ESearchCase::IgnoreCase))
					{
						Rule.Turns = Name.Turns;
						bFound = true;
					}
				}
				if (!bFound)
				{
					OutError = FString::Printf(TEXT("'%s': Turns is Exact, Up, UpFlip or Any"), *Token);
					return false;
				}
			}
			else if (Key.Equals(TEXT("PreferUp"), ESearchCase::IgnoreCase))
			{
				if (Value.Equals(TEXT("Yes"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("True"), ESearchCase::IgnoreCase))
				{
					Rule.bPreferUp = true;
				}
				else if (Value.Equals(TEXT("No"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("False"), ESearchCase::IgnoreCase))
				{
					Rule.bPreferUp = false;
				}
				else
				{
					OutError = FString::Printf(TEXT("'%s': PreferUp is Yes or No"), *Token);
					return false;
				}
			}
			else if (Key.Equals(TEXT("Spin"), ESearchCase::IgnoreCase))
			{
				if (Value.Equals(TEXT("Fixed"), ESearchCase::IgnoreCase))
				{
					Rule.Spin = EPCGVoxelRuleSpin::Fixed;
				}
				else if (Value.Equals(TEXT("Random"), ESearchCase::IgnoreCase))
				{
					Rule.Spin = EPCGVoxelRuleSpin::Random;
				}
				else
				{
					OutError = FString::Printf(TEXT("'%s': Spin is Fixed or Random"), *Token);
					return false;
				}
			}
			else
			{
				OutError = FString::Printf(TEXT("'%s': unknown setting '%s' (Turns, PreferUp, Spin)"), *Token, *Key);
				return false;
			}
		}
		return true;
	}
}

bool PCGVoxelRules::ParseCompound(const FString& Text, FPCGVoxelCompoundRule& OutRule, FString& OutError)
{
	using namespace PCGVoxelRulesPrivate;
	OutRule = FPCGVoxelCompoundRule();

	FString RulesText, SettingsText;
	if (!Text.Split(TEXT("|"), &RulesText, &SettingsText))
	{
		RulesText = Text;
	}
	if (SettingsText.Contains(TEXT("|")))
	{
		OutError = TEXT("more than one '|' (one rule is '<rules> | <settings>')");
		return false;
	}

	RulesText.ReplaceInline(TEXT("{"), TEXT(" { "));
	RulesText.ReplaceInline(TEXT("}"), TEXT(" } "));
	TArray<FString> Tokens;
	RulesText.ParseIntoArrayWS(Tokens);

	FFacesBuild Loose;
	FFacesBuild Group;
	bool bInGroup = false;
	bool bInvertNext = false;

	auto FacesAt = [&OutRule](int32 Index) { return OutRule.Rules[Index].GetMutablePtr<FPCGVoxelFacesRule>(); };

	for (const FString& Token : Tokens)
	{
		if (Token == TEXT("!"))
		{
			if (bInGroup || bInvertNext)
			{
				OutError = TEXT("'!' must come right before '{' or a Thin token");
				return false;
			}
			bInvertNext = true;
			continue;
		}
		if (Token == TEXT("{"))
		{
			if (bInGroup)
			{
				OutError = TEXT("'{' inside braces (braces do not nest)");
				return false;
			}
			bInGroup = true;
			Group = FFacesBuild();
			Group.RuleIndex = OutRule.Rules.Num();
			FPCGVoxelFacesRule Faces;
			Faces.bInvert = bInvertNext;
			bInvertNext = false;
			OutRule.Rules.Add(FInstancedStruct::Make(Faces));
			continue;
		}
		if (Token == TEXT("}"))
		{
			if (!bInGroup)
			{
				OutError = TEXT("'}' without a matching '{'");
				return false;
			}
			bInGroup = false;
			continue;
		}

		bool bInvert = false;
		FString Bare = Token;
		if (Bare.StartsWith(TEXT("!")))
		{
			bInvert = true;
			Bare.RightChopInline(1);
		}
		bInvert = bInvert || bInvertNext;
		FString Key, Value;
		if (!Bare.Split(TEXT("="), &Key, &Value) || Key.IsEmpty() || Value.IsEmpty())
		{
			OutError = FString::Printf(TEXT("'%s' is not a rule token (faces like +Z=E, Sides=One, Thin=Any)"), *Token);
			return false;
		}

		if (Key.Equals(TEXT("Thin"), ESearchCase::IgnoreCase))
		{
			if (bInGroup)
			{
				OutError = FString::Printf(TEXT("'%s' is inside braces; braces hold face tokens only"), *Token);
				return false;
			}
			FPCGVoxelThinRule Thin;
			Thin.bInvert = bInvert;
			bool bFound = false;
			for (const FAxisName& Name : AxisNames)
			{
				if (Value.Equals(Name.Name, ESearchCase::IgnoreCase))
				{
					Thin.Axis = Name.Axis;
					bFound = true;
				}
			}
			if (!bFound)
			{
				OutError = FString::Printf(TEXT("'%s': Thin is Any, Z, X, Y or Sides"), *Token);
				return false;
			}
			OutRule.Rules.Add(FInstancedStruct::Make(Thin));
			bInvertNext = false;
			continue;
		}

		if (bInvert)
		{
			OutError = FString::Printf(TEXT("'%s': to invert face tokens, wrap them: !{...}"), *Token);
			return false;
		}
		FFacesBuild& Build = bInGroup ? Group : Loose;
		if (Build.RuleIndex == INDEX_NONE)
		{
			Build.RuleIndex = OutRule.Rules.Num();
			OutRule.Rules.Add(FInstancedStruct::Make(FPCGVoxelFacesRule()));
		}
		bool bIsFaceToken = false;
		if (!ApplyFaceToken(Key, Value, Token, *FacesAt(Build.RuleIndex), Build, OutError, bIsFaceToken))
		{
			return false;
		}
		if (!bIsFaceToken)
		{
			OutError = FString::Printf(TEXT("'%s' is not a rule token (faces like +Z=E, Sides=One, Thin=Any)"), *Token);
			return false;
		}
	}
	if (bInGroup)
	{
		OutError = TEXT("'{' is never closed");
		return false;
	}
	if (bInvertNext)
	{
		OutError = TEXT("'!' at the end, with nothing to invert");
		return false;
	}
	return ParseSettings(SettingsText, OutRule, OutError);
}

bool PCGVoxelRules::ParseMapping(const FString& Text, FPCGVoxelRuleMapping& OutMapping, FString& OutError)
{
	OutMapping = FPCGVoxelRuleMapping();
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, /*bCullEmpty=*/false);
	for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
	{
		const FString Line = Lines[LineIndex].TrimStartAndEnd();
		if (Line.IsEmpty() || Line.StartsWith(TEXT("#")))
		{
			continue;
		}
		const int32 LineNumber = LineIndex + 1;
		FString Head, Body;
		if (!Line.Split(TEXT(":"), &Head, &Body))
		{
			OutError = FString::Printf(TEXT("Line %d: no ':' after the mesh index (a line is '<index> <name>: <rules> | <settings>')"), LineNumber);
			return false;
		}
		TArray<FString> HeadTokens;
		Head.ParseIntoArrayWS(HeadTokens);
		if (HeadTokens.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Line %d: no mesh index before ':'"), LineNumber);
			return false;
		}
		if (HeadTokens.Num() > 2)
		{
			OutError = FString::Printf(TEXT("Line %d: '%s' before ':' should be a mesh index and at most one word of name"), LineNumber, *Head.TrimStartAndEnd());
			return false;
		}
		FPCGVoxelRuleEntry& Entry = OutMapping.Entries.AddDefaulted_GetRef();
		const FString& IndexToken = HeadTokens[0];
		if (!IndexToken.IsNumeric() || IndexToken.Contains(TEXT(".")) || IndexToken.StartsWith(TEXT("-")) || IndexToken.StartsWith(TEXT("+")))
		{
			OutError = FString::Printf(TEXT("Line %d: '%s' is not a mesh index (a whole number, 0 or more)"), LineNumber, *IndexToken);
			return false;
		}
		Entry.MeshIndex = FCString::Atoi(*IndexToken);
		if (HeadTokens.Num() == 2)
		{
			Entry.Name = FName(*HeadTokens[1]);
		}
		FString Error;
		if (!ParseCompound(Body, Entry.Rule, Error))
		{
			OutError = FString::Printf(TEXT("Line %d: %s"), LineNumber, *Error);
			return false;
		}
	}
	return true;
}
