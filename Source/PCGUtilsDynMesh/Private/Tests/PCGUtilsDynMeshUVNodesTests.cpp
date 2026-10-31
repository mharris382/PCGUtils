// Copyright Max Harris
//
// The UV nodes: the Blender-style packer, Pack UV Islands, Project UVs From Bounds, Transform UVs, UV Channels,
// Texel Density and Auto UV, including that a selection leaves other triangles' UVs alone.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Elements/UV/PCGDynMeshAutoUV.h"
#include "Elements/UV/PCGDynMeshPackUVIslands.h"
#include "Elements/UV/PCGDynMeshProjectUVsFromBounds.h"
#include "Elements/UV/PCGDynMeshTexelDensity.h"
#include "Elements/UV/PCGDynMeshTransformUVs.h"
#include "Elements/UV/PCGDynMeshUVChannels.h"
#include "Geometry/PCGUtilsDynMeshUVPack.h"
#include "Geometry/PCGUtilsDynMeshUVRegion.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshUVNodesTests
{
	using namespace UE::Geometry;
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	UPCGDynamicMeshData* MakeBox(int32 Segments = 2, double Size = 100.0, FVector Offset = FVector::ZeroVector)
	{
		UPCGDynamicMeshData* Data = NewObject<UPCGDynamicMeshData>();
		const int32 Steps = Segments + 1;
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Data->GetMutableDynamicMesh(), FGeometryScriptPrimitiveOptions(),
			FTransform(Offset), Size, Size, Size, Steps, Steps, Steps, EGeometryScriptPrimitiveOriginMode::Center);
		return Data;
	}

	/** A flat XY grid, Size cm across, its minimum corner at Origin, with UVs 0 to 1 across it. */
	UPCGDynamicMeshData* MakeGrid(double Size = 100.0, FVector Origin = FVector::ZeroVector, int32 Segments = 4)
	{
		UPCGDynamicMeshData* Data = NewObject<UPCGDynamicMeshData>();
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendRectangleXY(Data->GetMutableDynamicMesh(), FGeometryScriptPrimitiveOptions(),
			FTransform(Origin + FVector(0.5 * Size, 0.5 * Size, 0.0)), Size, Size, Segments + 1, Segments + 1);
		return Data;
	}

	UPCGDynamicMeshSelectionData* SelectTriangles(const UPCGDynamicMeshData* MeshData, TArrayView<const int32> TriangleIDs)
	{
		FGeometrySelection Selection;
		Selection.InitializeTypes(EGeometryElementType::Face, EGeometryTopologyType::Triangle);
		for (const int32 TriangleID : TriangleIDs)
		{
			Selection.Selection.Add(FGeoSelectionID::MeshTriangle(TriangleID).Encoded());
		}
		UPCGDynamicMeshSelectionData* Data = NewObject<UPCGDynamicMeshSelectionData>();
		Data->Initialize(MeshData, MoveTemp(Selection));
		return Data;
	}

	TArray<FPCGTaggedData> Run(UPCGUtilsDynMeshProcessBaseSettings* Settings, const UPCGData* Input,
		TArray<TPair<FName, const UPCGData*>> Extra = {})
	{
		FPCGDataCollection Collection;
		FPCGTaggedData& Main = Collection.TaggedData.Emplace_GetRef();
		Main.Pin = Settings->AllInputPinProperties()[0].Label;
		Main.Data = Input;
		for (const TPair<FName, const UPCGData*>& Pair : Extra)
		{
			FPCGTaggedData& Tagged = Collection.TaggedData.Emplace_GetRef();
			Tagged.Pin = Pair.Key;
			Tagged.Data = Pair.Value;
		}
		Collection.TaggedData.Emplace_GetRef().Data = Settings;
		FPCGElementPtr Element = Settings->GetElement();
		TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&Collection, nullptr, nullptr)));
		Context->AsyncState.bIsRunningOnMainThread = true;
		Context->AsyncState.NumAvailableTasks = 1;
		for (int32 Iteration = 0; Iteration < 10; ++Iteration)
		{
			if (Element->Execute(Context.Get()))
			{
				return Context->OutputData.TaggedData;
			}
		}
		return {};
	}

	const UPCGDynamicMeshData* MeshDataOf(const UPCGData* Data)
	{
		if (const UPCGDynamicMeshSelectionData* Selection = Cast<const UPCGDynamicMeshSelectionData>(Data))
		{
			return Selection->GetSourceMeshData();
		}
		return Cast<const UPCGDynamicMeshData>(Data);
	}

	const FDynamicMesh3& MeshOf(const TArray<FPCGTaggedData>& Outputs)
	{
		return MeshDataOf(Outputs[0].Data)->GetDynamicMesh()->GetMeshRef();
	}

	FVector2f UVAt(const FDynamicMesh3& Mesh, int32 Layer, int32 TriangleID, int32 Corner)
	{
		FVector2f A, B, C;
		Mesh.Attributes()->GetUVLayer(Layer)->GetTriElements(TriangleID, A, B, C);
		return Corner == 0 ? A : Corner == 1 ? B : C;
	}

	/** True when every set UV of the layer lies in [Min, Max]. */
	bool AllUVsWithin(const FDynamicMesh3& Mesh, int32 Layer, float Min = -1e-4f, float Max = 1.0f + 1e-4f)
	{
		const FDynamicMeshUVOverlay* Overlay = Mesh.Attributes()->GetUVLayer(Layer);
		for (const int32 TriangleID : Mesh.TriangleIndicesItr())
		{
			if (!Overlay->IsSetTriangle(TriangleID))
			{
				continue;
			}
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				const FVector2f UV = UVAt(Mesh, Layer, TriangleID, Corner);
				if (UV.X < Min || UV.Y < Min || UV.X > Max || UV.Y > Max)
				{
					return false;
				}
			}
		}
		return true;
	}

	bool AllTrianglesHaveUVs(const FDynamicMesh3& Mesh, int32 Layer)
	{
		const FDynamicMeshUVOverlay* Overlay = Mesh.Attributes()->GetUVLayer(Layer);
		for (const int32 TriangleID : Mesh.TriangleIndicesItr())
		{
			if (!Overlay->IsSetTriangle(TriangleID))
			{
				return false;
			}
		}
		return true;
	}

	/** UV bounds per island of a layer. */
	TArray<FBox2D> IslandBounds(const FDynamicMesh3& Mesh, int32 Layer)
	{
		TArray<int32> All;
		for (const int32 TriangleID : Mesh.TriangleIndicesItr())
		{
			All.Add(TriangleID);
		}
		PCGUtilsDynMeshUV::FRegionUVs Region;
		PCGUtilsDynMeshUV::Gather(Mesh, *Mesh.Attributes()->GetUVLayer(Layer), All, Region);
		TArray<FBox2D> Boxes;
		for (const TArray<int32>& Island : PCGUtilsDynMeshUV::FindIslands(Region))
		{
			FBox2D Box(ForceInit);
			for (const int32 Index : PCGUtilsDynMeshUV::IslandUVIndices(Region, Island))
			{
				Box += FVector2D(Region.UVs[Index]);
			}
			Boxes.Add(Box);
		}
		return Boxes;
	}

	/** The smallest gap between any two boxes along the axis that separates them; negative when two overlap. */
	double SmallestGap(const TArray<FBox2D>& Boxes)
	{
		double Smallest = TNumericLimits<double>::Max();
		for (int32 A = 0; A < Boxes.Num(); ++A)
		{
			for (int32 B = A + 1; B < Boxes.Num(); ++B)
			{
				const double GapX = FMath::Max(Boxes[B].Min.X - Boxes[A].Max.X, Boxes[A].Min.X - Boxes[B].Max.X);
				const double GapY = FMath::Max(Boxes[B].Min.Y - Boxes[A].Max.Y, Boxes[A].Min.Y - Boxes[B].Max.Y);
				Smallest = FMath::Min(Smallest, FMath::Max(GapX, GapY));
			}
		}
		return Smallest;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshUVPackerTest, "PCGUtils.DynMesh.UV.Packer", PCGUtilsDynMeshUVNodesTests::Flags)

bool FPCGUtilsDynMeshUVPackerTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshUVPack;

	// Ten overlapping rectangles of different shapes, all at the origin.
	TArray<FVector2f> UVs;
	TArray<FIsland> Islands;
	for (int32 Index = 0; Index < 10; ++Index)
	{
		const float W = 0.2f + 0.1f * (Index % 4);
		const float H = 0.1f + 0.15f * (Index % 3);
		FIsland& Island = Islands.AddDefaulted_GetRef();
		for (const FVector2f Corner : {FVector2f(0, 0), FVector2f(W, 0), FVector2f(W, H), FVector2f(0, H)})
		{
			Island.UVIndices.Add(UVs.Add(Corner));
		}
	}
	FOptions Options;
	Options.bAverageIslandScale = false;
	Options.MarginMethod = EMargin::Fraction;
	Options.Margin = 0.02;
	const FResult Result = Pack(UVs, Islands, Options);
	TestTrue(TEXT("Packed"), Result.bPacked);

	TArray<FBox2D> Boxes;
	bool bInside = true;
	for (const FIsland& Island : Islands)
	{
		FBox2D Box(ForceInit);
		for (const int32 Index : Island.UVIndices)
		{
			Box += FVector2D(UVs[Index]);
		}
		Boxes.Add(Box);
		bInside &= Box.Min.X >= 0.01 - 1e-5 && Box.Min.Y >= 0.01 - 1e-5 && Box.Max.X <= 0.99 + 1e-5 && Box.Max.Y <= 0.99 + 1e-5;
	}
	TestTrue(TEXT("Every island is at least half a margin inside the unit square"), bInside);
	const double Gap = PCGUtilsDynMeshUVNodesTests::SmallestGap(Boxes);
	TestTrue(*FString::Printf(TEXT("Islands are at least a margin apart (smallest gap %f)"), Gap), Gap >= 0.02 - 1e-5);

	// Shapes are kept: each island's box keeps its aspect (allowing a quarter turn).
	const double Scale = Result.Scale;
	const FVector2D Size0 = Boxes[0].GetSize();
	TestTrue(TEXT("An island keeps its size times the pack scale"),
		(FMath::IsNearlyEqual(Size0.X, 0.2 * Scale, 1e-4) && FMath::IsNearlyEqual(Size0.Y, 0.1 * Scale, 1e-4))
		|| (FMath::IsNearlyEqual(Size0.Y, 0.2 * Scale, 1e-4) && FMath::IsNearlyEqual(Size0.X, 0.1 * Scale, 1e-4)));
	TestTrue(*FString::Printf(TEXT("The packing uses the square reasonably (scale %f)"), Scale), Scale > 1.0);

	// Axis Aligned turns a diagonal strip flat.
	TArray<FVector2f> Strip = {FVector2f(0, 0), FVector2f(1, 1), FVector2f(0.9f, 1.1f), FVector2f(-0.1f, 0.1f)};
	FIsland StripIsland;
	StripIsland.UVIndices = {0, 1, 2, 3};
	FOptions AlignOptions;
	AlignOptions.bAverageIslandScale = false;
	AlignOptions.Rotation = ERotation::AxisAligned;
	AlignOptions.Margin = 0.0;
	const TArray<FIsland> StripIslands = {StripIsland};
	Pack(Strip, StripIslands, AlignOptions);
	FBox2D StripBox(ForceInit);
	for (const FVector2f& UV : Strip)
	{
		StripBox += FVector2D(UV);
	}
	TestTrue(TEXT("Axis Aligned lays a diagonal strip along an axis"), StripBox.GetSize().GetMin() < 0.2 * StripBox.GetSize().GetMax());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshPackUVIslandsTest, "PCGUtils.DynMesh.UV.PackIslands", PCGUtilsDynMeshUVNodesTests::Flags)

bool FPCGUtilsDynMeshPackUVIslandsTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshUVNodesTests;
	// Geometry Script's box gives each face its own island, all overlapping 0 to 1.
	UPCGDynamicMeshData* Box = MakeBox();
	TestTrue(TEXT("The test box starts with overlapping islands"), SmallestGap(IslandBounds(Box->GetDynamicMesh()->GetMeshRef(), 0)) < 0.0);

	UPCGDynMeshPackUVIslandsSettings* Settings = NewObject<UPCGDynMeshPackUVIslandsSettings>();
	Settings->MarginMethod = EPCGUtilsDynMeshUVPackMargin::Fraction;
	Settings->Margin = 0.01;
	const TArray<FPCGTaggedData> Outputs = Run(Settings, Box);
	UTEST_EQUAL(TEXT("One output"), Outputs.Num(), 1);
	const FDynamicMesh3& Mesh = MeshOf(Outputs);
	const TArray<FBox2D> Islands = IslandBounds(Mesh, 0);
	TestEqual(TEXT("Six islands"), Islands.Num(), 6);
	TestTrue(TEXT("Packed islands do not overlap"), SmallestGap(Islands) >= 0.01 - 1e-4);
	TestTrue(TEXT("Packed UVs are in the unit square"), AllUVsWithin(Mesh, 0));

	// A selection packs only its island; the rest keep their UVs.
	const FDynamicMesh3& Source = Box->GetDynamicMesh()->GetMeshRef();
	const int32 Kept = Source.MaxTriangleID() - 1;
	const FVector2f Before = UVAt(Source, 0, Kept, 0);
	const TArray<int32> First = {0, 1};
	const TArray<FPCGTaggedData> Partial = Run(Settings, SelectTriangles(Box, First));
	UTEST_EQUAL(TEXT("One partial output"), Partial.Num(), 1);
	TestTrue(TEXT("An unselected triangle keeps its UVs"), UVAt(MeshOf(Partial), 0, Kept, 0).Equals(Before, 1e-6f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshProjectUVsFromBoundsTest, "PCGUtils.DynMesh.UV.ProjectFromBounds", PCGUtilsDynMeshUVNodesTests::Flags)

bool FPCGUtilsDynMeshProjectUVsFromBoundsTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshUVNodesTests;
	UPCGDynMeshProjectUVsFromBoundsSettings* Settings = NewObject<UPCGDynMeshProjectUVsFromBoundsSettings>();
	Settings->BoundsSource = EPCGUtilsDynMeshProjectionBounds::Custom;
	Settings->CustomBounds = FBox(FVector(0, 0, -10), FVector(400, 200, 10));
	Settings->Space = EPCGUtilsDynMeshProjectionSpace::Mesh;

	// A 100 cm grid at (100, 50): its corners map to known places in the 400 x 200 box.
	const TArray<FPCGTaggedData> Outputs = Run(Settings, MakeGrid(100.0, FVector(100, 50, 0)));
	UTEST_EQUAL(TEXT("One output"), Outputs.Num(), 1);
	const FDynamicMesh3& Mesh = MeshOf(Outputs);
	bool bExact = true;
	for (const int32 TriangleID : Mesh.TriangleIndicesItr())
	{
		const FIndex3i Triangle = Mesh.GetTriangle(TriangleID);
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const FVector3d P = Mesh.GetVertex(Triangle[Corner]);
			const FVector2f Expected(static_cast<float>(P.X / 400.0), static_cast<float>(P.Y / 200.0));
			bExact &= UVAt(Mesh, 0, TriangleID, Corner).Equals(Expected, 1e-5f);
		}
	}
	TestTrue(TEXT("Planar Z maps the box to 0..1: UV = (P - Min) / Size"), bExact);

	const UPCGMetadata* Metadata = Outputs[0].Data->ConstMetadata();
	const FPCGMetadataAttribute<FVector>* MinAttribute = Metadata->GetConstTypedAttribute<FVector>(FPCGAttributeIdentifier(TEXT("UVProjectionMin"), PCGMetadataDomainID::Data));
	const FPCGMetadataAttribute<FVector>* MaxAttribute = Metadata->GetConstTypedAttribute<FVector>(FPCGAttributeIdentifier(TEXT("UVProjectionMax"), PCGMetadataDomainID::Data));
	TestTrue(TEXT("The box is written to @Data"), MinAttribute && MaxAttribute
		&& MinAttribute->GetValueFromItemKey(PCGFirstEntryKey).Equals(FVector(0, 0, -10))
		&& MaxAttribute->GetValueFromItemKey(PCGFirstEntryKey).Equals(FVector(400, 200, 10)));

	// Square grows the short side about its centre; Padding grows every side.
	const FBox Squared = FPCGUtilsDynMeshProjectUVsFromBoundsOperation::FinalBox(FBox(FVector(0, 0, 0), FVector(400, 200, 10)), 10.0, true,
		EPCGUtilsDynMeshBoundsProjection::Planar, EPCGUtilsDynMeshProjectionAxis::Z);
	TestTrue(TEXT("Padding then Square give a 420 cm square centred on the box"),
		Squared.Min.Equals(FVector(-10, -110, -10)) && Squared.Max.Equals(FVector(410, 310, 20)));

	// Each Input fits the grid itself; Flip V reverses V.
	Settings->BoundsSource = EPCGUtilsDynMeshProjectionBounds::EachInput;
	Settings->bFlipV = true;
	const FDynamicMesh3& Fitted = MeshOf(Run(Settings, MakeGrid(100.0, FVector(100, 50, 0))));
	TestTrue(TEXT("Each Input fits the box to the mesh"), AllUVsWithin(Fitted, 0));
	const int32 T0 = *Fitted.TriangleIndicesItr().begin();
	const FVector3d P0 = Fitted.GetVertex(Fitted.GetTriangle(T0).A);
	TestTrue(TEXT("Flip V maps the box's minimum Y to V 1"),
		FMath::IsNearlyEqual(UVAt(Fitted, 0, T0, 0).Y, static_cast<float>(1.0 - (P0.Y - 50.0) / 100.0), 1e-5f));

	// Box projection: a cube's top face maps along Z, its side faces along X or Y.
	Settings->Projection = EPCGUtilsDynMeshBoundsProjection::Box;
	Settings->bFlipV = false;
	const FDynamicMesh3& Cube = MeshOf(Run(Settings, MakeBox(1)));
	TestTrue(TEXT("Box projection keeps every UV in 0..1"), AllUVsWithin(Cube, 0));
	TestTrue(TEXT("Box projection sets every triangle"), AllTrianglesHaveUVs(Cube, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshTransformUVsTest, "PCGUtils.DynMesh.UV.Transform", PCGUtilsDynMeshUVNodesTests::Flags)

bool FPCGUtilsDynMeshTransformUVsTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshUVNodesTests;
	UPCGDynamicMeshData* Grid = MakeGrid();
	const FDynamicMesh3& Source = Grid->GetDynamicMesh()->GetMeshRef();
	const int32 T0 = *Source.TriangleIndicesItr().begin();
	const FVector2f Before = UVAt(Source, 0, T0, 0);

	UPCGDynMeshTransformUVsSettings* Settings = NewObject<UPCGDynMeshTransformUVsSettings>();
	Settings->Scale.Constant = FVector2D(2.0, 3.0);
	Settings->Rotation.Constant = 90.0;
	Settings->Translation.Constant = FVector2D(10.0, 0.0);
	const FDynamicMesh3& Moved = MeshOf(Run(Settings, Grid));
	// About the origin: scale (2x, 3y), quarter turn (x, y) -> (-y, x), then + (10, 0).
	const FVector2f Expected(10.0f - 3.0f * Before.Y, 2.0f * Before.X);
	TestTrue(TEXT("Scale, rotate, translate in that order about the UV origin"), UVAt(Moved, 0, T0, 0).Equals(Expected, 1e-4f));

	// @Data drives the translation.
	Settings->Scale.Constant = FVector2D::UnitVector;
	Settings->Rotation.Constant = 0.0;
	Settings->Translation.Input = EPCGUtilsInputValueType::Attribute;
	UPCGDynamicMeshData* WithData = MakeGrid();
	FPCGMetadataDomain* Domain = WithData->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Data);
	FPCGMetadataAttribute<FVector2D>* TranslationAttribute =
		Domain->FindOrCreateAttribute<FVector2D>(TEXT("UVTranslation"), FVector2D(0.5, 0.25), false, false, true);
	Domain->AddEntry();
	TranslationAttribute->SetValue(PCGFirstEntryKey, FVector2D(0.5, 0.25));
	const FDynamicMesh3& Shifted = MeshOf(Run(Settings, WithData));
	TestTrue(TEXT("Translation read from @Data"), UVAt(Shifted, 0, T0, 0).Equals(Before + FVector2f(0.5f, 0.25f), 1e-5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshUVChannelsTest, "PCGUtils.DynMesh.UV.Channels", PCGUtilsDynMeshUVNodesTests::Flags)

bool FPCGUtilsDynMeshUVChannelsTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshUVNodesTests;
	UPCGDynamicMeshData* Box = MakeBox();
	UPCGDynMeshUVChannelsSettings* Settings = NewObject<UPCGDynMeshUVChannelsSettings>();
	Settings->UVLayer = 1;
	const FDynamicMesh3& Copied = MeshOf(Run(Settings, Box));
	TestTrue(TEXT("Copy creates the target layer"), Copied.Attributes()->NumUVLayers() >= 2);
	bool bSame = true;
	for (const int32 TriangleID : Copied.TriangleIndicesItr())
	{
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			bSame &= UVAt(Copied, 0, TriangleID, Corner).Equals(UVAt(Copied, 1, TriangleID, Corner));
		}
	}
	TestTrue(TEXT("UV1 equals UV0 after Copy"), bSame);

	Settings->Operation = EPCGUtilsDynMeshUVChannelOperation::Clear;
	Settings->UVLayer = 0;
	const TArray<int32> First = {0};
	const FDynamicMesh3& Cleared = MeshOf(Run(Settings, SelectTriangles(Box, First)));
	TestFalse(TEXT("Clear unsets the selected triangle"), Cleared.Attributes()->GetUVLayer(0)->IsSetTriangle(0));
	TestTrue(TEXT("Clear keeps the others"), Cleared.Attributes()->GetUVLayer(0)->IsSetTriangle(1));

	Settings->Operation = EPCGUtilsDynMeshUVChannelOperation::SetCount;
	Settings->LayerCount = 3;
	TestEqual(TEXT("Set Layer Count"), MeshOf(Run(Settings, Box)).Attributes()->NumUVLayers(), 3);

	Settings->Operation = EPCGUtilsDynMeshUVChannelOperation::Copy;
	Settings->SourceLayer = 5;
	Settings->UVLayer = 1;
	AddExpectedError(TEXT("Source Layer is 5, but the mesh has"), EAutomationExpectedErrorFlags::Contains, 1);
	Run(Settings, Box);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshTexelDensityTest, "PCGUtils.DynMesh.UV.TexelDensity", PCGUtilsDynMeshUVNodesTests::Flags)

bool FPCGUtilsDynMeshTexelDensityTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshUVNodesTests;
	// 100 cm with UVs 0..1. 512 px/m on a 1024 px texture is 512 px across 1 m: half the texture.
	UPCGDynMeshTexelDensitySettings* Settings = NewObject<UPCGDynMeshTexelDensitySettings>();
	const FDynamicMesh3& Mesh = MeshOf(Run(Settings, MakeGrid(100.0)));
	const TArray<FBox2D> Islands = IslandBounds(Mesh, 0);
	UTEST_EQUAL(TEXT("One island"), Islands.Num(), 1);
	TestTrue(*FString::Printf(TEXT("The island spans half the texture (%f)"), Islands[0].GetSize().X),
		FMath::IsNearlyEqual(Islands[0].GetSize().X, 0.5, 1e-4) && FMath::IsNearlyEqual(Islands[0].GetSize().Y, 0.5, 1e-4));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshAutoUVTest, "PCGUtils.DynMesh.UV.AutoUV", PCGUtilsDynMeshUVNodesTests::Flags)

bool FPCGUtilsDynMeshAutoUVTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshUVNodesTests;
	UPCGDynMeshAutoUVSettings* Settings = NewObject<UPCGDynMeshAutoUVSettings>();
	Settings->UVLayer = 1;
	for (const EPCGUtilsDynMeshAutoUVMethod Method : {EPCGUtilsDynMeshAutoUVMethod::PatchBuilder, EPCGUtilsDynMeshAutoUVMethod::XAtlas,
		EPCGUtilsDynMeshAutoUVMethod::Conformal, EPCGUtilsDynMeshAutoUVMethod::SpectralConformal, EPCGUtilsDynMeshAutoUVMethod::ExpMap})
	{
		Settings->Method = Method;
		const FString Name = StaticEnum<EPCGUtilsDynMeshAutoUVMethod>()->GetNameStringByValue(static_cast<int64>(Method));
		const TArray<FPCGTaggedData> Outputs = Run(Settings, MakeBox(3));
		if (!TestEqual(*(Name + TEXT(": one output")), Outputs.Num(), 1))
		{
			continue;
		}
		const FDynamicMesh3& Mesh = MeshOf(Outputs);
		TestTrue(*(Name + TEXT(": every triangle has UVs on layer 1")), AllTrianglesHaveUVs(Mesh, 1));
		TestTrue(*(Name + TEXT(": UVs are packed into the unit square")), AllUVsWithin(Mesh, 1, -1e-3f, 1.0f + 1e-3f));
	}

	// A selection unwraps only its triangles; the others keep their UV0.
	Settings->Method = EPCGUtilsDynMeshAutoUVMethod::XAtlas;
	Settings->UVLayer = 0;
	UPCGDynamicMeshData* Box = MakeBox(3);
	const FDynamicMesh3& Source = Box->GetDynamicMesh()->GetMeshRef();
	const int32 Kept = Source.MaxTriangleID() - 1;
	const FVector2f Before = UVAt(Source, 0, Kept, 1);
	TArray<int32> Half;
	for (int32 TriangleID = 0; TriangleID < Source.MaxTriangleID() / 2; ++TriangleID)
	{
		Half.Add(TriangleID);
	}
	const FDynamicMesh3& Partial = MeshOf(Run(Settings, SelectTriangles(Box, Half)));
	TestTrue(TEXT("An unselected triangle keeps its UVs"), UVAt(Partial, 0, Kept, 1).Equals(Before, 1e-6f));
	TestTrue(TEXT("Every triangle still has UVs"), AllTrianglesHaveUVs(Partial, 0));
	return true;
}

#endif
