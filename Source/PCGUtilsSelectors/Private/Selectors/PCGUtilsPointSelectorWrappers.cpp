// Copyright Max Harris

#include "Selectors/PCGUtilsPointSelectorWrappers.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHierarchy.h"
#include "GeometryCollection/GeometryCollection.h"
#include "Selectors/PCGUtilsPointSelector.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"
#include "Voxel/PCGVoxelGrid.h"

#define LOCTEXT_NAMESPACE "PCGUtilsPointSelectorWrappers"

namespace PCGUtilsPointSelectorWrappersPrivate
{
	/** Folds the wrapped selector and the conversion settings into a wrapper's Crc. */
	template<typename ConversionType>
	void AddWrapperCrc(FArchiveCrc32& Ar, bool bFullDataCrc, const UPCGUtilsPointSelectorData* PointSelector, const ConversionType& Conversion)
	{
		uint32 SelectorCrc = PointSelector ? PointSelector->GetOrComputeCrc(bFullDataCrc).GetValue() : 0;
		Ar << SelectorCrc;
		// Every authored value of the conversion, as text: any edit changes the Crc.
		FString Text;
		ConversionType::StaticStruct()->ExportText(Text, &Conversion, nullptr, nullptr, PPF_None, nullptr);
		Ar << Text;
	}

	class FDynMeshPointSelectorOperation final : public FPCGUtilsDynMeshSelectionOperation
	{
	public:
		explicit FDynMeshPointSelectorOperation(const UPCGUtilsDynMeshPointSelectorData* InData)
			: Data(InData)
		{
		}

		virtual bool Initialize(const FPCGUtilsDynMeshSelectionEvaluationContext& InSelectionContext) override
		{
			if (!FPCGUtilsDynMeshSelectionOperation::Initialize(InSelectionContext) || !Data || !Data->PointSelector)
			{
				return false;
			}

			FPCGUtilsElementPoints Points;
			FText Error;
			if (!PCGUtilsDynMeshElementPoints::ElementsToPoints(Data->Conversion, InSelectionContext, Context, Points, Error))
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("DynMeshConversionFailed", "Point selector on a DynMesh Selector pin: {0}"), Error), Context);
				return false;
			}

			TBitArray<> ElementMask;
			if (!PCGUtilsPointSelectors::SelectElements(Data->PointSelector, Points, Context, ElementMask))
			{
				return false;
			}

			// Keyed by the mesh's own element id, which is what TestElement is asked with.
			const UE::Geometry::FDynamicMesh3& Mesh = InSelectionContext.Mesh;
			int32 MaxID = 0;
			switch (InSelectionContext.Domain.ElementType)
			{
			case UE::Geometry::EGeometryElementType::Vertex: MaxID = Mesh.MaxVertexID(); break;
			case UE::Geometry::EGeometryElementType::Edge: MaxID = Mesh.MaxEdgeID(); break;
			default: MaxID = Mesh.MaxTriangleID(); break;
			}
			Selected.Init(false, MaxID);
			for (int32 Element = 0; Element < Points.NumElements(); ++Element)
			{
				if (ElementMask[Element] && Selected.IsValidIndex(Points.GetElementId(Element)))
				{
					Selected[Points.GetElementId(Element)] = true;
				}
			}
			return true;
		}

		/** The point selector's own answer. Invert is applied by the selector base, through bInvertSelection. */
		virtual bool TestElement(int32 ElementID) const override
		{
			return Selected.IsValidIndex(ElementID) && Selected[ElementID];
		}

	private:
		TObjectPtr<const UPCGUtilsDynMeshPointSelectorData> Data;
		TBitArray<> Selected;
	};

	class FVoxelPointSelectorOperation final : public FPCGVoxelSelectorOperation
	{
	public:
		FVoxelPointSelectorOperation(const UPCGVoxelPointSelectorData* InData, const FPCGVoxelGrid& InGrid, FPCGContext* InContext)
			: Data(InData), Grid(InGrid), Context(InContext)
		{
		}

		/** The filled cells, in one batch. */
		bool SelectFilledCells()
		{
			FPCGUtilsElementPoints Points;
			PCGVoxelSelectors::CellsToPoints(Data->Conversion, Grid, Grid.Cells, Points);
			return PCGUtilsPointSelectors::SelectElements(Data->PointSelector, Points, Context, FilledCells);
		}

		virtual bool Test(const FIntVector& Cell) override
		{
			const int32 CellIndex = Grid.FindCellIndex(Cell);
			if (CellIndex != INDEX_NONE)
			{
				return FilledCells[CellIndex];
			}
			if (const bool* Known = OtherCells.Find(Cell))
			{
				return *Known;
			}

			// An empty cell (an Add pattern is asking): answered on its own, once.
			FPCGUtilsElementPoints Points;
			PCGVoxelSelectors::CellsToPoints(Data->Conversion, Grid, MakeArrayView(&Cell, 1), Points);
			TBitArray<> Mask;
			const bool bSelected = PCGUtilsPointSelectors::SelectElements(Data->PointSelector, Points, Context, Mask) && Mask[0];
			OtherCells.Add(Cell, bSelected);
			return bSelected;
		}

	private:
		TObjectPtr<const UPCGVoxelPointSelectorData> Data;
		const FPCGVoxelGrid& Grid;
		FPCGContext* Context = nullptr;
		TBitArray<> FilledCells;
		TMap<FIntVector, bool> OtherCells;
	};
}

bool UPCGUtilsDynMeshPointSelectorData::SupportsDomain(const FPCGUtilsDynMeshSelectionDomain& Domain) const
{
	return Domain.TopologyType == UE::Geometry::EGeometryTopologyType::Triangle &&
		(Domain.ElementType == UE::Geometry::EGeometryElementType::Vertex ||
		 Domain.ElementType == UE::Geometry::EGeometryElementType::Edge ||
		 Domain.ElementType == UE::Geometry::EGeometryElementType::Face);
}

TSharedPtr<FPCGUtilsDynMeshSelectionOperation> UPCGUtilsDynMeshPointSelectorData::CreateOperationInternal() const
{
	return MakeShared<PCGUtilsPointSelectorWrappersPrivate::FDynMeshPointSelectorOperation>(this);
}

void UPCGUtilsDynMeshPointSelectorData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	PCGUtilsPointSelectorWrappersPrivate::AddWrapperCrc(Ar, bFullDataCrc, PointSelector.Get(), Conversion);
}

bool UPCGUtilsGeometryCollectionPointSelectorData::Evaluate(
	const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
	FPCGContext* InContext,
	FDataflowTransformSelection& OutSelection) const
{
	OutSelection.InitializeFromCollection(InEvaluationContext.Collection, false);
	if (!PointSelector)
	{
		return false;
	}

	FPCGUtilsElementPoints Points;
	FText Error;
	if (!PCGUtilsGeometryCollectionElementPoints::BonesToPoints(Conversion, InEvaluationContext, InContext, Points, Error))
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("GCConversionFailed", "Point selector on a GC Selection pin: {0}"), Error), InContext);
		return false;
	}

	TBitArray<> ElementMask;
	if (!PCGUtilsPointSelectors::SelectElements(PointSelector, Points, InContext, ElementMask))
	{
		return false;
	}

	TArray<int32> Matches;
	for (int32 Element = 0; Element < Points.NumElements(); ++Element)
	{
		if (ElementMask[Element])
		{
			Matches.Add(Points.GetElementId(Element));
		}
	}
	OutSelection.SetFromArray(Matches);
	return true;
}

void UPCGUtilsGeometryCollectionPointSelectorData::ApplyInversion(
	const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
	FDataflowTransformSelection& InOutSelection) const
{
	const TSet<int32> Selected(InOutSelection.AsArrayValidated(InEvaluationContext.Collection));
	TArray<int32> Pieces;
	PCGUtilsGeometryCollectionHierarchy::GatherPieces(InEvaluationContext.Collection, Pieces);
	Pieces.RemoveAll([&Selected](int32 Bone) { return Selected.Contains(Bone); });
	InOutSelection.InitializeFromCollection(InEvaluationContext.Collection, false);
	InOutSelection.SetFromArray(Pieces);
}

void UPCGUtilsGeometryCollectionPointSelectorData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	PCGUtilsPointSelectorWrappersPrivate::AddWrapperCrc(Ar, bFullDataCrc, PointSelector.Get(), Conversion);
}

TSharedPtr<FPCGVoxelSelectorOperation> UPCGVoxelPointSelectorData::CreateOperation(FPCGContext* Context, const FPCGVoxelGrid& Grid) const
{
	if (!PointSelector)
	{
		return nullptr;
	}
	TSharedRef<PCGUtilsPointSelectorWrappersPrivate::FVoxelPointSelectorOperation> Operation =
		MakeShared<PCGUtilsPointSelectorWrappersPrivate::FVoxelPointSelectorOperation>(this, Grid, Context);
	if (!Operation->SelectFilledCells())
	{
		return nullptr;
	}
	return Operation;
}

void UPCGVoxelPointSelectorData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	PCGUtilsPointSelectorWrappersPrivate::AddWrapperCrc(Ar, bFullDataCrc, PointSelector.Get(), Conversion);
}

#undef LOCTEXT_NAMESPACE
