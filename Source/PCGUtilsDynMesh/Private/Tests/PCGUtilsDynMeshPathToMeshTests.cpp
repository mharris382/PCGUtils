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
	"PCGUtils.DynMesh.PathToMesh.OpenPathsAreDiscarded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshPathToMeshOpenTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshPathToMeshTests;

	AddExpectedMessage(TEXT("the path is open"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedMessage(TEXT("was not found in the input's @Data domain"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedMessage(TEXT("the spline is not a closed loop"), EAutomationExpectedErrorFlags::Contains, 1);

	UPCGDynMeshPathToMeshSettings* Settings = NewObject<UPCGDynMeshPathToMeshSettings>();
	UTEST_EQUAL("A path marked open makes no mesh", Run(Settings, {MakePath(false, false)}).TaggedData.Num(), 0);
	UTEST_EQUAL("A path with no IsClosed makes no mesh", Run(Settings, {MakePath(false, {})}).TaggedData.Num(), 0);

	UPCGDynMeshPathToMeshSettings* SplineSettings = NewObject<UPCGDynMeshPathToMeshSettings>();
	SplineSettings->bInputSplines = true;
	UTEST_EQUAL("An open spline makes no mesh", Run(SplineSettings, {MakeSpline(false)}).TaggedData.Num(), 0);
	UTEST_EQUAL("A closed spline makes a mesh", Run(SplineSettings, {MakeSpline(true)}).TaggedData.Num(), 1);
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

	AddExpectedMessage(TEXT("Up Direction (input 0)"), EAutomationExpectedErrorFlags::Contains, 1);
	UTEST_EQUAL("A path without @Data.Normal fails in @Data mode", Run(Settings, {MakePath(false, true)}).TaggedData.Num(), 0);
	return true;
}

#endif
