#include "Elements/Deform/PCGWarp.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "GameFramework/Actor.h"
#include "MeshTarget/PCGUtilsMeshTargetFunctions.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGWarpElement"

namespace
{
	const FName WarpMeshPin = TEXT("Mesh");
	const FName WarpControlsPin = TEXT("Warp Controls");

	/**
	 * One resolved warp application: a frame (pivot + axis, already in the Dynamic Mesh's coordinate space) plus
	 * optional bounds-derived extents and a strength multiplier. Built once per controller in ResolveControllers,
	 * then applied to the target mesh in order by ApplyWarp.
	 */
	// Position + rotation only; Geometry Script's warp frame (FFrame3d) has no scale component, so control
	// point / Orientation scale is deliberately not carried in the controller - point scale instead only feeds
	// bounds inference below, avoiding double-applying it.
	using FWarpController = FPCGUtilsDynMeshWarpController;

	/** Position + rotation only, matching how Geometry Script's warp ops actually consume the orientation transform (see FFrame3d(const FTransform&), which discards scale entirely). */
	FTransform MakeWarpFrame(const FTransform& SourceTransform)
	{
		return FTransform(SourceTransform.GetRotation(), SourceTransform.GetLocation());
	}

	TArray<FWarpController> ResolveControllers(FPCGContext* Context, const UPCGWarpSettings* Settings)
	{
		TArray<FWarpController> Controllers;

		if (Settings->ControlMode == EPCGUtilsWarpControlMode::SettingsTransform)
		{
			FWarpController Controller;
			Controller.Orientation = MakeWarpFrame(Settings->Orientation);
			Controllers.Add(Controller);
			return Controllers;
		}

		// PointData mode: resolve the actor-local conversion transform once (if requested), then build one
		// controller per connected Point Data input from its first point only - later points are ignored by
		// design (see class comment).
		FTransform ActorTransform = FTransform::Identity;
		if (Settings->bConvertControlPointsToLocalSpace)
		{
			if (const AActor* TargetActor = Context->GetTargetActor(nullptr))
			{
				ActorTransform = TargetActor->GetActorTransform();
			}
			else
			{
				PCGLog::LogWarningOnGraph(LOCTEXT("MissingTargetActor",
					"Warp could not resolve a target actor; control point transforms remain in their original space."), Context);
			}
		}

		for (const FPCGTaggedData& ControlInput : Context->InputData.GetInputsByPin(WarpControlsPin))
		{
			const UPCGBasePointData* PointData = Cast<const UPCGBasePointData>(ControlInput.Data);
			if (!PointData || PointData->GetNumPoints() == 0)
			{
				continue;
			}

			const FTransform PointTransform = Settings->bConvertControlPointsToLocalSpace
				? PointData->GetConstTransformValueRange()[0].GetRelativeTransform(ActorTransform)
				: PointData->GetConstTransformValueRange()[0];

			FWarpController Controller;
			Controller.Orientation = MakeWarpFrame(PointTransform);
			Controller.Density = Settings->bScaleStrengthByPointDensity ? PointData->GetConstDensityValueRange()[0] : 1.0f;

			if (Settings->bInferExtentsFromPointBounds)
			{
				// Point origin -> warp pivot; local +Z/-Z bounds -> upper/lower extent. Scale is applied here
				// (not baked into Controller.Orientation, which is deliberately unscaled) so it is applied
				// exactly once. Abs() guards against negative scale or an authored Min>0/Max<0 flipping sign.
				const FVector BoundsMin = PointData->GetConstBoundsMinValueRange()[0];
				const FVector BoundsMax = PointData->GetConstBoundsMaxValueRange()[0];
				const double ScaleZ = PointTransform.GetScale3D().Z;
				const double UpperExtent = FMath::Abs(BoundsMax.Z * ScaleZ);
				const double LowerExtent = FMath::Abs(BoundsMin.Z * ScaleZ);

				if (UpperExtent + LowerExtent < UE_DOUBLE_KINDA_SMALL_NUMBER)
				{
					PCGLog::LogWarningOnGraph(LOCTEXT("DegenerateControlBounds",
						"Warp skipped a control point with degenerate (near-zero) bounds along its local Z axis."), Context);
					continue;
				}

				Controller.bHasBoundsExtents = true;
				Controller.BoundsLowerExtent = LowerExtent;
				Controller.BoundsUpperExtent = UpperExtent;
			}

			Controllers.Add(Controller);
		}

		if (Controllers.IsEmpty())
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("NoControllers",
				"Warp: Point Data control mode resolved no usable control points; meshes will pass through unchanged."), Context);
		}

		return Controllers;
	}

}

#if WITH_EDITOR
FText UPCGWarpSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Warp");
}

FText UPCGWarpSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Applies a Geometry Script Bend, Twist, or Flare/Squish space deformation to Dynamic Mesh data. If the "
		"Mesh input is a Mesh Selection, only selected geometry is deformed. Topology and unrelated mesh "
		"attributes such as UVs, vertex colors, materials, and PolyGroups are preserved.");
}

EPCGChangeType UPCGWarpSettings::GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const
{
	EPCGChangeType ChangeType = Super::GetChangeTypeForProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UPCGWarpSettings, ControlMode))
	{
		ChangeType |= EPCGChangeType::Structural;
	}
	return ChangeType;
}
#endif

FName UPCGWarpSettings::GetMainInputPinLabel() const
{
	return WarpMeshPin;
}

FPCGElementPtr UPCGWarpSettings::CreateElement() const
{
	return MakeShared<FPCGWarpElement>();
}

TSharedPtr<const FPCGUtilsDynMeshProcessOperation> UPCGWarpSettings::CreateProcessOperation(
	FPCGContext* InContext) const
{
	TSharedPtr<FPCGUtilsDynMeshWarpOperation> Operation = MakeShared<FPCGUtilsDynMeshWarpOperation>();

	// Controllers are resolved once, here, from this node's own control-point pin and target actor - not
	// per mesh, and never from whatever context later evaluates the operation.
	Operation->Controllers = ResolveControllers(InContext, this);

	Operation->SelectionBlend = SelectionBlend;
	Operation->WarpType = WarpType;
	Operation->BendAngle = BendAngle;
	Operation->BendExtent = BendExtent;
	Operation->bBendSymmetricExtents = bBendSymmetricExtents;
	Operation->BendLowerExtent = BendLowerExtent;
	Operation->bBendBidirectional = bBendBidirectional;
	Operation->TwistAngle = TwistAngle;
	Operation->TwistExtent = TwistExtent;
	Operation->bTwistSymmetricExtents = bTwistSymmetricExtents;
	Operation->TwistLowerExtent = TwistLowerExtent;
	Operation->bTwistBidirectional = bTwistBidirectional;
	Operation->FlarePercentX = FlarePercentX;
	Operation->FlarePercentY = FlarePercentY;
	Operation->FlareExtent = FlareExtent;
	Operation->bFlareSymmetricExtents = bFlareSymmetricExtents;
	Operation->FlareLowerExtent = FlareLowerExtent;
	Operation->FlareType = FlareType;
	return Operation;
}

void FPCGUtilsDynMeshWarpOperation::ApplyWarp(
	UDynamicMesh* Mesh, const FPCGUtilsDynMeshWarpController& Controller) const
{
	switch (WarpType)
	{
	case EPCGUtilsWarpType::Bend:
	{
		FGeometryScriptBendWarpOptions Options;
		Options.bSymmetricExtents = bBendSymmetricExtents;
		Options.LowerExtent = BendLowerExtent;
		Options.bBidirectional = bBendBidirectional;

		float Extent = BendExtent;
		if (Controller.bHasBoundsExtents)
		{
			Options.bSymmetricExtents = false;
			Options.LowerExtent = Controller.BoundsLowerExtent;
			Extent = Controller.BoundsUpperExtent;
		}

		UGeometryScriptLibrary_MeshDeformFunctions::ApplyBendWarpToMesh(
			Mesh, Options, Controller.Orientation, BendAngle * Controller.Density, Extent);
		break;
	}
	case EPCGUtilsWarpType::Twist:
	{
		FGeometryScriptTwistWarpOptions Options;
		Options.bSymmetricExtents = bTwistSymmetricExtents;
		Options.LowerExtent = TwistLowerExtent;
		Options.bBidirectional = bTwistBidirectional;

		float Extent = TwistExtent;
		if (Controller.bHasBoundsExtents)
		{
			Options.bSymmetricExtents = false;
			Options.LowerExtent = Controller.BoundsLowerExtent;
			Extent = Controller.BoundsUpperExtent;
		}

		UGeometryScriptLibrary_MeshDeformFunctions::ApplyTwistWarpToMesh(
			Mesh, Options, Controller.Orientation, TwistAngle * Controller.Density, Extent);
		break;
	}
	case EPCGUtilsWarpType::Flare:
	{
		FGeometryScriptFlareWarpOptions Options;
		Options.bSymmetricExtents = bFlareSymmetricExtents;
		Options.LowerExtent = FlareLowerExtent;
		Options.FlareType = FlareType;

		float Extent = FlareExtent;
		if (Controller.bHasBoundsExtents)
		{
			Options.bSymmetricExtents = false;
			Options.LowerExtent = Controller.BoundsLowerExtent;
			Extent = Controller.BoundsUpperExtent;
		}

		UGeometryScriptLibrary_MeshDeformFunctions::ApplyFlareWarpToMesh(
			Mesh, Options, Controller.Orientation,
			FlarePercentX * Controller.Density, FlarePercentY * Controller.Density, Extent);
		break;
	}
	default:
		checkNoEntry();
		break;
	}
}

bool FPCGUtilsDynMeshWarpOperation::Execute(
	const FPCGUtilsDynMeshProcessInvocation& Invocation,
	FPCGUtilsDynMeshProcessOutcome& OutOutcome) const
{
	// Space deformation moves vertices without changing connectivity.
	OutOutcome.SelectionOutcome = EPCGUtilsDynMeshProcessSelectionOutcome::Preserve;

	FPCGUtilsMeshTargetHandle Handle = FPCGUtilsMeshTargetFunctions::CreateTargetInPlace(
		Invocation, EPCGUtilsMeshTargetPreparation::FullMeshCopy);
	if (!Handle.IsValid())
	{
		return false;
	}

	if (!Handle.IsEmptySelectionNoOp())
	{
		UDynamicMesh* Mesh = Handle.GetTargetMesh();
		for (const FPCGUtilsDynMeshWarpController& Controller : Controllers)
		{
			ApplyWarp(Mesh, Controller);
		}
	}

	// Composites straight back into Invocation.MeshData - CreateTargetInPlace adopted it as the base mesh.
	FPCGUtilsMeshTargetFunctions::RestoreVertexPositions(Handle, SelectionBlend);
	FPCGUtilsMeshTargetFunctions::RecomputeSelectionAffectedNormals(Handle);
	return true;
}

#undef LOCTEXT_NAMESPACE
