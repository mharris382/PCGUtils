// Copyright Max Harris

#include "Elements/Conversion/PCGWriteDynMeshLODs.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Metadata/PCGMetadata.h"
#include "PCGContext.h"
#include "PCGParamData.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSourceData.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "GeometryScript/MeshAssetFunctions.h"
#include "StaticMeshResources.h"
#include "UObject/Package.h"
#endif

#define LOCTEXT_NAMESPACE "PCGWriteDynMeshLODs"

#if WITH_EDITOR
namespace
{
	/** One resolved (LOD index, mesh) write, in the order the pin presented its inputs. */
	struct FResolvedLODWrite
	{
		int32 LODIndex = INDEX_NONE;
		int32 InputOrder = INDEX_NONE;
		int32 TriangleCount = 0;
		UDynamicMesh* Mesh = nullptr;
	};

	int32 CountTriangles(const UDynamicMesh* Mesh)
	{
		const UE::Geometry::FDynamicMesh3* MeshPtr = Mesh ? Mesh->GetMeshPtr() : nullptr;
		return MeshPtr ? MeshPtr->TriangleCount() : 0;
	}

	/** Parses "<Prefix><N>" out of an input's tags. Returns INDEX_NONE when no tag matches. */
	int32 ParseLODIndexFromTags(const TSet<FString>& Tags, const FString& Prefix)
	{
		for (const FString& Tag : Tags)
		{
			if (!Tag.StartsWith(Prefix, ESearchCase::IgnoreCase))
			{
				continue;
			}
			const FString Suffix = Tag.RightChop(Prefix.Len());
			if (!Suffix.IsEmpty() && Suffix.IsNumeric())
			{
				return FCString::Atoi(*Suffix);
			}
		}
		return INDEX_NONE;
	}

	/**
	 * Creates an empty Static Mesh at Path with a single LOD0 source model and one material slot, so the
	 * subsequent CopyMeshToStaticMesh writes have a well-formed asset to land in. Deliberately does not save
	 * the package - saving is the batch orchestrator's job.
	 */
	UStaticMesh* CreateStaticMeshAsset(const FSoftObjectPath& Path, FPCGContext* Context)
	{
		const FString PackageName = Path.GetLongPackageName();
		const FString AssetName = Path.GetAssetName();
		if (PackageName.IsEmpty() || AssetName.IsEmpty())
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("BadAssetPath", "Write DynMesh LODs could not interpret '{0}' as an asset path."),
				FText::FromString(Path.ToString())), Context);
			return nullptr;
		}

		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("PackageCreateFailed", "Write DynMesh LODs could not create the package '{0}'."),
				FText::FromString(PackageName)), Context);
			return nullptr;
		}
		Package->FullyLoad();

		UStaticMesh* NewMesh = NewObject<UStaticMesh>(
			Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
		if (!NewMesh)
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("AssetCreateFailed", "Write DynMesh LODs could not create a Static Mesh named '{0}'."),
				FText::FromString(AssetName)), Context);
			return nullptr;
		}

		NewMesh->AddSourceModel();
		NewMesh->SetStaticMaterials({FStaticMaterial()});
		FAssetRegistryModule::AssetCreated(NewMesh);
		return NewMesh;
	}
}
#endif // WITH_EDITOR

#if WITH_EDITOR
FText UPCGWriteDynMeshLODsSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Write DynMesh LODs");
}

FText UPCGWriteDynMeshLODsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Writes each incoming DynMesh into a specific LOD slot on a target Static Mesh asset. Editor-only "
		"asset authoring, intended for batch LOD-generation graphs: upstream Simplify nodes produce "
		"progressively decimated meshes and this node commits them. The package is marked dirty but never "
		"saved - saving is left to whatever orchestrates the batch run.");
}
#endif

TArray<FPCGPinProperties> UPCGWriteDynMeshLODsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(
		PCGWriteDynMeshLODsConstants::SourceMeshesLabel, EPCGDataType::DynamicMesh,
		/*bInAllowMultipleConnections=*/true, /*bAllowMultipleData=*/true).SetRequiredPin();

	// Always present rather than structural on bUseTargetAssetAttribute: the pin is optional, and keeping it
	// stable avoids a pin rebuild (and a broken edge) every time the toggle is flipped.
	Pins.Emplace(
		PCGWriteDynMeshLODsConstants::TargetAssetLabel, EPCGDataType::Param,
		/*bInAllowMultipleConnections=*/false, /*bAllowMultipleData=*/false);
	return Pins;
}

TArray<FPCGPinProperties> UPCGWriteDynMeshLODsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(
		PCGWriteDynMeshLODsConstants::OutputLabel, EPCGDataType::Param,
		/*bInAllowMultipleConnections=*/true, /*bAllowMultipleData=*/false);
	return Pins;
}

FPCGElementPtr UPCGWriteDynMeshLODsSettings::CreateElement() const
{
	return MakeShared<FPCGWriteDynMeshLODsElement>();
}

bool FPCGWriteDynMeshLODsElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGWriteDynMeshLODsElement::ExecuteInternal);
	check(Context);

	const UPCGWriteDynMeshLODsSettings* Settings =
		Context->GetInputSettings<UPCGWriteDynMeshLODsSettings>();
	check(Settings);

	FSoftObjectPath TargetPath;
	bool bSucceeded = false;
	int32 LODsWritten = 0;

#if WITH_EDITOR
	// Resolve the target path -------------------------------------------------------------------------------
	if (Settings->bUseTargetAssetAttribute)
	{
		const TArray<FPCGTaggedData> TargetInputs =
			Context->InputData.GetInputsByPin(PCGWriteDynMeshLODsConstants::TargetAssetLabel);
		const UPCGParamData* TargetData = TargetInputs.IsEmpty()
			? nullptr : Cast<const UPCGParamData>(TargetInputs[0].Data);
		const UPCGMetadata* TargetMetadata = TargetData ? TargetData->ConstMetadata() : nullptr;
		const FPCGMetadataAttribute<FSoftObjectPath>* TargetAttribute = TargetMetadata
			? TargetMetadata->GetConstTypedAttribute<FSoftObjectPath>(Settings->TargetAssetAttribute)
			: nullptr;

		if (!TargetData)
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("MissingTargetInput", "Write DynMesh LODs requires an Attribute Set on the Target pin when Use Target Asset Attribute is enabled."),
				Context);
		}
		else if (!TargetAttribute)
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("MissingTargetAttribute", "The Target Attribute Set does not contain an FSoftObjectPath attribute named '{0}'."),
				FText::FromName(Settings->TargetAssetAttribute)), Context);
		}
		else
		{
			const PCGMetadataEntryKey FirstEntry = TargetMetadata->GetItemKeyCountForParent();
			const PCGMetadataEntryKey EntryCount = TargetMetadata->GetItemCountForChild();
			if (FirstEntry >= EntryCount)
			{
				PCGLog::LogErrorOnGraph(
					LOCTEXT("EmptyTargetAttribute", "The Target Attribute Set contains no entries."), Context);
			}
			else
			{
				TargetPath = TargetAttribute->GetValueFromItemKey(FirstEntry);
				if (EntryCount - FirstEntry > 1)
				{
					// Iterating a batch of target assets is an orchestrator concern, not this node's.
					PCGLog::LogWarningOnGraph(FText::Format(
						LOCTEXT("MultipleTargets", "Write DynMesh LODs received {0} target entries and used only the first ('{1}'); drive one asset per execution."),
						FText::AsNumber(static_cast<int32>(EntryCount - FirstEntry)),
						FText::FromString(TargetPath.ToString())), Context);
				}
			}
		}
	}
	else
	{
		TargetPath = Settings->TargetStaticMesh.ToSoftObjectPath();
		if (TargetPath.IsNull())
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("NoTargetConfigured", "Write DynMesh LODs has no Target Static Mesh configured."), Context);
		}
	}

	// Resolve (or create) the target asset ------------------------------------------------------------------
	UStaticMesh* TargetMesh = nullptr;
	if (!TargetPath.IsNull())
	{
		TargetMesh = Cast<UStaticMesh>(TargetPath.TryLoad());
		if (!TargetMesh)
		{
			if (Settings->bCreateAssetIfMissing)
			{
				TargetMesh = CreateStaticMeshAsset(TargetPath, Context);
			}
			else
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("TargetNotFound", "Write DynMesh LODs could not resolve '{0}' to a Static Mesh. Enable Create Asset If Missing to author it instead."),
					FText::FromString(TargetPath.ToString())), Context);
			}
		}
	}

	// Resolve each source mesh to a LOD slot ----------------------------------------------------------------
	if (TargetMesh)
	{
		const FString TagPrefix = Settings->LODTagPrefix.ToString();
		const TArray<FPCGTaggedData>& SourceInputs =
			Context->InputData.GetInputsByPin(PCGWriteDynMeshLODsConstants::SourceMeshesLabel);

		TArray<FResolvedLODWrite> Writes;
		TSet<int32> ClaimedLODs;
		int32 InputOrder = 0;

		for (const FPCGTaggedData& Input : SourceInputs)
		{
			if (!Cast<const UPCGDynamicMeshData>(Input.Data))
			{
				PCGLog::LogWarningOnGraph(
					LOCTEXT("NonMeshInput", "Write DynMesh LODs skipped a Source Meshes input that was not DynMesh data."),
					Context);
				continue;
			}

			const int32 ThisInputOrder = InputOrder++;
			int32 LODIndex = INDEX_NONE;
			if (Settings->LODAssignmentMode == EPCGUtilsLODAssignmentMode::ByInputOrder)
			{
				LODIndex = Settings->BaseLODIndex + ThisInputOrder;
			}
			else
			{
				LODIndex = ParseLODIndexFromTags(Input.Tags, TagPrefix);
				if (LODIndex == INDEX_NONE)
				{
					PCGLog::LogErrorOnGraph(FText::Format(
						LOCTEXT("MissingLODTag", "Write DynMesh LODs skipped an input with no '{0}<N>' tag while in By Tag mode."),
						FText::FromString(TagPrefix)), Context);
					continue;
				}
			}

			if (LODIndex < 0 || LODIndex >= MAX_STATIC_MESH_LODS)
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("LODOutOfRange", "Write DynMesh LODs skipped LOD index {0}; the engine supports 0 to {1}."),
					FText::AsNumber(LODIndex), FText::AsNumber(MAX_STATIC_MESH_LODS - 1)), Context);
				continue;
			}

			bool bAlreadyClaimed = false;
			ClaimedLODs.Add(LODIndex, &bAlreadyClaimed);
			if (bAlreadyClaimed)
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("DuplicateLOD", "Write DynMesh LODs skipped a second input targeting LOD {0}."),
					FText::AsNumber(LODIndex)), Context);
				continue;
			}

			// CopyMeshToStaticMesh only reads the mesh, but takes it non-const; CopyOrSteal is the engine's
			// sanctioned way to obtain a mutable DynMesh from a tagged input without a gratuitous copy.
			UPCGDynamicMeshData* MeshData = CopyOrSteal(Input, Context);
			UDynamicMesh* SourceMesh = MeshData ? MeshData->GetMutableDynamicMesh() : nullptr;
			if (!SourceMesh)
			{
				PCGLog::LogWarningOnGraph(
					LOCTEXT("InvalidSourceMesh", "Write DynMesh LODs skipped an input with no valid mesh."), Context);
				ClaimedLODs.Remove(LODIndex);
				continue;
			}

			Writes.Add({LODIndex, ThisInputOrder, CountTriangles(SourceMesh), SourceMesh});
		}

		if (Writes.IsEmpty())
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("NoWrites", "Write DynMesh LODs resolved no valid LOD writes."), Context);
		}
		else
		{
			// Cheap tripwire for a miswired graph: a warning, never a failure.
			if (Settings->LODAssignmentMode == EPCGUtilsLODAssignmentMode::ByInputOrder &&
				Settings->bWarnOnNonMonotonicTriangleCount)
			{
				for (int32 Index = 1; Index < Writes.Num(); ++Index)
				{
					if (Writes[Index].TriangleCount > Writes[Index - 1].TriangleCount)
					{
						PCGLog::LogWarningOnGraph(FText::Format(
							LOCTEXT("NonMonotonicTriangles", "Write DynMesh LODs: input {0} ({1} triangles) has more geometry than input {2} ({3} triangles). LODs are usually wired most to least detailed."),
							FText::AsNumber(Writes[Index].InputOrder), FText::AsNumber(Writes[Index].TriangleCount),
							FText::AsNumber(Writes[Index - 1].InputOrder), FText::AsNumber(Writes[Index - 1].TriangleCount)),
							Context);
					}
				}
			}

			Writes.Sort([](const FResolvedLODWrite& A, const FResolvedLODWrite& B)
			{
				return A.LODIndex < B.LODIndex;
			});

			// Grow the source-model array once up front. CopyMeshToStaticMesh also grows as needed, but doing
			// it here keeps the asset's LOD count correct even if a later write fails.
			const int32 RequiredSourceModels = Writes.Last().LODIndex + 1;
			if (TargetMesh->GetNumSourceModels() < RequiredSourceModels)
			{
				TargetMesh->SetNumSourceModels(RequiredSourceModels);
			}

			FGeometryScriptCopyMeshToAssetOptions Options;
			Options.bEnableRecomputeNormals = Settings->bRecomputeNormals;
			Options.bEnableRecomputeTangents = Settings->bRecomputeTangents;
			// One rebuild after every LOD lands, rather than one per LOD.
			Options.bDeferMeshPostEditChange = true;
			// A batch graph run is not an interactive edit; keep it out of the transaction buffer.
			Options.bEmitTransaction = false;
			// Non-Nanite project: leave the asset's Nanite settings entirely alone.
			Options.bApplyNaniteSettings = false;

			for (const FResolvedLODWrite& Write : Writes)
			{
				FGeometryScriptMeshWriteLOD TargetLOD;
				TargetLOD.bWriteHiResSource = false;
				TargetLOD.LODIndex = Write.LODIndex;

				EGeometryScriptOutcomePins Outcome = EGeometryScriptOutcomePins::Failure;
				UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshToStaticMesh(
					Write.Mesh, TargetMesh, Options, TargetLOD, Outcome, Settings->bUseSectionMaterials);

				if (Outcome != EGeometryScriptOutcomePins::Success)
				{
					PCGLog::LogErrorOnGraph(FText::Format(
						LOCTEXT("LODWriteFailed", "Write DynMesh LODs failed to write LOD {0} on '{1}'."),
						FText::AsNumber(Write.LODIndex), FText::FromString(TargetPath.ToString())), Context);
					continue;
				}
				++LODsWritten;

				// CopyMeshToStaticMesh already calls ResetReductionSetting() on the written source model, so
				// the engine reducer will not re-touch geometry that was hand-decimated upstream.
				if (Settings->LODScreenSizes.IsValidIndex(Write.LODIndex) &&
					Settings->LODScreenSizes[Write.LODIndex] > 0.0f &&
					TargetMesh->IsSourceModelValid(Write.LODIndex))
				{
					TargetMesh->GetSourceModel(Write.LODIndex).ScreenSize =
						FPerPlatformFloat(Settings->LODScreenSizes[Write.LODIndex]);
				}
			}

			if (LODsWritten > 0)
			{
				const bool bHasExplicitScreenSizes = Settings->LODScreenSizes.ContainsByPredicate(
					[](float ScreenSize) { return ScreenSize > 0.0f; });
				if (bHasExplicitScreenSizes)
				{
					// Otherwise the auto screen-size pass would overwrite what was just set.
					TargetMesh->SetAutoComputeLODScreenSize(false);
				}

				TargetMesh->PostEditChange();
				TargetMesh->MarkPackageDirty();
				bSucceeded = (LODsWritten == Writes.Num());
			}
		}
	}
#else
	PCGLog::LogErrorOnGraph(
		LOCTEXT("EditorOnly", "Write DynMesh LODs mutates asset source data and only runs in the editor."), Context);
#endif // WITH_EDITOR

	// Status output ------------------------------------------------------------------------------------------
	UPCGParamData* StatusData = FPCGContext::NewObject_AnyThread<UPCGParamData>(Context);
	check(StatusData && StatusData->Metadata);
	StatusData->Metadata->CreateAttribute<FSoftObjectPath>(
		PCGWriteDynMeshLODsConstants::StatusTargetAttribute, FSoftObjectPath(),
		/*bAllowsInterpolation=*/false, /*bOverrideParent=*/false);
	StatusData->Metadata->CreateAttribute<bool>(
		PCGWriteDynMeshLODsConstants::StatusSuccessAttribute, false,
		/*bAllowsInterpolation=*/false, /*bOverrideParent=*/false);
	StatusData->Metadata->CreateAttribute<int32>(
		PCGWriteDynMeshLODsConstants::StatusLODsWrittenAttribute, 0,
		/*bAllowsInterpolation=*/false, /*bOverrideParent=*/false);

	const PCGMetadataEntryKey StatusEntry = StatusData->Metadata->AddEntry();
	StatusData->Metadata->GetMutableTypedAttribute<FSoftObjectPath>(
		PCGWriteDynMeshLODsConstants::StatusTargetAttribute)->SetValue(StatusEntry, TargetPath);
	StatusData->Metadata->GetMutableTypedAttribute<bool>(
		PCGWriteDynMeshLODsConstants::StatusSuccessAttribute)->SetValue(StatusEntry, bSucceeded);
	StatusData->Metadata->GetMutableTypedAttribute<int32>(
		PCGWriteDynMeshLODsConstants::StatusLODsWrittenAttribute)->SetValue(StatusEntry, LODsWritten);

	FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
	Output.Data = StatusData;
	Output.Pin = PCGWriteDynMeshLODsConstants::OutputLabel;

	return true;
}

#undef LOCTEXT_NAMESPACE
