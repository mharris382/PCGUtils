// Copyright Max Harris

#include "Elements/Topology/PCGDynMeshExtrudeToPaths.h"

#include "Data/PCGBasePointData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "Elements/Topology/PCGDynMeshFaceRegion.h"
#include "MeshRegionBoundaryLoops.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Operations/OffsetMeshRegion.h"
#include "PCGContext.h"
#include "PCGModule.h"
#include "PCGPin.h"
#include "Selections/MeshConnectedComponents.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshExtrudeToPaths"

namespace
{
	class FExtrudeToPathsOperation final : public FPCGUtilsDynMeshTopologyOperation
	{
	public:
		/** Mesh vertex ID -> where that rim vertex goes, in the mesh's own space. */
		TMap<int32, FVector3d> Targets;
		EPCGUtilsDynMeshExtrudeToPathsInterior InteriorVertices = EPCGUtilsDynMeshExtrudeToPathsInterior::Keep;
		EPCGUtilsDynMeshFaceRegionResult ResultRegion = EPCGUtilsDynMeshFaceRegionResult::Faces;
		float UVScale = 1.0f;

		virtual bool Apply(UE::Geometry::FDynamicMesh3& Mesh, const UE::Geometry::FGeometrySelection* Selection,
			TArray<int32>& OutTriangles) const override
		{
			using namespace UE::Geometry;

			TArray<TArray<int32>> Selected = PCGUtilsDynMeshFaceRegion::SplitAreas(Mesh, Selection, EGeometryScriptPolyOperationArea::EntireSelection);
			if (Selected.IsEmpty()) { return true; }

			// Each connected area is its own extrude, so its inside follows its own rim and not another area's.
			FMeshConnectedComponents Components(&Mesh);
			Components.FindConnectedTriangles(Selected[0]);
			TArray<TArray<int32>> Areas;
			for (const FMeshConnectedComponents::FComponent& Component : Components)
			{
				TArray<int32>& Area = Areas.Add_GetRef(Component.Indices);
				Area.Sort();
			}
			Areas.Sort([](const TArray<int32>& A, const TArray<int32>& B) { return A[0] < B[0]; });

			TSet<int32> RimVertices;
			int32 NumRimWithoutPoint = 0;
			int32 NumAreasWithoutRim = 0;
			for (TArray<int32>& Area : Areas)
			{
				FMeshRegionBoundaryLoops Loops(&Mesh, Area, true);
				TSet<int32> AreaRim;
				for (const FEdgeLoop& Loop : Loops.Loops) { AreaRim.Append(Loop.Vertices); }
				if (AreaRim.IsEmpty())
				{
					// A whole connected piece of mesh: nothing separates it from anything, so there is nothing to move.
					++NumAreasWithoutRim;
					PCGUtilsDynMeshFaceRegion::AppendResult(Area, {}, ResultRegion, OutTriangles);
					continue;
				}
				RimVertices.Append(AreaRim);

				// Separating the area gives its rim vertices new IDs, at the positions the old ones had. So the
				// offset is told where to go by position, and corrected by pairing afterwards for the one case
				// position cannot settle: two rim vertices in exactly the same place.
				TMap<FVector3d, FVector3d> TargetAt;
				FVector3d SumOffset = FVector3d::Zero();
				int32 NumMoved = 0;
				bool bSamePlaceDifferentTargets = false;
				for (const int32 VertexID : AreaRim)
				{
					const FVector3d* Target = Targets.Find(VertexID);
					if (!Target)
					{
						++NumRimWithoutPoint;
						continue;
					}
					const FVector3d Position = Mesh.GetVertex(VertexID);
					if (const FVector3d* Existing = TargetAt.Find(Position))
					{
						bSamePlaceDifferentTargets |= !Existing->Equals(*Target, 0.0);
					}
					else
					{
						TargetAt.Add(Position, *Target);
					}
					SumOffset += *Target - Position;
					++NumMoved;
				}
				const FVector3d InteriorOffset = InteriorVertices == EPCGUtilsDynMeshExtrudeToPathsInterior::FollowRim && NumMoved > 0
					? SumOffset / double(NumMoved) : FVector3d::Zero();

				const int32 FirstNewVertex = Mesh.MaxVertexID();
				FOffsetMeshRegion Offset(&Mesh);
				Offset.Triangles = MoveTemp(Area);
				Offset.ExtrusionVectorType = FOffsetMeshRegion::EVertexExtrusionVectorType::Zero;
				Offset.bOffsetFullComponentsAsSolids = false;
				Offset.UVScaleFactor = UVScale;
				Offset.OffsetPositionFunc = [&TargetAt, &AreaRim, InteriorOffset, FirstNewVertex](const FVector3d& Position, const FVector3d&, int32 VertexID)
				{
					// A rim vertex is either one made by the separation, or, on the mesh's own open edge, the original.
					if (VertexID >= FirstNewVertex || AreaRim.Contains(VertexID))
					{
						const FVector3d* Target = TargetAt.Find(Position);
						return Target ? *Target : Position;
					}
					return Position + InteriorOffset;
				};
				if (!Offset.Apply()) { return false; }

				for (const FOffsetMeshRegion::FOffsetInfo& Region : Offset.OffsetRegions)
				{
					if (bSamePlaceDifferentTargets && Region.BaseLoops.Num() == Region.OffsetLoops.Num())
					{
						for (int32 LoopIndex = 0; LoopIndex < Region.BaseLoops.Num(); ++LoopIndex)
						{
							const TArray<int32>& Base = Region.BaseLoops[LoopIndex].Vertices;
							const TArray<int32>& Moved = Region.OffsetLoops[LoopIndex].Vertices;
							for (int32 Index = 0; Index < Base.Num() && Base.Num() == Moved.Num(); ++Index)
							{
								const FVector3d* Target = Targets.Find(Base[Index]);
								if (!Target) { Target = Targets.Find(Moved[Index]); }
								if (Target) { Mesh.SetVertex(Moved[Index], *Target); }
							}
						}
					}
					PCGUtilsDynMeshFaceRegion::AppendResult(Region.OffsetTids, Region.StitchTriangles, ResultRegion, OutTriangles);
				}
			}

			// An operation has no graph to report on (it may run later, from a Builder), so these go to the log.
			int32 NumPointsOffRim = 0;
			for (const TPair<int32, FVector3d>& Target : Targets)
			{
				NumPointsOffRim += RimVertices.Contains(Target.Key) ? 0 : 1;
			}
			if (NumRimWithoutPoint > 0)
			{
				UE_LOG(LogPCG, Warning, TEXT("DynMesh | Extrude To Paths: %d of the selection's %d rim vertices are named by no path point and stayed in place, leaving a strip of no width there."),
					NumRimWithoutPoint, RimVertices.Num());
			}
			if (NumPointsOffRim > 0)
			{
				UE_LOG(LogPCG, Warning, TEXT("DynMesh | Extrude To Paths: %d of %d path points name a vertex that is not on the rim of the selection and were ignored. The paths must come from the same mesh and selection."),
					NumPointsOffRim, Targets.Num());
			}
			if (NumAreasWithoutRim > 0)
			{
				UE_LOG(LogPCG, Warning, TEXT("DynMesh | Extrude To Paths: %d selected area(s) are whole connected pieces of mesh with no rim, and were left as they are."), NumAreasWithoutRim);
			}
			return true;
		}
	};
}

#if WITH_EDITOR
FText UPCGDynMeshExtrudeToPathsSettings::GetDefaultNodeTitle() const { return LOCTEXT("Title", "DynMesh | Extrude To Paths"); }
FText UPCGDynMeshExtrudeToPathsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Extrudes or insets the selected faces to a rim supplied as paths: the faces are separated along their boundary, joined back with a strip of new faces, and each rim vertex is moved to the path point that names it by vertex ID. Use after DynMesh | Selection To Paths (Output Vertex Index on) and any path nodes that move the points. A rim vertex no point names stays in place. Requires a selection. Can emit the result as a Selector on the Result pin.");
}
#endif

TArray<FPCGPinProperties> UPCGDynMeshExtrudeToPathsSettings::InputPinProperties() const
{
	// Mutated mesh, then the required paths, then the optional Selector the base contributes.
	TArray<FPCGPinProperties> Pins = Super::InputPinProperties();
	FPCGPinProperties Paths(PCGDynMeshExtrudeToPathsConstants::PathsPin, EPCGDataType::Point, true, true,
		LOCTEXT("PathsTooltip", "Where the rim of the selected faces goes: points that each carry the mesh vertex ID they belong to. Normally the output of DynMesh | Selection To Paths with Output Vertex Index on, after path nodes have moved the points."));
	Paths.SetRequiredPin();
	Pins.Insert(MoveTemp(Paths), 1);
	return Pins;
}

TSharedPtr<FPCGUtilsDynMeshTopologyOperation> UPCGDynMeshExtrudeToPathsSettings::CreateTopologyOperation(FPCGContext* Context) const
{
	auto Operation = MakeShared<FExtrudeToPathsOperation>();
	Operation->InteriorVertices = InteriorVertices;
	Operation->ResultRegion = ResultRegion;
	Operation->UVScale = UVScale;
	if (!Context) { return Operation; }

	if (VertexIndexAttribute.IsNone())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("NoAttributeName", "DynMesh | Extrude To Paths: Vertex Index Attribute is None; it must name the int32 point attribute holding each point's mesh vertex ID. Nothing was moved."), Context);
		return Operation;
	}

	const FTransform MeshToWorld = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(Context, nullptr, bPathsInWorldSpace);
	const TArray<FPCGTaggedData> PathInputs = Context->InputData.GetInputsByPin(PCGDynMeshExtrudeToPathsConstants::PathsPin);
	for (int32 InputIndex = 0; InputIndex < PathInputs.Num(); ++InputIndex)
	{
		const UPCGBasePointData* Points = Cast<UPCGBasePointData>(PathInputs[InputIndex].Data);
		if (!Points)
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("NotPoints", "DynMesh | Extrude To Paths: Paths input {0} is not point data; skipped."), FText::AsNumber(InputIndex)), Context);
			continue;
		}
		const FPCGMetadataAttributeBase* AnyAttribute = Points->ConstMetadata() ? Points->ConstMetadata()->GetConstAttribute(VertexIndexAttribute) : nullptr;
		if (!AnyAttribute)
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("MissingAttribute", "DynMesh | Extrude To Paths: Paths input {0} has no point attribute '{1}' (Vertex Index Attribute); skipped. Turn on Output Vertex Index on DynMesh | Selection To Paths."),
				FText::AsNumber(InputIndex), FText::FromName(VertexIndexAttribute)), Context);
			continue;
		}
		const FPCGMetadataAttribute<int32>* VertexIndex = Points->ConstMetadata()->GetConstTypedAttribute<int32>(VertexIndexAttribute);
		if (!VertexIndex)
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("WrongAttributeType", "DynMesh | Extrude To Paths: point attribute '{1}' on Paths input {0} is not int32; skipped."),
				FText::AsNumber(InputIndex), FText::FromName(VertexIndexAttribute)), Context);
			continue;
		}

		const auto Transforms = Points->GetConstTransformValueRange();
		const auto Entries = Points->GetConstMetadataEntryValueRange();
		for (int32 PointIndex = 0; PointIndex < Points->GetNumPoints(); ++PointIndex)
		{
			const int32 VertexID = VertexIndex->GetValueFromItemKey(Entries[PointIndex]);
			if (VertexID < 0)
			{
				PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("NegativeVertex", "DynMesh | Extrude To Paths: Paths input {0} point {1} has '{2}' = {3}; a vertex ID is 0 or more. The point was ignored."),
					FText::AsNumber(InputIndex), FText::AsNumber(PointIndex), FText::FromName(VertexIndexAttribute), FText::AsNumber(VertexID)), Context);
				continue;
			}
			const FVector3d Position = MeshToWorld.InverseTransformPosition(Transforms[PointIndex].GetLocation());
			if (const FVector3d* Existing = Operation->Targets.Find(VertexID))
			{
				if (!Existing->Equals(Position, UE_DOUBLE_KINDA_SMALL_NUMBER))
				{
					PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("DuplicateVertex", "DynMesh | Extrude To Paths: Paths input {0} point {1} names vertex {2}, which an earlier point already placed somewhere else. The earlier point was kept."),
						FText::AsNumber(InputIndex), FText::AsNumber(PointIndex), FText::AsNumber(VertexID)), Context);
				}
				continue;
			}
			Operation->Targets.Add(VertexID, Position);
		}
	}
	return Operation;
}

#undef LOCTEXT_NAMESPACE
