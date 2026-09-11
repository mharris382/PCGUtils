// Copyright Max Harris

#include "Elements/Topology/PCGDynMeshArray.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGSplineData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "Elements/PCGUtilsSplineHelpers.h"
#include "GeometryScript/MeshBasicEditFunctions.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "MeshTarget/PCGUtilsMeshTargetFunctions.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#if WITH_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

#define LOCTEXT_NAMESPACE "PCGDynMeshArray"

namespace
{
	constexpr double ArrayTolerance = UE_DOUBLE_KINDA_SMALL_NUMBER;

	FTransform ResolveArrayActorTransform(const FPCGUtilsDynMeshProcessInvocation& Invocation)
	{
		return Invocation.Context
			? PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(
				Invocation.Context, Invocation.MeshData, true)
			: FTransform::Identity;
	}

	FTransform ResolveArraySpaceFrame(
		const FPCGUtilsDynMeshProcessInvocation& Invocation,
		EPCGUtilsDynMeshTransformSpace Space,
		const UE::Geometry::FDynamicMesh3& Mesh)
	{
		switch (Space)
		{
		case EPCGUtilsDynMeshTransformSpace::World:
			return ResolveArrayActorTransform(Invocation).Inverse();
		case EPCGUtilsDynMeshTransformSpace::BuilderLocal:
			if (Invocation.bHasBuilderFrame)
			{
				return Invocation.BuilderFrame;
			}
			[[fallthrough]];
		case EPCGUtilsDynMeshTransformSpace::DynMeshLocal:
			return Mesh.VertexCount() > 0
				? FTransform(FVector(Mesh.GetBounds().Center()))
				: FTransform::Identity;
		case EPCGUtilsDynMeshTransformSpace::ActorLocal:
		default:
			return FTransform::Identity;
		}
	}

	FBox ComputeBoundsInFrame(const UE::Geometry::FDynamicMesh3& Mesh, const FTransform& Frame)
	{
		FBox Bounds(ForceInit);
		for (const int32 VertexID : Mesh.VertexIndicesItr())
		{
			Bounds += Frame.InverseTransformPosition(FVector(Mesh.GetVertex(VertexID)));
		}
		return Bounds;
	}

	FTransform TranslationInFrame(const FVector& Translation, const FTransform& Frame)
	{
		return Frame.Inverse() * FTransform(Translation) * Frame;
	}

	void AddCountTransforms(
		int32 Count,
		const FVector& Step,
		const FTransform& Frame,
		TArray<FTransform>& OutTransforms)
	{
		OutTransforms.Reserve(FMath::Max(0, Count - 1));
		for (int32 CopyIndex = 1; CopyIndex < Count; ++CopyIndex)
		{
			OutTransforms.Add(TranslationInFrame(Step * CopyIndex, Frame));
		}
	}

	bool AddBoundsTransforms(
		const FPCGUtilsDynMeshProcessInvocation& Invocation,
		const FPCGUtilsDynMeshArrayOperation& Operation,
		const UE::Geometry::FDynamicMesh3& SourceMesh,
		const FTransform& ArrayFrame,
		const FVector& ArrayStep,
		TArray<FTransform>& OutTransforms)
	{
		const FTransform ActorTransform = ResolveArrayActorTransform(Invocation);
		const FTransform BoundsFrame = Operation.Bounds.WorldTransform.GetRelativeTransform(ActorTransform);
		const FBox SourceInBounds = ComputeBoundsInFrame(SourceMesh, BoundsFrame);
		const FVector SourceSize = SourceInBounds.GetSize();
		const FVector TargetSize = Operation.Bounds.LocalMax - Operation.Bounds.LocalMin;
		const TCHAR* AxisNames[] = {TEXT("X"), TEXT("Y"), TEXT("Z")};
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (SourceSize[Axis] > TargetSize[Axis] + ArrayTolerance)
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("SourceDoesNotFitAxis", "DynMesh Array Bounds mode cannot fit the source on {0}: source size is {1}, but bound size is {2}."),
					FText::FromString(AxisNames[Axis]), FText::AsNumber(SourceSize[Axis]), FText::AsNumber(TargetSize[Axis])),
					Invocation.Context);
				return false;
			}
		}

		const FVector StepMesh = ArrayFrame.TransformVector(ArrayStep);
		const FVector StepBounds = BoundsFrame.InverseTransformVector(StepMesh);
		if (StepBounds.IsNearlyZero())
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("ZeroBoundsOffset", "DynMesh Array Bounds mode requires Relative Offset plus Constant Offset to produce a non-zero translation."),
				Invocation.Context);
			return false;
		}

		FVector StartBounds = FVector::ZeroVector;
		double MaxIntervals = TNumericLimits<double>::Max();
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const double Available = FMath::Max(0.0, TargetSize[Axis] - SourceSize[Axis]);
			if (StepBounds[Axis] > ArrayTolerance)
			{
				StartBounds[Axis] = Operation.Bounds.LocalMin[Axis] - SourceInBounds.Min[Axis];
				MaxIntervals = FMath::Min(MaxIntervals, Available / StepBounds[Axis]);
			}
			else if (StepBounds[Axis] < -ArrayTolerance)
			{
				StartBounds[Axis] = Operation.Bounds.LocalMax[Axis] - SourceInBounds.Max[Axis];
				MaxIntervals = FMath::Min(MaxIntervals, Available / -StepBounds[Axis]);
			}
			else
			{
				StartBounds[Axis] =
					(Operation.Bounds.LocalMin[Axis] + Operation.Bounds.LocalMax[Axis]) * 0.5
					- SourceInBounds.GetCenter()[Axis];
			}
		}

		if (!FMath::IsFinite(MaxIntervals))
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("NoMovingBoundsAxis", "DynMesh Array Bounds mode could not resolve a moving axis from the configured offset."),
				Invocation.Context);
			return false;
		}

		const int32 CopyCount = FMath::FloorToInt(MaxIntervals + ArrayTolerance) + 1;
		if (CopyCount > Operation.MaxCopies)
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("BoundsCopyLimit", "DynMesh Array Bounds mode resolved {0} copies, exceeding Max Copies ({1}). Increase the offset or Max Copies."),
				FText::AsNumber(CopyCount), FText::AsNumber(Operation.MaxCopies)), Invocation.Context);
			return false;
		}

		FVector EffectiveStepBounds = StepBounds;
		if (Operation.bFitBoundsPerfectly && CopyCount > 1)
		{
			double Scale = TNumericLimits<double>::Max();
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				if (FMath::Abs(StepBounds[Axis]) > ArrayTolerance)
				{
					Scale = FMath::Min(Scale,
						(TargetSize[Axis] - SourceSize[Axis]) /
						(FMath::Abs(StepBounds[Axis]) * static_cast<double>(CopyCount - 1)));
				}
			}
			if (FMath::IsFinite(Scale))
			{
				EffectiveStepBounds *= Scale;
			}
		}

		OutTransforms.Reserve(CopyCount);
		for (int32 CopyIndex = 0; CopyIndex < CopyCount; ++CopyIndex)
		{
			const FVector Translation = StartBounds + EffectiveStepBounds * CopyIndex;
			OutTransforms.Add(TranslationInFrame(Translation, BoundsFrame));
		}
		return true;
	}
}

#if WITH_EDITOR
FText UPCGDynMeshArraySettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "DynMesh | Array");
}

FText UPCGDynMeshArraySettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Duplicates a DynMesh using Blender-style constant/relative offsets, point transforms, spline frames, or "
		"as many copies as fit in an oriented point bound. Supports immediate DynMesh and deferred Builder pipelines.");
}

FString UPCGDynMeshArraySettings::GetAdditionalTitleInformation() const
{
	return UEnum::GetDisplayValueAsText(Mode).ToString();
}
#endif

bool UPCGDynMeshArraySettings::GetRequiredSelectionDomain(
	UE::Geometry::EGeometryElementType& OutElementType) const
{
	OutElementType = UE::Geometry::EGeometryElementType::Face;
	return true;
}

TArray<FPCGPinProperties> UPCGDynMeshArraySettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins = Super::InputPinProperties();
	const int32 SelectorIndex = Pins.IndexOfByPredicate([](const FPCGPinProperties& Pin)
	{
		return Pin.Label == PCGUtilsDynMeshProcessConstants::SelectionFactoryInputPin;
	});
	const int32 DependencyIndex = SelectorIndex == INDEX_NONE ? Pins.Num() : SelectorIndex;

	switch (Mode)
	{
	case EPCGUtilsDynMeshArrayMode::Points:
		Pins.Insert(FPCGPinProperties(PCGDynMeshArrayConstants::PointsInputPin, EPCGDataType::Point, false, false), DependencyIndex);
		Pins[DependencyIndex].SetRequiredPin();
		break;
	case EPCGUtilsDynMeshArrayMode::Spline:
		Pins.Insert(FPCGPinProperties(PCGDynMeshArrayConstants::SplineInputPin, EPCGDataType::Spline, false, false), DependencyIndex);
		Pins[DependencyIndex].SetRequiredPin();
		break;
	case EPCGUtilsDynMeshArrayMode::FitBounds:
		Pins.Insert(FPCGPinProperties(PCGDynMeshArrayConstants::BoundsInputPin, EPCGDataType::Point, false, false), DependencyIndex);
		Pins[DependencyIndex].SetRequiredPin();
		break;
	case EPCGUtilsDynMeshArrayMode::Count:
	default:
		break;
	}
	return Pins;
}

FPCGElementPtr UPCGDynMeshArraySettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshArrayElement>();
}

TSharedPtr<const FPCGUtilsDynMeshProcessOperation> UPCGDynMeshArraySettings::CreateProcessOperation(
	FPCGContext* InContext) const
{
	TSharedPtr<FPCGUtilsDynMeshArrayOperation> Operation = MakeShared<FPCGUtilsDynMeshArrayOperation>();
	Operation->Mode = Mode;
	Operation->Count = Count;
	Operation->ConstantOffset = ConstantOffset;
	Operation->RelativeOffset = RelativeOffset;
	Operation->Space = Space;
	Operation->bKeepOriginal = bKeepOriginal;
	Operation->SplineSpacing = SplineSpacing;
	Operation->bFitSplineSpacing = bFitSplineSpacing;
	Operation->bUseSplineScale = bUseSplineScale;
	Operation->bFitBoundsPerfectly = bFitBoundsPerfectly;
	Operation->MaxCopies = MaxCopies;

	if (MaxCopies < 1)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("InvalidMaxCopies", "DynMesh Array requires Max Copies to be at least 1."), InContext);
		Operation->bIsValid = false;
		return Operation;
	}

	if (Mode == EPCGUtilsDynMeshArrayMode::Count)
	{
		if (Count < 1)
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("InvalidCount", "DynMesh Array Count mode requires Count to be at least 1; received {0}."),
				FText::AsNumber(Count)), InContext);
			Operation->bIsValid = false;
		}
		else if (Count > MaxCopies)
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("CountExceedsLimit", "DynMesh Array Count ({0}) exceeds Max Copies ({1})."),
				FText::AsNumber(Count), FText::AsNumber(MaxCopies)), InContext);
			Operation->bIsValid = false;
		}
		return Operation;
	}

	if (Mode == EPCGUtilsDynMeshArrayMode::Points)
	{
		const TArray<FPCGTaggedData>& Inputs = InContext->InputData.GetInputsByPin(PCGDynMeshArrayConstants::PointsInputPin);
		if (Inputs.Num() != 1)
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("PointDataCount", "DynMesh Array Points mode requires exactly one Point Data object; received {0}."),
				FText::AsNumber(Inputs.Num())), InContext);
			Operation->bIsValid = false;
			return Operation;
		}
		const UPCGBasePointData* PointData = Cast<const UPCGBasePointData>(Inputs[0].Data);
		if (!PointData)
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("InvalidPointData", "DynMesh Array Points mode received data that is not Point Data."), InContext);
			Operation->bIsValid = false;
			return Operation;
		}
		if (PointData->GetNumPoints() + (bKeepOriginal ? 1 : 0) > MaxCopies)
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("PointCopyLimit", "DynMesh Array Points mode resolved {0} copies, exceeding Max Copies ({1})."),
				FText::AsNumber(PointData->GetNumPoints() + (bKeepOriginal ? 1 : 0)), FText::AsNumber(MaxCopies)), InContext);
			Operation->bIsValid = false;
			return Operation;
		}
		for (const FTransform& Transform : PointData->GetConstTransformValueRange())
		{
			if (Transform.ContainsNaN())
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("InvalidPointTransform", "DynMesh Array Points mode received a non-finite transform at point index {0}."),
					FText::AsNumber(Operation->PointWorldTransforms.Num())), InContext);
				Operation->bIsValid = false;
				return Operation;
			}
			Operation->PointWorldTransforms.Add(Transform);
		}
		return Operation;
	}

	if (Mode == EPCGUtilsDynMeshArrayMode::Spline)
	{
		if (!FMath::IsFinite(SplineSpacing) || SplineSpacing <= ArrayTolerance)
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("InvalidSplineSpacing", "DynMesh Array Spline mode requires Spline Spacing greater than zero; received {0}."),
				FText::AsNumber(SplineSpacing)), InContext);
			Operation->bIsValid = false;
			return Operation;
		}
		const UPCGSplineData* SplineData = PCGUtilsSplineHelpers::ResolveSingleSpline(
			InContext, PCGDynMeshArrayConstants::SplineInputPin);
		if (!SplineData)
		{
			Operation->bIsValid = false;
			return Operation;
		}
		Operation->Spline = SplineData->SplineStruct;
		const double SplineLength = Operation->Spline.GetSplineLength();
		if (SplineLength <= ArrayTolerance)
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("ZeroSplineLength", "DynMesh Array Spline mode requires a spline with non-zero length."), InContext);
			Operation->bIsValid = false;
			return Operation;
		}
		const int32 SplineCopies = FMath::FloorToInt(SplineLength / SplineSpacing) + 1 + (bKeepOriginal ? 1 : 0);
		if (SplineCopies > MaxCopies)
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("SplineCopyLimit", "DynMesh Array Spline mode resolved {0} copies, exceeding Max Copies ({1})."),
				FText::AsNumber(SplineCopies), FText::AsNumber(MaxCopies)), InContext);
			Operation->bIsValid = false;
		}
		return Operation;
	}

	const TArray<FPCGTaggedData>& Inputs = InContext->InputData.GetInputsByPin(PCGDynMeshArrayConstants::BoundsInputPin);
	if (Inputs.Num() != 1)
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("BoundsDataCount", "DynMesh Array Bounds mode requires exactly one Point Data object; received {0}."),
			FText::AsNumber(Inputs.Num())), InContext);
		Operation->bIsValid = false;
		return Operation;
	}
	const UPCGBasePointData* BoundsData = Cast<const UPCGBasePointData>(Inputs[0].Data);
	if (!BoundsData)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("InvalidBoundsData", "DynMesh Array Bounds mode received data that is not Point Data."), InContext);
		Operation->bIsValid = false;
		return Operation;
	}
	if (BoundsData->GetNumPoints() != 1)
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("BoundsPointCount", "DynMesh Array Bounds mode requires exactly one bounding point; received {0}."),
			FText::AsNumber(BoundsData->GetNumPoints())), InContext);
		Operation->bIsValid = false;
		return Operation;
	}
	Operation->Bounds.WorldTransform = BoundsData->GetConstTransformValueRange()[0];
	Operation->Bounds.LocalMin = BoundsData->GetConstBoundsMinValueRange()[0];
	Operation->Bounds.LocalMax = BoundsData->GetConstBoundsMaxValueRange()[0];
	if (Operation->Bounds.WorldTransform.ContainsNaN())
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("InvalidBoundsTransform", "DynMesh Array Bounds mode requires a finite transform on bounding point index 0."), InContext);
		Operation->bIsValid = false;
		return Operation;
	}
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (Operation->Bounds.LocalMax[Axis] < Operation->Bounds.LocalMin[Axis])
		{
			const TCHAR* AxisNames[] = {TEXT("X"), TEXT("Y"), TEXT("Z")};
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("InvertedBounds", "DynMesh Array Bounds mode requires Bounds Min <= Bounds Max on {0}; received min {1} and max {2} at point index 0."),
				FText::FromString(AxisNames[Axis]), FText::AsNumber(Operation->Bounds.LocalMin[Axis]),
				FText::AsNumber(Operation->Bounds.LocalMax[Axis])), InContext);
			Operation->bIsValid = false;
			return Operation;
		}
	}
	Operation->Bounds.bIsSet = true;
	return Operation;
}

bool FPCGUtilsDynMeshArrayOperation::Execute(
	const FPCGUtilsDynMeshProcessInvocation& Invocation,
	FPCGUtilsDynMeshProcessOutcome& OutOutcome) const
{
	if (!bIsValid)
	{
		return true;
	}

	FPCGUtilsMeshTargetHandle Handle = FPCGUtilsMeshTargetFunctions::CreateTargetInPlace(
		Invocation, EPCGUtilsMeshTargetPreparation::Region);
	if (!Handle.IsValid())
	{
		return false;
	}
	if (Handle.IsEmptySelectionNoOp())
	{
		OutOutcome.SelectionOutcome = EPCGUtilsDynMeshProcessSelectionOutcome::Clear;
		return FPCGUtilsMeshTargetFunctions::RestoreRegion(Handle);
	}

	UDynamicMesh* TargetMesh = Handle.GetTargetMesh();
	const UE::Geometry::FDynamicMesh3* SourceMesh = TargetMesh ? TargetMesh->GetMeshPtr() : nullptr;
	if (!SourceMesh || SourceMesh->VertexCount() == 0)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("InvalidSourceMesh", "DynMesh Array requires an input mesh containing at least one vertex."),
			Invocation.Context);
		FPCGUtilsMeshTargetFunctions::RestoreRegion(Handle);
		return false;
	}

	UDynamicMesh* TemplateMesh = FPCGContext::NewObject_AnyThread<UDynamicMesh>(Invocation.Context);
	TemplateMesh->SetMesh(*SourceMesh);
	const FTransform ArrayFrame = ResolveArraySpaceFrame(Invocation, Space, *SourceMesh);
	const FBox ArrayBounds = ComputeBoundsInFrame(*SourceMesh, ArrayFrame);
	const FVector Step = ConstantOffset + RelativeOffset * ArrayBounds.GetSize();
	TArray<FTransform> CopyTransforms;
	bool bResetToPlacements = false;

	switch (Mode)
	{
	case EPCGUtilsDynMeshArrayMode::Count:
		AddCountTransforms(Count, Step, ArrayFrame, CopyTransforms);
		break;

	case EPCGUtilsDynMeshArrayMode::Points:
	{
		bResetToPlacements = !bKeepOriginal;
		const FTransform ActorTransform = ResolveArrayActorTransform(Invocation);
		CopyTransforms.Reserve(PointWorldTransforms.Num());
		for (const FTransform& WorldTransform : PointWorldTransforms)
		{
			const FTransform MeshTransform = WorldTransform.GetRelativeTransform(ActorTransform);
			CopyTransforms.Add(ArrayFrame.Inverse() * MeshTransform);
		}
		break;
	}

	case EPCGUtilsDynMeshArrayMode::Spline:
	{
		bResetToPlacements = !bKeepOriginal;
		const double SplineLength = Spline.GetSplineLength();
		const int32 SampleCount = FMath::FloorToInt(SplineLength / SplineSpacing) + 1;
		const double EffectiveSpacing = bFitSplineSpacing && SampleCount > 1
			? SplineLength / static_cast<double>(SampleCount - 1)
			: SplineSpacing;
		const FTransform ActorTransform = ResolveArrayActorTransform(Invocation);
		CopyTransforms.Reserve(SampleCount);
		for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
		{
			const double Distance = FMath::Min(SplineLength, EffectiveSpacing * SampleIndex);
			const float InputKey = Spline.GetInputKeyAtDistanceAlongSpline(Distance);
			const FTransform WorldTransform = Spline.GetTransformAtSplineInputKey(
				InputKey, ESplineCoordinateSpace::World, bUseSplineScale);
			const FTransform MeshTransform = WorldTransform.GetRelativeTransform(ActorTransform);
			CopyTransforms.Add(ArrayFrame.Inverse() * MeshTransform);
		}
		break;
	}

	case EPCGUtilsDynMeshArrayMode::FitBounds:
		bResetToPlacements = true;
		if (!Bounds.bIsSet || !AddBoundsTransforms(
			Invocation, *this, *SourceMesh, ArrayFrame, Step, CopyTransforms))
		{
			OutOutcome.SelectionOutcome = EPCGUtilsDynMeshProcessSelectionOutcome::Clear;
			return FPCGUtilsMeshTargetFunctions::RestoreRegion(Handle);
		}
		break;
	}

	if (bResetToPlacements)
	{
		TargetMesh->Reset();
	}
	if (!CopyTransforms.IsEmpty())
	{
		UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMeshTransformed(
			TargetMesh, TemplateMesh, CopyTransforms, FTransform::Identity);
	}

	if (!FPCGUtilsMeshTargetFunctions::RestoreRegion(Handle))
	{
		return false;
	}
	OutOutcome.SelectionOutcome = EPCGUtilsDynMeshProcessSelectionOutcome::Clear;
	return true;
}

#undef LOCTEXT_NAMESPACE

#if WITH_AUTOMATION_TESTS

namespace PCGDynMeshArrayTests
{
	UPCGDynamicMeshData* MakeArrayTestBox(double Size = 100.0)
	{
		UPCGDynamicMeshData* Data = NewObject<UPCGDynamicMeshData>();
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
			Data->GetMutableDynamicMesh(), FGeometryScriptPrimitiveOptions(), FTransform::Identity,
			Size, Size, Size, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Center);
		return Data;
	}

	FBox GetArrayTestBounds(const UPCGDynamicMeshData* Data)
	{
		const UDynamicMesh* MeshObject = Data ? Data->GetDynamicMesh() : nullptr;
		const UE::Geometry::FDynamicMesh3* Mesh = MeshObject ? MeshObject->GetMeshPtr() : nullptr;
		return Mesh && Mesh->VertexCount() > 0
			? FBox(FVector(Mesh->GetBounds().Min), FVector(Mesh->GetBounds().Max))
			: FBox(ForceInit);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshArrayCountTest,
	"PCGUtils.DynMesh.Array.CountRelativeOffset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshArrayCountTest::RunTest(const FString&)
{
	using namespace PCGDynMeshArrayTests;
	UPCGDynamicMeshData* Data = MakeArrayTestBox();
	const int32 SourceTriangles = Data->GetDynamicMesh()->GetMeshPtr()->TriangleCount();

	FPCGUtilsDynMeshArrayOperation Operation;
	Operation.Mode = EPCGUtilsDynMeshArrayMode::Count;
	Operation.Count = 3;
	Operation.RelativeOffset = FVector(1.0, 0.0, 0.0);
	Operation.ConstantOffset = FVector::ZeroVector;
	Operation.Space = EPCGUtilsDynMeshTransformSpace::ActorLocal;

	FPCGUtilsDynMeshProcessInvocation Invocation;
	Invocation.MeshData = Data;
	FPCGUtilsDynMeshProcessOutcome Outcome;
	TestTrue(TEXT("Count array executes"), Operation.Execute(Invocation, Outcome));
	TestEqual(TEXT("Count array emits one source worth of triangles per copy"),
		Data->GetDynamicMesh()->GetMeshPtr()->TriangleCount(), SourceTriangles * 3);
	TestTrue(TEXT("Relative X offset produces a three-source-width result"),
		GetArrayTestBounds(Data).GetSize().Equals(FVector(300.0, 100.0, 100.0), 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshArrayPerfectBoundsTest,
	"PCGUtils.DynMesh.Array.PerfectBoundsFit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshArrayPerfectBoundsTest::RunTest(const FString&)
{
	using namespace PCGDynMeshArrayTests;
	UPCGDynamicMeshData* Data = MakeArrayTestBox();
	const int32 SourceTriangles = Data->GetDynamicMesh()->GetMeshPtr()->TriangleCount();

	FPCGUtilsDynMeshArrayOperation Operation;
	Operation.Mode = EPCGUtilsDynMeshArrayMode::FitBounds;
	Operation.RelativeOffset = FVector(1.0, 0.0, 0.0);
	Operation.Space = EPCGUtilsDynMeshTransformSpace::ActorLocal;
	Operation.bFitBoundsPerfectly = true;
	Operation.Bounds.bIsSet = true;
	Operation.Bounds.WorldTransform = FTransform::Identity;
	Operation.Bounds.LocalMin = FVector(0.0, -50.0, -50.0);
	Operation.Bounds.LocalMax = FVector(450.0, 50.0, 50.0);

	FPCGUtilsDynMeshProcessInvocation Invocation;
	Invocation.MeshData = Data;
	FPCGUtilsDynMeshProcessOutcome Outcome;
	TestTrue(TEXT("Perfect bounds array executes"), Operation.Execute(Invocation, Outcome));
	TestEqual(TEXT("Requested step fits four copies before redistribution"),
		Data->GetDynamicMesh()->GetMeshPtr()->TriangleCount(), SourceTriangles * 4);
	const FBox Bounds = GetArrayTestBounds(Data);
	TestTrue(TEXT("Perfect bounds array reaches both X bounds"),
		FMath::IsNearlyEqual(Bounds.Min.X, 0.0, 0.01) && FMath::IsNearlyEqual(Bounds.Max.X, 450.0, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshArrayPinContractTest,
	"PCGUtils.DynMesh.Array.PinContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshArrayPinContractTest::RunTest(const FString&)
{
	UPCGDynMeshArraySettings* Settings = NewObject<UPCGDynMeshArraySettings>();
	Settings->Mode = EPCGUtilsDynMeshArrayMode::Spline;
	const TArray<FPCGPinProperties> Pins = Settings->AllInputPinProperties();
	TestTrue(TEXT("Mutated DynMesh input is first"),
		Pins.Num() >= 3 && Pins[0].Label == PCGUtilsDynMeshProcessConstants::InputPin);
	TestTrue(TEXT("Required spline dependency follows the mutated input"),
		Pins.Num() >= 3 && Pins[1].Label == PCGDynMeshArrayConstants::SplineInputPin && Pins[1].IsRequiredPin());
	TestTrue(TEXT("Optional Selector is last"),
		Pins.Num() >= 3 && Pins.Last().Label == PCGUtilsDynMeshProcessConstants::SelectionFactoryInputPin);
	return true;
}

#endif
