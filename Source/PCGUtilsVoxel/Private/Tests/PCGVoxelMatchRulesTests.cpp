// Copyright Max Harris
//
// Voxel | Match Rules: rule tiles. Shapes match under their turns, points turn so the mesh-frame pattern lands on the
// cell, Prefer Up and Spin settle ties, Thin, the text form, and validation.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Elements/PCGVoxelMatchRules.h"
#include "PCGVoxelTestHelpers.h"

namespace PCGVoxelMatchRulesTests
{
	using namespace PCGVoxelTestHelpers;

	struct FOutPoint
	{
		FIntVector Cell;
		FTransform Transform;
		FVector Min;
		FVector Max;
		int32 MeshIndex = -99;
		int32 RuleIndex = -99;
		int32 Id = -99;
	};

	struct FResult
	{
		TArray<FOutPoint> Out;
		TArray<FOutPoint> Unmatched;
		int32 NumOutputs = 0;
	};

	int32 IntAt(const UPCGBasePointData* Points, FName Name, int64 Entry)
	{
		const FPCGMetadataDomain* Domain = Points->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Elements);
		const FPCGMetadataAttributeBase* Base = Domain ? Domain->GetConstAttribute(Name) : nullptr;
		return Base ? static_cast<const FPCGMetadataAttribute<int32>*>(Base)->GetValueFromItemKey(Entry) : -99;
	}

	TArray<FOutPoint> Read(const UPCGBasePointData* Points, const FVector& VoxelSize)
	{
		TArray<FOutPoint> Result;
		if (!Points)
		{
			return Result;
		}
		const TConstPCGValueRange<FTransform> Transforms = Points->GetConstTransformValueRange();
		const TConstPCGValueRange<FVector> Mins = Points->GetConstBoundsMinValueRange();
		const TConstPCGValueRange<FVector> Maxs = Points->GetConstBoundsMaxValueRange();
		const TConstPCGValueRange<int64> Entries = Points->GetConstMetadataEntryValueRange();
		for (int32 Index = 0; Index < Points->GetNumPoints(); ++Index)
		{
			const FVector P = Transforms[Index].GetLocation() / VoxelSize;
			FOutPoint& Out = Result.AddDefaulted_GetRef();
			Out.Cell = FIntVector(FMath::RoundToInt32(P.X), FMath::RoundToInt32(P.Y), FMath::RoundToInt32(P.Z));
			Out.Transform = Transforms[Index];
			Out.Min = Mins[Index];
			Out.Max = Maxs[Index];
			Out.MeshIndex = IntAt(Points, TEXT("MeshIndex"), Entries[Index]);
			Out.RuleIndex = IntAt(Points, TEXT("RuleIndex"), Entries[Index]);
			Out.Id = IntAt(Points, TEXT("Id"), Entries[Index]);
		}
		return Result;
	}

	FResult Match(const UPCGData* In, TFunctionRef<void(UPCGVoxelMatchRulesSettings&)> Edit, const FVector& VoxelSize = FVector(100.0))
	{
		UPCGVoxelMatchRulesSettings* Settings = NewObject<UPCGVoxelMatchRulesSettings>();
		Edit(*Settings);
		const TArray<FPCGTaggedData> Outputs = Run<FPCGVoxelMatchRulesElement>(Settings, { { PCGPinConstants::DefaultInputLabel, In } });
		FResult Result;
		Result.NumOutputs = Outputs.Num();
		for (const FPCGTaggedData& Output : Outputs)
		{
			const TArray<FOutPoint> Points = Read(Cast<UPCGBasePointData>(Output.Data), VoxelSize);
			(Output.Pin == TEXT("Unmatched") ? Result.Unmatched : Result.Out).Append(Points);
		}
		return Result;
	}

	/** Uses the text form, so the tests read like the doc. */
	void SetText(UPCGVoxelMatchRulesSettings& S, const TCHAR* Text)
	{
		S.RulesSource = EPCGVoxelRulesSource::Text;
		S.RulesText = Text;
	}

	FIntVector RoundDir(const FVector& V)
	{
		return FIntVector(FMath::RoundToInt32(V.X), FMath::RoundToInt32(V.Y), FMath::RoundToInt32(V.Z));
	}

	const FIntVector FaceDir[6] = {
		FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0), FIntVector(0, 0, 1), FIntVector(0, 0, -1) };

	/** The cell's open faces seen in the point's (turned) frame, in the Cell Properties bits. */
	uint8 MeshFrameMask(const FOutPoint& P, const TSet<FIntVector>& Cells)
	{
		uint8 Mask = 0;
		for (int32 Face = 0; Face < 6; ++Face)
		{
			const FIntVector Grid = RoundDir(P.Transform.GetRotation().RotateVector(FVector(FaceDir[Face])));
			if (!Cells.Contains(P.Cell + Grid))
			{
				Mask |= 1 << Face;
			}
		}
		return Mask;
	}

	int32 CountIndex(const TArray<FOutPoint>& Points, int32 MeshIndex)
	{
		return Points.FilterByPredicate([&](const FOutPoint& P) { return P.MeshIndex == MeshIndex; }).Num();
	}

	const FOutPoint* At(const TArray<FOutPoint>& Points, const FIntVector& Cell)
	{
		return Points.FindByPredicate([&](const FOutPoint& P) { return P.Cell == Cell; });
	}

	// Mask bits: +X 1, -X 2, +Y 4, -Y 8, +Z 16, -Z 32.
	const TCHAR* BlockRules =
		TEXT("0 Corner: +Z=E -Z=F Sides=TwoAdjacent | Turns=Any\n")
		TEXT("1 Edge: +Z=E -Z=F Sides=One | Turns=Any\n")
		TEXT("2 Face: +Z=F -Z=F Sides=One | Turns=Any\n");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelMatchRulesShapesTest,
	"PCGUtils.Voxel.MatchRules.ShapesAndTurns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPCGVoxelMatchRulesShapesTest::RunTest(const FString& Parameters)
{
	using namespace PCGVoxelMatchRulesTests;
	const TArray<FIntVector> BlockCells = Block(3, 3, 3);
	const TSet<FIntVector> Cells(BlockCells);

	// Three meshes cover every surface cell of a block under Any: 8 corners, 12 edges, 6 faces; the middle is dropped.
	const FResult All = Match(MakeVoxels(BlockCells), [](UPCGVoxelMatchRulesSettings& S) { SetText(S, BlockRules); });
	TestEqual(TEXT("26 surface cells out"), All.Out.Num(), 26);
	TestEqual(TEXT("nothing unmatched"), All.Unmatched.Num(), 0);
	TestEqual(TEXT("8 corners"), CountIndex(All.Out, 0), 8);
	TestEqual(TEXT("12 edges"), CountIndex(All.Out, 1), 12);
	TestEqual(TEXT("6 faces"), CountIndex(All.Out, 2), 6);
	TestNull(TEXT("the inside cell is dropped"), At(All.Out, FIntVector(1, 1, 1)));

	// Every point is turned so its mesh-frame pattern lands exactly on the cell.
	const uint8 Canonical[3] = { 16 | 4 | 1, 16 | 4, 4 };
	bool bAllFit = true;
	bool bBoundsKept = true;
	for (const FOutPoint& P : All.Out)
	{
		bAllFit &= P.MeshIndex >= 0 && P.MeshIndex < 3 && MeshFrameMask(P, Cells) == Canonical[P.MeshIndex];
		bBoundsKept &= P.Min.Equals(FVector(-50.0)) && P.Max.Equals(FVector(50.0));
	}
	TestTrue(TEXT("every point's turned pattern matches its cell"), bAllFit);
	TestTrue(TEXT("cube cells keep cube bounds"), bBoundsKept);

	// Prefer Up: a wall faces out along mesh +Y with mesh Z up.
	if (const FOutPoint* Wall = At(All.Out, FIntVector(2, 1, 1)))
	{
		TestEqual(TEXT("wall: mesh +Y points out of the wall (+X)"), RoundDir(Wall->Transform.GetRotation().RotateVector(FVector::YAxisVector)), FIntVector(1, 0, 0));
		TestEqual(TEXT("wall: mesh Z stays up"), RoundDir(Wall->Transform.GetRotation().RotateVector(FVector::ZAxisVector)), FIntVector(0, 0, 1));
	}
	if (const FOutPoint* Corner = At(All.Out, FIntVector(2, 2, 2)))
	{
		TestTrue(TEXT("the corner drawn as +X+Y+Z needs no turn"), Corner->Transform.GetRotation().Equals(FQuat::Identity, 1e-4));
		TestEqual(TEXT("Rule Index is the entry's position"), Corner->RuleIndex, 0);
	}

	// A chunk gives the same result as its cells.
	const TArray<FIntVector> Spans = { FIntVector(3) };
	const FResult Chunk = Match(MakeVoxels({ FIntVector(0) }, 100.0, &Spans), [](UPCGVoxelMatchRulesSettings& S) { SetText(S, BlockRules); });
	TestEqual(TEXT("a 3x3x3 chunk expands to 26 matched cells"), Chunk.Out.Num(), 26);

	// About Up: bottom corners would need a flip, so only the 4 top corners match.
	const TCHAR* TopCorner = TEXT("0 Corner: +Z=E -Z=F Sides=TwoAdjacent | Turns=Up");
	const FResult Up = Match(MakeVoxels(BlockCells), [&](UPCGVoxelMatchRulesSettings& S) { SetText(S, TopCorner); });
	TestEqual(TEXT("About Up: 4 top corners"), Up.Out.Num(), 4);
	TestEqual(TEXT("the rest to the Unmatched pin"), Up.Unmatched.Num(), 22);
	TestTrue(TEXT("unmatched cells have Mesh Index -1"), Up.Unmatched.Num() > 0 && Up.Unmatched[0].MeshIndex == -1);

	const FResult UpFlip = Match(MakeVoxels(BlockCells), [](UPCGVoxelMatchRulesSettings& S)
		{ SetText(S, TEXT("0: +Z=E -Z=F Sides=TwoAdjacent | Turns=UpFlip")); });
	TestEqual(TEXT("About Up + Flip: the bottom corners match too"), UpFlip.Out.Num(), 8);

	const FResult Keep = Match(MakeVoxels(BlockCells), [&](UPCGVoxelMatchRulesSettings& S)
		{ SetText(S, TopCorner); S.Unmatched = EPCGVoxelUnmatchedCells::Keep; });
	TestEqual(TEXT("Keep: all 26 in Out"), Keep.Out.Num(), 26);
	TestEqual(TEXT("Keep: 22 with -1"), CountIndex(Keep.Out, -1), 22);
	TestEqual(TEXT("Keep: no Unmatched output"), Keep.Unmatched.Num(), 0);

	const FResult Drop = Match(MakeVoxels(BlockCells), [&](UPCGVoxelMatchRulesSettings& S)
		{ SetText(S, TopCorner); S.Unmatched = EPCGVoxelUnmatchedCells::Drop; S.bDropInsideCells = false; });
	TestEqual(TEXT("Drop: only the matches"), Drop.Out.Num(), 4);
	TestEqual(TEXT("Drop: one output (no Unmatched pin data)"), Drop.NumOutputs, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelMatchRulesSpinThinTest,
	"PCGUtils.Voxel.MatchRules.SpinThinAndBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPCGVoxelMatchRulesSpinThinTest::RunTest(const FString& Parameters)
{
	using namespace PCGVoxelMatchRulesTests;

	// Floor tiles: Fixed keeps them unturned, Random spreads the four yaws.
	const TArray<FIntVector> Plate = Block(6, 6, 2);
	auto Yaws = [](const FResult& R)
	{
		TSet<FIntVector> Seen;
		for (const FOutPoint& P : R.Out)
		{
			Seen.Add(RoundDir(P.Transform.GetRotation().RotateVector(FVector::XAxisVector)));
		}
		return Seen;
	};
	const FResult Fixed = Match(MakeVoxels(Plate), [](UPCGVoxelMatchRulesSettings& S) { SetText(S, TEXT("4 Floor: +Z=E -Z=F Sides=None | Turns=Up")); });
	TestEqual(TEXT("16 floor cells"), Fixed.Out.Num(), 16);
	TestEqual(TEXT("Fixed: one yaw"), Yaws(Fixed).Num(), 1);
	const FResult Random = Match(MakeVoxels(Plate), [](UPCGVoxelMatchRulesSettings& S) { SetText(S, TEXT("4 Floor: +Z=E -Z=F Sides=None | Turns=Up Spin=Random")); });
	TestEqual(TEXT("Random: same cells"), Random.Out.Num(), 16);
	TestTrue(TEXT("Random: more than one yaw"), Yaws(Random).Num() > 1);
	bool bUp = true;
	for (const FOutPoint& P : Random.Out)
	{
		bUp &= RoundDir(P.Transform.GetRotation().RotateVector(FVector::ZAxisVector)) == FIntVector(0, 0, 1);
	}
	TestTrue(TEXT("Random only spins about up"), bUp);

	// Thin: a row along X is thin on Y and Z.
	const TArray<FIntVector> Row = { FIntVector(0, 0, 0), FIntVector(1, 0, 0), FIntVector(2, 0, 0) };
	TestEqual(TEXT("Thin Any: the whole row"), Match(MakeVoxels(Row), [](UPCGVoxelMatchRulesSettings& S) { SetText(S, TEXT("5: Thin=Any | Turns=Exact")); }).Out.Num(), 3);
	TestEqual(TEXT("Thin X: none"), Match(MakeVoxels(Row), [](UPCGVoxelMatchRulesSettings& S) { SetText(S, TEXT("5: Thin=X | Turns=Exact")); }).Out.Num(), 0);
	TestEqual(TEXT("not Thin: no block cell is thin"),
		Match(MakeVoxels(Block(3, 3, 3)), [](UPCGVoxelMatchRulesSettings& S) { SetText(S, TEXT("5: !Thin=Any | Turns=Exact")); }).Out.Num(), 26);

	// Non-cubic cells: a turned point's bounds still cover its cell exactly.
	const FVector Size(100.0, 200.0, 300.0);
	UPCGPointArrayData* Pair = MakeVoxels({ FIntVector(0, 0, 0), FIntVector(1, 0, 0) });
	PCGUtilsDataDomain::WriteValue(*PCGUtilsDataDomain::GetMutableDataDomain(Pair), TEXT("VoxelSize"), Size);
	{
		auto Transforms = Pair->GetTransformValueRange();
		auto Mins = Pair->GetBoundsMinValueRange();
		auto Maxs = Pair->GetBoundsMaxValueRange();
		for (int32 I = 0; I < 2; ++I)
		{
			Transforms[I] = FTransform(FVector(I * Size.X, 0.0, 0.0));
			Mins[I] = -Size * 0.5;
			Maxs[I] = Size * 0.5;
		}
	}
	// Only the mesh's -Z closed: the cell at 0 is closed on +X, so the mesh lies on its side.
	const FResult Side = Match(Pair, [](UPCGVoxelMatchRulesSettings& S) { SetText(S, TEXT("6: -Z=F +Z=E Sides=Four | Turns=Any")); }, Size);
	if (const FOutPoint* P = At(Side.Out, FIntVector(0)))
	{
		TestEqual(TEXT("mesh down points at the filled neighbour"), RoundDir(P->Transform.GetRotation().RotateVector(-FVector::ZAxisVector)), FIntVector(1, 0, 0));
		const FBox World = FBox(P->Min, P->Max).TransformBy(P->Transform);
		TestTrue(TEXT("turned bounds cover the 100 x 200 x 300 cell"), World.Min.Equals(-Size * 0.5, 1e-3) && World.Max.Equals(Size * 0.5, 1e-3));
	}
	else
	{
		AddError(TEXT("the side-lying cell did not match"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGVoxelMatchRulesTextTest,
	"PCGUtils.Voxel.MatchRules.TextAndErrors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPCGVoxelMatchRulesTextTest::RunTest(const FString& Parameters)
{
	using namespace PCGVoxelMatchRulesTests;

	// Round trip.
	const FString Text =
		TEXT("0 Corner: +Z=E Sides=TwoAdjacent | Turns=Any\n")
		TEXT("# a comment\n")
		TEXT("\n")
		TEXT("1: !Thin=Z {+X=F} !{-Y=E} +Z=e | turns=exact preferup=no spin=random\n")
		TEXT("2 Default:");
	FPCGVoxelRuleMapping Mapping;
	FString Error;
	TestTrue(TEXT("parses"), PCGVoxelRules::ParseMapping(Text, Mapping, Error));
	TestEqual(TEXT("parse error is empty"), Error, FString());
	TestEqual(TEXT("3 entries"), Mapping.Entries.Num(), 3);
	const FString Written = Mapping.ToText();
	TArray<FString> Lines;
	Written.ParseIntoArrayLines(Lines);
	TestTrue(TEXT("entry 0 writes as read"), Lines.IsValidIndex(0) && Lines[0] == TEXT("0 Corner: +Z=E Sides=TwoAdjacent | Turns=Any"));
	// The first plain Faces rule is written loose, the others in braces.
	TestTrue(TEXT("entry 1 keeps every rule and setting"), Lines.IsValidIndex(1) && Lines[1] == TEXT("1: !Thin=Z +X=F !{-Y=E} {+Z=E} | Turns=Exact PreferUp=No Spin=Random"));
	TestTrue(TEXT("a rule-less entry writes its settings only"), Lines.IsValidIndex(2) && Lines[2] == TEXT("2 Default: | Turns=Up"));
	AddInfo(TEXT("Written: ") + Written);
	FPCGVoxelRuleMapping Again;
	TestTrue(TEXT("its text parses again"), PCGVoxelRules::ParseMapping(Written, Again, Error));
	TestEqual(TEXT("and writes the same text"), Again.ToText(), Written);
	TestTrue(TEXT("entry 1 has 4 rules"), Mapping.Entries.IsValidIndex(1) && Mapping.Entries[1].Rule.Rules.Num() == 4);
	TestTrue(TEXT("entry 2 has no rules"), Mapping.Entries.IsValidIndex(2) && Mapping.Entries[2].Rule.Rules.Num() == 0);

	auto ErrorOf = [](const TCHAR* Bad)
	{
		FPCGVoxelRuleMapping Unused;
		FString Message;
		PCGVoxelRules::ParseMapping(Bad, Unused, Message);
		return Message;
	};
	TestTrue(TEXT("unknown token named, with its line"), ErrorOf(TEXT("0: +Z=E\n1 X: +Q=E")).StartsWith(TEXT("Line 2: '+Q=E'")));
	TestTrue(TEXT("bad index named"), ErrorOf(TEXT("x: +Z=E")).Contains(TEXT("'x' is not a mesh index")));
	TestTrue(TEXT("Sides and a side token clash"), ErrorOf(TEXT("0: Sides=One +X=E")).Contains(TEXT("use one or the other")));
	TestTrue(TEXT("a face set twice"), ErrorOf(TEXT("0: +Z=E +Z=F")).Contains(TEXT("second time")));
	TestTrue(TEXT("bad Turns"), ErrorOf(TEXT("0: | Turns=Sideways")).Contains(TEXT("Turns is Exact")));
	TestTrue(TEXT("unclosed brace"), ErrorOf(TEXT("0: {+Z=E")).Contains(TEXT("never closed")));
	TestTrue(TEXT("no colon"), ErrorOf(TEXT("0 +Z=E")).Contains(TEXT("no ':'")));

	// Node validation, one invariant each.
	const TArray<FIntVector> One = { FIntVector(0) };
	AddExpectedErrorPlain(TEXT("Rules Text, Line 1"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("bad text stops the node"), Match(MakeVoxels(One), [](UPCGVoxelMatchRulesSettings& S) { SetText(S, TEXT("0: +Q=E")); }).NumOutputs, 0);

	AddExpectedErrorPlain(TEXT("Rules Asset is not set"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("no asset"), Match(MakeVoxels(One), [](UPCGVoxelMatchRulesSettings& S) { S.RulesSource = EPCGVoxelRulesSource::Asset; }).NumOutputs, 0);

	AddExpectedErrorPlain(TEXT("no enabled entries"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("empty mapping"), Match(MakeVoxels(One), [](UPCGVoxelMatchRulesSettings&) {}).NumOutputs, 0);

	AddExpectedErrorPlain(TEXT("Entry 0 ('Bad') has Mesh Index -2"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("negative index"), Match(MakeVoxels(One), [](UPCGVoxelMatchRulesSettings& S)
	{
		FPCGVoxelRuleEntry& Entry = S.Rules.Entries.AddDefaulted_GetRef();
		Entry.Name = TEXT("Bad");
		Entry.MeshIndex = -2;
	}).NumOutputs, 0);

	AddExpectedErrorPlain(TEXT("Entry 0, rule 0 has no type"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("untyped rule"), Match(MakeVoxels(One), [](UPCGVoxelMatchRulesSettings& S)
	{
		S.Rules.Entries.AddDefaulted_GetRef().Rule.Rules.AddDefaulted();
	}).NumOutputs, 0);

	AddExpectedErrorPlain(TEXT("are both 'MeshIndex'"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("same names"), Match(MakeVoxels(One), [](UPCGVoxelMatchRulesSettings& S)
	{
		S.Rules.Entries.AddDefaulted();
		S.RuleIndexAttribute = TEXT("MeshIndex");
	}).NumOutputs, 0);

	// Inline works the same as text, and an entry with no rules catches everything.
	const FResult Inline = Match(MakeVoxels(One), [](UPCGVoxelMatchRulesSettings& S)
	{
		S.Rules.Entries.AddDefaulted_GetRef().MeshIndex = 7;
	});
	TestTrue(TEXT("a rule-less entry matches a lone cell, keeping Id and tags"), Inline.Out.Num() == 1 && Inline.Out[0].MeshIndex == 7 && Inline.Out[0].Id == 0);
	return true;
}

#endif
