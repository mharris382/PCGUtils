// Copyright Max Harris

#include "Elements/Selections/PCGDynMeshInsideMeshSelectionFactory.h"
#include "Helpers/PCGUtilsParallelInputs.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
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
				PCGLog::LogErrorOnGraph(LOCTEXT("NonFiniteOffset", "Select | Inside Mesh: Offset is not a finite number."), Context);
				return false;
			}
			if (!(Factory->WindingThreshold > 0.0 && Factory->WindingThreshold < 1.0))
			{
				PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("BadThreshold",
					"Select | Inside Mesh: Winding Threshold is {0}; it must be greater than 0 and less than 1."),
					FText::AsNumber(Factory->WindingThreshold)), Context);
				return false;
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
						"Select | Inside Mesh: Volume input {0} has no triangles and encloses nothing; it is ignored."),
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
					"Select | Inside Mesh needs at least one DynMesh with triangles on its Volume pin."), Context);
				return false;
			}
			return true;
		}

		virtual bool TestElement(int32 ElementID) const override
		{
			FVector3d Center;
			if (!GetElementCenter(ElementID, Center))
			{
				return false;
			}
			for (const FVolume& Volume : Volumes)
			{
				if (IsInside(Volume, FVector3d(Volume.TestedToVolume.TransformPosition(Center))))
				{
					return true;
				}
			}
			return false;
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

		bool GetElementCenter(int32 ElementID, FVector3d& OutCenter) const
		{
			const UE::Geometry::FDynamicMesh3& Mesh = SelectionContext->Mesh;
			switch (SelectionContext->Domain.ElementType)
			{
			case UE::Geometry::EGeometryElementType::Vertex:
				if (!Mesh.IsVertex(ElementID)) { return false; }
				OutCenter = Mesh.GetVertex(ElementID);
				return true;
			case UE::Geometry::EGeometryElementType::Edge:
			{
				if (!Mesh.IsEdge(ElementID)) { return false; }
				const UE::Geometry::FIndex2i Edge = Mesh.GetEdgeV(ElementID);
				OutCenter = (Mesh.GetVertex(Edge.A) + Mesh.GetVertex(Edge.B)) * 0.5;
				return true;
			}
			case UE::Geometry::EGeometryElementType::Face:
				if (!Mesh.IsTriangle(ElementID)) { return false; }
				OutCenter = Mesh.GetTriCentroid(ElementID);
				return true;
			default:
				return false;
			}
		}

		TObjectPtr<const UPCGDynMeshInsideMeshSelectionFactoryData> Factory;
		TArray<FVolume> Volumes;
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
		Ar << ThresholdValue;
		Ar << OffsetValue;
		Ar << bLocal;
	}
}

#if WITH_EDITOR
FText UPCGDynMeshInsideMeshSelectionFactoryProviderSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Select | Inside Mesh");
}

FText UPCGDynMeshInsideMeshSelectionFactoryProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Selects the vertices, edges or triangles that lie inside the volume enclosed by the DynMeshes on the Volume "
		"pin (winding number, tested at each element's center). Invert Selection selects what lies outside. Offset "
		"grows or shrinks the volume by a distance from its surface.");
}

FString UPCGDynMeshInsideMeshSelectionFactoryProviderSettings::GetAdditionalTitleInformation() const
{
	const TCHAR* Side = bInvertSelection ? TEXT("Outside") : TEXT("Inside");
	return Offset == 0.0 ? FString(Side) : FString::Printf(TEXT("%s %+g"), Side, Offset);
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
			"Select | Inside Mesh needs DynMesh data on its Volume pin."), InContext);
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
	Factory->WindingThreshold = WindingThreshold;
	Factory->Offset = Offset;
	Factory->bMeshesAreActorLocal = bMeshesAreActorLocal;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE
