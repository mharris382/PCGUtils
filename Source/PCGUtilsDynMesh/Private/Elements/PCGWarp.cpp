#include "Elements/PCGWarp.h"

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
	struct FWarpController
	{
		// Position + rotation only; Geometry Script's warp frame (FFrame3d) has no scale component, so control
		// point / Orientation scale is deliberately not carried here - point scale instead only feeds bounds
		// inference below, avoiding double-applying it.
		FTransform Orientation = FTransform::Identity;

		float Density = 1.0f;

		bool bHasBoundsExtents = false;
		double BoundsLowerExtent = 0.0;
		double BoundsUpperExtent = 0.0;
	};

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

	void ApplyWarp(UDynamicMesh* Mesh, const UPCGWarpSettings* Settings, const FWarpController& Controller)
	{
		switch (Settings->WarpType)
		{
		case EPCGUtilsWarpType::Bend:
		{
			FGeometryScriptBendWarpOptions Options;
			Options.bSymmetricExtents = Settings->bBendSymmetricExtents;
			Options.LowerExtent = Settings->BendLowerExtent;
			Options.bBidirectional = Settings->bBendBidirectional;

			float Extent = Settings->BendExtent;
			if (Controller.bHasBoundsExtents)
			{
				Options.bSymmetricExtents = false;
				Options.LowerExtent = Controller.BoundsLowerExtent;
				Extent = Controller.BoundsUpperExtent;
			}

			UGeometryScriptLibrary_MeshDeformFunctions::ApplyBendWarpToMesh(
				Mesh, Options, Controller.Orientation, Settings->BendAngle * Controller.Density, Extent);
			break;
		}
		case EPCGUtilsWarpType::Twist:
		{
			FGeometryScriptTwistWarpOptions Options;
			Options.bSymmetricExtents = Settings->bTwistSymmetricExtents;
			Options.LowerExtent = Settings->TwistLowerExtent;
			Options.bBidirectional = Settings->bTwistBidirectional;

			float Extent = Settings->TwistExtent;
			if (Controller.bHasBoundsExtents)
			{
				Options.bSymmetricExtents = false;
				Options.LowerExtent = Controller.BoundsLowerExtent;
				Extent = Controller.BoundsUpperExtent;
			}

			UGeometryScriptLibrary_MeshDeformFunctions::ApplyTwistWarpToMesh(
				Mesh, Options, Controller.Orientation, Settings->TwistAngle * Controller.Density, Extent);
			break;
		}
		case EPCGUtilsWarpType::Flare:
		{
			FGeometryScriptFlareWarpOptions Options;
			Options.bSymmetricExtents = Settings->bFlareSymmetricExtents;
			Options.LowerExtent = Settings->FlareLowerExtent;
			Options.FlareType = Settings->FlareType;

			float Extent = Settings->FlareExtent;
			if (Controller.bHasBoundsExtents)
			{
				Options.bSymmetricExtents = false;
				Options.LowerExtent = Controller.BoundsLowerExtent;
				Extent = Controller.BoundsUpperExtent;
			}

			UGeometryScriptLibrary_MeshDeformFunctions::ApplyFlareWarpToMesh(
				Mesh, Options, Controller.Orientation,
				Settings->FlarePercentX * Controller.Density, Settings->FlarePercentY * Controller.Density, Extent);
			break;
		}
		default:
			checkNoEntry();
			break;
		}
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

TArray<FPCGPinProperties> UPCGWarpSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Add(FPCGUtilsMeshTargetFunctions::MakeMeshInputPinProperties(WarpMeshPin));
	if (ControlMode == EPCGUtilsWarpControlMode::PointData)
	{
		Pins.Emplace(WarpControlsPin, EPCGDataType::Point, true, true);
	}
	return Pins;
}

TArray<FPCGPinProperties> UPCGWarpSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(PCGPinConstants::DefaultOutputLabel, EPCGDataType::DynamicMesh, true, true)};
}

FPCGElementPtr UPCGWarpSettings::CreateElement() const
{
	return MakeShared<FPCGWarpElement>();
}

bool FPCGWarpElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGWarpElement::ExecuteInternal);
	check(Context);

	const UPCGWarpSettings* Settings = Context->GetInputSettings<UPCGWarpSettings>();
	check(Settings);

	// Controllers are resolved once and applied identically (and in the same order) to every Mesh input, rather
	// than being re-resolved per mesh.
	const TArray<FWarpController> Controllers = ResolveControllers(Context, Settings);

	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(WarpMeshPin))
	{
		FPCGUtilsMeshTargetHandle Handle = FPCGUtilsMeshTargetFunctions::CreateTarget(
			Input.Data, EPCGUtilsMeshTargetPreparation::FullMeshCopy, Context);
		if (!Handle.IsValid())
		{
			continue;
		}

		if (!Handle.IsEmptySelectionNoOp())
		{
			UDynamicMesh* Mesh = Handle.GetTargetMesh();
			for (const FWarpController& Controller : Controllers)
			{
				ApplyWarp(Mesh, Settings, Controller);
			}
		}

		FPCGUtilsMeshTargetFunctions::RestoreVertexPositions(Handle, Settings->SelectionBlend);
		FPCGUtilsMeshTargetFunctions::RecomputeSelectionAffectedNormals(Handle);
		FPCGUtilsMeshTargetFunctions::EmitOutput(Context, Input, Handle);
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
