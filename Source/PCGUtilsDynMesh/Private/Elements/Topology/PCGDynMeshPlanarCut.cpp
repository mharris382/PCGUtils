// Copyright Max Harris

#include "Elements/Topology/PCGDynMeshPlanarCut.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "MeshTarget/PCGUtilsMeshTargetFunctions.h"
#include "PCGContext.h"
#include "Serialization/ArchiveCrc32.h"
#include "UDynamicMesh.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshPlanarCut"

namespace
{
	class FDynMeshPlanarCutOperation final : public FPCGUtilsDynMeshProcessOperation
	{
	public:
		EPCGUtilsPlaneTransformMode TransformMode = EPCGUtilsPlaneTransformMode::Explicit;
		FTransform PlaneTransform = FTransform::Identity;
		FPCGUtilsBoundsRelativeTransformDetails BoundsPlacement;
		FGeometryScriptMeshPlaneCutOptions Options;

		virtual bool Execute(const FPCGUtilsDynMeshProcessInvocation& Invocation,
			FPCGUtilsDynMeshProcessOutcome& OutOutcome) const override
		{
			FPCGUtilsMeshTargetHandle Handle = FPCGUtilsMeshTargetFunctions::CreateTargetInPlace(
				Invocation, EPCGUtilsMeshTargetPreparation::Region);
			if (!Handle.IsValid())
			{
				return false;
			}
			if (Handle.IsEmptySelectionNoOp())
			{
				return true;
			}
			UDynamicMesh* Target = Handle.GetTargetMesh();

			FTransform ResolvedPlane = PlaneTransform;
			if (TransformMode == EPCGUtilsPlaneTransformMode::BoundsRelative)
			{
				FBox Bounds(ForceInit);
				Target->ProcessMesh([&Bounds](const UE::Geometry::FDynamicMesh3& Mesh)
				{
					const UE::Geometry::FAxisAlignedBox3d MeshBounds = Mesh.GetBounds();
					if (!MeshBounds.IsEmpty())
					{
						Bounds = FBox(FVector(MeshBounds.Min), FVector(MeshBounds.Max));
					}
				});
				ResolvedPlane = BoundsPlacement.ComputeTransform(Bounds);
			}

			UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshPlaneCut(Target, ResolvedPlane, Options);
			FPCGUtilsMeshTargetFunctions::RestoreRegion(Handle);
			OutOutcome.SelectionOutcome = EPCGUtilsDynMeshProcessSelectionOutcome::Clear;
			return true;
		}
	};
}

#if WITH_EDITOR
FText UPCGDynMeshPlanarCutSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DynMesh | Planar Cut");
}

FText UPCGDynMeshPlanarCutSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Cuts away one side of a DynMesh with an infinite plane. Use an explicit local-space transform, or place "
		"the plane against each target's bounds with Builder-style alignment, asymmetric padding, and a local "
		"offset/rotation. A DynMesh Selection or connected Selector cuts only that extracted region and reinserts "
		"the result into the untouched mesh.");
}
#endif

TSharedPtr<const FPCGUtilsDynMeshProcessOperation> UPCGDynMeshPlanarCutSettings::CreateProcessOperation(FPCGContext*) const
{
	TSharedPtr<FDynMeshPlanarCutOperation> Operation = MakeShared<FDynMeshPlanarCutOperation>();
	Operation->TransformMode = TransformMode;
	Operation->PlaneTransform = PlaneTransform;
	Operation->BoundsPlacement = BoundsPlacement;
	Operation->Options = Options;
	return Operation;
}

void UPCGDynMeshPlanarCutSettings::AddProcessOperationToCrc(FArchiveCrc32& Ar) const
{
	Super::AddProcessOperationToCrc(Ar);
	uint8 LocalMode = static_cast<uint8>(TransformMode);
	Ar << LocalMode;
	if (TransformMode == EPCGUtilsPlaneTransformMode::Explicit)
	{
		FTransform LocalTransform = PlaneTransform;
		Ar << LocalTransform;
	}
	else
	{
		BoundsPlacement.AddToCrc(Ar);
	}
	FGeometryScriptMeshPlaneCutOptions LocalOptions = Options;
	Ar << LocalOptions.bFillHoles << LocalOptions.HoleFillMaterialID << LocalOptions.bFillSpans
		<< LocalOptions.bFlipCutSide << LocalOptions.UVWorldDimension;
}

#undef LOCTEXT_NAMESPACE
