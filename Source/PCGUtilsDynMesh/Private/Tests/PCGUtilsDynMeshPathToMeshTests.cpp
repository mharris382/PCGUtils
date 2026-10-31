// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "Components/SplineComponent.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGPointArrayData.h"
#include "Data/PCGSplineData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Creation/PCGDynMeshPathToMesh.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataDomain.h"
#include "PCGContext.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshPathToMeshTests
{
	const FVector SquareCenter(500.0, 200.0, 30.0);

	TArray<FVector> MakeSquare(double HalfSize, bool bReversed)
	{
		TArray<FVector> Square = {
			SquareCenter + FVector(-HalfSize, -HalfSize, 0), SquareCenter + FVector(HalfSize, -HalfSize, 0),
			SquareCenter + FVector(HalfSize, HalfSize, 0), SquareCenter + FVector(-HalfSize, HalfSize, 0)};
		if (bReversed)
		{
			Algo::Reverse(Square);
		}
		return Square;
	}

	/** Writes one @Data value. Call with bAddEntry on the first attribute written to a data object only. */
	template <typename T>
	void SetDataValue(UPCGData* Data, FName Name, const T& Value, bool bAddEntry)
	{
		FPCGMetadataDomain* Domain = Data->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Data);
		FPCGMetadataAttribute<T>* Attribute = Domain->FindOrCreateAttribute<T>(Name, T{}, false, false, true);
		if (bAddEntry) { Domain->AddEntry(); }
		Attribute->SetValue(PCGFirstEntryKey, Value);
	}

	/** A square path. bClosed unset means the path carries no IsClosed attribute at all. */
	UPCGPointArrayData* MakePath(bool bReversed, TOptional<bool> bClosed)
	{
		const TArray<FVector> Square = MakeSquare(50.0, bReversed);
		UPCGPointArrayData* Data = NewObject<UPCGPointArrayData>();
		Data->SetNumPoints(Square.Num());
		TPCGValueRange<FTransform> Transforms = Data->GetTransformValueRange();
		for (int32 Index = 0; Index < Square.Num(); ++Index)
		{
			Transforms[Index] = FTransform(Square[Index]);
		}
		// A user attribute first, so the copy has something besides IsClosed to carry.
		SetDataValue<float>(Data, TEXT("RoomHeight"), 250.0f, /*bAddEntry=*/true);
		if (bClosed.IsSet())
		{
			SetDataValue<bool>(Data, TEXT("IsClosed"), *bClosed, /*bAddEntry=*/false);
		}
		return Data;
	}

	UPCGSplineData* MakeSpline(bool bClosed)
	{
		TArray<FSplinePoint> Points;
		const TArray<FVector> Square = MakeSquare(50.0, false);
		for (int32 Index = 0; Index < Square.Num(); ++Index)
		{
			Points.Emplace(static_cast<float>(Index), Square[Index], ESplinePointType::Linear);
		}
		UPCGSplineData* Data = NewObject<UPCGSplineData>();
		Data->Initialize(Points, bClosed, FTransform::Identity);
		SetDataValue<float>(Data, TEXT("RoomHeight"), 250.0f, /*bAddEntry=*/true);
		return Data;
	}

	FPCGDataCollection Run(UPCGSettings* Settings, TArray<const UPCGData*> Inputs)
	{
		FPCGDataCollection Input;
		for (const UPCGData* Data : Inputs)
		{
			FPCGTaggedData& Tagged = Input.TaggedData.Emplace_GetRef();
			Tagged.Data = Data;
			Tagged.Pin = PCGPinConstants::DefaultInputLabel;
		}
		Input.TaggedData.Emplace_GetRef().Data = Settings;
		FPCGElementPtr Element = Settings->GetElement();
		TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&Input, nullptr, nullptr)));
		Context->AsyncState.bIsRunningOnMainThread = true;
		Context->AsyncState.NumAvailableTasks = 1;
		for (int32 Iteration = 0; Iteration < 10; ++Iteration)
		{
			if (Element->Execute(Context.Get())) { return Context->OutputData; }
		}
		return {};
	}

	const UE::Geometry::FDynamicMesh3* MeshOf(const FPCGTaggedData& Tagged)
	{
		const UPCGDynamicMeshData* Data = Cast<UPCGDynamicMeshData>(Tagged.Data);
		return Data ? Data->GetDynamicMesh()->GetMeshPtr() : nullptr;
	}

	bool ReadMeshTransform(const FPCGTaggedData& Tagged, FTransform& Out)
	{
		const FPCGMetadataAttribute<FTransform>* Attribute = Tagged.Data->ConstMetadata()->GetConstTypedAttribute<FTransform>(
			FPCGAttributeIdentifier(TEXT("MeshTransform"), PCGMetadataDomainID::Data));
		if (!Attribute) { return false; }
		Out = Attribute->GetValueFromItemKey(PCGFirstEntryKey);
		return true;
	}

	bool HasDataAttribute(const FPCGTaggedData& Tagged, FName Name)
	{
		const FPCGMetadataDomain* Domain = Tagged.Data->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Data);
		return Domain && Domain->HasAttribute(Name);
	}

	/** Every triangle's normal, in world space, agrees with Direction. */
	bool AllFacesToward(const UE::Geometry::FDynamicMesh3& Mesh, const FTransform& Frame, const FVector& Direction)
	{
		for (const int32 TriangleID : Mesh.TriangleIndicesItr())
		{
			if (Frame.TransformVectorNoScale(Mesh.GetTriNormal(TriangleID)).Dot(Direction) <= 0.0)
			{
				return false;
			}
		}
		return Mesh.TriangleCount() > 0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshPathToMeshFillTest,
	"PCGUtils.DynMesh.PathToMesh.ClosedPathFillsFacingUpAtCentroid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshPathToMeshFillTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshPathToMeshTests;

	// The winding of an authored path is not something a user controls: both must come out facing up.
	for (const bool bReversed : {false, true})
	{
		UPCGDynMeshPathToMeshSettings* Settings = NewObject<UPCGDynMeshPathToMeshSettings>();
		const FPCGDataCollection Output = Run(Settings, {MakePath(bReversed, true)});
		UTEST_EQUAL("One mesh per closed path", Output.TaggedData.Num(), 1);

		const UE::Geometry::FDynamicMesh3* Mesh = MeshOf(Output.TaggedData[0]);
		UTEST_NOT_NULL("The output is DynMesh data", Mesh);
		UTEST_EQUAL("A square fills with two triangles", Mesh->TriangleCount(), 2);

		FTransform Frame;
		UTEST_TRUE("MeshTransform is written", ReadMeshTransform(Output.TaggedData[0], Frame));
		UTEST_TRUE("The frame is the path centroid", Frame.GetLocation().Equals(SquareCenter, 1e-6));
		UTEST_TRUE("The frame has no rotation", Frame.GetRotation().Equals(FQuat::Identity, 1e-9));
		UTEST_TRUE("Vertices are relative to the frame", Mesh->GetBounds().Center().Equals(FVector3d::ZeroVector, 1e-6));
		UTEST_TRUE("Faces point up for either winding", AllFacesToward(*Mesh, Frame, FVector::UpVector));

		UTEST_TRUE("Path @Data attributes are copied", HasDataAttribute(Output.TaggedData[0], TEXT("RoomHeight")));
		UTEST_FALSE("IsClosed is not copied by default", HasDataAttribute(Output.TaggedData[0], TEXT("IsClosed")));
		const FPCGMetadataAttribute<float>* Height = Output.TaggedData[0].Data->ConstMetadata()->GetConstTypedAttribute<float>(
			FPCGAttributeIdentifier(TEXT("RoomHeight"), PCGMetadataDomainID::Data));
		UTEST_EQUAL("The copied value survives", Height->GetValueFromItemKey(PCGFirstEntryKey), 250.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshPathToMeshOpenTest,
	"PCGUtils.DynMesh.PathToMesh.OpenInputsFillLikeTheEngineNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshPathToMeshOpenTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshPathToMeshTests;

	// The engine's Spline To Mesh treats an open curve as closed by default, and asks nothing of @Data.
	UPCGDynMeshPathToMeshSettings* Settings = NewObject<UPCGDynMeshPathToMeshSettings>();
	UTEST_EQUAL("A path marked open is filled as closed", Run(Settings, {MakePath(false, false)}).TaggedData.Num(), 1);
	UTEST_EQUAL("A path with no IsClosed is filled", Run(Settings, {MakePath(false, {})}).TaggedData.Num(), 1);
	Settings->ClosedAttributeName = NAME_None;
	UTEST_EQUAL("An empty Closed Attribute Name is not an error", Run(Settings, {MakePath(false, true)}).TaggedData.Num(), 1);

	UPCGDynMeshPathToMeshSettings* SplineSettings = NewObject<UPCGDynMeshPathToMeshSettings>();
	SplineSettings->bInputSplines = true;
	UTEST_EQUAL("An open spline is filled as closed", Run(SplineSettings, {MakeSpline(false)}).TaggedData.Num(), 1);
	UTEST_EQUAL("A closed spline makes a mesh", Run(SplineSettings, {MakeSpline(true)}).TaggedData.Num(), 1);

	// Open Curves = Offset strokes an open input into a band, as in the engine node; a closed one is still filled.
	SplineSettings->OpenCurves = EOffsetOpenCurvesMethod::Offset;
	SplineSettings->FlattenMethod = EFlattenCurveMethod::ToBestFitPlane;
	SplineSettings->CurveOffset = 5.0;
	const FPCGDataCollection Band = Run(SplineSettings, {MakeSpline(false)});
	UTEST_EQUAL("An offset open spline makes a mesh", Band.TaggedData.Num(), 1);
	const UE::Geometry::FDynamicMesh3* BandMesh = MeshOf(Band.TaggedData[0]);
	UTEST_NOT_NULL("The band is DynMesh data", BandMesh);
	// The band follows three sides of the square; the fill would cover the whole 100 x 100 square.
	const UE::Geometry::FAxisAlignedBox3d BandBounds = BandMesh->GetBounds();
	double BandArea = 0.0;
	for (const int32 TriangleID : BandMesh->TriangleIndicesItr())
	{
		BandArea += BandMesh->GetTriArea(TriangleID);
	}
	UTEST_TRUE("The band is far smaller than the filled square", BandArea < 0.5 * 100.0 * 100.0);
	UTEST_TRUE("The band still spans the square", BandBounds.Width() > 90.0);

	UPCGDynMeshPathToMeshSettings* TwoPointSettings = NewObject<UPCGDynMeshPathToMeshSettings>();
	UPCGPointArrayData* TwoPoints = MakePath(false, {});
	TwoPoints->SetNumPoints(2);
	AddExpectedMessage(TEXT("at least 3 are needed to enclose an area"), EAutomationExpectedErrorFlags::Contains, 1);
	UTEST_EQUAL("Two points cannot be filled", Run(TwoPointSettings, {TwoPoints}).TaggedData.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshPathToMeshInheritedDataTest,
	"PCGUtils.DynMesh.PathToMesh.InheritedDataAttributesAreCopied",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshPathToMeshInheritedDataTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshPathToMeshTests;

	// Data made by an upstream node from another data inherits its @Data through a parent, rather than owning it.
	// That is the usual case in a real graph, and the engine's domain copy refused it ("Invalid parenting").
	UPCGSplineData* SourceSpline = MakeSpline(true);
	UPCGSplineData* ChildSpline = Cast<UPCGSplineData>(SourceSpline->DuplicateData(/*Context=*/nullptr, /*bInitializeMetadata=*/true));
	UTEST_NOT_NULL("The spline duplicates", ChildSpline);

	UPCGPointArrayData* SourcePath = MakePath(false, true);
	UPCGPointArrayData* ChildPath = NewObject<UPCGPointArrayData>();
	ChildPath->InitializeFromData(SourcePath);
	ChildPath->SetNumPoints(SourcePath->GetNumPoints());
	SourcePath->CopyPropertiesTo(ChildPath, 0, 0, SourcePath->GetNumPoints(), EPCGPointNativeProperties::All);

	UPCGDynMeshPathToMeshSettings* SplineSettings = NewObject<UPCGDynMeshPathToMeshSettings>();
	SplineSettings->bInputSplines = true;
	UPCGDynMeshPathToMeshSettings* PathSettings = NewObject<UPCGDynMeshPathToMeshSettings>();

	const TPair<UPCGDynMeshPathToMeshSettings*, const UPCGData*> Cases[] = {{SplineSettings, ChildSpline}, {PathSettings, ChildPath}};
	for (const TPair<UPCGDynMeshPathToMeshSettings*, const UPCGData*>& Case : Cases)
	{
		const FPCGDataCollection Output = Run(Case.Key, {Case.Value});
		UTEST_EQUAL("One mesh", Output.TaggedData.Num(), 1);
		const FPCGMetadataAttribute<float>* Height = Output.TaggedData[0].Data->ConstMetadata()->GetConstTypedAttribute<float>(
			FPCGAttributeIdentifier(TEXT("RoomHeight"), PCGMetadataDomainID::Data));
		UTEST_NOT_NULL("The inherited @Data attribute is copied", Height);
		UTEST_EQUAL("The inherited value survives", Height->GetValueFromItemKey(PCGFirstEntryKey), 250.0f);
		FTransform Frame;
		UTEST_TRUE("MeshTransform is still written", ReadMeshTransform(Output.TaggedData[0], Frame));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshPathToMeshFrameAttributeTest,
	"PCGUtils.DynMesh.PathToMesh.TransformAttributeFramesTheMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshPathToMeshFrameAttributeTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshPathToMeshTests;

	UPCGPointArrayData* Path = MakePath(false, true);
	const FTransform ComponentTransform(FRotator(0, 30, 0), FVector(100, 0, 0));
	SetDataValue<FTransform>(Path, TEXT("RelativeTransform"), ComponentTransform, /*bAddEntry=*/false);

	UPCGDynMeshPathToMeshSettings* Settings = NewObject<UPCGDynMeshPathToMeshSettings>();
	const FPCGDataCollection Output = Run(Settings, {Path});
	UTEST_EQUAL("One mesh", Output.TaggedData.Num(), 1);

	FTransform Frame;
	UTEST_TRUE("MeshTransform is written", ReadMeshTransform(Output.TaggedData[0], Frame));
	UTEST_TRUE("The attribute is the frame", Frame.Equals(ComponentTransform, 1e-6));

	// The frame changes how vertices are stored, never where the mesh is.
	const UE::Geometry::FDynamicMesh3* Mesh = MeshOf(Output.TaggedData[0]);
	FBox WorldBounds(ForceInit);
	for (const int32 VertexID : Mesh->VertexIndicesItr())
	{
		WorldBounds += Frame.TransformPosition(Mesh->GetVertex(VertexID));
	}
	UTEST_TRUE("The mesh stays on the path", WorldBounds.GetCenter().Equals(SquareCenter, 1e-4));
	UTEST_TRUE("Faces still point up in a rotated frame", AllFacesToward(*Mesh, Frame, FVector::UpVector));

	Settings->bUseTransformAttribute = false;
	const FPCGDataCollection CentroidOutput = Run(Settings, {Path});
	UTEST_TRUE("MeshTransform is written", ReadMeshTransform(CentroidOutput.TaggedData[0], Frame));
	UTEST_TRUE("With the attribute off, the centroid frames it", Frame.GetLocation().Equals(SquareCenter, 1e-6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshPathToMeshFacingTest,
	"PCGUtils.DynMesh.PathToMesh.FacingAndThickness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshPathToMeshFacingTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshPathToMeshTests;

	for (const bool bReversed : {false, true})
	{
		UPCGDynMeshPathToMeshSettings* Settings = NewObject<UPCGDynMeshPathToMeshSettings>();
		Settings->UpDirection.Constant = FVector(0, 0, -3);
		FPCGDataCollection Output = Run(Settings, {MakePath(bReversed, true)});
		FTransform Frame;
		UTEST_TRUE("MeshTransform is written", ReadMeshTransform(Output.TaggedData[0], Frame));
		UTEST_TRUE("Faces follow a downward Up Direction", AllFacesToward(*MeshOf(Output.TaggedData[0]), Frame, FVector::DownVector));

		Settings->UpDirection.Constant = FVector::UpVector;
		Settings->bFlipResult = true;
		Output = Run(Settings, {MakePath(bReversed, true)});
		UTEST_TRUE("Flip makes them face away from Up Direction", AllFacesToward(*MeshOf(Output.TaggedData[0]), Frame, FVector::DownVector));

		// Thickness grows the solid toward the side the faces point.
		Settings->bFlipResult = false;
		Settings->Thickness.Constant = 40.0;
		Output = Run(Settings, {MakePath(bReversed, true)});
		const UE::Geometry::FAxisAlignedBox3d Bounds = MeshOf(Output.TaggedData[0])->GetBounds();
		UTEST_EQUAL_TOLERANCE("The solid starts at the path", Bounds.Min.Z, 0.0, 1e-6);
		UTEST_EQUAL_TOLERANCE("and rises by Thickness", Bounds.Max.Z, 40.0, 1e-6);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshPathToMeshDataUpTest,
	"PCGUtils.DynMesh.PathToMesh.UpDirectionFromData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshPathToMeshDataUpTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshPathToMeshTests;

	UPCGPointArrayData* Path = MakePath(false, true);
	SetDataValue<FVector>(Path, TEXT("Normal"), FVector::DownVector, /*bAddEntry=*/false);

	UPCGDynMeshPathToMeshSettings* Settings = NewObject<UPCGDynMeshPathToMeshSettings>();
	Settings->UpDirection.Input = EPCGUtilsInputValueType::Attribute;
	const FPCGDataCollection Output = Run(Settings, {Path});
	UTEST_EQUAL("One mesh", Output.TaggedData.Num(), 1);
	FTransform Frame;
	UTEST_TRUE("MeshTransform is written", ReadMeshTransform(Output.TaggedData[0], Frame));
	UTEST_TRUE("@Data.Normal drives the facing", AllFacesToward(*MeshOf(Output.TaggedData[0]), Frame, FVector::DownVector));

	AddExpectedMessage(TEXT("Up Direction (input 0)"), EAutomationExpectedErrorFlags::Contains, 1, /*bIsRegex=*/false);
	UTEST_EQUAL("A path without @Data.Normal fails in @Data mode", Run(Settings, {MakePath(false, true)}).TaggedData.Num(), 0);
	return true;
}

#endif
