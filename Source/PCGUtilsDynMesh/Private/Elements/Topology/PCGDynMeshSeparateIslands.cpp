// Copyright Max Harris

#include "Elements/Topology/PCGDynMeshSeparateIslands.h"

#include "Algo/Unique.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicSubmesh3.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "Materials/MaterialInterface.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataDomain.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Spatial/PointHashGrid3.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshSeparateIslands"

namespace
{
	/** Union-find over a dense index range, with path halving. */
	struct FDisjointSets
	{
		TArray<int32> Parent;

		explicit FDisjointSets(int32 Num)
		{
			Parent.SetNumUninitialized(Num);
			for (int32 Index = 0; Index < Num; ++Index)
			{
				Parent[Index] = Index;
			}
		}

		int32 Find(int32 Index)
		{
			while (Parent[Index] != Index)
			{
				Parent[Index] = Parent[Parent[Index]];
				Index = Parent[Index];
			}
			return Index;
		}

		void Union(int32 A, int32 B)
		{
			A = Find(A);
			B = Find(B);
			if (A != B)
			{
				// The lower index wins, which keeps roots stable regardless of visit order.
				Parent[FMath::Max(A, B)] = FMath::Min(A, B);
			}
		}
	};

	void WriteDataInt(UPCGDynamicMeshData& Data, FName AttributeName, int32 Value)
	{
		UPCGMetadata* Metadata = Data.MutableMetadata();
		FPCGMetadataDomain* Domain = Metadata ? Metadata->GetMetadataDomain(PCGMetadataDomainID::Data) : nullptr;
		if (!Domain)
		{
			return;
		}
		// Replace whatever an attribute of this name held before, including one inherited from upstream.
		Domain->DeleteAttribute(AttributeName);
		Domain->CreateAttribute<int32>(AttributeName, Value, /*bAllowsInterpolation=*/false, /*bOverrideParent=*/true);
	}

	/** Checks one enabled output attribute name. Logs the exact failure and returns false when it cannot be written. */
	bool ValidateOutputName(const TCHAR* PropertyLabel, FName AttributeName, FPCGContext* Context)
	{
		if (AttributeName.IsNone())
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("NoneName",
				"Separate Islands: {0} is None while its output is enabled. Give it a name or turn the output off."),
				FText::FromString(PropertyLabel)), Context);
			return false;
		}
		if (!FPCGMetadataAttributeBase::IsValidName(AttributeName))
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("InvalidName",
				"Separate Islands: {0} = '{1}' is not a valid PCG attribute name (FPCGMetadataAttributeBase::IsValidName rejected it)."),
				FText::FromString(PropertyLabel), FText::FromName(AttributeName)), Context);
			return false;
		}
		return true;
	}

	UPCGDynamicMeshData* MakeOutput(FPCGContext* Context, const UPCGDynamicMeshData* SourceData, UE::Geometry::FDynamicMesh3&& Mesh)
	{
		TArray<UMaterialInterface*> Materials;
		Materials.Reserve(SourceData->GetMaterials().Num());
		for (UMaterialInterface* Material : SourceData->GetMaterials())
		{
			Materials.Add(Material);
		}
		// Each output is part of the input: it keeps the input's metadata (every domain, @Data included) and target actor.
		UPCGDynamicMeshData* Output = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(Context);
		Output->InitializeFromData(SourceData);
		Output->Initialize(MoveTemp(Mesh), Materials);
		return Output;
	}
}

void PCGDynMeshSeparateIslands::FindIslands(const UE::Geometry::FDynamicMesh3& Mesh, EPCGDynMeshIslandConnectivity Connectivity,
	double ConnectDistance, const TArray<int32>* TriangleFilter, TArray<TArray<int32>>& OutIslands)
{
	using namespace UE::Geometry;
	OutIslands.Reset();

	// The triangles to group, ascending, and membership by triangle ID.
	TArray<int32> Triangles;
	if (TriangleFilter)
	{
		Triangles.Reserve(TriangleFilter->Num());
		for (const int32 TriangleID : *TriangleFilter)
		{
			if (Mesh.IsTriangle(TriangleID))
			{
				Triangles.Add(TriangleID);
			}
		}
		Triangles.Sort();
		Triangles.SetNum(Algo::Unique(Triangles));
	}
	else
	{
		Triangles.Reserve(Mesh.TriangleCount());
		for (const int32 TriangleID : Mesh.TriangleIndicesItr())
		{
			Triangles.Add(TriangleID);
		}
	}
	if (Triangles.IsEmpty())
	{
		return;
	}

	TBitArray<> InSet(false, Mesh.MaxTriangleID());
	for (const int32 TriangleID : Triangles)
	{
		InSet[TriangleID] = true;
	}

	// Each triangle's island key: a triangle ID (SharedEdges) or a vertex ID (the vertex modes).
	TFunction<int32(int32)> KeyOf;
	TOptional<FDisjointSets> Sets;

	if (Connectivity == EPCGDynMeshIslandConnectivity::SharedEdges)
	{
		Sets.Emplace(Mesh.MaxTriangleID());
		for (const int32 TriangleID : Triangles)
		{
			const FIndex3i Edges = Mesh.GetTriEdges(TriangleID);
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				const FIndex2i EdgeTriangles = Mesh.GetEdgeT(Edges[Corner]);
				const int32 Other = EdgeTriangles.A == TriangleID ? EdgeTriangles.B : EdgeTriangles.A;
				if (Other != IndexConstants::InvalidID && InSet[Other])
				{
					Sets->Union(TriangleID, Other);
				}
			}
		}
		KeyOf = [&Sets](int32 TriangleID) { return Sets->Find(TriangleID); };
	}
	else
	{
		Sets.Emplace(Mesh.MaxVertexID());
		TBitArray<> UsedVertex(false, Mesh.MaxVertexID());
		for (const int32 TriangleID : Triangles)
		{
			const FIndex3i Tri = Mesh.GetTriangle(TriangleID);
			Sets->Union(Tri.A, Tri.B);
			Sets->Union(Tri.A, Tri.C);
			UsedVertex[Tri.A] = true;
			UsedVertex[Tri.B] = true;
			UsedVertex[Tri.C] = true;
		}

		const bool bUsesDistance = Connectivity == EPCGDynMeshIslandConnectivity::Proximity
			|| Connectivity == EPCGDynMeshIslandConnectivity::Intersecting;
		if (bUsesDistance && ConnectDistance > 0.0)
		{
			TPointHashGrid3d<int32> Grid(ConnectDistance, IndexConstants::InvalidID);
			for (TConstSetBitIterator<> It(UsedVertex); It; ++It)
			{
				Grid.InsertPointUnsafe(It.GetIndex(), Mesh.GetVertex(It.GetIndex()));
			}
			TArray<int32> Nearby;
			for (TConstSetBitIterator<> It(UsedVertex); It; ++It)
			{
				const int32 VertexID = It.GetIndex();
				const FVector3d Position = Mesh.GetVertex(VertexID);
				Nearby.Reset();
				Grid.FindPointsInBall(Position, ConnectDistance,
					[&Mesh, &Position](const int32& Other) { return DistanceSquared(Mesh.GetVertex(Other), Position); }, Nearby);
				for (const int32 Other : Nearby)
				{
					Sets->Union(VertexID, Other);
				}
			}
		}

		if (Connectivity == EPCGDynMeshIslandConnectivity::Intersecting)
		{
			// Every pair of crossing triangles joins their parts. Pairs sharing a vertex are already joined, so the
			// query skips them; the filter keeps a selection's connectivity inside the selection.
			FDynamicMeshAABBTree3 Tree(&Mesh, /*bAutoBuild=*/true);
			IMeshSpatial::FQueryOptions Options;
			if (TriangleFilter)
			{
				Options.TriangleFilterF = [&InSet](int32 TriangleID) { return InSet.IsValidIndex(TriangleID) && InSet[TriangleID]; };
			}
			const MeshIntersection::FIntersectionsQueryResult Crossings = Tree.FindAllSelfIntersections(/*bIgnoreTopoConnected=*/true, Options);
			auto Join = [&Sets, &Mesh](const int32 (&TriangleIDs)[2])
			{
				Sets->Union(Mesh.GetTriangle(TriangleIDs[0]).A, Mesh.GetTriangle(TriangleIDs[1]).A);
			};
			for (const MeshIntersection::FPointIntersection& Crossing : Crossings.Points)
			{
				Join(Crossing.TriangleID);
			}
			for (const MeshIntersection::FSegmentIntersection& Crossing : Crossings.Segments)
			{
				Join(Crossing.TriangleID);
			}
			for (const MeshIntersection::FPolygonIntersection& Crossing : Crossings.Polygons)
			{
				Join(Crossing.TriangleID);
			}
		}
		KeyOf = [&Sets, &Mesh](int32 TriangleID) { return Sets->Find(Mesh.GetTriangle(TriangleID).A); };
	}

	// Ascending triangle order numbers the islands by their lowest triangle ID.
	TMap<int32, int32> IslandOfKey;
	for (const int32 TriangleID : Triangles)
	{
		const int32 Key = KeyOf(TriangleID);
		int32* Island = IslandOfKey.Find(Key);
		if (!Island)
		{
			Island = &IslandOfKey.Add(Key, OutIslands.Num());
			OutIslands.AddDefaulted();
		}
		OutIslands[*Island].Add(TriangleID);
	}
}

UE::Geometry::FDynamicMesh3 PCGDynMeshSeparateIslands::ExtractTriangles(const UE::Geometry::FDynamicMesh3& Source, const TArray<int32>& Triangles)
{
	using namespace UE::Geometry;
	// AppendTriangles copies positions, overlays and material IDs, but renumbers polygroups and leaves weight layers
	// at zero. Both are put back from the source below, so every per-element value matches the input.
	FDynamicSubmesh3 Submesh(&Source, Triangles);
	FDynamicMesh3& Result = Submesh.GetSubmesh();

	if (Source.HasTriangleGroups() && Result.HasTriangleGroups())
	{
		for (const int32 SubTriangleID : Result.TriangleIndicesItr())
		{
			Result.SetTriangleGroup(SubTriangleID, Source.GetTriangleGroup(Submesh.MapTriangleToBaseMesh(SubTriangleID)));
		}
	}

	if (Source.HasAttributes() && Result.HasAttributes())
	{
		const FDynamicMeshAttributeSet& SourceAttributes = *Source.Attributes();
		FDynamicMeshAttributeSet& ResultAttributes = *Result.Attributes();

		const int32 NumPolygroupLayers = FMath::Min(SourceAttributes.NumPolygroupLayers(), ResultAttributes.NumPolygroupLayers());
		for (int32 LayerIndex = 0; LayerIndex < NumPolygroupLayers; ++LayerIndex)
		{
			const FDynamicMeshPolygroupAttribute* SourceLayer = SourceAttributes.GetPolygroupLayer(LayerIndex);
			FDynamicMeshPolygroupAttribute* ResultLayer = ResultAttributes.GetPolygroupLayer(LayerIndex);
			ResultLayer->SetName(SourceLayer->GetName());
			for (const int32 SubTriangleID : Result.TriangleIndicesItr())
			{
				ResultLayer->SetValue(SubTriangleID, SourceLayer->GetValue(Submesh.MapTriangleToBaseMesh(SubTriangleID)));
			}
		}

		ResultAttributes.SetNumWeightLayers(SourceAttributes.NumWeightLayers());
		for (int32 LayerIndex = 0; LayerIndex < SourceAttributes.NumWeightLayers(); ++LayerIndex)
		{
			const FDynamicMeshWeightAttribute* SourceLayer = SourceAttributes.GetWeightLayer(LayerIndex);
			FDynamicMeshWeightAttribute* ResultLayer = ResultAttributes.GetWeightLayer(LayerIndex);
			ResultLayer->SetName(SourceLayer->GetName());
			for (const int32 SubVertexID : Result.VertexIndicesItr())
			{
				float Value = 0.0f;
				SourceLayer->GetValue(Submesh.MapVertexToBaseMesh(SubVertexID), &Value);
				ResultLayer->SetScalarValue(SubVertexID, Value);
			}
		}
	}

	return MoveTemp(Result);
}

#if WITH_EDITOR
FText UPCGDynMeshSeparateIslandsSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DynMesh | Separate Islands");
}

FText UPCGDynMeshSeparateIslandsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Splits each mesh into its connected islands and emits each island as its own DynMesh. "
		"With a selection, only the selected triangles are split; the rest is emitted on Unselected.");
}
#endif

TArray<FPCGPinProperties> UPCGDynMeshSeparateIslandsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	FPCGPinProperties& Islands = Pins.Emplace_GetRef(PCGDynMeshSeparateIslandsConstants::IslandsPin, EPCGDataType::DynamicMesh, true, true);
	FPCGPinProperties& Unselected = Pins.Emplace_GetRef(PCGDynMeshSeparateIslandsConstants::UnselectedPin, EPCGDataType::DynamicMesh, true, true);
#if WITH_EDITOR
	Islands.Tooltip = LOCTEXT("IslandsTip", "One DynMesh per island, in order of each island's lowest triangle ID.");
	Unselected.Tooltip = LOCTEXT("UnselectedTip", "With a selection, the unselected part of each mesh, whole. Empty without a selection.");
#endif
	return Pins;
}

FPCGElementPtr UPCGDynMeshSeparateIslandsSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshSeparateIslandsElement>();
}

bool FPCGDynMeshSeparateIslandsElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UPCGDynMeshSeparateIslandsSettings* Settings = Context->GetInputSettings<UPCGDynMeshSeparateIslandsSettings>();
	check(Settings);

	if (Settings->bWriteIslandIndex && !ValidateOutputName(TEXT("Island Index Attribute"), Settings->IslandIndexAttribute, Context))
	{
		return true;
	}
	if (Settings->bWriteIslandCount && !ValidateOutputName(TEXT("Island Count Attribute"), Settings->IslandCountAttribute, Context))
	{
		return true;
	}
	if (Settings->Connectivity == EPCGDynMeshIslandConnectivity::Proximity && !(Settings->ConnectDistance > 0.0))
	{
		PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("BadConnectDistance",
			"Separate Islands: Connect Distance = {0}; Proximity needs a distance greater than 0."),
			FText::AsNumber(Settings->ConnectDistance)), Context);
		return true;
	}
	if (Settings->Connectivity == EPCGDynMeshIslandConnectivity::Intersecting && Settings->ConnectDistance < 0.0)
	{
		PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("NegativeConnectDistance",
			"Separate Islands: Connect Distance = {0}; Intersecting needs 0 (off) or more."),
			FText::AsNumber(Settings->ConnectDistance)), Context);
		return true;
	}

	const FPCGUtilsDynMeshFreeProperties* OutputProperties =
		Settings->OutputProperties.AnyEnabled() && PCGUtilsDynMeshProperties::Validate(Settings->OutputProperties, Context)
			? &Settings->OutputProperties : nullptr;

	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(PCGUtilsDynMeshProcessConstants::InputPin))
	{
		const FPCGUtilsDynMeshResolvedInput Resolved = FPCGUtilsDynMeshProcessFunctions::ResolveInput(Input.Data, Settings, Context);
		if (!Resolved.IsValid())
		{
			continue;
		}
		const UPCGDynamicMeshData* SourceData = Resolved.MeshData;
		const UDynamicMesh* SourceObject = SourceData->GetDynamicMesh();
		const UE::Geometry::FDynamicMesh3* SourceMesh = SourceObject ? SourceObject->GetMeshPtr() : nullptr;
		if (!SourceMesh)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("NoMesh", "Separate Islands skipped an input with no mesh."), Context);
			continue;
		}

		// A selection limits the split to its triangles (already converted to the face domain by the resolver).
		TArray<int32> SelectedTriangles;
		const bool bHasSelection = Resolved.SelectionData != nullptr;
		if (bHasSelection)
		{
			FGeometryScriptMeshSelection Selection;
			Selection.SetSelection(Resolved.SelectionData->GetSelection());
			Selection.ConvertToMeshIndexArray(*SourceMesh, SelectedTriangles, EGeometryScriptIndexType::Triangle);
		}

		TArray<TArray<int32>> Islands;
		PCGDynMeshSeparateIslands::FindIslands(*SourceMesh, Settings->Connectivity, Settings->ConnectDistance,
			bHasSelection ? &SelectedTriangles : nullptr, Islands);

		for (int32 IslandIndex = 0; IslandIndex < Islands.Num(); ++IslandIndex)
		{
			UPCGDynamicMeshData* Output = MakeOutput(Context, SourceData,
				PCGDynMeshSeparateIslands::ExtractTriangles(*SourceMesh, Islands[IslandIndex]));
			if (Settings->bWriteIslandIndex)
			{
				WriteDataInt(*Output, Settings->IslandIndexAttribute, IslandIndex);
			}
			if (Settings->bWriteIslandCount)
			{
				WriteDataInt(*Output, Settings->IslandCountAttribute, Islands.Num());
			}
			if (OutputProperties)
			{
				PCGUtilsDynMeshProperties::Write(*Output, *OutputProperties);
			}
			FPCGTaggedData& Tagged = Context->OutputData.TaggedData.Emplace_GetRef(Input);
			Tagged.Data = Output;
			Tagged.Pin = PCGDynMeshSeparateIslandsConstants::IslandsPin;
		}

		if (bHasSelection)
		{
			TBitArray<> Selected(false, SourceMesh->MaxTriangleID());
			for (const TArray<int32>& Island : Islands)
			{
				for (const int32 TriangleID : Island)
				{
					Selected[TriangleID] = true;
				}
			}
			TArray<int32> Rest;
			for (const int32 TriangleID : SourceMesh->TriangleIndicesItr())
			{
				if (!Selected[TriangleID])
				{
					Rest.Add(TriangleID);
				}
			}
			if (!Rest.IsEmpty())
			{
				UPCGDynamicMeshData* Output = MakeOutput(Context, SourceData, PCGDynMeshSeparateIslands::ExtractTriangles(*SourceMesh, Rest));
				if (OutputProperties)
				{
					PCGUtilsDynMeshProperties::Write(*Output, *OutputProperties);
				}
				FPCGTaggedData& Tagged = Context->OutputData.TaggedData.Emplace_GetRef(Input);
				Tagged.Data = Output;
				Tagged.Pin = PCGDynMeshSeparateIslandsConstants::UnselectedPin;
			}
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
