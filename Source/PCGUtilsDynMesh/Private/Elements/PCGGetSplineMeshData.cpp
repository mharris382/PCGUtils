#include "Elements/PCGGetSplineMeshData.h"

#include "Components/SplineMeshComponent.h"
#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/StaticMesh.h"
#include "FunctionLibraries/PCGUtilsSplineMeshFunctions.h"
#include "GameFramework/Actor.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "Helpers/PCGHelpers.h"
#include "Materials/MaterialInterface.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGGetSplineMeshDataElement"

namespace
{
	struct FCollectedSplineMesh
	{
		UDynamicMesh* Mesh = nullptr;
		TArray<UMaterialInterface*> Materials;
	};

	/** Remaps each triangle's Material ID attribute value through Remap (old ID -> new ID), enabling the
	 *  attribute first if the mesh did not already have one (e.g. single-material-slot source meshes). */
	void RemapMaterialIDs(UDynamicMesh* DynMesh, const TArray<int32>& Remap)
	{
		if (!DynMesh || Remap.IsEmpty())
		{
			return;
		}

		DynMesh->EditMesh([&Remap](UE::Geometry::FDynamicMesh3& Mesh)
		{
			if (!Mesh.HasAttributes())
			{
				Mesh.EnableAttributes();
			}
			if (!Mesh.Attributes()->HasMaterialID())
			{
				Mesh.Attributes()->EnableMaterialID();
			}

			UE::Geometry::FDynamicMeshMaterialAttribute* MaterialIDs = Mesh.Attributes()->GetMaterialID();
			for (int32 TriangleID : Mesh.TriangleIndicesItr())
			{
				const int32 OldID = MaterialIDs->GetValue(TriangleID);
				const int32 NewID = Remap.IsValidIndex(OldID) ? Remap[OldID] : 0;
				MaterialIDs->SetValue(TriangleID, NewID);
			}
		}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, true);
	}
}

UPCGGetSplineMeshDataSettings::UPCGGetSplineMeshDataSettings()
{
	// This node always operates on USplineMeshComponent - specialize (and hide) the inherited component
	// selector's class rather than adding a separate/conflicting ComponentClass setting, mirroring how Epic's
	// own typed getters (e.g. Get Virtual Texture Data) fix their component selector's class.
	Mode = EPCGGetDataFromActorMode::ParseActorComponents;

	ComponentSelector.ComponentSelection = EPCGComponentSelection::ByClass;
	ComponentSelector.ComponentSelectionClass = USplineMeshComponent::StaticClass();
	ComponentSelector.bShowComponentSelectionClass = false;
}

#if WITH_EDITOR
FText UPCGGetSplineMeshDataSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Get Spline Mesh Data");
}

FText UPCGGetSplineMeshDataSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Finds Spline Mesh Components on the selected actors and extracts their already-authored, deformed render "
		"geometry as Dynamic Mesh data. Read-only: source components, their static meshes, and materials are never "
		"created, modified, hidden, attached/detached, or destroyed.");
}
#endif

TArray<FPCGPinProperties> UPCGGetSplineMeshDataSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::DynamicMesh);
	return PinProperties;
}

FPCGElementPtr UPCGGetSplineMeshDataSettings::CreateElement() const
{
	return MakeShared<FPCGGetSplineMeshDataElement>();
}

void FPCGGetSplineMeshDataElement::ProcessActors(FPCGContext* InContext, const UPCGDataFromActorSettings* InSettings,
	const TArray<AActor*>& FoundActors, TArray<FPCGTaskId>& OutDynamicDependencies) const
{
	check(InContext);
	FPCGDataFromActorContext* Context = static_cast<FPCGDataFromActorContext*>(InContext);
	const UPCGGetSplineMeshDataSettings* Settings = CastChecked<UPCGGetSplineMeshDataSettings>(InSettings);

	// Per spec: for the PCG node, the "Reference Actor Context" required by the reusable conversion utility is
	// the PCG execution's own target actor, resolved via the normal PCG context API.
	AActor* ReferenceActor = InContext->GetTargetActor(nullptr);
	bool bConvertToLocal = Settings->bConvertWorldToActorLocal;
	if (bConvertToLocal && !ReferenceActor)
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("MissingTargetActor",
			"Get Spline Mesh Data could not resolve a PCG target actor; output remains in world space."), InContext);
		bConvertToLocal = false;
	}

	TArray<FCollectedSplineMesh> Collected;

	for (AActor* Actor : FoundActors)
	{
		if (!IsValid(Actor))
		{
			continue;
		}

		TInlineComponentArray<USplineMeshComponent*, 4> SplineMeshComponents;
		Actor->GetComponents(SplineMeshComponents);
		if (SplineMeshComponents.IsEmpty())
		{
			// Actor has no SplineMeshComponents - skip it, this is not an error.
			continue;
		}

		for (USplineMeshComponent* Component : SplineMeshComponents)
		{
			if (!IsValid(Component) || !Context->ComponentSelector.FilterComponent(Component))
			{
				continue;
			}

			if (Settings->bIgnorePCGGeneratedComponents && Component->ComponentTags.Contains(PCGHelpers::DefaultPCGTag))
			{
				continue;
			}

			if (!IsValid(Component->GetStaticMesh()))
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("NoStaticMesh", "Get Spline Mesh Data skipped Spline Mesh Component '{0}' on Actor '{1}': it has no Static Mesh."),
					FText::FromString(Component->GetName()), FText::FromString(Actor->GetActorNameOrLabel())), InContext);
				continue;
			}

			UDynamicMesh* Mesh = UPCGUtilsSplineMeshFunctions::SplineMeshComponentToDynamicMesh(
				ReferenceActor, Component, bConvertToLocal, Settings->RenderLODIndex);
			if (!Mesh)
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("ConversionFailed", "Get Spline Mesh Data failed to convert Spline Mesh Component '{0}' on Actor '{1}'."),
					FText::FromString(Component->GetName()), FText::FromString(Actor->GetActorNameOrLabel())), InContext);
				continue;
			}

			FCollectedSplineMesh& Item = Collected.Emplace_GetRef();
			Item.Mesh = Mesh;

			// Collect the component's *effective* materials (respecting component-level overrides), not just
			// the static mesh's default materials.
			const int32 NumMaterials = Component->GetNumMaterials();
			Item.Materials.Reserve(NumMaterials);
			for (int32 MaterialIndex = 0; MaterialIndex < NumMaterials; ++MaterialIndex)
			{
				Item.Materials.Add(Component->GetMaterial(MaterialIndex));
			}
		}
	}

	if (Collected.IsEmpty())
	{
		return;
	}

	if (!Settings->bUnionMeshes)
	{
		// Default mode: each SplineMeshComponent independently produces its own UPCGDynamicMeshData.
		for (const FCollectedSplineMesh& Item : Collected)
		{
			UPCGDynamicMeshData* OutputData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(InContext);
			OutputData->Initialize(Item.Mesh, true, Item.Materials);
			FPCGTaggedData& Output = InContext->OutputData.TaggedData.Emplace_GetRef();
			Output.Data = OutputData;
			Output.Pin = PCGPinConstants::DefaultOutputLabel;
		}
		return;
	}

	// Union mode: union every spline mesh component found across the whole actor query into a single output
	// (not one output per actor). Before combining, remap each source mesh's Material IDs into one coherent
	// FinalMaterials array, since Material ID 0 on one component's mesh does not necessarily correspond to the
	// same UMaterialInterface as Material ID 0 on another's.
	TArray<UMaterialInterface*> FinalMaterials;
	UDynamicMesh* CombinedMesh = nullptr;
	bool bAnyBooleanFailed = false;
	UGeometryScriptDebug* BooleanDebug = FPCGContext::NewObject_AnyThread<UGeometryScriptDebug>(InContext);

	for (const FCollectedSplineMesh& Item : Collected)
	{
		TArray<int32> Remap;
		Remap.Reserve(Item.Materials.Num());
		for (UMaterialInterface* Material : Item.Materials)
		{
			int32 Index = FinalMaterials.IndexOfByKey(Material);
			if (Index == INDEX_NONE)
			{
				Index = FinalMaterials.Add(Material);
			}
			Remap.Add(Index);
		}
		RemapMaterialIDs(Item.Mesh, Remap);

		if (!CombinedMesh)
		{
			CombinedMesh = Item.Mesh;
			continue;
		}

		// Both meshes were already produced in the same output coordinate space (world or reference-actor
		// local, per bConvertToLocal above), so no additional relative transform is needed here.
		const int32 PreviousMessageCount = BooleanDebug->Messages.Num();
		UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
			CombinedMesh, FTransform::Identity, Item.Mesh, FTransform::Identity,
			EGeometryScriptBooleanOperation::Union, Settings->UnionOptions, BooleanDebug);
		if (BooleanDebug->Messages.Num() > PreviousMessageCount)
		{
			bAnyBooleanFailed = true;
		}
	}

	if (bAnyBooleanFailed)
	{
		// Do not crash - report the failure and preserve as much of the (partially unioned) output as possible.
		PCGLog::LogWarningOnGraph(LOCTEXT("BooleanFailed",
			"Get Spline Mesh Data: one or more boolean union operations failed; output may be incomplete."), InContext);
	}

	if (CombinedMesh)
	{
		UPCGDynamicMeshData* OutputData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(InContext);
		OutputData->Initialize(CombinedMesh, true, FinalMaterials);
		FPCGTaggedData& Output = InContext->OutputData.TaggedData.Emplace_GetRef();
		Output.Data = OutputData;
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
	}
}

void FPCGGetSplineMeshDataElement::ProcessActors(FPCGContext* Context, const UPCGDataFromActorSettings* Settings, const TArray<AActor*>& FoundActors) const
{
	checkf(false, TEXT("This should never be called directly"));
}

void FPCGGetSplineMeshDataElement::ProcessActor(FPCGContext* Context, const UPCGDataFromActorSettings* Settings, AActor* FoundActor) const
{
	checkf(false, TEXT("This should never be called directly"));
}

#undef LOCTEXT_NAMESPACE
