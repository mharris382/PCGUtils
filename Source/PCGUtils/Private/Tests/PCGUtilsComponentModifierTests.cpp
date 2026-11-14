// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Components/PCGMarkerComponent.h"
#include "Components/PCGSplineComponent.h"
#include "Data/PCGPointArrayData.h"
#include "Data/PCGSplineData.h"
#include "Elements/PCGGetMarkerData.h"
#include "Elements/PCGGetPathData.h"
#include "Elements/PCGGetPointData.h"
#include "GameFramework/Actor.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataDomain.h"
#include "Modifiers/PCGUtilsArrayModifier.h"
#include "Modifiers/PCGUtilsMirrorModifier.h"
#include "Modifiers/PCGUtilsModifierStack.h"
#include "PCGContext.h"

namespace PCGUtilsComponentModifierTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** Points with 100 cm bounds, an "Id" attribute per point and a "Source" @Data attribute. */
	UPCGPointArrayData* MakePoints(const TArray<FTransform>& Transforms)
	{
		UPCGPointArrayData* Points = NewObject<UPCGPointArrayData>();
		Points->SetNumPoints(Transforms.Num());
		Points->AllocateProperties(EPCGPointNativeProperties::All);
		FPCGMetadataDomain* Elements = Points->Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements);
		FPCGMetadataDomain* Data = Points->Metadata->GetMetadataDomain(PCGMetadataDomainID::Data);
		FPCGMetadataAttribute<int32>* Id = Elements->CreateAttribute<int32>(TEXT("Id"), -1, false);
		Data->CreateAttribute<int32>(TEXT("Source"), 42, false);
		TPCGValueRange<FTransform> TransformRange = Points->GetTransformValueRange();
		TPCGValueRange<FVector> Mins = Points->GetBoundsMinValueRange();
		TPCGValueRange<FVector> Maxs = Points->GetBoundsMaxValueRange();
		TPCGValueRange<int64> Entries = Points->GetMetadataEntryValueRange();
		for (int32 Index = 0; Index < Transforms.Num(); ++Index)
		{
			TransformRange[Index] = Transforms[Index];
			Mins[Index] = FVector(-50.0);
			Maxs[Index] = FVector(50.0);
			Entries[Index] = Elements->AddEntry();
			Id->SetValue(Entries[Index], 10 + Index);
		}
		return Points;
	}

	UPCGSplineData* MakeSpline(const TArray<FVector>& Positions, bool bClosed, bool bLinear = true)
	{
		TArray<FSplinePoint> Points;
		for (int32 Index = 0; Index < Positions.Num(); ++Index)
		{
			Points.Emplace(static_cast<float>(Index), Positions[Index], bLinear ? ESplinePointType::Linear : ESplinePointType::Curve);
		}
		UPCGSplineData* Spline = NewObject<UPCGSplineData>();
		Spline->Initialize(Points, bClosed, FTransform::Identity);
		Spline->Metadata->GetMetadataDomain(PCGMetadataDomainID::Data)->CreateAttribute<int32>(TEXT("Source"), 42, false);
		return Spline;
	}

	FPCGUtilsModifierContext MakeContext(TArray<FPCGUtilsModifierWarning>* Warnings, const FTransform& Space = FTransform::Identity)
	{
		FPCGUtilsModifierContext Context;
		Context.ModifierToWorld = Space;
		Context.Warnings = Warnings;
		return Context;
	}

	int32 ReadPointInt(const UPCGBasePointData* Data, FName Name, int32 Index)
	{
		const FPCGMetadataAttribute<int32>* Attribute = Data->ConstMetadata()->GetConstTypedAttribute<int32>(Name);
		return Attribute ? Attribute->GetValueFromItemKey(Data->GetMetadataEntry(Index)) : INDEX_NONE;
	}

	int32 ReadDataInt(const UPCGData* Data, FName Name)
	{
		const FPCGMetadataAttribute<int32>* Attribute = Data->ConstMetadata()->GetConstTypedAttribute<int32>(FPCGAttributeIdentifier(Name, PCGMetadataDomainID::Data));
		return Attribute ? Attribute->GetValueFromItemKey(PCGFirstEntryKey) : INDEX_NONE;
	}

	TArray<FVector> WorldPositions(const UPCGSplineData* Spline)
	{
		TArray<FVector> Result;
		for (const FPCGPathPoint& Point : FPCGUtilsModifierHelpers::SplineToPathPoints(Spline))
		{
			Result.Add(Point.Transform.GetLocation());
		}
		return Result;
	}

	bool ContainsNear(const TArray<FVector>& Points, const FVector& Expected, double Tolerance = 0.01)
	{
		return Points.ContainsByPredicate([&](const FVector& Point) { return Point.Equals(Expected, Tolerance); });
	}

	template <typename ModifierType>
	FPCGUtilsModifierStack MakeStack(ModifierType*& OutModifier)
	{
		FPCGUtilsModifierStack Stack;
		OutModifier = NewObject<ModifierType>();
		Stack.Modifiers.Add(OutModifier);
		return Stack;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsArrayModifierLineTest, "PCGUtils.Modifiers.Array.Line", PCGUtilsComponentModifierTests::Flags)
bool FPCGUtilsArrayModifierLineTest::RunTest(const FString& Parameters)
{
	using namespace PCGUtilsComponentModifierTests;
	UPCGUtilsArrayModifier* Array = nullptr;
	const FPCGUtilsModifierStack Stack = MakeStack(Array);
	Array->Count = 3;
	Array->bWriteCopyIndex = true;
	TArray<FPCGUtilsModifierWarning> Warnings;

	// Component turned 90 degrees and moved: the row follows the component's own X.
	const FTransform Space(FRotator(0, 90, 0), FVector(1000, 0, 0));
	TArray<UPCGBasePointData*> Results = Stack.ApplyToPoints(MakeContext(&Warnings, Space), MakePoints({Space}));
	if (!TestEqual(TEXT("One dataset out"), Results.Num(), 1)) return false;
	const UPCGBasePointData* Out = Results[0];
	if (!TestEqual(TEXT("Three copies"), Out->GetNumPoints(), 3)) return false;
	TestTrue(TEXT("Copy 1 one bounds width along the component's X (world +Y)"), Out->GetTransform(1).GetLocation().Equals(FVector(1000, 100, 0), 0.01));
	TestTrue(TEXT("Copy 2 two widths along"), Out->GetTransform(2).GetLocation().Equals(FVector(1000, 200, 0), 0.01));
	TestEqual(TEXT("Copy index 2"), ReadPointInt(Out, TEXT("ArrayIndex"), 2), 2);
	TestEqual(TEXT("Copies inherit the source's point attribute"), ReadPointInt(Out, TEXT("Id"), 2), 10);
	TestEqual(TEXT("@Data survives"), ReadDataInt(Out, TEXT("Source")), 42);

	// Rotation compounds per copy, position stays linear, the line never turns.
	Array->RotationStep = FRotator(0, 10, 0);
	Results = Stack.ApplyToPoints(MakeContext(&Warnings), MakePoints({FTransform::Identity}));
	Out = Results[0];
	TestTrue(TEXT("Rotated copy 2 still on the line"), Out->GetTransform(2).GetLocation().Equals(FVector(200, 0, 0), 0.01));
	TestEqual(TEXT("Copy 2 turned twice the step"), Out->GetTransform(2).Rotator().Yaw, 20.0, 0.01);

	// Fit Length: floor(250 / 100) + 1.
	Array->FitType = EPCGUtilsArrayFit::FitLength;
	Array->Length = 250.0;
	Results = Stack.ApplyToPoints(MakeContext(&Warnings), MakePoints({FTransform::Identity}));
	TestEqual(TEXT("Fit Length count"), Results[0]->GetNumPoints(), 3);
	TestEqual(TEXT("No warnings"), Warnings.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsArrayModifierTransformCircleTest, "PCGUtils.Modifiers.Array.TransformAndCircle", PCGUtilsComponentModifierTests::Flags)
bool FPCGUtilsArrayModifierTransformCircleTest::RunTest(const FString& Parameters)
{
	using namespace PCGUtilsComponentModifierTests;
	UPCGUtilsArrayModifier* Array = nullptr;
	const FPCGUtilsModifierStack Stack = MakeStack(Array);
	TArray<FPCGUtilsModifierWarning> Warnings;

	// Blender's object offset: step = turn 90 then move 100 along X; it compounds into position.
	Array->Mode = EPCGUtilsArrayMode::Transform;
	Array->bUseRelativeOffset = false;
	Array->ObjectOffset = FVector(100, 0, 0);
	Array->RotationStep = FRotator(0, 90, 0);
	Array->Count = 4;
	const UPCGBasePointData* Out = Stack.ApplyToPoints(MakeContext(&Warnings), MakePoints({FTransform::Identity}))[0];
	TestTrue(TEXT("Transform copy 1"), Out->GetTransform(1).GetLocation().Equals(FVector(100, 0, 0), 0.01));
	TestTrue(TEXT("Transform copy 2 turns the path"), Out->GetTransform(2).GetLocation().Equals(FVector(100, 100, 0), 0.01));
	TestTrue(TEXT("Transform copy 3"), Out->GetTransform(3).GetLocation().Equals(FVector(0, 100, 0), 0.01));

	// Circle about (-200, 0, 0): four copies a quarter turn apart, no seam duplicate.
	Array->Mode = EPCGUtilsArrayMode::Circle;
	Array->CircleCenter = FVector(-200, 0, 0);
	Out = Stack.ApplyToPoints(MakeContext(&Warnings), MakePoints({FTransform::Identity}))[0];
	if (!TestEqual(TEXT("Circle count"), Out->GetNumPoints(), 4)) return false;
	TestTrue(TEXT("Circle copy 1"), Out->GetTransform(1).GetLocation().Equals(FVector(-200, 200, 0), 0.01));
	TestTrue(TEXT("Circle copy 2"), Out->GetTransform(2).GetLocation().Equals(FVector(-400, 0, 0), 0.01));
	TestEqual(TEXT("Aligned copy faces along the ring"), Out->GetTransform(1).Rotator().Yaw, 90.0, 0.01);

	Array->bAlignToRing = false;
	Out = Stack.ApplyToPoints(MakeContext(&Warnings), MakePoints({FTransform::Identity}))[0];
	TestEqual(TEXT("Unaligned copy keeps its orientation"), Out->GetTransform(1).Rotator().Yaw, 0.0, 0.01);

	// Merge First and Last removes a 360 ring's closing copy when it lands on the original.
	Array->Mode = EPCGUtilsArrayMode::Transform;
	Array->ObjectOffset = FVector::ZeroVector;
	Array->Count = 5;
	Array->bMerge = true;
	Array->bMergeFirstAndLast = true;
	Out = Stack.ApplyToPoints(MakeContext(&Warnings), MakePoints({FTransform(FVector(100, 0, 0))}))[0];
	TestEqual(TEXT("Seam duplicate merged"), Out->GetNumPoints(), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsArrayModifierPathTest, "PCGUtils.Modifiers.Array.Path", PCGUtilsComponentModifierTests::Flags)
bool FPCGUtilsArrayModifierPathTest::RunTest(const FString& Parameters)
{
	using namespace PCGUtilsComponentModifierTests;
	UPCGUtilsArrayModifier* Array = nullptr;
	const FPCGUtilsModifierStack Stack = MakeStack(Array);
	Array->Count = 3;
	Array->bWriteCopyIndex = true;
	TArray<FPCGUtilsModifierWarning> Warnings;

	// A 100 x 50 path: relative offset (1, 0, 0) steps it by its own width.
	const TArray<UPCGSplineData*> Results = Stack.ApplyToPath(MakeContext(&Warnings), MakeSpline({FVector(0, 0, 0), FVector(100, 0, 0), FVector(100, 50, 0)}, false));
	if (!TestEqual(TEXT("One path per copy"), Results.Num(), 3)) return false;
	TestTrue(TEXT("Copy 2 shifted two widths"), WorldPositions(Results[2])[0].Equals(FVector(200, 0, 0), 0.01));
	TestEqual(TEXT("Copy index on @Data"), ReadDataInt(Results[2], TEXT("ArrayIndex")), 2);
	TestEqual(TEXT("Source @Data survives"), ReadDataInt(Results[2], TEXT("Source")), 42);
	TestFalse(TEXT("Open stays open"), Results[2]->IsClosed());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsMirrorModifierPointsTest, "PCGUtils.Modifiers.Mirror.Points", PCGUtilsComponentModifierTests::Flags)
bool FPCGUtilsMirrorModifierPointsTest::RunTest(const FString& Parameters)
{
	using namespace PCGUtilsComponentModifierTests;
	UPCGUtilsMirrorModifier* Mirror = nullptr;
	const FPCGUtilsModifierStack Stack = MakeStack(Mirror);
	Mirror->bWriteMirrorSide = true;
	TArray<FPCGUtilsModifierWarning> Warnings;

	const UPCGBasePointData* Out = Stack.ApplyToPoints(MakeContext(&Warnings), MakePoints({FTransform(FVector(50, 20, 0)), FTransform(FVector(3, 0, 0))}))[0];
	if (!TestEqual(TEXT("The point on the plane is merged, the other mirrored"), Out->GetNumPoints(), 3)) return false;
	TestTrue(TEXT("Mirror across X"), Out->GetTransform(2).GetLocation().Equals(FVector(-50, 20, 0), 0.01));
	TestTrue(TEXT("Mirrored point is handed (negative determinant)"), Out->GetTransform(2).ToMatrixWithScale().Determinant() < 0.0);
	TestEqual(TEXT("Mirror side bit for X"), ReadPointInt(Out, TEXT("MirrorSide"), 2), 1);
	TestEqual(TEXT("Mirrored copy inherits Id"), ReadPointInt(Out, TEXT("Id"), 2), 10);

	// Two axes give four copies of an off-plane point.
	Mirror->bMirrorY = true;
	Out = Stack.ApplyToPoints(MakeContext(&Warnings), MakePoints({FTransform(FVector(50, 20, 0))}))[0];
	TestEqual(TEXT("X and Y: four points"), Out->GetNumPoints(), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsMirrorModifierPathTest, "PCGUtils.Modifiers.Mirror.Path", PCGUtilsComponentModifierTests::Flags)
bool FPCGUtilsMirrorModifierPathTest::RunTest(const FString& Parameters)
{
	using namespace PCGUtilsComponentModifierTests;
	UPCGUtilsMirrorModifier* Mirror = nullptr;
	const FPCGUtilsModifierStack Stack = MakeStack(Mirror);
	TArray<FPCGUtilsModifierWarning> Warnings;

	// Half a diamond, both ends on the X plane: one closed path of four points.
	TArray<UPCGSplineData*> Results = Stack.ApplyToPath(MakeContext(&Warnings), MakeSpline({FVector(2, -100, 0), FVector(100, 0, 0), FVector(-3, 100, 0)}, false));
	if (!TestEqual(TEXT("Fused into one path"), Results.Num(), 1)) return false;
	TestTrue(TEXT("Both ends joined: closed"), Results[0]->IsClosed());
	TArray<FVector> Points = WorldPositions(Results[0]);
	TestEqual(TEXT("Four points"), Points.Num(), 4);
	TestTrue(TEXT("Welded end sits on the plane"), ContainsNear(Points, FVector(0, -100, 0)));
	TestTrue(TEXT("Mirror of the tip"), ContainsNear(Points, FVector(-100, 0, 0)));
	TestEqual(TEXT("Source @Data survives a fuse"), ReadDataInt(Results[0], TEXT("Source")), 42);
	TestEqual(TEXT("No warnings when both ends join"), Warnings.Num(), 0);

	// One end off the plane: joined at the other end only, open, one warning, nothing moved.
	Warnings.Reset();
	Results = Stack.ApplyToPath(MakeContext(&Warnings), MakeSpline({FVector(0, -100, 0), FVector(100, 0, 0), FVector(50, 100, 0)}, false));
	if (!TestEqual(TEXT("Still one path"), Results.Num(), 1)) return false;
	TestFalse(TEXT("Open"), Results[0]->IsClosed());
	Points = WorldPositions(Results[0]);
	TestEqual(TEXT("Five points"), Points.Num(), 5);
	TestTrue(TEXT("Unjoined end left where it was drawn"), ContainsNear(Points, FVector(50, 100, 0)));
	TestTrue(TEXT("Its twin too"), ContainsNear(Points, FVector(-50, 100, 0)));
	TestEqual(TEXT("One warning"), Warnings.Num(), 1);
	TestTrue(TEXT("Warning placed at the end"), Warnings.Num() == 1 && Warnings[0].WorldLocation.IsSet() && Warnings[0].WorldLocation->Equals(FVector(50, 100, 0), 0.01));

	// Fuse off: two paths, the mirror wound the same way.
	Mirror->bFuse = false;
	Results = Stack.ApplyToPath(MakeContext(&Warnings), MakeSpline({FVector(0, -100, 0), FVector(100, 0, 0), FVector(0, 100, 0)}, false));
	TestEqual(TEXT("Unfused: two paths"), Results.Num(), 2);
	if (Results.Num() == 2)
	{
		TestTrue(TEXT("Mirror reversed: starts at the twin of the last point"), WorldPositions(Results[1])[0].Equals(FVector(0, 100, 0), 0.01));
	}

	// Closed path, Fuse on: union. Overlapping square gives one shape spanning both sides.
	Mirror->bFuse = true;
	Warnings.Reset();
	Results = Stack.ApplyToPath(MakeContext(&Warnings), MakeSpline({FVector(-20, -50, 0), FVector(80, -50, 0), FVector(80, 50, 0), FVector(-20, 50, 0)}, true));
	if (!TestEqual(TEXT("Union: one shape"), Results.Num(), 1)) return false;
	TestTrue(TEXT("Union is closed"), Results[0]->IsClosed());
	FBox Bounds(ForceInit);
	for (const FVector& Point : WorldPositions(Results[0])) Bounds += Point;
	TestTrue(TEXT("Union spans -80..80"), Bounds.Min.Equals(FVector(-80, -50, 0), 0.1) && Bounds.Max.Equals(FVector(80, 50, 0), 0.1));

	// Apart: two shapes and a warning.
	Results = Stack.ApplyToPath(MakeContext(&Warnings), MakeSpline({FVector(10, -50, 0), FVector(110, -50, 0), FVector(110, 50, 0), FVector(10, 50, 0)}, true));
	TestEqual(TEXT("Disjoint union: two shapes"), Results.Num(), 2);
	TestEqual(TEXT("Disjoint union warns"), Warnings.Num(), 1);
	return true;
}

namespace PCGUtilsComponentModifierTests
{
	struct FMarkerElement : FPCGGetMarkerDataElement
	{
		using FPCGGetMarkerDataElement::ProcessActor;
	};
	struct FPathElement : FPCGGetPathElement
	{
		using FPCGGetPathElement::ProcessActor;
	};
	struct FPointElement : FPCGGetPointDataElement
	{
		using FPCGGetPointDataElement::ProcessActor;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsModifierGetNodesTest, "PCGUtils.Modifiers.GetNodes", PCGUtilsComponentModifierTests::Flags)
bool FPCGUtilsModifierGetNodesTest::RunTest(const FString& Parameters)
{
	using namespace PCGUtilsComponentModifierTests;

	AActor* Actor = NewObject<AActor>(GetTransientPackage());
	UPCGMarkerComponent* Marker = NewObject<UPCGMarkerComponent>(Actor, TEXT("Marker"));
	Actor->AddOwnedComponent(Marker);
	UPCGUtilsArrayModifier* Array = NewObject<UPCGUtilsArrayModifier>(Marker);
	Array->Count = 4;
	Marker->ModifierStack.Modifiers.Add(Array);

	// Get Marker Data: one marker, four points out.
	{
		UPCGGetMarkerDataSettings* Settings = NewObject<UPCGGetMarkerDataSettings>();
		FPCGContext Context;
		FMarkerElement().ProcessActor(&Context, Settings, Actor);
		if (!TestEqual(TEXT("Marker: one output"), Context.OutputData.TaggedData.Num(), 1)) return false;
		const UPCGBasePointData* Points = Cast<UPCGBasePointData>(Context.OutputData.TaggedData[0].Data);
		TestTrue(TEXT("Marker: four points"), Points && Points->GetNumPoints() == 4);
	}

	// Get PCG Point Data reads the marker through its point provider interface, with the same stack.
	{
		UPCGGetPointDataSettings* Settings = NewObject<UPCGGetPointDataSettings>();
		FPCGContext Context;
		FPointElement().ProcessActor(&Context, Settings, Actor);
		if (!TestEqual(TEXT("Point provider: one output"), Context.OutputData.TaggedData.Num(), 1)) return false;
		const UPCGBasePointData* Points = Cast<UPCGBasePointData>(Context.OutputData.TaggedData[0].Data);
		TestTrue(TEXT("Point provider: four points"), Points && Points->GetNumPoints() == 4);
	}

	// Get PCG Path Data on a spline with a fusing mirror, both output modes.
	UPCGSplineComponent* Spline = NewObject<UPCGSplineComponent>(Actor, TEXT("Spline"));
	Actor->AddOwnedComponent(Spline);
	Spline->ClearSplinePoints(false);
	Spline->AddSplinePoint(FVector(0, -100, 0), ESplineCoordinateSpace::Local, false);
	Spline->AddSplinePoint(FVector(100, 0, 0), ESplineCoordinateSpace::Local, false);
	Spline->AddSplinePoint(FVector(0, 100, 0), ESplineCoordinateSpace::Local, false);
	Spline->SetClosedLoop(false, false);
	for (int32 Index = 0; Index < 3; ++Index) Spline->SetSplinePointType(Index, ESplinePointType::Linear, false);
	Spline->UpdateSpline();
	Spline->ModifierStack.Modifiers.Add(NewObject<UPCGUtilsMirrorModifier>(Spline));

	for (const EPCGPathDataOutputMode Mode : {EPCGPathDataOutputMode::SplineData, EPCGPathDataOutputMode::PointData})
	{
		UPCGGetPathDataSettings* Settings = NewObject<UPCGGetPathDataSettings>();
		Settings->OutputMode = Mode;
		FPCGContext Context;
		FPathElement().ProcessActor(&Context, Settings, Actor);
		const FString ModeName = Mode == EPCGPathDataOutputMode::SplineData ? TEXT("Spline") : TEXT("Points");
		if (!TestEqual(ModeName + TEXT(": one fused path"), Context.OutputData.TaggedData.Num(), 1)) return false;
		const UPCGData* Data = Context.OutputData.TaggedData[0].Data;
		const FPCGMetadataAttribute<bool>* IsClosed = Data->ConstMetadata()->GetConstTypedAttribute<bool>(FPCGAttributeIdentifier(TEXT("IsClosed"), PCGMetadataDomainID::Data));
		TestTrue(ModeName + TEXT(": @Data.IsClosed is the result's (closed), not the component's (open)"), IsClosed && IsClosed->GetValueFromItemKey(PCGFirstEntryKey));
		if (const UPCGSplineData* SplineData = Cast<UPCGSplineData>(Data))
		{
			TestEqual(TEXT("Spline: four control points"), SplineData->SplineStruct.GetNumberOfPoints(), 4);
		}
		else if (const UPCGBasePointData* Points = Cast<UPCGBasePointData>(Data))
		{
			TestEqual(TEXT("Points: four points"), Points->GetNumPoints(), 4);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsModifierInstancingTest, "PCGUtils.Modifiers.Instancing", PCGUtilsComponentModifierTests::Flags)
bool FPCGUtilsModifierInstancingTest::RunTest(const FString& Parameters)
{
	// Modifiers are instanced subobjects: duplicating a component (copy/paste, Blueprint construction) must give the
	// copy its own modifiers, not share the original's.
	AActor* Actor = NewObject<AActor>(GetTransientPackage());
	UPCGMarkerComponent* Marker = NewObject<UPCGMarkerComponent>(Actor, TEXT("Marker"));
	UPCGUtilsArrayModifier* Array = NewObject<UPCGUtilsArrayModifier>(Marker);
	Array->Count = 7;
	Marker->ModifierStack.Modifiers.Add(Array);

	UPCGMarkerComponent* Copy = DuplicateObject(Marker, Actor, TEXT("MarkerCopy"));
	if (!TestEqual(TEXT("Copy has one modifier"), Copy->ModifierStack.Modifiers.Num(), 1)) return false;
	const UPCGUtilsArrayModifier* CopiedArray = Cast<UPCGUtilsArrayModifier>(Copy->ModifierStack.Modifiers[0]);
	TestTrue(TEXT("Copied modifier is a new object"), CopiedArray && CopiedArray != Array);
	TestTrue(TEXT("Copied modifier is owned by the copy"), CopiedArray && CopiedArray->GetOuter() == Copy);
	TestTrue(TEXT("Settings carried over"), CopiedArray && CopiedArray->Count == 7);
	return true;
}

#endif
