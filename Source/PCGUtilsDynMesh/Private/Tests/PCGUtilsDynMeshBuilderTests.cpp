// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Creation/CreatePrimitive/PCGCreatePrimitiveSettingsBase.h"
#include "Elements/Creation/PrimitiveBuilder/PCGPrimitiveBuilderFactory.h"
#include "Elements/Creation/PrimitiveBuilder/PCGPrimitiveBuilders.h"
#include "Elements/Deform/PCGTransformDynMesh.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"
#include "Factories/PCGUtilsDynMeshProcessBuilderFactory.h"
#include "PCGContext.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshBuilderTests
{
	/**
	 * These tests exercise the Builder/operation layer directly rather than through a PCG graph execution:
	 * everything below is reachable with a null FPCGContext, which NewObject_AnyThread handles on the game
	 * thread. That is the point of the refactor - the geometry algorithm and the deferred evaluation are
	 * separable from PCG element execution.
	 */
	UPCGPrimitiveBuilderFactoryData* MakeBoxBuilder(
		FVector Dimensions,
		EGeometryScriptPrimitiveOriginMode Origin = EGeometryScriptPrimitiveOriginMode::Center)
	{
		UPCGCreatePrimitiveBoxSettings* Box = NewObject<UPCGCreatePrimitiveBoxSettings>();
		Box->DimensionX = Dimensions.X;
		Box->DimensionY = Dimensions.Y;
		Box->DimensionZ = Dimensions.Z;
		Box->Origin = Origin;

		UPCGPrimitiveBuilderFactoryData* Builder = NewObject<UPCGPrimitiveBuilderFactoryData>();
		Builder->Primitive = Box;
		// Fitting defaults to Uniform/Min; pin it off so the leaf keeps its native size and the seed transform
		// is applied as a plain rigid placement. Tests that want fitting opt in explicitly.
		Builder->Fitting.ScaleToFit.ScaleToFitMode = EPCGUtilsFitMode::None;
		return Builder;
	}

	/** Test-only process operation that replaces the active selection with every triangle of the mesh. */
	class FSelectAllTrianglesOperation final : public FPCGUtilsDynMeshProcessOperation
	{
	public:
		virtual bool Execute(
			const FPCGUtilsDynMeshProcessInvocation& Invocation,
			FPCGUtilsDynMeshProcessOutcome& OutOutcome) const override
		{
			using namespace UE::Geometry;
			const UDynamicMesh* MeshObject = Invocation.MeshData ? Invocation.MeshData->GetDynamicMesh() : nullptr;
			const FDynamicMesh3* Mesh = MeshObject ? MeshObject->GetMeshPtr() : nullptr;
			if (!Mesh)
			{
				return false;
			}

			FGeometrySelection Selection;
			Selection.InitializeTypes(EGeometryElementType::Face, EGeometryTopologyType::Triangle);
			for (const int32 TriangleID : Mesh->TriangleIndicesItr())
			{
				Selection.Selection.Add(FGeoSelectionID::MeshTriangle(TriangleID).Encoded());
			}

			UPCGDynamicMeshSelectionData* SelectionData =
				FPCGContext::NewObject_AnyThread<UPCGDynamicMeshSelectionData>(Invocation.Context);
			SelectionData->Initialize(Invocation.MeshData, MoveTemp(Selection));

			OutOutcome.SelectionOutcome = EPCGUtilsDynMeshProcessSelectionOutcome::Replace;
			OutOutcome.NewSelectionData = SelectionData;
			return true;
		}
	};

	UPCGUtilsDynMeshProcessBuilderFactoryData* MakeDecorator(
		const UPCGUtilsDynMeshBuilderFactoryData* Child,
		TSharedPtr<const FPCGUtilsDynMeshProcessOperation> Operation,
		const TCHAR* Label)
	{
		UPCGUtilsDynMeshProcessBuilderFactoryData* Decorator =
			NewObject<UPCGUtilsDynMeshProcessBuilderFactoryData>();
		Decorator->ChildBuilder = Child;
		Decorator->Operation = MoveTemp(Operation);
		Decorator->ProcessLabel = Label;
		return Decorator;
	}

	TSharedPtr<const FPCGUtilsDynMeshProcessOperation> MakeTransformOperation(
		const FTransform& Transform,
		EPCGUtilsDynMeshTransformSpace Space = EPCGUtilsDynMeshTransformSpace::ActorLocal)
	{
		TSharedPtr<FPCGUtilsDynMeshTransformOperation> Operation = MakeShared<FPCGUtilsDynMeshTransformOperation>();
		Operation->Transform = Transform;
		Operation->Space = Space;
		return Operation;
	}

	UPCGUtilsDynMeshProcessBuilderFactoryData* MakeSpacedTransformDecorator(
		const UPCGUtilsDynMeshBuilderFactoryData* Child, const FTransform& Transform,
		EPCGUtilsDynMeshTransformSpace Space)
	{
		return MakeDecorator(Child, MakeTransformOperation(Transform, Space), TEXT("TransformDynMesh"));
	}

	UPCGUtilsDynMeshProcessBuilderFactoryData* MakeTransformDecorator(
		const UPCGUtilsDynMeshBuilderFactoryData* Child, const FTransform& Transform)
	{
		return MakeDecorator(Child, MakeTransformOperation(Transform), TEXT("TransformDynMesh"));
	}

	FPCGUtilsDynMeshBuildContext MakeSeed(const FTransform& SeedTransform, const FBox& SeedLocalBounds)
	{
		FPCGUtilsDynMeshBuildContext BuildContext;
		BuildContext.Context = nullptr;
		BuildContext.SeedTransform = SeedTransform;
		BuildContext.SeedLocalBounds = SeedLocalBounds;
		return BuildContext;
	}

	bool BuildOnce(
		const UPCGUtilsDynMeshBuilderFactoryData* Builder,
		const FPCGUtilsDynMeshBuildContext& BuildContext,
		FPCGUtilsDynMeshBuildResult& OutResult)
	{
		const TSharedPtr<FPCGUtilsDynMeshBuilderOperation> Operation = Builder->CreateOperation(nullptr);
		return Operation.IsValid() && Operation->Build(BuildContext, OutResult);
	}

	FBox GetResultBounds(const FPCGUtilsDynMeshBuildResult& Result)
	{
		const UDynamicMesh* MeshObject = Result.MeshData ? Result.MeshData->GetDynamicMesh() : nullptr;
		const UE::Geometry::FDynamicMesh3* Mesh = MeshObject ? MeshObject->GetMeshPtr() : nullptr;
		if (!Mesh || Mesh->TriangleCount() == 0)
		{
			return FBox(ForceInit);
		}
		const UE::Geometry::FAxisAlignedBox3d Bounds = Mesh->GetBounds();
		return FBox(FVector(Bounds.Min), FVector(Bounds.Max));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshBuilderLeafResultTest,
	"PCGUtils.DynMesh.Builder.LeafResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshBuilderLeafResultTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshBuilderTests;

	UPCGPrimitiveBuilderFactoryData* Builder = MakeBoxBuilder(FVector(100.0, 100.0, 100.0));

	FPCGUtilsDynMeshBuildResult Result;
	TestTrue(TEXT("Leaf Builder evaluates"),
		BuildOnce(Builder, MakeSeed(FTransform::Identity, FBox(FVector(-50.0), FVector(50.0))), Result));
	TestTrue(TEXT("Leaf Builder returns a valid PCG DynMesh result"), Result.IsValid());
	TestNotNull(TEXT("Leaf Builder result carries UPCGDynamicMeshData"), Result.MeshData);
	TestFalse(TEXT("A no-selection Builder result reports no active selection"), Result.HasSelection());
	TestNull(TEXT("A no-selection Builder result exposes the bare mesh as its process input"),
		Cast<const UPCGDynamicMeshSelectionData>(Result.GetProcessInputData()));

	const FBox Bounds = GetResultBounds(Result);
	TestTrue(TEXT("Leaf Builder produced geometry"), Bounds.IsValid != 0);
	TestTrue(TEXT("Leaf box matches its configured native size"),
		Bounds.GetSize().Equals(FVector(100.0, 100.0, 100.0), 0.01));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshDeferredTransformMatchesImmediateTest,
	"PCGUtils.DynMesh.Builder.DeferredTransformMatchesImmediate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshDeferredTransformMatchesImmediateTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshBuilderTests;

	const FTransform ProcessTransform(
		FRotator(0.0, 30.0, 0.0), FVector(25.0, -10.0, 7.5), FVector(2.0, 1.0, 0.5));
	const FTransform SeedTransform(FRotator::ZeroRotator, FVector(1000.0, 0.0, 0.0), FVector::OneVector);
	const FBox SeedBounds(FVector(-50.0), FVector(50.0));

	// Immediate: realize the primitive first, then run the very same operation against it.
	UPCGPrimitiveBuilderFactoryData* ImmediateLeaf = MakeBoxBuilder(FVector(100.0, 100.0, 100.0));
	FPCGUtilsDynMeshBuildResult ImmediateResult;
	TestTrue(TEXT("Immediate leaf evaluates"),
		BuildOnce(ImmediateLeaf, MakeSeed(SeedTransform, SeedBounds), ImmediateResult));

	FPCGUtilsDynMeshTransformOperation ImmediateOperation;
	ImmediateOperation.Transform = ProcessTransform;
	FPCGUtilsDynMeshProcessInvocation Invocation;
	Invocation.Context = nullptr;
	Invocation.MeshData = ImmediateResult.MeshData;
	FPCGUtilsDynMeshProcessOutcome ImmediateOutcome;
	TestTrue(TEXT("Immediate Transform runs"), ImmediateOperation.Execute(Invocation, ImmediateOutcome));

	// Deferred: wrap the same leaf configuration in the process Builder decorator and realize afterwards.
	UPCGPrimitiveBuilderFactoryData* DeferredLeaf = MakeBoxBuilder(FVector(100.0, 100.0, 100.0));
	UPCGUtilsDynMeshProcessBuilderFactoryData* Decorator = MakeTransformDecorator(DeferredLeaf, ProcessTransform);
	FPCGUtilsDynMeshBuildResult DeferredResult;
	TestTrue(TEXT("Deferred Transform Builder evaluates"),
		BuildOnce(Decorator, MakeSeed(SeedTransform, SeedBounds), DeferredResult));

	const FBox ImmediateBounds = GetResultBounds(ImmediateResult);
	const FBox DeferredBounds = GetResultBounds(DeferredResult);
	TestTrue(TEXT("Immediate result has geometry"), ImmediateBounds.IsValid != 0);
	TestTrue(TEXT("Deferred result has geometry"), DeferredBounds.IsValid != 0);
	TestTrue(TEXT("Deferred Transform matches immediate Transform (min)"),
		DeferredBounds.Min.Equals(ImmediateBounds.Min, 0.01));
	TestTrue(TEXT("Deferred Transform matches immediate Transform (max)"),
		DeferredBounds.Max.Equals(ImmediateBounds.Max, 0.01));

	TestEqual(TEXT("Topology-preserving Transform reports Preserve"),
		static_cast<int32>(ImmediateOutcome.SelectionOutcome),
		static_cast<int32>(EPCGUtilsDynMeshProcessSelectionOutcome::Preserve));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshDeferredPerSeedTest,
	"PCGUtils.DynMesh.Builder.DeferredEvaluatesPerSeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshDeferredPerSeedTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshBuilderTests;

	// Scale-to-fit makes the leaf's output depend on the seed's own bounds, so two differently-sized seeds must
	// produce differently-sized results from one shared operation.
	UPCGPrimitiveBuilderFactoryData* Leaf = MakeBoxBuilder(FVector(100.0, 100.0, 100.0));
	Leaf->Fitting.ScaleToFit.ScaleToFitMode = EPCGUtilsFitMode::Uniform;
	Leaf->Fitting.ScaleToFit.ScaleToFit = EPCGUtilsScaleToFit::Fill;

	UPCGUtilsDynMeshProcessBuilderFactoryData* Decorator =
		MakeTransformDecorator(Leaf, FTransform(FVector(10.0, 0.0, 0.0)));

	const TSharedPtr<FPCGUtilsDynMeshBuilderOperation> Operation = Decorator->CreateOperation(nullptr);
	TestTrue(TEXT("Decorator operation created"), Operation.IsValid());
	if (!Operation.IsValid())
	{
		return false;
	}

	FPCGUtilsDynMeshBuildResult SmallResult;
	TestTrue(TEXT("Small seed evaluates"), Operation->Build(
		MakeSeed(FTransform::Identity, FBox(FVector(-50.0), FVector(50.0))), SmallResult));

	FPCGUtilsDynMeshBuildResult LargeResult;
	TestTrue(TEXT("Large seed evaluates"), Operation->Build(
		MakeSeed(FTransform::Identity, FBox(FVector(-200.0), FVector(200.0))), LargeResult));

	const FVector SmallSize = GetResultBounds(SmallResult).GetSize();
	const FVector LargeSize = GetResultBounds(LargeResult).GetSize();
	TestTrue(TEXT("Small seed fitted to its own bounds"), SmallSize.Equals(FVector(100.0), 0.01));
	TestTrue(TEXT("Large seed fitted to its own bounds"), LargeSize.Equals(FVector(400.0), 0.01));
	TestTrue(TEXT("Each seed is evaluated independently, not from one globally realized mesh"),
		SmallResult.MeshData != LargeResult.MeshData);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshDeferredSelectionPreservedTest,
	"PCGUtils.DynMesh.Builder.SelectionSurvivesTransform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshDeferredSelectionPreservedTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshBuilderTests;

	// Box Builder -> (process that establishes a selection) -> Transform.
	UPCGUtilsDynMeshProcessBuilderFactoryData* Selecting = MakeDecorator(
		MakeBoxBuilder(FVector(100.0, 100.0, 100.0)),
		MakeShared<FSelectAllTrianglesOperation>(),
		TEXT("TestSelectAll"));

	UPCGUtilsDynMeshProcessBuilderFactoryData* Decorator =
		MakeTransformDecorator(Selecting, FTransform(FVector(0.0, 0.0, 250.0)));

	FPCGUtilsDynMeshBuildResult Result;
	TestTrue(TEXT("Selection-carrying Builder chain evaluates"),
		BuildOnce(Decorator, MakeSeed(FTransform::Identity, FBox(FVector(-50.0), FVector(50.0))), Result));
	TestTrue(TEXT("Result is valid"), Result.IsValid());

	// Transform is topology preserving, so the active selection must survive it rather than being dropped
	// because vertex positions moved.
	TestTrue(TEXT("Active selection survives a topology-preserving Transform"), Result.HasSelection());
	if (Result.HasSelection())
	{
		TestTrue(TEXT("Surviving selection references the result's own mesh data"),
			Result.SelectionData->GetSourceMeshData() == Result.MeshData);
		TestTrue(TEXT("Surviving selection is non-empty"),
			Result.SelectionData->GetSelection().Selection.Num() > 0);
		TestNotNull(TEXT("A selection-bearing result exposes the selection as its process input"),
			Cast<const UPCGDynamicMeshSelectionData>(Result.GetProcessInputData()));
	}

	const FBox Bounds = GetResultBounds(Result);
	TestTrue(TEXT("Deferred Transform moved the geometry"),
		FMath::IsNearlyEqual(Bounds.GetCenter().Z, 250.0, 0.01));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshBuilderReuseTest,
	"PCGUtils.DynMesh.Builder.ExpressionReuseIsDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshBuilderReuseTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshBuilderTests;

	// One Builder expression feeding two independent materializers must not have its factory state mutated
	// by either of them.
	UPCGUtilsDynMeshProcessBuilderFactoryData* Shared = MakeTransformDecorator(
		MakeBoxBuilder(FVector(100.0, 100.0, 100.0)), FTransform(FVector(5.0, 0.0, 0.0)));

	const FPCGUtilsDynMeshBuildContext Seed = MakeSeed(FTransform::Identity, FBox(FVector(-50.0), FVector(50.0)));

	FPCGUtilsDynMeshBuildResult FirstResult;
	FPCGUtilsDynMeshBuildResult SecondResult;
	TestTrue(TEXT("First consumer evaluates"), BuildOnce(Shared, Seed, FirstResult));
	TestTrue(TEXT("Second consumer evaluates"), BuildOnce(Shared, Seed, SecondResult));

	const FBox FirstBounds = GetResultBounds(FirstResult);
	const FBox SecondBounds = GetResultBounds(SecondResult);
	TestTrue(TEXT("Both consumers see identical geometry (min)"), FirstBounds.Min.Equals(SecondBounds.Min, 0.001));
	TestTrue(TEXT("Both consumers see identical geometry (max)"), FirstBounds.Max.Equals(SecondBounds.Max, 0.001));
	TestTrue(TEXT("Each consumer owns its own private result mesh"),
		FirstResult.MeshData != SecondResult.MeshData);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshTypedBuilderNodeTest,
	"PCGUtils.DynMesh.Builder.TypedBuilderNodeDrivesGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshTypedBuilderNodeTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshBuilderTests;

	// The point of one node per primitive type: these are ordinary reflected properties on the Settings
	// object, so PCG can build override pins for them. Setting them here stands in for a graph override.
	UPCGBoxBuilderSettings* Node = NewObject<UPCGBoxBuilderSettings>();
	Node->DimensionX = 200.0f;
	Node->DimensionY = 50.0f;
	Node->DimensionZ = 25.0f;
	Node->Origin = EGeometryScriptPrimitiveOriginMode::Center;
	Node->Fitting.ScaleToFit.ScaleToFitMode = EPCGUtilsFitMode::None;

	const UPCGUtilsDynMeshBuilderFactoryData* Builder =
		Cast<UPCGUtilsDynMeshBuilderFactoryData>(Node->CreateFactory(nullptr));
	TestNotNull(TEXT("Box Builder node produces a Builder factory"), Builder);
	if (!Builder)
	{
		return false;
	}

	FPCGUtilsDynMeshBuildResult Result;
	TestTrue(TEXT("Box Builder node evaluates"),
		BuildOnce(Builder, MakeSeed(FTransform::Identity, FBox(FVector(-50.0), FVector(50.0))), Result));
	TestTrue(TEXT("Box Builder node produced geometry"), Result.IsValid());
	TestTrue(TEXT("The node's own overridable dimensions reached the geometry"),
		GetResultBounds(Result).GetSize().Equals(FVector(200.0, 50.0, 25.0), 0.01));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshAsymmetricPaddingTest,
	"PCGUtils.DynMesh.Builder.PaddingMinAndMaxAreIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshAsymmetricPaddingTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshBuilderTests;

	// Fill on every axis makes the primitive exactly span the padded seed bounds, so the resulting box *is*
	// the padded bounds and padding is directly observable in the result.
	auto MakePaddedBuilder = [](const FVector& PaddingMin, const FVector& PaddingMax)
	{
		UPCGPrimitiveBuilderFactoryData* Builder = MakeBoxBuilder(FVector(100.0, 100.0, 100.0));
		Builder->Fitting.ScaleToFit.ScaleToFitMode = EPCGUtilsFitMode::Individual;
		Builder->Fitting.ScaleToFit.ScaleToFitX = EPCGUtilsScaleToFit::Fill;
		Builder->Fitting.ScaleToFit.ScaleToFitY = EPCGUtilsScaleToFit::Fill;
		Builder->Fitting.ScaleToFit.ScaleToFitZ = EPCGUtilsScaleToFit::Fill;
		Builder->Fitting.PaddingMin = PaddingMin;
		Builder->Fitting.PaddingMax = PaddingMax;
		return Builder;
	};

	const FPCGUtilsDynMeshBuildContext Seed = MakeSeed(FTransform::Identity, FBox(FVector(-50.0), FVector(50.0)));

	// Inset only the bottom of Z: the result must sit at -30..50, not a symmetric -30..30.
	FPCGUtilsDynMeshBuildResult BottomOnly;
	TestTrue(TEXT("Bottom-padded Builder evaluates"),
		BuildOnce(MakePaddedBuilder(FVector(0.0, 0.0, 20.0), FVector::ZeroVector), Seed, BottomOnly));
	const FBox BottomBounds = GetResultBounds(BottomOnly);
	TestTrue(TEXT("PaddingMin insets only the min side of Z"),
		FMath::IsNearlyEqual(BottomBounds.Min.Z, -30.0, 0.01));
	TestTrue(TEXT("PaddingMin leaves the max side of Z alone"),
		FMath::IsNearlyEqual(BottomBounds.Max.Z, 50.0, 0.01));

	// Inset only the top of Z: the mirror image.
	FPCGUtilsDynMeshBuildResult TopOnly;
	TestTrue(TEXT("Top-padded Builder evaluates"),
		BuildOnce(MakePaddedBuilder(FVector::ZeroVector, FVector(0.0, 0.0, 20.0)), Seed, TopOnly));
	const FBox TopBounds = GetResultBounds(TopOnly);
	TestTrue(TEXT("PaddingMax insets only the max side of Z"),
		FMath::IsNearlyEqual(TopBounds.Max.Z, 30.0, 0.01));
	TestTrue(TEXT("PaddingMax leaves the min side of Z alone"),
		FMath::IsNearlyEqual(TopBounds.Min.Z, -50.0, 0.01));

	// Both sides at once still behaves like the old symmetric inset.
	FPCGUtilsDynMeshBuildResult BothSides;
	TestTrue(TEXT("Symmetrically padded Builder evaluates"),
		BuildOnce(MakePaddedBuilder(FVector(0.0, 0.0, 20.0), FVector(0.0, 0.0, 20.0)), Seed, BothSides));
	const FBox BothBounds = GetResultBounds(BothSides);
	TestTrue(TEXT("Equal min/max padding insets both sides"),
		BothBounds.Min.Equals(FVector(-50.0, -50.0, -30.0), 0.01) &&
		BothBounds.Max.Equals(FVector(50.0, 50.0, 30.0), 0.01));

	// The X/Y axes were never padded and must be untouched throughout.
	TestTrue(TEXT("Unpadded axes keep the full seed bounds"),
		FMath::IsNearlyEqual(BottomBounds.GetSize().X, 100.0, 0.01) &&
		FMath::IsNearlyEqual(BottomBounds.GetSize().Y, 100.0, 0.01));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshBuilderLocalTransformTest,
	"PCGUtils.DynMesh.Builder.BuilderLocalTransformActsAboutPlacementPivot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshBuilderLocalTransformTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshBuilderTests;

	// A seed well away from the actor origin, so "scales in place" and "scales away from the origin" are
	// visibly different outcomes.
	const FTransform SeedTransform(FRotator::ZeroRotator, FVector(300.0, 0.0, 0.0), FVector::OneVector);
	const FBox SeedBounds(FVector(-50.0), FVector(50.0));
	const FPCGUtilsDynMeshBuildContext Seed = MakeSeed(SeedTransform, SeedBounds);
	const FTransform ScaleXY(FQuat::Identity, FVector::ZeroVector, FVector(2.0, 2.0, 1.0));

	FPCGUtilsDynMeshBuildResult Baseline;
	TestTrue(TEXT("Untransformed Builder evaluates"),
		BuildOnce(MakeBoxBuilder(FVector(100.0)), Seed, Baseline));
	const FBox BaselineBounds = GetResultBounds(Baseline);
	TestTrue(TEXT("Leaf records a Builder frame"), Baseline.bHasBuilderFrame);

	// Builder local: the box gets bigger where it stands.
	FPCGUtilsDynMeshBuildResult BuilderLocal;
	TestTrue(TEXT("Builder-local scaled Builder evaluates"), BuildOnce(
		MakeSpacedTransformDecorator(MakeBoxBuilder(FVector(100.0)), ScaleXY,
			EPCGUtilsDynMeshTransformSpace::BuilderLocal), Seed, BuilderLocal));
	const FBox BuilderLocalBounds = GetResultBounds(BuilderLocal);
	TestTrue(TEXT("Builder-local scale leaves the shape where it was"),
		BuilderLocalBounds.GetCenter().Equals(BaselineBounds.GetCenter(), 0.01));
	TestTrue(TEXT("Builder-local scale actually resizes the shape"),
		BuilderLocalBounds.GetSize().Equals(BaselineBounds.GetSize() * FVector(2.0, 2.0, 1.0), 0.01));

	// Actor local: the same scale drags the box away from the actor origin. This is the behaviour that made a
	// column need four primitives instead of two.
	FPCGUtilsDynMeshBuildResult ActorLocal;
	TestTrue(TEXT("Actor-local scaled Builder evaluates"), BuildOnce(
		MakeSpacedTransformDecorator(MakeBoxBuilder(FVector(100.0)), ScaleXY,
			EPCGUtilsDynMeshTransformSpace::ActorLocal), Seed, ActorLocal));
	TestTrue(TEXT("Actor-local scale moves the shape away from the origin"),
		FMath::IsNearlyEqual(GetResultBounds(ActorLocal).GetCenter().X, BaselineBounds.GetCenter().X * 2.0, 0.01));

	// Builder local and DynMesh local differ whenever the primitive's own pivot is not its bounds centre.
	// A Base-origin box is placed with its pivot on its underside.
	const FPCGUtilsDynMeshBuildContext OriginSeed =
		MakeSeed(FTransform::Identity, FBox(FVector(-50.0), FVector(50.0)));
	const FTransform ScaleZ(FQuat::Identity, FVector::ZeroVector, FVector(1.0, 1.0, 2.0));

	FPCGUtilsDynMeshBuildResult AboutPivot;
	TestTrue(TEXT("Base-origin builder-local scale evaluates"), BuildOnce(
		MakeSpacedTransformDecorator(
			MakeBoxBuilder(FVector(100.0), EGeometryScriptPrimitiveOriginMode::Base), ScaleZ,
			EPCGUtilsDynMeshTransformSpace::BuilderLocal), OriginSeed, AboutPivot));
	TestTrue(TEXT("Scaling about the placement pivot grows the box upward from its base"),
		FMath::IsNearlyEqual(GetResultBounds(AboutPivot).Min.Z, -50.0, 0.01) &&
		FMath::IsNearlyEqual(GetResultBounds(AboutPivot).Max.Z, 150.0, 0.01));

	FPCGUtilsDynMeshBuildResult AboutCentre;
	TestTrue(TEXT("Base-origin DynMesh-local scale evaluates"), BuildOnce(
		MakeSpacedTransformDecorator(
			MakeBoxBuilder(FVector(100.0), EGeometryScriptPrimitiveOriginMode::Base), ScaleZ,
			EPCGUtilsDynMeshTransformSpace::DynMeshLocal), OriginSeed, AboutCentre));
	TestTrue(TEXT("Scaling about the bounds centre grows the box both ways"),
		FMath::IsNearlyEqual(GetResultBounds(AboutCentre).Min.Z, -100.0, 0.01) &&
		FMath::IsNearlyEqual(GetResultBounds(AboutCentre).Max.Z, 100.0, 0.01));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPCGUtilsDynMeshBuilderFrameFollowsTest,
	"PCGUtils.DynMesh.Builder.BuilderFrameFollowsTheContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshBuilderFrameFollowsTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshBuilderTests;

	const FPCGUtilsDynMeshBuildContext Seed =
		MakeSeed(FTransform::Identity, FBox(FVector(-50.0), FVector(50.0)));

	FPCGUtilsDynMeshBuildResult Baseline;
	TestTrue(TEXT("Untransformed Builder evaluates"),
		BuildOnce(MakeBoxBuilder(FVector(100.0)), Seed, Baseline));
	const FVector BaselineCentre = GetResultBounds(Baseline).GetCenter();

	// Move the shape in its own space, then scale it in its own space. If the frame did not follow the first
	// transform, the second would scale about the *original* pivot and drag the shape back down.
	UPCGUtilsDynMeshProcessBuilderFactoryData* Moved = MakeSpacedTransformDecorator(
		MakeBoxBuilder(FVector(100.0)), FTransform(FVector(0.0, 0.0, 200.0)),
		EPCGUtilsDynMeshTransformSpace::BuilderLocal);
	UPCGUtilsDynMeshProcessBuilderFactoryData* MovedThenScaled = MakeSpacedTransformDecorator(
		Moved, FTransform(FQuat::Identity, FVector::ZeroVector, FVector(2.0)),
		EPCGUtilsDynMeshTransformSpace::BuilderLocal);

	FPCGUtilsDynMeshBuildResult Result;
	TestTrue(TEXT("Chained builder-local transforms evaluate"), BuildOnce(MovedThenScaled, Seed, Result));
	TestTrue(TEXT("Frame survives the chain"), Result.bHasBuilderFrame);

	const FBox Bounds = GetResultBounds(Result);
	TestTrue(TEXT("The second builder-local scale acts about the moved pivot, not the original one"),
		Bounds.GetCenter().Equals(BaselineCentre + FVector(0.0, 0.0, 200.0), 0.01));
	TestTrue(TEXT("The scale still resized the shape"),
		Bounds.GetSize().Equals(FVector(200.0), 0.01));
	TestTrue(TEXT("The recorded frame tracked the translation"),
		Result.BuilderFrame.GetLocation().Equals(
			Baseline.BuilderFrame.GetLocation() + FVector(0.0, 0.0, 200.0), 0.01));
	TestTrue(TEXT("The recorded frame stays rigid despite the scale"),
		Result.BuilderFrame.GetScale3D().Equals(FVector::OneVector, 0.001));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
