// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGBasePointData.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "Data/PCGPointArrayData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Query/PCGDynMeshProjectPoints.h"
#include "Generators/GridBoxMeshGenerator.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataDomain.h"
#include "OrientedBoxTypes.h"
#include "PCGContext.h"
#include "Selections/GeometrySelection.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshProjectPointsTests
{
	/** A slab 200 x 200 across and 20 thick, centred on the origin: its top is at Z = 10, its bottom at Z = -10. */
	UPCGDynamicMeshData* Slab()
	{
		UE::Geometry::FGridBoxMeshGenerator Generator;
		Generator.Box = UE::Geometry::FOrientedBox3d(FVector3d::Zero(), FVector3d(100, 100, 10));
		Generator.EdgeVertices = UE::Geometry::FIndex3i(3, 3, 2);
		UPCGDynamicMeshData* MeshData = NewObject<UPCGDynamicMeshData>();
		MeshData->Initialize(UE::Geometry::FDynamicMesh3(&Generator.Generate()));
		return MeshData;
	}

	UPCGDynamicMeshSelectionData* TopOf(const UPCGDynamicMeshData* MeshData)
	{
		const UE::Geometry::FDynamicMesh3& Mesh = MeshData->GetDynamicMesh()->GetMeshRef();
		UE::Geometry::FGeometrySelection Selection;
		Selection.InitializeTypes(UE::Geometry::EGeometryElementType::Face, UE::Geometry::EGeometryTopologyType::Triangle);
		for (const int32 TriangleID : Mesh.TriangleIndicesItr())
		{
			if (Mesh.GetTriNormal(TriangleID).Z > 0.9)
			{
				Selection.Selection.Add(UE::Geometry::FGeoSelectionID::MeshTriangle(TriangleID).Encoded());
			}
		}
		UPCGDynamicMeshSelectionData* SelectionData = NewObject<UPCGDynamicMeshSelectionData>();
		SelectionData->Initialize(MeshData, MoveTemp(Selection));
		return SelectionData;
	}

	/** Points at the given places, each carrying Keep = its index, plus a @Data attribute on the set. */
	UPCGPointArrayData* PointsAt(TArrayView<const FVector> Positions, TArrayView<const FRotator> Rotations = {})
	{
		UPCGPointArrayData* Points = NewObject<UPCGPointArrayData>();
		Points->SetNumPoints(Positions.Num());
		Points->AllocateProperties(EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::MetadataEntry);
		UPCGMetadata* Metadata = Points->MutableMetadata();
		FPCGMetadataDomain* Elements = Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements);
		FPCGMetadataAttribute<int32>* Keep = Elements->FindOrCreateAttribute<int32>(TEXT("Keep"), -1, false, true);
		auto Transforms = Points->GetTransformValueRange();
		auto Entries = Points->GetMetadataEntryValueRange();
		for (int32 Index = 0; Index < Positions.Num(); ++Index)
		{
			Transforms[Index] = FTransform(Rotations.IsValidIndex(Index) ? Rotations[Index] : FRotator::ZeroRotator, Positions[Index]);
			Entries[Index] = PCGInvalidEntryKey;
			Elements->InitializeOnSet(Entries[Index]);
			Keep->SetValue(Entries[Index], Index);
		}

		FPCGMetadataDomain* DataDomain = Metadata->GetMetadataDomain(PCGMetadataDomainID::Data);
		DataDomain->AddEntry();
		DataDomain->FindOrCreateAttribute<int32>(TEXT("GroupID"), 0, false, true)->SetValue(PCGFirstEntryKey, 7);
		return Points;
	}

	UPCGDynMeshProjectPointsSettings* LocalSettings()
	{
		UPCGDynMeshProjectPointsSettings* Settings = NewObject<UPCGDynMeshProjectPointsSettings>();
		Settings->bWorldSpace = false;
		return Settings;
	}

	const UPCGBasePointData* Run(UPCGDynMeshProjectPointsSettings* Settings, const UPCGData* Points, const UPCGData* Mesh)
	{
		FPCGDataCollection Input;
		FPCGTaggedData& PointsInput = Input.TaggedData.Emplace_GetRef();
		PointsInput.Data = Points;
		PointsInput.Pin = PCGDynMeshProjectPointsConstants::PointsInputPin;
		FPCGTaggedData& MeshInput = Input.TaggedData.Emplace_GetRef();
		MeshInput.Data = Mesh;
		MeshInput.Pin = PCGDynMeshProjectPointsConstants::MeshInputPin;
		Input.TaggedData.Emplace_GetRef().Data = Settings;

		FPCGElementPtr Element = Settings->GetElement();
		TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&Input, nullptr, nullptr)));
		Context->AsyncState.bIsRunningOnMainThread = true;
		Context->AsyncState.NumAvailableTasks = 1;
		for (int32 Iteration = 0; Iteration < 10; ++Iteration)
		{
			if (Element->Execute(Context.Get()))
			{
				const TArray<FPCGTaggedData> Outputs = Context->OutputData.GetInputsByPin(PCGDynMeshProjectPointsConstants::PointsOutputPin);
				return Outputs.Num() == 1 ? Cast<UPCGBasePointData>(Outputs[0].Data) : nullptr;
			}
		}
		return nullptr;
	}

	FVector PositionOf(const UPCGBasePointData* Points, int32 Index)
	{
		return Points->GetConstTransformValueRange()[Index].GetLocation();
	}

	template <typename T>
	T ValueOf(const UPCGBasePointData* Points, FName Name, int32 Index, T Fallback)
	{
		const FPCGMetadataAttribute<T>* Attribute = Points->ConstMetadata()->GetConstTypedAttribute<T>(Name);
		return Attribute ? Attribute->GetValueFromItemKey(Points->GetConstMetadataEntryValueRange()[Index]) : Fallback;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshProjectPointsDropTest,
	"PCGUtils.DynMesh.ProjectPoints.PointsDropOntoTheSurfaceAndKeepWhatTheyHad", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshProjectPointsDropTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshProjectPointsTests;

	const UPCGDynamicMeshData* Mesh = Slab();
	const FVector Positions[] = {FVector(0, 0, 100), FVector(50, 20, 300), FVector(500, 0, 100)};
	const UPCGPointArrayData* Points = PointsAt(Positions);

	UPCGDynMeshProjectPointsSettings* Settings = LocalSettings();
	Settings->bOutputSuccess = Settings->bOutputDistance = Settings->bOutputHitNormal = Settings->bOutputTriangleId = true;
	Settings->bOutputBarycentric = Settings->bOutputMeshIndex = Settings->bOutputBackFace = true;
	const UPCGBasePointData* Out = Run(Settings, Points, Mesh);
	UTEST_NOT_NULL(TEXT("One output"), Out);
	UTEST_EQUAL(TEXT("Misses are kept by default"), Out->GetNumPoints(), 3);

	TestTrue(TEXT("The first point lands on the top"), PositionOf(Out, 0).Equals(FVector(0, 0, 10), 1.e-3));
	TestTrue(TEXT("The second point lands on the top"), PositionOf(Out, 1).Equals(FVector(50, 20, 10), 1.e-3));
	TestTrue(TEXT("The point past the edge stays where it was"), PositionOf(Out, 2).Equals(Positions[2], 1.e-3));

	TestTrue(TEXT("Hit flags the first"), ValueOf<bool>(Out, TEXT("ProjectionHit"), 0, false));
	TestFalse(TEXT("Hit is false for the miss"), ValueOf<bool>(Out, TEXT("ProjectionHit"), 2, true));
	TestEqual(TEXT("The first travelled 90"), ValueOf<double>(Out, TEXT("ProjectionDistance"), 0, -1.0), 90.0, 1.e-3);
	TestEqual(TEXT("The second travelled 290"), ValueOf<double>(Out, TEXT("ProjectionDistance"), 1, -1.0), 290.0, 1.e-3);
	TestTrue(TEXT("The normal points up"), ValueOf<FVector>(Out, TEXT("HitNormal"), 0, FVector::ZeroVector).Equals(FVector::UpVector, 1.e-3));
	TestFalse(TEXT("The top is hit from the front"), ValueOf<bool>(Out, TEXT("HitBackFace"), 0, true));
	TestEqual(TEXT("Mesh index names the only mesh"), ValueOf<int32>(Out, TEXT("MeshIndex"), 0, -2), 0);
	TestEqual(TEXT("Mesh index is -1 for the miss"), ValueOf<int32>(Out, TEXT("MeshIndex"), 2, -2), -1);

	const UE::Geometry::FDynamicMesh3& Source = Mesh->GetDynamicMesh()->GetMeshRef();
	const int32 TriangleID = ValueOf<int32>(Out, TEXT("TriangleId"), 0, INDEX_NONE);
	UTEST_TRUE(TEXT("The triangle ID is a triangle of the mesh"), Source.IsTriangle(TriangleID));
	TestTrue(TEXT("And it faces up"), Source.GetTriNormal(TriangleID).Z > 0.9);
	const FVector Bary = ValueOf<FVector>(Out, TEXT("Barycentric"), 0, FVector::ZeroVector);
	TestEqual(TEXT("Barycentrics sum to 1"), Bary.X + Bary.Y + Bary.Z, 1.0, 1.e-4);
	TestTrue(TEXT("And rebuild the hit"), FVector(Source.GetTriBaryPoint(TriangleID, Bary.X, Bary.Y, Bary.Z)).Equals(FVector(0, 0, 10), 1.e-3));

	for (int32 Index = 0; Index < 3; ++Index)
	{
		TestEqual(TEXT("Every point keeps its own attribute"), ValueOf<int32>(Out, TEXT("Keep"), Index, -1), Index);
	}
	const FPCGMetadataAttribute<int32>* GroupID = Out->ConstMetadata()->GetConstTypedAttribute<int32>(
		FPCGAttributeIdentifier(TEXT("GroupID"), PCGMetadataDomainID::Data));
	UTEST_NOT_NULL(TEXT("The set keeps its @Data"), GroupID);
	TestEqual(TEXT("With its value"), GroupID->GetValueFromItemKey(PCGFirstEntryKey), 7);

	Settings->bPruneMisses = true;
	const UPCGBasePointData* Pruned = Run(Settings, Points, Mesh);
	UTEST_NOT_NULL(TEXT("One pruned output"), Pruned);
	UTEST_EQUAL(TEXT("Prune Misses drops the miss"), Pruned->GetNumPoints(), 2);
	TestEqual(TEXT("The kept points are still themselves"), ValueOf<int32>(Pruned, TEXT("Keep"), 1, -1), 1);

	Settings->bPruneMisses = false;
	Settings->MaxDistance = 100.0;
	const UPCGBasePointData* Limited = Run(Settings, Points, Mesh);
	UTEST_NOT_NULL(TEXT("One limited output"), Limited);
	TestTrue(TEXT("90 is within Max Distance"), ValueOf<bool>(Limited, TEXT("ProjectionHit"), 0, false));
	TestFalse(TEXT("290 is not"), ValueOf<bool>(Limited, TEXT("ProjectionHit"), 1, true));
	TestTrue(TEXT("So that point stays put"), PositionOf(Limited, 1).Equals(Positions[1], 1.e-3));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshProjectPointsDirectionTest,
	"PCGUtils.DynMesh.ProjectPoints.DirectionAxesAndRotation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshProjectPointsDirectionTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshProjectPointsTests;

	const UPCGDynamicMeshData* Mesh = Slab();
	const FVector Above[] = {FVector(0, 0, 100)};
	const UPCGPointArrayData* Points = PointsAt(Above);

	// A 45 degree ray from 90 above the top lands 90 along X.
	UPCGDynMeshProjectPointsSettings* Settings = LocalSettings();
	Settings->Direction = FVector(1, 0, -1);
	const UPCGBasePointData* Slanted = Run(Settings, Points, Mesh);
	UTEST_NOT_NULL(TEXT("Slanted output"), Slanted);
	TestTrue(TEXT("A slanted ray lands where it meets the top"), PositionOf(Slanted, 0).Equals(FVector(90, 0, 10), 1.e-3));

	Settings->PositionAxes = static_cast<uint8>(EPCGUtilsDynMeshProjectAxes::Z);
	const UPCGBasePointData* ZOnly = Run(Settings, Points, Mesh);
	UTEST_NOT_NULL(TEXT("Z-only output"), ZOnly);
	TestTrue(TEXT("With only Z, the point keeps its X and Y"), PositionOf(ZOnly, 0).Equals(FVector(0, 0, 10), 1.e-3));

	Settings->PositionAxes = static_cast<uint8>(EPCGUtilsDynMeshProjectAxes::X | EPCGUtilsDynMeshProjectAxes::Y | EPCGUtilsDynMeshProjectAxes::Z);
	Settings->Direction = FVector(0, 0, -1);
	Settings->SurfaceOffset = 5.0;
	const UPCGBasePointData* Offset = Run(Settings, Points, Mesh);
	UTEST_NOT_NULL(TEXT("Offset output"), Offset);
	TestTrue(TEXT("Surface Offset lifts the point along the normal"), PositionOf(Offset, 0).Equals(FVector(0, 0, 15), 1.e-3));
	Settings->SurfaceOffset = 0.0;

	// A point under the slab, projecting down, finds nothing - unless it may also look up.
	const FVector Below[] = {FVector(0, 0, -100)};
	const UPCGPointArrayData* Under = PointsAt(Below);
	Settings->bOutputSuccess = Settings->bOutputDistance = true;
	const UPCGBasePointData* OneWay = Run(Settings, Under, Mesh);
	UTEST_NOT_NULL(TEXT("One-way output"), OneWay);
	TestFalse(TEXT("Nothing is below the point"), ValueOf<bool>(OneWay, TEXT("ProjectionHit"), 0, true));
	Settings->bBidirectional = true;
	const UPCGBasePointData* BothWays = Run(Settings, Under, Mesh);
	UTEST_NOT_NULL(TEXT("Bidirectional output"), BothWays);
	TestTrue(TEXT("Looking back finds the bottom"), PositionOf(BothWays, 0).Equals(FVector(0, 0, -10), 1.e-3));
	TestEqual(TEXT("A backward hit has a negative distance"), ValueOf<double>(BothWays, TEXT("ProjectionDistance"), 0, 0.0), -90.0, 1.e-3);
	Settings->bBidirectional = false;

	// Rotation: the point's up axis turns to the surface normal.
	const FRotator Tilted[] = {FRotator(20, 30, 10)};
	const UPCGPointArrayData* TiltedPoints = PointsAt(Above, Tilted);
	Settings->bProjectRotation = true;
	const UPCGBasePointData* Aligned = Run(Settings, TiltedPoints, Mesh);
	UTEST_NOT_NULL(TEXT("Aligned output"), Aligned);
	const FQuat Rotation = Aligned->GetConstTransformValueRange()[0].GetRotation();
	TestTrue(TEXT("Align Rotation turns Z to the normal"), Rotation.GetAxisZ().Equals(FVector::UpVector, 1.e-3));
	Settings->bProjectRotation = false;

	// Direction from an attribute, in point space: a point pitched over 90 degrees has its own -X pointing down.
	Settings->DirectionSource = EPCGUtilsDynMeshProjectDirectionSource::Attribute;
	Settings->DirectionAttribute.Update(TEXT("$Rotation.Forward"));
	Settings->bInvertDirection = true;
	const FRotator Pitched[] = {FRotator(90, 0, 0)};
	const UPCGBasePointData* FromAttribute = Run(Settings, PointsAt(Above, Pitched), Mesh);
	UTEST_NOT_NULL(TEXT("Attribute output"), FromAttribute);
	TestTrue(TEXT("The inverted forward axis of a point pitched up points down"), PositionOf(FromAttribute, 0).Equals(FVector(0, 0, 10), 1.e-3));

	Settings->DirectionAttribute.Update(TEXT("NoSuchAttribute"));
	AddExpectedError(TEXT("Direction Attribute 'NoSuchAttribute' does not exist"), EAutomationExpectedErrorFlags::Contains, 1);
	TestNull(TEXT("A missing direction attribute produces no output"), Run(Settings, Points, Mesh));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshProjectPointsSelectionTest,
	"PCGUtils.DynMesh.ProjectPoints.ASelectionLimitsTheSurface", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshProjectPointsSelectionTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshProjectPointsTests;

	const UPCGDynamicMeshData* Mesh = Slab();
	const FVector Below[] = {FVector(10, 10, -100)};
	const UPCGPointArrayData* Points = PointsAt(Below);

	UPCGDynMeshProjectPointsSettings* Settings = LocalSettings();
	Settings->Direction = FVector(0, 0, 1);
	Settings->bOutputTriangleId = Settings->bOutputBackFace = true;

	const UPCGBasePointData* Whole = Run(Settings, Points, Mesh);
	UTEST_NOT_NULL(TEXT("Whole-mesh output"), Whole);
	TestTrue(TEXT("Against the whole slab, the ray stops at the bottom"), PositionOf(Whole, 0).Equals(FVector(10, 10, -10), 1.e-3));

	const UPCGBasePointData* TopOnly = Run(Settings, Points, TopOf(Mesh));
	UTEST_NOT_NULL(TEXT("Selection output"), TopOnly);
	TestTrue(TEXT("Against only the top, the ray passes through to it"), PositionOf(TopOnly, 0).Equals(FVector(10, 10, 10), 1.e-3));
	TestTrue(TEXT("Reaching the top from below is a back face"), ValueOf<bool>(TopOnly, TEXT("HitBackFace"), 0, false));
	const UE::Geometry::FDynamicMesh3& Source = Mesh->GetDynamicMesh()->GetMeshRef();
	const int32 TriangleID = ValueOf<int32>(TopOnly, TEXT("TriangleId"), 0, INDEX_NONE);
	UTEST_TRUE(TEXT("The triangle ID is the source mesh's, not the selection's"), Source.IsTriangle(TriangleID));
	TestTrue(TEXT("And it is a top triangle"), Source.GetTriNormal(TriangleID).Z > 0.9);

	Settings->bIgnoreBackFaces = true;
	Settings->bOutputSuccess = true;
	const UPCGBasePointData* FrontOnly = Run(Settings, Points, TopOf(Mesh));
	UTEST_NOT_NULL(TEXT("Front-only output"), FrontOnly);
	TestFalse(TEXT("Ignoring back faces, the top is not hit from below"), ValueOf<bool>(FrontOnly, TEXT("ProjectionHit"), 0, true));
	return true;
}

#endif
