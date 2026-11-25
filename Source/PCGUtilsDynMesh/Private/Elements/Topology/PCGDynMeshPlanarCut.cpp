// Copyright Max Harris

#include "Elements/Topology/PCGDynMeshPlanarCut.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Topology/PCGDynMeshBoolean.h"
#include "Factories/PCGUtilsDynMeshSelectionFactory.h"
#include "Helpers/PCGUtilsParallelInputs.h"
#include "MeshTarget/PCGUtilsMeshTargetFunctions.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Serialization/ArchiveCrc32.h"
#include "UDynamicMesh.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshPlanarCut"

namespace
{
	/** What a planar cut left on a mesh, found from the result: the cut outline and the faces capping it. */
	struct FPlanarCutRegions
	{
		TSet<int32> CutEdges;
		TSet<int32> CapTriangles;
	};

	/**
	 * Geometry Script's plane cut reports nothing about what it made, so the cut is read back from the result. Every
	 * vertex the cut creates lies on the plane, and so does every face that fills it:
	 *   - a cap face has all three corners on the plane;
	 *   - a cut edge has both ends on the plane and is either open (nothing filled it) or has a cap face on exactly
	 *     one side. An edge with cap faces on both sides is inside the cap; an edge with none and two neighbours is
	 *     a ridge of the mesh that merely touches the plane.
	 */
	void FindPlanarCutRegions(const UE::Geometry::FDynamicMesh3& Mesh, const FTransform& Plane, FPlanarCutRegions& OutRegions)
	{
		using namespace UE::Geometry;
		const FVector3d Origin = Plane.GetLocation();
		const FVector3d Normal = Plane.GetUnitAxis(EAxis::Z);
		// Cut vertices are placed on the plane to within rounding; the slack allows for large coordinates.
		const double Tolerance = FMath::Max(1.e-4, 1.e-7 * Mesh.GetBounds().MaxDim());

		TBitArray<> OnPlane(false, Mesh.MaxVertexID());
		for (const int32 VID : Mesh.VertexIndicesItr())
		{
			OnPlane[VID] = FMath::Abs((Mesh.GetVertex(VID) - Origin).Dot(Normal)) <= Tolerance;
		}
		auto IsCap = [&Mesh, &OnPlane](int32 TID)
		{
			if (!Mesh.IsTriangle(TID)) { return false; }
			const FIndex3i V = Mesh.GetTriangle(TID);
			return OnPlane[V.A] && OnPlane[V.B] && OnPlane[V.C];
		};

		for (const int32 TID : Mesh.TriangleIndicesItr())
		{
			if (IsCap(TID)) { OutRegions.CapTriangles.Add(TID); }
		}
		for (const int32 EID : Mesh.EdgeIndicesItr())
		{
			const FIndex2i V = Mesh.GetEdgeV(EID);
			if (!OnPlane[V.A] || !OnPlane[V.B]) { continue; }
			const FIndex2i T = Mesh.GetEdgeT(EID);
			if (T.B == FDynamicMesh3::InvalidID ? !IsCap(T.A) : (IsCap(T.A) != IsCap(T.B)))
			{
				OutRegions.CutEdges.Add(EID);
			}
		}
	}

	/** One selector per enabled category for one result mesh. The data class is the Boolean's: a mesh and ID sets. */
	void PlanarCutEmitSelectors(const UPCGDynamicMeshData* Reference, const FPlanarCutRegions& Regions, bool bCut, bool bFill,
		FName CutTag, FName FillTag, FPCGContext* Context, FPCGUtilsDynMeshProcessOutcome& Outcome)
	{
		using namespace UE::Geometry;
		const FDynamicMesh3& Mesh = *Reference->GetDynamicMesh()->GetMeshPtr();
		for (const bool bRim : { true, false })
		{
			if (bRim ? !bCut : !bFill) { continue; }
			auto* Selector = PCGUtilsAsyncObjects::NewObject<UPCGDynMeshBooleanSelectionFactoryData>(Context);
			Selector->ReferenceMesh = Reference;
			Selector->RouteMesh = Reference;
			// The category flag: true for the cut outline, false for the cap.
			Selector->bOverlapping = bRim;
			Selector->RimEdgeIDs = Regions.CutEdges;
			if (bRim)
			{
				for (const int32 EID : Regions.CutEdges)
				{
					Selector->EdgeIDs.Add(EID);
					const FIndex2i V = Mesh.GetEdgeV(EID), T = Mesh.GetEdgeT(EID);
					Selector->VertexIDs.Add(V.A); Selector->VertexIDs.Add(V.B);
					if (Mesh.IsTriangle(T.A)) { Selector->TriangleIDs.Add(T.A); }
					if (Mesh.IsTriangle(T.B)) { Selector->TriangleIDs.Add(T.B); }
				}
			}
			else
			{
				for (const int32 TID : Regions.CapTriangles)
				{
					Selector->TriangleIDs.Add(TID);
					const FIndex3i V = Mesh.GetTriangle(TID), E = Mesh.GetTriEdges(TID);
					for (int32 C = 0; C < 3; ++C) { Selector->VertexIDs.Add(V[C]); Selector->EdgeIDs.Add(E[C]); }
				}
			}
			FPCGUtilsDynMeshAuxiliaryDataOutput& Output = Outcome.AuxiliaryDataOutputs.Emplace_GetRef();
			Output.Pin = PCGUtilsDynMeshSelectionFactoryConstants::OutputPin;
			Output.Data = Selector;
			const FName Tag = bRim ? CutTag : FillTag;
			if (!Tag.IsNone()) { Output.Tags.Add(Tag.ToString()); }
		}
	}

	class FDynMeshPlanarCutOperation final : public FPCGUtilsDynMeshProcessOperation
	{
	public:
		EPCGUtilsPlaneTransformMode TransformMode = EPCGUtilsPlaneTransformMode::Explicit;
		FTransform PlaneTransform = FTransform::Identity;
		FPCGUtilsBoundsRelativeTransformDetails BoundsPlacement;
		FGeometryScriptMeshPlaneCutOptions Options;
		bool bOutputSelectors = false;
		bool bCut = true;
		bool bFill = true;
		FName CutTag;
		FName FillTag;

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
				// Nothing was cut. Still emit (empty) selectors, so a downstream Selector input does not become
				// disconnected in effect and process the whole mesh.
				if (bOutputSelectors && Invocation.MeshData)
				{
					PlanarCutEmitSelectors(Invocation.MeshData, {}, bCut, bFill, CutTag, FillTag, Invocation.Context, OutOutcome);
				}
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

			if (bOutputSelectors && Invocation.MeshData && Invocation.MeshData->GetDynamicMesh())
			{
				// Read from the finished mesh, after a cut region has been put back: the IDs are the output's.
				FPlanarCutRegions Regions;
				FindPlanarCutRegions(*Invocation.MeshData->GetDynamicMesh()->GetMeshPtr(), ResolvedPlane, Regions);
				PlanarCutEmitSelectors(Invocation.MeshData, Regions, bCut, bFill, CutTag, FillTag, Invocation.Context, OutOutcome);
			}
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
		"the result into the untouched mesh. Output Selectors adds a pin carrying tagged selectors of the result: Cut "
		"is the outline the plane left on the mesh (the cross section, as edges), Fill is the cap when Fill Holes is on.");
}
#endif

TSharedPtr<const FPCGUtilsDynMeshProcessOperation> UPCGDynMeshPlanarCutSettings::CreateProcessOperation(FPCGContext*) const
{
	TSharedPtr<FDynMeshPlanarCutOperation> Operation = MakeShared<FDynMeshPlanarCutOperation>();
	Operation->TransformMode = TransformMode;
	Operation->PlaneTransform = PlaneTransform;
	Operation->BoundsPlacement = BoundsPlacement;
	Operation->Options = Options;
	Operation->bOutputSelectors = bOutputSelectors;
	Operation->bCut = bCutSelector;
	// Without Fill Holes the cut has no cap to select.
	Operation->bFill = bFillSelector && Options.bFillHoles;
	Operation->CutTag = CutSelectorTag;
	Operation->FillTag = FillSelectorTag;
	return Operation;
}

FPCGElementPtr UPCGDynMeshPlanarCutSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshPlanarCutElement>();
}

TArray<FPCGPinProperties> UPCGDynMeshPlanarCutSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins = Super::OutputPinProperties();
	if (bOutputSelectors)
	{
		Pins.Emplace(PCGUtilsDynMeshSelectionFactoryConstants::OutputPin,
			FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId(), true, true,
			LOCTEXT("SelectorPin", "One tagged selector per category (Cut, Fill) across all results. Each selector routes to the matching mesh. Filter by category tag before connecting to a Selector input."));
	}
	return Pins;
}

FPCGDataTypeIdentifier UPCGDynMeshPlanarCutSettings::GetCurrentPinTypesID(const UPCGPin* Pin) const
{
	if (Pin && Pin->IsOutputPin() && Pin->Properties.Label == PCGUtilsDynMeshSelectionFactoryConstants::OutputPin)
	{
		return FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId();
	}
	return Super::GetCurrentPinTypesID(Pin);
}

void UPCGDynMeshPlanarCutSettings::EmitAdditionalOutputs(FPCGContext* Context) const
{
	if (bOutputSelectors)
	{
		UPCGDynMeshBooleanSelectionFactoryData::BundleByCategory(Context);
	}
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
