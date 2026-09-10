// Copyright Max Harris

#include "Elements/Selections/PCGGeometryCollectionSelectionFromDynMesh.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGGeometryCollectionData.h"
#include "Data/PCGUtilsGeometryCollectionPieceMesh.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Factories/PCGUtilsDynMeshDomainSelectionFactory.h"
#include "Factories/PCGUtilsDynMeshFactories.h"
#include "Factories/PCGUtilsDynMeshSelectionFactory.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHierarchy.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionMeshPresentation.h"
#include "GeometryCollection/GeometryCollection.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGUtilsFracture.h"
#include "Serialization/ArchiveCrc32.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGGeometryCollectionSelectionFromDynMesh"

namespace
{
	using namespace UE::Geometry;

	/**
	 * The domain the predicate is evaluated in.
	 *
	 * A domain-native selector states its own domain, and honouring it is what keeps evaluation cheap: the
	 * alternative - asking for Face and letting the factory convert - materializes a whole FGeometrySelection
	 * per piece and throws away the short-circuit. Anything else is probed Face first, because a face-domain
	 * evaluation is the only one that needs no welded copy.
	 */
	bool ResolveDomain(
		const UPCGUtilsDynMeshSelectionFactoryData* InSelector, FPCGUtilsDynMeshSelectionDomain& OutDomain)
	{
		OutDomain.TopologyType = EGeometryTopologyType::Triangle;

		if (const UPCGUtilsDynMeshDomainSelectionFactoryData* DomainSelector =
			Cast<const UPCGUtilsDynMeshDomainSelectionFactoryData>(InSelector))
		{
			OutDomain.ElementType = DomainSelector->GetNativeElementType();
			return true;
		}

		for (const EGeometryElementType Candidate :
			{EGeometryElementType::Face, EGeometryElementType::Vertex, EGeometryElementType::Edge})
		{
			FPCGUtilsDynMeshSelectionDomain Probe;
			Probe.ElementType = Candidate;
			Probe.TopologyType = EGeometryTopologyType::Triangle;
			if (InSelector->SupportsDomain(Probe))
			{
				OutDomain = Probe;
				return true;
			}
		}
		return false;
	}

	/**
	 * The mesh the predicate actually sees, per piece.
	 *
	 * Face domain gets the cached view verbatim: triangles there are 1:1 with collection faces, which is both
	 * the cheapest form and the only one in which provenance is exact.
	 *
	 * Vertex and edge domains must not use it. The canonical view is unwelded, and the collection stores the
	 * corners where an original surface meets a fracture cut as *separate* vertices - so on the raw view no
	 * vertex and no edge is ever shared between an exterior and an interior triangle. That inflates the element
	 * counts (which changes what an All aggregation is even asking) and leaves the exterior-biased boundary rule
	 * in PCGUtilsGeometryCollectionSurface with nothing to decide. Welding restores both.
	 */
	void BuildEvaluationMesh(
		const FPCGUtilsGeometryCollectionPieceMeshView& InView,
		EGeometryElementType InElementType,
		FDynamicMesh3& OutMesh)
	{
		if (InElementType == EGeometryElementType::Face)
		{
			OutMesh = *InView.Mesh;
			return;
		}

		PCGUtilsGeometryCollectionMeshPresentation::FPresentationOptions Options;
		Options.bSkipHiddenFaces = true;
		Options.bWeldVertices = true;
		Options.bPreserveIsolatedVertices = false;
		PCGUtilsGeometryCollectionMeshPresentation::PresentPiece(
			InView, FTransform::Identity, Options, OutMesh);
	}

	bool ElementMatchesTarget(
		const FDynamicMesh3& InMesh, EGeometryElementType InElementType, int32 InElementID,
		EPCGUtilsGeometryCollectionSurfaceTarget InTarget)
	{
		switch (InElementType)
		{
		case EGeometryElementType::Vertex:
			return PCGUtilsGeometryCollectionSurface::VertexMatchesTarget(InMesh, InElementID, InTarget);
		case EGeometryElementType::Edge:
			return PCGUtilsGeometryCollectionSurface::EdgeMatchesTarget(InMesh, InElementID, InTarget);
		case EGeometryElementType::Face:
		default:
			// The face path evaluates the raw view, so hidden faces are still present and must be excluded
			// here; the welded vertex/edge meshes already had them dropped during presentation.
			return PCGUtilsGeometryCollectionSurface::IsVisibleTriangle(InMesh, InElementID)
				&& PCGUtilsGeometryCollectionSurface::TriangleMatchesTarget(InMesh, InElementID, InTarget);
		}
	}

	/** Aggregates the predicate over one piece's eligible elements, short-circuiting as soon as it can. */
	bool EvaluatePiece(
		const FPCGUtilsDynMeshSelectionOperation& InOperation,
		const FDynamicMesh3& InMesh,
		EGeometryElementType InElementType,
		EPCGUtilsGeometryCollectionSurfaceTarget InTarget,
		EPCGGeometryCollectionMeshPredicateAggregation InAggregation)
	{
		const bool bRequireAll = InAggregation == EPCGGeometryCollectionMeshPredicateAggregation::All;
		int32 EligibleCount = 0;

		auto Consider = [&](int32 ElementID, bool& bOutStop, bool& bOutResult) -> void
		{
			if (!ElementMatchesTarget(InMesh, InElementType, ElementID, InTarget))
			{
				return;
			}
			++EligibleCount;
			const bool bPassed = InOperation.TestElement(ElementID);
			if (bPassed != bRequireAll)
			{
				// Any + pass, or All + fail: the answer cannot change from here.
				bOutStop = true;
				bOutResult = bPassed;
			}
		};

		bool bStop = false;
		bool bResult = false;

		if (InElementType == EGeometryElementType::Vertex)
		{
			for (const int32 VertexID : InMesh.VertexIndicesItr())
			{
				Consider(VertexID, bStop, bResult);
				if (bStop) { return bResult; }
			}
		}
		else if (InElementType == EGeometryElementType::Edge)
		{
			for (const int32 EdgeID : InMesh.EdgeIndicesItr())
			{
				Consider(EdgeID, bStop, bResult);
				if (bStop) { return bResult; }
			}
		}
		else
		{
			for (const int32 TriangleID : InMesh.TriangleIndicesItr())
			{
				Consider(TriangleID, bStop, bResult);
				if (bStop) { return bResult; }
			}
		}

		// Nothing short-circuited. All passed every eligible element; Any passed none. Either way an empty
		// eligible set is false - a predicate that had nothing to look at has not selected anything.
		return EligibleCount > 0 && bRequireAll;
	}
}

bool UPCGGeometryCollectionSelectionFromDynMeshFactoryData::Evaluate(
	const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
	FPCGContext* InContext,
	FDataflowTransformSelection& OutSelection) const
{
	const FGeometryCollection& Collection = InEvaluationContext.Collection;
	// InitializeFromCollection is the module convention and what every FractureEngine entry point size-checks.
	OutSelection.InitializeFromCollection(Collection, false);

	if (!MeshSelector)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("MissingSelector", "The mesh predicate has no DynMesh Selector to evaluate."), InContext);
		return false;
	}

	FPCGUtilsDynMeshSelectionDomain Domain;
	if (!ResolveDomain(MeshSelector, Domain))
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("NoSupportedDomain",
				"The connected DynMesh Selector supports no vertex, edge or face domain, so it cannot be "
				"evaluated against a fracture piece."), InContext);
		return false;
	}

	TArray<int32> Pieces;
	PCGUtilsGeometryCollectionHierarchy::GatherPieces(Collection, Pieces);

	TArray<int32> SelectedBones;
	int32 SkippedPieces = 0;

	for (const int32 BoneIndex : Pieces)
	{
		const int32 GeometryIndex = Collection.TransformToGeometryIndex[BoneIndex];
		const TSharedPtr<const FPCGUtilsGeometryCollectionPieceMeshView> View =
			InEvaluationContext.CollectionData.GetPieceMeshCache().GetOrBuild(Collection, GeometryIndex);
		if (!View.IsValid() || !View->IsValid())
		{
			++SkippedPieces;
			continue;
		}

		// A transient UPCGDynamicMeshData is not optional. Most DynMesh selectors reach the mesh through the
		// evaluation context's MeshData - every UPCGUtilsDynMeshDomainSelectionFactoryData does, for domain
		// conversion, and the spatial ones do for the octree and actor transform - and they fail outright when
		// it is null. The mesh handed to the context must therefore be the *same* mesh this data owns, or an
		// operation reading one and testing against the other would silently disagree.
		UPCGDynamicMeshData* PieceData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(InContext);
		{
			FDynamicMesh3 EvaluationMesh;
			BuildEvaluationMesh(*View, Domain.ElementType, EvaluationMesh);
			PieceData->Initialize(MoveTemp(EvaluationMesh));
		}

		const UDynamicMesh* PieceMesh = PieceData->GetDynamicMesh();
		const FDynamicMesh3* Mesh = PieceMesh ? PieceMesh->GetMeshPtr() : nullptr;
		if (!Mesh)
		{
			++SkippedPieces;
			continue;
		}

		const FPCGUtilsDynMeshSelectionEvaluationContext MeshContext(PieceData, *Mesh, Domain);
		const TSharedPtr<FPCGUtilsDynMeshSelectionOperation> Operation = MeshSelector->CreateOperation(InContext);
		if (!Operation || !Operation->Initialize(MeshContext))
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("OperationFailed",
					"The connected DynMesh Selector could not be prepared against a fracture piece."), InContext);
			return false;
		}

		if (EvaluatePiece(*Operation, *Mesh, Domain.ElementType, SurfaceTarget, Aggregation))
		{
			SelectedBones.Add(BoneIndex);
		}
	}

	if (SkippedPieces > 0)
	{
		PCGLog::LogWarningOnGraph(
			FText::Format(LOCTEXT("SkippedPieces",
				"{0} fracture piece(s) had no usable geometry and were not selected."), SkippedPieces), InContext);
	}

	OutSelection.SetFromArray(SelectedBones);

	UE_LOG(LogPCGUtilsFracture, Verbose,
		TEXT("GC Select By Mesh Predicate: %d of %d piece(s) selected (domain %d, aggregation %s)"),
		SelectedBones.Num(), Pieces.Num(), static_cast<int32>(Domain.ElementType),
		Aggregation == EPCGGeometryCollectionMeshPredicateAggregation::All ? TEXT("All") : TEXT("Any"));
	return true;
}

void UPCGGeometryCollectionSelectionFromDynMeshFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc)
	{
		return;
	}

	uint8 LocalAggregation = static_cast<uint8>(Aggregation);
	uint8 LocalTarget = static_cast<uint8>(SurfaceTarget);
	Ar << LocalAggregation;
	Ar << LocalTarget;

	// The predicate is part of this factory's identity; without it two different selectors would share a key.
	uint32 ChildCrc = MeshSelector ? MeshSelector->GetOrComputeCrc(true).GetValue() : 0;
	Ar << ChildCrc;
}

#if WITH_EDITOR
FText UPCGGeometryCollectionSelectionFromDynMeshSettings::GetDefaultNodeTitle() const
{
	// Drawn compact, so the title is only the aggregation mode; the surface qualifier is appended because it
	// changes the answer and would otherwise be invisible on a collapsed node.
	const bool bAll = Aggregation == EPCGGeometryCollectionMeshPredicateAggregation::All;
	switch (SurfaceTarget)
	{
	case EPCGUtilsGeometryCollectionSurfaceTarget::Exterior:
		return bAll ? LOCTEXT("AllExTitle", "All (ex)") : LOCTEXT("AnyExTitle", "Any (ex)");
	case EPCGUtilsGeometryCollectionSurfaceTarget::Interior:
		return bAll ? LOCTEXT("AllInTitle", "All (in)") : LOCTEXT("AnyInTitle", "Any (in)");
	case EPCGUtilsGeometryCollectionSurfaceTarget::All:
	default:
		return bAll ? LOCTEXT("AllTitle", "All") : LOCTEXT("AnyTitle", "Any");
	}
}

FText UPCGGeometryCollectionSelectionFromDynMeshSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Selects the fracture pieces whose surface satisfies a DynMesh Selector, turning any geometric mesh "
		"predicate - bounds, normal, vertex colour, occlusion - into a Geometry Collection bone selection. "
		"Evaluates pieces only: clusters and roots are never selected directly, because a cluster's shape is "
		"the union of its pieces - lift the result with GC | Select | Parent instead. Use Surface Target to "
		"restrict the predicate to the piece's original exterior or to the faces a cut created.");
}

TArray<FPCGPreConfiguredSettingsInfo>
UPCGGeometryCollectionSelectionFromDynMeshSettings::GetPreconfiguredInfo() const
{
	// Two entries, because Any and All are different questions rather than two spellings of one - unlike the
	// Selector/Selection representation split, which was withdrawn from the palette for being exactly that.
	return {
		{PCGGeometryCollectionSelectionFromDynMeshConstants::PreconfiguredAny,
			LOCTEXT("AnyPreset", "GC | Select | By Mesh Predicate (Any)"),
			LOCTEXT("AnyPresetTooltip", "Selects a piece when at least one eligible element passes."),
			LOCTEXT("AnyPresetHints", "any some one passes")},
		{PCGGeometryCollectionSelectionFromDynMeshConstants::PreconfiguredAll,
			LOCTEXT("AllPreset", "GC | Select | By Mesh Predicate (All)"),
			LOCTEXT("AllPresetTooltip", "Selects a piece only when every eligible element passes."),
			LOCTEXT("AllPresetHints", "all every entirely whole")}
	};
}

void UPCGGeometryCollectionSelectionFromDynMeshSettings::ApplyPreconfiguredSettings(
	const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo)
{
	switch (PreconfiguredInfo.PreconfiguredIndex)
	{
	case PCGGeometryCollectionSelectionFromDynMeshConstants::PreconfiguredAny:
		Aggregation = EPCGGeometryCollectionMeshPredicateAggregation::Any;
		break;
	case PCGGeometryCollectionSelectionFromDynMeshConstants::PreconfiguredAll:
		Aggregation = EPCGGeometryCollectionMeshPredicateAggregation::All;
		break;
	default:
		ensureMsgf(false, TEXT("Unknown GC mesh-predicate preconfiguration index: %d"),
			PreconfiguredInfo.PreconfiguredIndex);
		break;
	}
}
#endif

FName UPCGGeometryCollectionSelectionFromDynMeshSettings::GetMainOutputPin() const
{
	return PCGUtilsGeometryCollectionSelectionFactoryConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGGeometryCollectionSelectionFromDynMeshSettings::GetFactoryTypeId() const
{
	return FPCGUtilsGeometryCollectionSelectionFactoryDataTypeInfo::AsId();
}

TArray<FPCGPinProperties> UPCGGeometryCollectionSelectionFromDynMeshSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	// Single connection, matching every other DynMesh Selector consumer. Combining predicates is what
	// Select | AND is for, and allowing several here would leave it ambiguous whether Any/All applied per
	// selector or across them.
	Pins.Emplace_GetRef(
		PCGGeometryCollectionSelectionFromDynMeshConstants::SelectorInputPin,
		FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId(),
		/*bInAllowMultipleConnections=*/false, /*bAllowMultipleData=*/false).SetRequiredPin();
	return Pins;
}

UPCGUtilsGeometryCollectionFactoryData* UPCGGeometryCollectionSelectionFromDynMeshSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory) const
{
	TArray<TObjectPtr<const UPCGUtilsDynMeshSelectionFactoryData>> MeshSelectors;
	if (!PCGUtilsDynMeshFactories::GetInputFactories(
		InContext, PCGGeometryCollectionSelectionFromDynMeshConstants::SelectorInputPin, MeshSelectors,
		PCGUtilsDynMeshFactories::GetSelectionFactoryTypes(), /*bRequired=*/true))
	{
		return nullptr;
	}

	if (MeshSelectors.Num() != 1)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("OneSelectorOnly",
				"The mesh predicate takes exactly one DynMesh Selector. Combine several with Select | AND "
				"before connecting them here."), InContext);
		return nullptr;
	}

	UPCGGeometryCollectionSelectionFromDynMeshFactoryData* Factory = InFactory
		? Cast<UPCGGeometryCollectionSelectionFromDynMeshFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGGeometryCollectionSelectionFromDynMeshFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->MeshSelector = MeshSelectors[0];
	Factory->Aggregation = Aggregation;
	Factory->SurfaceTarget = SurfaceTarget;
	Factory->Priority = Priority;
	return Factory;
}

#undef LOCTEXT_NAMESPACE
