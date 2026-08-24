// Copyright Max Harris

#include "Factories/PCGUtilsDynMeshDomainSelectionFactory.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "GeometryScript/MeshSelectionFunctions.h"
#include "PCGContext.h"
#include "Serialization/ArchiveCrc32.h"
#include "UDynamicMesh.h"

namespace
{
	bool IsSupportedElementType(UE::Geometry::EGeometryElementType ElementType)
	{
		return ElementType == UE::Geometry::EGeometryElementType::Vertex ||
			ElementType == UE::Geometry::EGeometryElementType::Edge ||
			ElementType == UE::Geometry::EGeometryElementType::Face;
	}

	EGeometryScriptMeshSelectionType ToScriptSelectionType(
		UE::Geometry::EGeometryElementType ElementType)
	{
		switch (ElementType)
		{
		case UE::Geometry::EGeometryElementType::Vertex:
			return EGeometryScriptMeshSelectionType::Vertices;
		case UE::Geometry::EGeometryElementType::Edge:
			return EGeometryScriptMeshSelectionType::Edges;
		case UE::Geometry::EGeometryElementType::Face:
		default:
			return EGeometryScriptMeshSelectionType::Triangles;
		}
	}

	EGeometryScriptIndexType ToScriptIndexType(UE::Geometry::EGeometryElementType ElementType)
	{
		switch (ElementType)
		{
		case UE::Geometry::EGeometryElementType::Vertex:
			return EGeometryScriptIndexType::Vertex;
		case UE::Geometry::EGeometryElementType::Edge:
			return EGeometryScriptIndexType::Edge;
		case UE::Geometry::EGeometryElementType::Face:
		default:
			return EGeometryScriptIndexType::Triangle;
		}
	}

	void AddElement(
		const UE::Geometry::FDynamicMesh3& Mesh,
		UE::Geometry::EGeometryElementType ElementType,
		int32 ElementID,
		UE::Geometry::FGeometrySelection& OutSelection)
	{
		using namespace UE::Geometry;
		switch (ElementType)
		{
		case EGeometryElementType::Vertex:
			if (Mesh.IsVertex(ElementID))
			{
				OutSelection.Selection.Add(FGeoSelectionID::MeshVertex(ElementID).Encoded());
			}
			break;
		case EGeometryElementType::Edge:
			if (Mesh.IsEdge(ElementID))
			{
				Mesh.EnumerateTriEdgeIDsFromEdgeID(ElementID, [&OutSelection](FMeshTriEdgeID TriEdgeID)
				{
					OutSelection.Selection.Add(FGeoSelectionID::MeshEdge(TriEdgeID).Encoded());
				});
			}
			break;
		case EGeometryElementType::Face:
			if (Mesh.IsTriangle(ElementID))
			{
				OutSelection.Selection.Add(FGeoSelectionID::MeshTriangle(ElementID).Encoded());
			}
			break;
		default:
			break;
		}
	}

	void MaterializeNativeOperation(
		const FPCGUtilsDynMeshSelectionOperation& Operation,
		const UE::Geometry::FDynamicMesh3& Mesh,
		UE::Geometry::EGeometryElementType ElementType,
		UE::Geometry::FGeometrySelection& OutSelection)
	{
		using namespace UE::Geometry;
		OutSelection.InitializeTypes(ElementType, EGeometryTopologyType::Triangle);
		if (ElementType == EGeometryElementType::Vertex)
		{
			for (const int32 VertexID : Mesh.VertexIndicesItr())
			{
				if (Operation.TestElement(VertexID))
				{
					AddElement(Mesh, ElementType, VertexID, OutSelection);
				}
			}
		}
		else if (ElementType == EGeometryElementType::Edge)
		{
			for (const int32 EdgeID : Mesh.EdgeIndicesItr())
			{
				if (Operation.TestElement(EdgeID))
				{
					AddElement(Mesh, ElementType, EdgeID, OutSelection);
				}
			}
		}
		else
		{
			for (const int32 TriangleID : Mesh.TriangleIndicesItr())
			{
				if (Operation.TestElement(TriangleID))
				{
					AddElement(Mesh, ElementType, TriangleID, OutSelection);
				}
			}
		}
	}

	class FDomainSelectionAdapterOperation final : public FPCGUtilsDynMeshSelectionOperation
	{
	public:
		explicit FDomainSelectionAdapterOperation(
			const UPCGUtilsDynMeshDomainSelectionFactoryData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual bool Initialize(const FPCGUtilsDynMeshSelectionEvaluationContext& InSelectionContext) override
		{
			if (!FPCGUtilsDynMeshSelectionOperation::Initialize(InSelectionContext) ||
				!Factory || !InSelectionContext.MeshData)
			{
				return false;
			}

			const UE::Geometry::EGeometryElementType NativeElementType = Factory->GetNativeElementType();
			FPCGUtilsDynMeshSelectionDomain NativeDomain;
			NativeDomain.ElementType = NativeElementType;
			NativeDomain.TopologyType = UE::Geometry::EGeometryTopologyType::Triangle;
			NativeEvaluationContext = MakeUnique<FPCGUtilsDynMeshSelectionEvaluationContext>(
				InSelectionContext.MeshData, InSelectionContext.Mesh, NativeDomain);

			NativeOperation = Factory->CreateNativeOperation(Context);
			if (!NativeOperation || !NativeOperation->Initialize(*NativeEvaluationContext))
			{
				return false;
			}

			if (InSelectionContext.Domain.ElementType == NativeElementType)
			{
				bUseNativeOperation = true;
				return true;
			}

			UE::Geometry::FGeometrySelection NativeSelection;
			MaterializeNativeOperation(
				*NativeOperation, InSelectionContext.Mesh, NativeElementType, NativeSelection);
			UE::Geometry::FGeometrySelection ConvertedSelection;
			if (!PCGUtilsDynMeshSelectionDomains::ConvertSelection(
				InSelectionContext.MeshData, InSelectionContext.Mesh, NativeSelection,
				InSelectionContext.Domain.ElementType, Factory->bAllowPartialInclusion,
				ConvertedSelection))
			{
				return false;
			}

			FGeometryScriptMeshSelection ScriptSelection;
			ScriptSelection.SetSelection(ConvertedSelection);
			TArray<int32> ConvertedIDs;
			const EGeometryScriptIndexType RequestedIndexType =
				ToScriptIndexType(InSelectionContext.Domain.ElementType);
			if (ScriptSelection.ConvertToMeshIndexArray(
				InSelectionContext.Mesh, ConvertedIDs, RequestedIndexType) != RequestedIndexType)
			{
				return false;
			}
			ConvertedElementIDs.Append(ConvertedIDs);
			return true;
		}

		virtual bool TestElement(int32 ElementID) const override
		{
			return bUseNativeOperation
				? NativeOperation->TestElement(ElementID)
				: ConvertedElementIDs.Contains(ElementID);
		}

	private:
		TObjectPtr<const UPCGUtilsDynMeshDomainSelectionFactoryData> Factory;
		TUniquePtr<FPCGUtilsDynMeshSelectionEvaluationContext> NativeEvaluationContext;
		TSharedPtr<FPCGUtilsDynMeshSelectionOperation> NativeOperation;
		TSet<int32> ConvertedElementIDs;
		bool bUseNativeOperation = false;
	};
}

bool PCGUtilsDynMeshSelectionDomains::ConvertSelection(
	const UPCGDynamicMeshData* MeshData,
	const UE::Geometry::FDynamicMesh3& Mesh,
	const UE::Geometry::FGeometrySelection& FromSelection,
	UE::Geometry::EGeometryElementType ToElementType,
	bool bAllowPartialInclusion,
	UE::Geometry::FGeometrySelection& OutSelection)
{
	using namespace UE::Geometry;
	if (!MeshData || FromSelection.TopologyType != EGeometryTopologyType::Triangle ||
		!IsSupportedElementType(FromSelection.ElementType) || !IsSupportedElementType(ToElementType))
	{
		return false;
	}

	if (FromSelection.ElementType == ToElementType)
	{
		OutSelection = FromSelection;
		return true;
	}

	OutSelection.InitializeTypes(ToElementType, EGeometryTopologyType::Triangle);
	if (FromSelection.IsEmpty())
	{
		return true;
	}

	const UDynamicMesh* DynamicMesh = MeshData->GetDynamicMesh();
	if (!DynamicMesh)
	{
		return false;
	}

	FGeometryScriptMeshSelection ScriptFrom;
	ScriptFrom.SetSelection(FromSelection);
	FGeometryScriptMeshSelection ScriptTo;
	UGeometryScriptLibrary_MeshSelectionFunctions::ConvertMeshSelection(
		const_cast<UDynamicMesh*>(DynamicMesh), ScriptFrom, ScriptTo,
		ToScriptSelectionType(ToElementType), bAllowPartialInclusion);

	TArray<int32> ConvertedIDs;
	const EGeometryScriptIndexType IndexType = ToScriptIndexType(ToElementType);
	if (ScriptTo.ConvertToMeshIndexArray(Mesh, ConvertedIDs, IndexType) != IndexType)
	{
		return false;
	}
	for (const int32 ElementID : ConvertedIDs)
	{
		AddElement(Mesh, ToElementType, ElementID, OutSelection);
	}
	return true;
}

bool UPCGUtilsDynMeshDomainSelectionFactoryData::SupportsDomain(
	const FPCGUtilsDynMeshSelectionDomain& Domain) const
{
	return Domain.TopologyType == UE::Geometry::EGeometryTopologyType::Triangle &&
		IsSupportedElementType(Domain.ElementType);
}

TSharedPtr<FPCGUtilsDynMeshSelectionOperation>
UPCGUtilsDynMeshDomainSelectionFactoryData::CreateNativeOperation(FPCGContext* InContext) const
{
	TSharedPtr<FPCGUtilsDynMeshSelectionOperation> Operation = CreateNativeOperationInternal();
	if (Operation)
	{
		Operation->BindContext(InContext);
	}
	return Operation;
}

TSharedPtr<FPCGUtilsDynMeshSelectionOperation>
UPCGUtilsDynMeshDomainSelectionFactoryData::CreateOperationInternal() const
{
	return MakeShared<FDomainSelectionAdapterOperation>(this);
}

void UPCGUtilsDynMeshDomainSelectionFactoryData::AddToCrc(
	FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc)
	{
		bool bAllowPartial = bAllowPartialInclusion;
		Ar << bAllowPartial;
	}
}

UPCGUtilsDynMeshFactoryData*
UPCGUtilsDynMeshDomainSelectionFactoryProviderSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	UPCGUtilsDynMeshDomainSelectionFactoryData* DomainFactory =
		Cast<UPCGUtilsDynMeshDomainSelectionFactoryData>(InFactory);
	if (!DomainFactory)
	{
		return nullptr;
	}
	DomainFactory->bAllowPartialInclusion = bAllowPartialInclusion;
	return Super::CreateFactory(InContext, DomainFactory);
}
