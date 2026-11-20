// Copyright Max Harris

#include "Elements/Selections/PCGDynMeshInsideMeshSelectionFactory.h"
#include "Helpers/PCGUtilsParallelInputs.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "Distance/DistPoint3Triangle3.h"
#include "Distance/DistSegment3Triangle3.h"
#include "Distance/DistTriangle3Triangle3.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Serialization/ArchiveCrc32.h"
#include "Spatial/FastWinding.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshInsideMeshSelectionFactory"

namespace
{
	/** One Volume mesh with its spatial index, and the transform from the tested mesh's space into its space. */
	struct FVolume
	{
		TUniquePtr<UE::Geometry::FDynamicMeshAABBTree3> Tree;
		TUniquePtr<UE::Geometry::TFastWindingTree<UE::Geometry::FDynamicMesh3>> Winding;
		FTransform TestedToVolume = FTransform::Identity;
	};

	/** A mesh element's exact shape: 1 point (vertex), 2 (edge) or 3 (triangle), plus the center tested for inside. */
	struct FElementShape
	{
		FVector3d Points[3];
		FVector3d Center = FVector3d::Zero();
		int32 Count = 0;
	};

	class FInsideMeshSelectionOperation final : public FPCGUtilsDynMeshSelectionOperation
	{
	public:
		explicit FInsideMeshSelectionOperation(const UPCGDynMeshInsideMeshSelectionFactoryData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual bool Initialize(const FPCGUtilsDynMeshSelectionEvaluationContext& InSelectionContext) override
		{
			using namespace UE::Geometry;

			if (!FPCGUtilsDynMeshSelectionOperation::Initialize(InSelectionContext) || !Factory)
			{
				return false;
			}
			if (!FMath::IsFinite(Factory->Offset))
			{
				PCGLog::LogErrorOnGraph(LOCTEXT("NonFiniteOffset", "Select | Mesh Inclusion: Offset is not a finite number."), Context);
				return false;
			}
			if (!(Factory->WindingThreshold > 0.0 && Factory->WindingThreshold < 1.0))
			{
				PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("BadThreshold",
					"Select | Mesh Inclusion: Winding Threshold is {0}; it must be greater than 0 and less than 1."),
					FText::AsNumber(Factory->WindingThreshold)), Context);
				return false;
			}

			if (Factory->CheckType != EPCGDynMeshInclusionCheck::IsInside)
			{
				FText Error;
				if (!Factory->Expansion.TryReadDataValue(InSelectionContext.MeshData, Expansion, Error,
					LOCTEXT("ExpansionLabel", "Expansion")))
				{
					PCGLog::LogErrorOnGraph(Error, Context);
					return false;
				}
				if (!FMath::IsFinite(Expansion))
				{
					PCGLog::LogErrorOnGraph(LOCTEXT("NonFiniteExpansion", "Select | Mesh Inclusion: Expansion is not a finite number."), Context);
					return false;
				}
				if (Expansion < 0.0)
				{
					PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("NegativeExpansion",
						"Select | Mesh Inclusion: Expansion is {0}; it must be 0 or greater."),
						FText::AsNumber(Expansion)), Context);
					return false;
				}
			}

			const FTransform TestedActor = Factory->bMeshesAreActorLocal
				? PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(Context, InSelectionContext.MeshData, true)
				: FTransform::Identity;

			for (int32 Index = 0; Index < Factory->VolumeMeshes.Num(); ++Index)
			{
				const UPCGDynamicMeshData* VolumeData = Factory->VolumeMeshes[Index];
				const FDynamicMesh3* VolumeMesh = VolumeData && VolumeData->GetDynamicMesh()
					? VolumeData->GetDynamicMesh()->GetMeshPtr() : nullptr;
				if (!VolumeMesh || VolumeMesh->TriangleCount() == 0)
				{
					PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("EmptyVolume",
						"Select | Mesh Inclusion: Volume input {0} has no triangles and encloses nothing; it is ignored."),
						FText::AsNumber(Index)), Context);
					continue;
				}

				const FTransform VolumeActor = Factory->bMeshesAreActorLocal
					? PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(Context, VolumeData, true)
					: FTransform::Identity;

				FVolume& Volume = Volumes.Emplace_GetRef();
				Volume.Tree = MakeUnique<FDynamicMeshAABBTree3>(VolumeMesh, true);
				Volume.Winding = MakeUnique<TFastWindingTree<FDynamicMesh3>>(Volume.Tree.Get(), true);
				Volume.TestedToVolume = TestedActor * VolumeActor.Inverse();
			}

			if (Volumes.IsEmpty())
			{
				PCGLog::LogErrorOnGraph(LOCTEXT("NoVolumes",
					"Select | Mesh Inclusion needs at least one DynMesh with triangles on its Volume pin."), Context);
				return false;
			}
			return true;
		}

		virtual bool TestElement(int32 ElementID) const override
		{
			FElementShape Shape;
			if (!GetElementShape(ElementID, Shape))
			{
				return false;
			}

			// Same per-mesh logic and union semantics as PCGEx's FDynamicMeshFilter::TestQuery.
			const EPCGDynMeshInclusionCheck CheckType = Factory->CheckType;
			bool bFoundInside = false;
			for (const FVolume& Volume : Volumes)
			{
				FElementShape Local;
				Local.Count = Shape.Count;
				for (int32 i = 0; i < Shape.Count; ++i)
				{
					Local.Points[i] = FVector3d(Volume.TestedToVolume.TransformPosition(Shape.Points[i]));
				}
				Local.Center = FVector3d(Volume.TestedToVolume.TransformPosition(Shape.Center));

				const bool bInside = IsInside(Volume, Local.Center);
				switch (CheckType)
				{
				case EPCGDynMeshInclusionCheck::IsInside:
					if (bInside) { return true; }
					break;
				case EPCGDynMeshInclusionCheck::Intersects:
					if (!bInside && Crosses(Volume, Local)) { return true; }
					break;
				case EPCGDynMeshInclusionCheck::IsInsideOrIntersects:
					if (bInside || Crosses(Volume, Local)) { return true; }
					break;
				case EPCGDynMeshInclusionCheck::IsOutsideOrIntersects:
					if (bInside) { bFoundInside = true; }
					else if (Crosses(Volume, Local)) { return true; }
					break;
				case EPCGDynMeshInclusionCheck::IsFullyInside:
					if (bInside && !Crosses(Volume, Local)) { return true; }
					break;
				default:
					checkNoEntry();
					break;
				}
			}
			return CheckType == EPCGDynMeshInclusionCheck::IsOutsideOrIntersects && !bFoundInside;
		}

	private:
		bool IsInside(const FVolume& Volume, const FVector3d& Point) const
		{
			const bool bInside = Volume.Winding->IsInside(Point, Factory->WindingThreshold);
			const double Offset = Factory->Offset;
			if (Offset == 0.0 || (Offset > 0.0 && bInside) || (Offset < 0.0 && !bInside))
			{
				return bInside;
			}

			double DistanceSq = TNumericLimits<double>::Max();
			Volume.Tree->FindNearestTriangle(Point, DistanceSq);
			// Grown volume: outside points within Offset of the surface also count. Shrunk volume: inside points
			// must be deeper than |Offset|.
			return Offset > 0.0 ? DistanceSq <= Offset * Offset : DistanceSq > Offset * Offset;
		}

		/**
		 * True when the element comes within Expansion of any Volume triangle: it touches or crosses the surface,
		 * or (with Expansion) a whole Volume part sits inside the grown element.
		 */
		bool Crosses(const FVolume& Volume, const FElementShape& Shape) const
		{
			using namespace UE::Geometry;

			// A small tolerance so an element lying exactly on, or passing through, the surface counts at Expansion 0.
			const double Reach = Expansion + UE_KINDA_SMALL_NUMBER;
			FAxisAlignedBox3d ShapeBox = FAxisAlignedBox3d::Empty();
			for (int32 i = 0; i < Shape.Count; ++i)
			{
				ShapeBox.Contain(Shape.Points[i]);
			}
			ShapeBox.Expand(Reach);
			if (!Volume.Tree->GetBoundingBox().Intersects(ShapeBox))
			{
				return false;
			}

			const FDynamicMesh3* VolumeMesh = Volume.Tree->GetMesh();
			const double ReachSq = Reach * Reach;
			bool bHit = false;
			FDynamicMeshAABBTree3::FTreeTraversal Traversal;
			Traversal.NextBoxF = [&bHit, &ShapeBox](const FAxisAlignedBox3d& NodeBox, int32)
			{
				return !bHit && NodeBox.Intersects(ShapeBox);
			};
			Traversal.NextTriangleF = [&](int32 TriangleID)
			{
				if (bHit)
				{
					return;
				}
				FVector3d A, B, C;
				VolumeMesh->GetTriVertices(TriangleID, A, B, C);
				const FTriangle3d VolumeTriangle(A, B, C);
				double DistanceSq = TNumericLimits<double>::Max();
				switch (Shape.Count)
				{
				case 1:
					DistanceSq = FDistPoint3Triangle3d(Shape.Points[0], VolumeTriangle).GetSquared();
					break;
				case 2:
					DistanceSq = FDistSegment3Triangle3d(FSegment3d(Shape.Points[0], Shape.Points[1]), VolumeTriangle).GetSquared();
					break;
				default:
					DistanceSq = FDistTriangle3Triangle3d(FTriangle3d(Shape.Points[0], Shape.Points[1], Shape.Points[2]), VolumeTriangle).GetSquared();
					break;
				}
				bHit = DistanceSq <= ReachSq;
			};
			Volume.Tree->DoTraversal(Traversal);
			return bHit;
		}

		bool GetElementShape(int32 ElementID, FElementShape& OutShape) const
		{
			const UE::Geometry::FDynamicMesh3& Mesh = SelectionContext->Mesh;
			switch (SelectionContext->Domain.ElementType)
			{
			case UE::Geometry::EGeometryElementType::Vertex:
				if (!Mesh.IsVertex(ElementID)) { return false; }
				OutShape.Count = 1;
				OutShape.Points[0] = Mesh.GetVertex(ElementID);
				OutShape.Center = OutShape.Points[0];
				return true;
			case UE::Geometry::EGeometryElementType::Edge:
			{
				if (!Mesh.IsEdge(ElementID)) { return false; }
				const UE::Geometry::FIndex2i Edge = Mesh.GetEdgeV(ElementID);
				OutShape.Count = 2;
				OutShape.Points[0] = Mesh.GetVertex(Edge.A);
				OutShape.Points[1] = Mesh.GetVertex(Edge.B);
				OutShape.Center = (OutShape.Points[0] + OutShape.Points[1]) * 0.5;
				return true;
			}
			case UE::Geometry::EGeometryElementType::Face:
				if (!Mesh.IsTriangle(ElementID)) { return false; }
				OutShape.Count = 3;
				Mesh.GetTriVertices(ElementID, OutShape.Points[0], OutShape.Points[1], OutShape.Points[2]);
				OutShape.Center = Mesh.GetTriCentroid(ElementID);
				return true;
			default:
				return false;
			}
		}

		TObjectPtr<const UPCGDynMeshInsideMeshSelectionFactoryData> Factory;
		TArray<FVolume> Volumes;
		/** Resolved once per evaluation from the Expansion shorthand (constant or the tested mesh's @Data). */
		double Expansion = 0.0;
	};
}

bool UPCGDynMeshInsideMeshSelectionFactoryData::SupportsDomain(const FPCGUtilsDynMeshSelectionDomain& Domain) const
{
	return Domain.TopologyType == UE::Geometry::EGeometryTopologyType::Triangle &&
		(Domain.ElementType == UE::Geometry::EGeometryElementType::Vertex ||
		 Domain.ElementType == UE::Geometry::EGeometryElementType::Edge ||
		 Domain.ElementType == UE::Geometry::EGeometryElementType::Face);
}

TSharedPtr<FPCGUtilsDynMeshSelectionOperation> UPCGDynMeshInsideMeshSelectionFactoryData::CreateOperationInternal() const
{
	return MakeShared<FInsideMeshSelectionOperation>(this);
}

void UPCGDynMeshInsideMeshSelectionFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc)
	{
		double ThresholdValue = WindingThreshold;
		double OffsetValue = Offset;
		bool bLocal = bMeshesAreActorLocal;
		uint8 CheckValue = static_cast<uint8>(CheckType);
		uint8 ExpansionInput = static_cast<uint8>(Expansion.Input);
		double ExpansionConstant = Expansion.Constant;
		FString ExpansionAttribute = Expansion.Attribute.ToString();
		Ar << CheckValue;
		Ar << ExpansionInput;
		Ar << ExpansionConstant;
		Ar << ExpansionAttribute;
		Ar << ThresholdValue;
		Ar << OffsetValue;
		Ar << bLocal;
	}
}

#if WITH_EDITOR
FText UPCGDynMeshInsideMeshSelectionFactoryProviderSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Select | Mesh Inclusion");
}

FText UPCGDynMeshInsideMeshSelectionFactoryProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Selects the vertices, edges or triangles that are inside, crossing the surface of, or fully inside the volume "
		"enclosed by the DynMeshes on the Volume pin (see Check Type; mirrors PCGEx Filter : Inclusion (Dynamic Mesh)). "
		"Inside is the winding number at each element's center; crossing tests the element's exact shape grown by "
		"Expansion. Offset grows or shrinks the volume for the inside test. Invert Selection complements the result.");
}

FString UPCGDynMeshInsideMeshSelectionFactoryProviderSettings::GetAdditionalTitleInformation() const
{
	const TCHAR* Check = TEXT("Inside");
	switch (CheckType)
	{
	case EPCGDynMeshInclusionCheck::Intersects: Check = TEXT("Intersects"); break;
	case EPCGDynMeshInclusionCheck::IsInsideOrIntersects: Check = TEXT("Inside or Intersects"); break;
	case EPCGDynMeshInclusionCheck::IsOutsideOrIntersects: Check = TEXT("Outside or Intersects"); break;
	case EPCGDynMeshInclusionCheck::IsFullyInside: Check = TEXT("Fully Inside"); break;
	default: break;
	}
	FString Title = bInvertSelection ? FString::Printf(TEXT("Not %s"), Check) : FString(Check);
	if (Offset != 0.0)
	{
		Title += FString::Printf(TEXT(" %+g"), Offset);
	}
	return Title;
}
#endif

const FPCGDataTypeBaseId& UPCGDynMeshInsideMeshSelectionFactoryProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId();
}

TArray<FPCGPinProperties> UPCGDynMeshInsideMeshSelectionFactoryProviderSettings::SourceInputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGDynMeshInsideMeshSelectionFactoryConstants::VolumeInputPin,
		EPCGDataType::DynamicMesh, true, true).SetRequiredPin();
	return Pins;
}

UPCGUtilsDynMeshFactoryData* UPCGDynMeshInsideMeshSelectionFactoryProviderSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	TArray<TObjectPtr<const UPCGDynamicMeshData>> Volumes;
	for (const FPCGTaggedData& Input : InContext->InputData.GetInputsByPin(
		PCGDynMeshInsideMeshSelectionFactoryConstants::VolumeInputPin))
	{
		if (const UPCGDynamicMeshData* MeshData = Cast<const UPCGDynamicMeshData>(Input.Data))
		{
			Volumes.Add(MeshData);
		}
	}

	if (Volumes.IsEmpty())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("MissingVolume",
			"Select | Mesh Inclusion needs DynMesh data on its Volume pin."), InContext);
		return nullptr;
	}

	UPCGDynMeshInsideMeshSelectionFactoryData* Factory = InFactory
		? Cast<UPCGDynMeshInsideMeshSelectionFactoryData>(InFactory)
		: PCGUtilsAsyncObjects::NewObject<UPCGDynMeshInsideMeshSelectionFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Priority = Priority;
	Factory->VolumeMeshes = MoveTemp(Volumes);
	Factory->CheckType = CheckType;
	Factory->Expansion = Expansion;
	Factory->WindingThreshold = WindingThreshold;
	Factory->Offset = Offset;
	Factory->bMeshesAreActorLocal = bMeshesAreActorLocal;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE
