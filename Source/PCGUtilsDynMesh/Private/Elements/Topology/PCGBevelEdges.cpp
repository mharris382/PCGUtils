#include "Elements/Topology/PCGBevelEdges.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Utils/PCGLogErrors.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "Operations/MeshBevel.h"
#include "Geometry/PCGUtilsDynMeshFaceAngles.h"
#include "Geometry/PCGUtilsDynMeshBevelClamp.h"

#define LOCTEXT_NAMESPACE "PCGBevelEdges"

namespace
{
	const FName BevelMeshPin = TEXT("Mesh");
}

UPCGBevelEdgesSettings::UPCGBevelEdgesSettings()
	: BevelDistance(1.0, TEXT("BevelDistance")), MinimumBevelDistance(0.001, TEXT("MinimumBevelDistance")), FaceAngleComparison(PCGUtilsDynMeshFaceAngles::MakeDefaultComparison())
{
	// Preserve the existing bare-mesh "bevel all edges" behavior. Authors can now require a selection explicitly.
	bRequireSelection = false;
}

#if WITH_EDITOR
FText UPCGBevelEdgesSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DynMesh | Bevel Edges");
}

FText UPCGBevelEdgesSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Bevels a DynMesh edge selection, or every edge when a bare mesh is supplied. Clamp Overlap reduces excessive distances at individual endpoints in Per Vertex mode by default, keeping full width elsewhere; Minimum Bevel Distance controls when unusable incident edges are omitted and the remaining selection rebuilt. Selection Island and Whole Mesh offer uniform amounts. Filter By Face Angle optionally removes edges that fail an adjacent-face normal comparison; the default threshold keeps unsigned face-normal differences of at least 45 degrees. Distance and angle thresholds can be constants or per-mesh @Data values. Newly created bevel faces become the result selection. Can assign a named result PolyGroup and emit a reusable Selector on Result. Builder inputs require constant distance, enabled minimum distance, and angle thresholds.");
}

EPCGChangeType UPCGBevelEdgesSettings::GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const
{
	EPCGChangeType ChangeType = Super::GetChangeTypeForProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UPCGBevelEdgesSettings, BevelDistance) ||
		PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UPCGBevelEdgesSettings, MinimumBevelDistance) ||
		PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UPCGBevelEdgesSettings, bClampOverlap) ||
		PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UPCGBevelEdgesSettings, bFilterByFaceAngle) ||
		PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UPCGBevelEdgesSettings, FaceAngleComparison))
	{
		// Switching Bevel Distance between Constant and @Data decides whether the main pin accepts Builders.
		ChangeType |= EPCGChangeType::Structural;
	}
	return ChangeType;
}
#endif

FText UPCGBevelEdgesSettings::GetBuilderInputRejectionReason() const
{
	if (bClampOverlap && !MinimumBevelDistance.IsConstant())
	{ return LOCTEXT("MinimumBuilderRejected", "Bevel Edges reads Minimum Bevel Distance from @Data, and a Builder has no @Data until it is realized. Realize the Builder first, or use a constant minimum distance."); }
	if (bFilterByFaceAngle && FaceAngleComparison.ThresholdInput == EPCGUtilsDynMeshInputValueType::Attribute)
	{ return LOCTEXT("AngleBuilderRejected", "Bevel Edges reads the Face Normal Comparison threshold from @Data, and a Builder has no @Data until it is realized. Realize the Builder first, or use a constant angle threshold."); }
	return LOCTEXT("BuilderRejected", "Bevel Edges reads Bevel Distance from @Data, and a Builder has no @Data until it is realized. Realize the Builder first, or set Bevel Distance to Constant.");
}

FName UPCGBevelEdgesSettings::GetMainInputPinLabel() const
{
	return BevelMeshPin;
}

FPCGElementPtr UPCGBevelEdgesSettings::CreateElement() const
{
	return MakeShared<FPCGBevelEdgesElement>();
}

TSharedPtr<FPCGUtilsDynMeshTopologyOperation> UPCGBevelEdgesSettings::CreateTopologyOperation(
	FPCGContext* InContext) const
{
	TSharedPtr<FPCGUtilsDynMeshBevelEdgesOperation> Operation = MakeShared<FPCGUtilsDynMeshBevelEdgesOperation>();
	Operation->BevelDistanceInput = BevelDistance;
	Operation->MinimumBevelDistanceInput = MinimumBevelDistance;
	Operation->MinimumBevelDistance = MinimumBevelDistance.Constant;
	Operation->bClampOverlap = bClampOverlap;
	Operation->ClampMode = ClampMode;
	Operation->bFilterByFaceAngle = bFilterByFaceAngle;
	Operation->FaceAngleComparison = FaceAngleComparison;
	Operation->BevelDistance = BevelDistance.Constant;
	Operation->Subdivisions = Subdivisions;
	Operation->RoundWeight = RoundWeight;
	Operation->bInferMaterialID = bInferMaterialID;
	Operation->SetMaterialID = SetMaterialID;
	return Operation;
}

bool FPCGUtilsDynMeshBevelEdgesOperation::Execute(const FPCGUtilsDynMeshProcessInvocation& Invocation,
	FPCGUtilsDynMeshProcessOutcome& OutOutcome) const
{
	// The operation is shared across inputs and must stay immutable, so the per-input distance is resolved
	// into a local copy that runs this one invocation.
	const UPCGDynamicMeshData* Source = Invocation.SourceMeshData ? Invocation.SourceMeshData : Invocation.MeshData;
	FPCGUtilsDynMeshBevelEdgesOperation Resolved(*this);
	if (!BevelDistanceInput.IsConstant())
	{
		double Value = 0.0;
		FText Error;
		if (!BevelDistanceInput.TryReadDataValue(Source, Value, Error, LOCTEXT("BevelDistanceLabel", "Bevel Distance"), Invocation.InputIndex))
		{
			PCGLog::LogErrorOnGraph(Error, Invocation.Context);
			return false;
		}
		Resolved.BevelDistanceInput.Input = EPCGUtilsInputValueType::Constant;
		Resolved.BevelDistance = Value;
	}
	if (!FMath::IsFinite(Resolved.BevelDistance))
	{
		PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("DistanceNotFinite", "Bevel Edges input {0}: Bevel Distance ({1}) must be finite."), FText::AsNumber(Invocation.InputIndex), FText::AsNumber(Resolved.BevelDistance)), Invocation.Context);
		return false;
	}
	if (Resolved.BevelDistance < 0.0)
	{
		PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("DistanceNegative", "Bevel Edges input {0}: Bevel Distance ({1}) must be zero or greater."), FText::AsNumber(Invocation.InputIndex), FText::AsNumber(Resolved.BevelDistance)), Invocation.Context);
		return false;
	}
	if (bClampOverlap)
	{
		if (!MinimumBevelDistanceInput.IsConstant())
		{
			FText Error;
			if (!MinimumBevelDistanceInput.TryReadDataValue(Source, Resolved.MinimumBevelDistance, Error, LOCTEXT("MinimumDistanceLabel", "Minimum Bevel Distance"), Invocation.InputIndex))
			{ PCGLog::LogErrorOnGraph(Error, Invocation.Context); return false; }
		}
		if (!FMath::IsFinite(Resolved.MinimumBevelDistance))
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("MinimumNotFinite", "Bevel Edges input {0}: Minimum Bevel Distance ({1}) must be finite."), FText::AsNumber(Invocation.InputIndex), FText::AsNumber(Resolved.MinimumBevelDistance)), Invocation.Context); return false;
		}
		if (Resolved.MinimumBevelDistance < 0.0)
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("MinimumNegative", "Bevel Edges input {0}: Minimum Bevel Distance ({1}) must be zero or greater."), FText::AsNumber(Invocation.InputIndex), FText::AsNumber(Resolved.MinimumBevelDistance)), Invocation.Context); return false;
		}
	}
	if (bFilterByFaceAngle)
	{
		FText Error;
		if (!PCGUtilsDynMeshFaceAngles::ResolveComparison(FaceAngleComparison, Source, Resolved.FaceAngleThresholdDot, Error))
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("AngleThresholdError", "Bevel Edges input {0}: {1}"), FText::AsNumber(Invocation.InputIndex), Error), Invocation.Context);
			return false;
		}
	}
	PCGUtilsDynMeshAmountClamp::FResult ClampResult;
	Resolved.InvocationClampResult = &ClampResult;
	const bool bSuccess = Resolved.FPCGUtilsDynMeshTopologyOperation::Execute(Invocation, OutOutcome);
	if (ClampResult.bCouldNotFindSafeAmount)
	{
		PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("ClampUnresolved", "Bevel Edges input {0}: Clamp Overlap left this mesh unchanged. {1}"),
			FText::AsNumber(Invocation.InputIndex), FText::FromString(ClampResult.FailureReason)), Invocation.Context);
	}
	else if (ClampResult.SkippedEdges > 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("ClampPartial", "Bevel Edges input {0}: omitted {1} selected edges incident to endpoints without a usable offset. Remaining edges were beveled; Minimum Bevel Distance is {2}."), FText::AsNumber(Invocation.InputIndex), FText::AsNumber(ClampResult.SkippedEdges), FText::AsNumber(Resolved.MinimumBevelDistance)), Invocation.Context);
	}
	return bSuccess;
}

bool FPCGUtilsDynMeshBevelEdgesOperation::Apply(UE::Geometry::FDynamicMesh3& Mesh,
	const UE::Geometry::FGeometrySelection* Selection, TArray<int32>& OutResultTriangles) const
{
	TArray<int32> Edges;
	if (Selection)
	{
		FGeometryScriptMeshSelection ScriptSelection;
		ScriptSelection.SetSelection(*Selection);
		ScriptSelection.ConvertToMeshIndexArray(Mesh, Edges, EGeometryScriptIndexType::Edge);
	}
	else
	{
		for (int32 ID : Mesh.EdgeIndicesItr()) { Edges.Add(ID); }
	}
	if (bFilterByFaceAngle)
	{
		Edges.RemoveAll([this, &Mesh](int32 EdgeID)
		{ return !PCGUtilsDynMeshFaceAngles::TestEdge(Mesh, EdgeID, FaceAngleComparison, FaceAngleThresholdDot); });
	}
	if (Edges.IsEmpty() || BevelDistance == 0.0f) { return true; }
	if (bClampOverlap)
	{
		PCGUtilsDynMeshBevelClamp::FOptions Options;
		Options.Distance = BevelDistance;
		Options.MinimumDistance = MinimumBevelDistance;
		Options.Mode = ClampMode;
		Options.Subdivisions = FMath::Clamp(Subdivisions, 0, 9999);
		Options.RoundWeight = FMath::Clamp(RoundWeight, -10.0f, 10.0f);
		Options.bInferMaterialID = bInferMaterialID;
		Options.MaterialID = SetMaterialID;
		return PCGUtilsDynMeshBevelClamp::Apply(Mesh, Edges, Options, OutResultTriangles, InvocationClampResult);
	}
	UE::Geometry::FMeshBevel Bevel;
	Bevel.InsetDistance = BevelDistance;
	Bevel.MaterialIDMode = bInferMaterialID ? UE::Geometry::FMeshBevel::EMaterialIDMode::InferMaterialID
		: UE::Geometry::FMeshBevel::EMaterialIDMode::ConstantMaterialID;
	Bevel.SetConstantMaterialID = SetMaterialID;
	Bevel.NumSubdivisions = FMath::Clamp(Subdivisions, 0, 9999);
	Bevel.RoundWeight = FMath::Clamp(RoundWeight, -10.0f, 10.0f);
	Bevel.InitializeFromTriangleEdges(Mesh, Edges, [&Mesh](int32 VertexID)
	{
		return Mesh.HasTriangleGroups() && Mesh.IsGroupJunctionVertex(VertexID);
	});
	if (!Bevel.Apply(Mesh, nullptr)) { return false; }
	OutResultTriangles = MoveTemp(Bevel.NewTriangles);
	return true;
}

#undef LOCTEXT_NAMESPACE
