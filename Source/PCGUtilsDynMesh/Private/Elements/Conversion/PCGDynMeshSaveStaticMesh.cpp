// Copyright Max Harris

#include "Elements/Conversion/PCGDynMeshSaveStaticMesh.h"
#include "Helpers/PCGUtilsParallelInputs.h"

#include "Data/PCGDynamicMeshData.h"
#include "Engine/StaticMesh.h"
#include "Helpers/PCGUtilsDataDomainHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Misc/Paths.h"
#include "PCGAssetExporter.h"
#include "PCGAssetExporterUtils.h"
#include "PCGContext.h"
#include "PCGParamData.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGDynMeshSaveStaticMesh)

#define LOCTEXT_NAMESPACE "PCGDynMeshSaveStaticMesh"

namespace PCGDynMeshSaveStaticMeshPrivate
{
	/** One mesh to save, once its name has been worked out and checked. */
	struct FSaveJob
	{
		int32 InputIndex = INDEX_NONE;
		const UPCGDynamicMeshData* MeshData = nullptr;
		FString AssetName;
		FString AssetPath;
		int32 LOD = 0;
		/** Written to the asset's Hi-Res Source Model instead of a LOD. */
		bool bHiResSource = false;
	};

	/** One asset to write: a single mesh, or with Save LODs From Attribute, its LODs in order and its Hi-Res Source. */
	struct FSaveAsset
	{
		FString PackageName;
		TArray<FSaveJob> LODs;
		TOptional<FSaveJob> HiResSource;
		bool bValid = true;
	};

	/**
	 * Reads the optional bool @Data attribute marking a Hi-Res Source. A missing attribute reads as false (the mesh
	 * is a LOD); one of another type fails with OutError.
	 */
	bool ReadIsHiResSource(const UPCGData* Data, FName Name, int32 InputIndex, bool& bOutIsSource, FText& OutError)
	{
		bOutIsSource = false;
		const UPCGMetadata* Metadata = Data ? Data->ConstMetadata() : nullptr;
		const FPCGMetadataAttributeBase* Attribute = Metadata
			? Metadata->GetConstAttribute(FPCGAttributeIdentifier(Name, PCGMetadataDomainID::Data)) : nullptr;
		if (!Attribute)
		{
			return true;
		}
		if (Attribute->GetTypeId() != PCG::Private::MetadataTypes<bool>::Id)
		{
			OutError = FText::Format(LOCTEXT("HiResType",
				"DynMesh | Save Static Mesh: input {0}: @Data attribute '{1}' (High Quality Source Attribute) is not a Boolean; not saved."),
				FText::AsNumber(InputIndex), FText::FromName(Name));
			return false;
		}
		bOutIsSource = static_cast<const FPCGMetadataAttribute<bool>*>(Attribute)->GetValueFromItemKey(PCGFirstEntryKey);
		return true;
	}

	/** Reads an integer @Data attribute. False with OutError naming the failed condition. */
	bool ReadLOD(const UPCGData* Data, FName Name, int32 InputIndex, int32& OutLOD, FText& OutError)
	{
		const UPCGMetadata* Metadata = Data ? Data->ConstMetadata() : nullptr;
		const FPCGMetadataAttributeBase* Attribute = Metadata
			? Metadata->GetConstAttribute(FPCGAttributeIdentifier(Name, PCGMetadataDomainID::Data)) : nullptr;
		if (!Attribute)
		{
			OutError = FText::Format(LOCTEXT("NoLOD",
				"DynMesh | Save Static Mesh: input {0} has no @Data attribute '{1}' (LOD Attribute), which Save LODs From "
				"Attribute needs on every mesh; not saved."), FText::AsNumber(InputIndex), FText::FromName(Name));
			return false;
		}
		if (Attribute->GetTypeId() == PCG::Private::MetadataTypes<int32>::Id)
		{
			OutLOD = static_cast<const FPCGMetadataAttribute<int32>*>(Attribute)->GetValueFromItemKey(PCGFirstEntryKey);
		}
		else if (Attribute->GetTypeId() == PCG::Private::MetadataTypes<int64>::Id)
		{
			OutLOD = static_cast<int32>(static_cast<const FPCGMetadataAttribute<int64>*>(Attribute)->GetValueFromItemKey(PCGFirstEntryKey));
		}
		else
		{
			OutError = FText::Format(LOCTEXT("LODType",
				"DynMesh | Save Static Mesh: input {0}: @Data attribute '{1}' (LOD Attribute) is not an integer (expected "
				"int32 or int64); not saved."), FText::AsNumber(InputIndex), FText::FromName(Name));
			return false;
		}
		if (OutLOD < 0)
		{
			OutError = FText::Format(LOCTEXT("NegativeLOD",
				"DynMesh | Save Static Mesh: input {0}: @Data '{1}' (LOD Attribute) is {2}; a LOD must be 0 or more; not saved."),
				FText::AsNumber(InputIndex), FText::FromName(Name), FText::AsNumber(OutLOD));
			return false;
		}
		return true;
	}
}

UPCGDynMeshSaveStaticMeshSettings::UPCGDynMeshSaveStaticMeshSettings()
{
	// DynMesh data's default domain is @Data, so the attribute names need no prefix.
	AssetName.Attribute.Update(TEXT("AssetName"));
	AssetPath.Attribute.Update(TEXT("AssetPath"));
}

#if WITH_EDITOR
FText UPCGDynMeshSaveStaticMeshSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DynMesh | Save Static Mesh");
}

FText UPCGDynMeshSaveStaticMeshSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Saves every DynMesh on In as a Static Mesh asset, in one run: the engine's Save Dynamic Mesh To Asset for many meshes. "
		"Asset Name and Asset Path are constants or read from each mesh's @Data (Get Asset Save Parameters in Input Data mode writes both). "
		"A constant Asset Name given more than one mesh gets the mesh's index as a suffix: Name_0, Name_1, ... "
		"With Save LODs From Attribute, meshes naming the same asset become its LODs, numbered by @Data.LOD. Editor only.");
}

TArray<FPCGSettingsOverridableParam> UPCGDynMeshSaveStaticMeshSettings::GatherOverridableParams() const
{
	TArray<FPCGSettingsOverridableParam> Params = Super::GatherOverridableParams();
	PCGUtilsInputShorthands::ApplyOverrideLabels(Params);
	return Params;
}
#endif

TArray<FPCGPinProperties> UPCGDynMeshSaveStaticMeshSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::DynamicMesh, /*bAllowMultipleConnections=*/true, /*bAllowMultipleData=*/true,
		LOCTEXT("InTooltip", "The DynMeshes to save, one asset each (or one LOD each, with Save LODs From Attribute)."))
		.SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGDynMeshSaveStaticMeshSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGDynMeshSaveStaticMeshConstants::OutputLabel, EPCGDataType::Param, /*bAllowMultipleConnections=*/true, /*bAllowMultipleData=*/true,
		LOCTEXT("OutTooltip", "One attribute set per saved mesh, in input order, naming the asset in Output Asset Attribute Name. It keeps the mesh's tags and @Data."));
	return Pins;
}

FPCGElementPtr UPCGDynMeshSaveStaticMeshSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshSaveStaticMeshElement>();
}

bool FPCGDynMeshSaveStaticMeshElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGDynMeshSaveStaticMeshElement::ExecuteInternal);
	using namespace PCGDynMeshSaveStaticMeshPrivate;
	check(Context);

#if WITH_EDITOR
	const UPCGDynMeshSaveStaticMeshSettings* Settings = Context->GetInputSettings<UPCGDynMeshSaveStaticMeshSettings>();
	check(Settings);

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	if (Inputs.IsEmpty())
	{
		return true;
	}

	if (Settings->OutputAssetAttributeName.IsNone())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("NoOutputName",
			"DynMesh | Save Static Mesh: Output Asset Attribute Name is None; it names the attribute each output records its saved asset in. Nothing was saved."), Context);
		return true;
	}
	const bool bLODs = Settings->bSaveLODsFromAttribute;
	if (bLODs && Settings->LODAttribute.IsNone())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("NoLODName",
			"DynMesh | Save Static Mesh: LOD Attribute is None while Save LODs From Attribute is on; it names the @Data attribute holding each mesh's LOD. Nothing was saved."), Context);
		return true;
	}
	const bool bHiResSources = bLODs && Settings->bSaveHighQualitySourceFromAttribute;
	if (bHiResSources && Settings->HighQualitySourceAttribute.IsNone())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("NoHiResName",
			"DynMesh | Save Static Mesh: High Quality Source Attribute is None while Save High Quality Source From Attribute is on; name the @Data attribute, or turn the option off. Nothing was saved."), Context);
		return true;
	}
	if (bHiResSources && Settings->HighQualitySourceAttribute == Settings->LODAttribute)
	{
		PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("HiResIsLOD",
			"DynMesh | Save Static Mesh: High Quality Source Attribute and LOD Attribute are both '{0}'; they must differ. Nothing was saved."),
			FText::FromName(Settings->LODAttribute)), Context);
		return true;
	}

	// Work out and check every name first, so a clash between two meshes is reported before either is written.
	// With LODs, meshes sharing a name are one asset's LODs, so a constant name is not numbered.
	const bool bNumberConstantName = Settings->AssetName.IsConstant() && Inputs.Num() > 1 && !bLODs;
	TArray<FSaveAsset> Assets;
	TMap<FString, int32> AssetOfPackage;
	for (int32 InputIndex = 0; InputIndex < Inputs.Num(); ++InputIndex)
	{
		const UPCGDynamicMeshData* MeshData = Cast<const UPCGDynamicMeshData>(Inputs[InputIndex].Data);
		if (!MeshData || !MeshData->GetDynamicMesh())
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("NotDynMesh",
				"DynMesh | Save Static Mesh: input {0} is not DynMesh data; not saved."), FText::AsNumber(InputIndex)), Context);
			continue;
		}

		FSaveJob Job;
		Job.InputIndex = InputIndex;
		Job.MeshData = MeshData;
		FText ReadError;
		if (!Settings->AssetName.TryReadDataValue(MeshData, Job.AssetName, ReadError, LOCTEXT("AssetNameLabel", "Asset Name"), InputIndex))
		{
			PCGLog::LogErrorOnGraph(ReadError, Context);
			continue;
		}
		if (!Settings->AssetPath.TryReadDataValue(MeshData, Job.AssetPath, ReadError, LOCTEXT("AssetPathLabel", "Asset Path"), InputIndex))
		{
			PCGLog::LogErrorOnGraph(ReadError, Context);
			continue;
		}
		if (bHiResSources && !ReadIsHiResSource(MeshData, Settings->HighQualitySourceAttribute, InputIndex, Job.bHiResSource, ReadError))
		{
			PCGLog::LogErrorOnGraph(ReadError, Context);
			continue;
		}
		// A Hi-Res Source is not a LOD, so its LOD Attribute (Generate LODs writes -1) is not read.
		if (bLODs && !Job.bHiResSource && !ReadLOD(MeshData, Settings->LODAttribute, InputIndex, Job.LOD, ReadError))
		{
			PCGLog::LogErrorOnGraph(ReadError, Context);
			continue;
		}
		Job.AssetName.TrimStartAndEndInline();
		Job.AssetPath.TrimStartAndEndInline();
		if (Job.AssetName.IsEmpty())
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("EmptyName",
				"DynMesh | Save Static Mesh: input {0}: Asset Name is empty; it must name the asset to save; not saved."), FText::AsNumber(InputIndex)), Context);
			continue;
		}
		if (Job.AssetPath.IsEmpty())
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("EmptyPath",
				"DynMesh | Save Static Mesh: input {0}: Asset Path is empty; it must name a content folder such as /Game/Meshes; not saved."), FText::AsNumber(InputIndex)), Context);
			continue;
		}
		if (bNumberConstantName)
		{
			Job.AssetName = FString::Printf(TEXT("%s_%d"), *Job.AssetName, InputIndex);
		}

		const FString PackageName = FPaths::Combine(Job.AssetPath, Job.AssetName);
		if (Job.bHiResSource)
		{
			const int32* Existing = AssetOfPackage.Find(PackageName);
			const int32 AssetIndex = Existing ? *Existing : Assets.Num();
			if (!Existing)
			{
				AssetOfPackage.Add(PackageName, AssetIndex);
				Assets.AddDefaulted_GetRef().PackageName = PackageName;
			}
			FSaveAsset& Asset = Assets[AssetIndex];
			if (Asset.HiResSource.IsSet())
			{
				PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("TwoHiRes",
					"DynMesh | Save Static Mesh: inputs {0} and {1} are both the High Quality Source of '{2}'; an asset has one; '{2}' not saved."),
					FText::AsNumber(Asset.HiResSource->InputIndex), FText::AsNumber(InputIndex), FText::FromString(PackageName)), Context);
				Asset.bValid = false;
				continue;
			}
			Asset.HiResSource = MoveTemp(Job);
			continue;
		}
		if (const int32* Existing = AssetOfPackage.Find(PackageName))
		{
			FSaveAsset& Asset = Assets[*Existing];
			if (!bLODs)
			{
				PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("SamePackage",
					"DynMesh | Save Static Mesh: input {0} would be saved as '{1}', which input {2} is saved as; each mesh needs its own Asset Name or Asset Path; input {0} not saved."),
					FText::AsNumber(InputIndex), FText::FromString(PackageName), FText::AsNumber(Asset.LODs[0].InputIndex)), Context);
				continue;
			}
			if (const FSaveJob* SameLOD = Asset.LODs.FindByPredicate([&Job](const FSaveJob& Other) { return Other.LOD == Job.LOD; }))
			{
				PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("SameLOD",
					"DynMesh | Save Static Mesh: inputs {0} and {1} are both LOD {2} of '{3}'; each LOD of an asset needs one mesh; '{3}' not saved."),
					FText::AsNumber(SameLOD->InputIndex), FText::AsNumber(InputIndex), FText::AsNumber(Job.LOD), FText::FromString(PackageName)), Context);
				Asset.bValid = false;
				continue;
			}
			Asset.LODs.Add(MoveTemp(Job));
			continue;
		}
		AssetOfPackage.Add(PackageName, Assets.Num());
		FSaveAsset& Asset = Assets.AddDefaulted_GetRef();
		Asset.PackageName = PackageName;
		Asset.LODs.Add(MoveTemp(Job));
	}

	if (bLODs)
	{
		for (FSaveAsset& Asset : Assets)
		{
			if (Asset.bValid && Asset.LODs.IsEmpty())
			{
				PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("HiResOnly",
					"DynMesh | Save Static Mesh: '{0}' has a High Quality Source (input {1}) but no LOD 0; an asset needs a LOD 0 to save; not saved."),
					FText::FromString(Asset.PackageName), FText::AsNumber(Asset.HiResSource.IsSet() ? Asset.HiResSource->InputIndex : INDEX_NONE)), Context);
				Asset.bValid = false;
				continue;
			}
			Asset.LODs.Sort([](const FSaveJob& A, const FSaveJob& B) { return A.LOD < B.LOD; });
			for (int32 Expected = 0; Asset.bValid && Expected < Asset.LODs.Num(); ++Expected)
			{
				if (Asset.LODs[Expected].LOD != Expected)
				{
					PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("LODGap",
						"DynMesh | Save Static Mesh: '{0}' has no LOD {1} (the next LOD given is {2}, from input {3}); its LODs must run 0, 1, 2... without gaps; not saved."),
						FText::FromString(Asset.PackageName), FText::AsNumber(Expected), FText::AsNumber(Asset.LODs[Expected].LOD),
						FText::AsNumber(Asset.LODs[Expected].InputIndex)), Context);
					Asset.bValid = false;
				}
			}
		}
	}

	int32 ValidAssets = 0;
	for (const FSaveAsset& Asset : Assets)
	{
		ValidAssets += Asset.bValid ? 1 : 0;
	}
	bool bOpenSaveDialog = Settings->bOpenSaveDialog;
	if (bOpenSaveDialog && ValidAssets > 1)
	{
		PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("NoDialogForMany",
			"DynMesh | Save Static Mesh: Open Save Dialog works with one asset; {0} assets are saved without it."), FText::AsNumber(ValidAssets)), Context);
		bOpenSaveDialog = false;
	}

	TMap<int32, UPCGParamData*> OutputOfInput;
	for (const FSaveAsset& Asset : Assets)
	{
		if (!Asset.bValid)
		{
			continue;
		}
		const FSaveJob& First = Asset.LODs[0];
		FPCGAssetExporterParameters ExportParams;
		ExportParams.bOpenSaveDialog = bOpenSaveDialog;
		ExportParams.AssetName = First.AssetName;
		ExportParams.AssetPath = First.AssetPath;
		ExportParams.bSaveOnExportEnded = Settings->bSaveOnExportEnded;

		UObject* SavedAsset = nullptr;
		UPackage* Package = UPCGAssetExporterUtils::CreateAsset<UStaticMesh>(ExportParams,
			[&Asset, Settings, Context, bLODs, &SavedAsset](const FString&, UObject* Object)
			{
				UStaticMesh* StaticMesh = CastChecked<UStaticMesh>(Object);
				for (const FSaveJob& Job : Asset.LODs)
				{
					// The engine node's export, once per LOD. With LODs, the asset is rebuilt once at the end.
					FGeometryScriptCopyMeshToAssetOptions AssetOptions = Settings->CopyMeshToAssetOptions;
					AssetOptions.bEmitTransaction = false;
					AssetOptions.bDeferMeshPostEditChange = bLODs;
					if (Settings->bExportMaterialsFromDynMesh && Job.LOD == 0 && !Job.MeshData->GetMaterials().IsEmpty())
					{
						AssetOptions.bReplaceMaterials = true;
						AssetOptions.NewMaterials = Job.MeshData->GetMaterials();
					}
					else if (Settings->bExportMaterialsFromDynMesh)
					{
						// Lower LODs index the material slots LOD 0 set.
						AssetOptions.bReplaceMaterials = false;
					}

					FGeometryScriptMeshWriteLOD WriteLOD = Settings->MeshWriteLOD;
					if (bLODs)
					{
						WriteLOD.bWriteHiResSource = false;
						WriteLOD.LODIndex = Job.LOD;
					}

					// The Geometry Script API takes a mutable mesh; it does not modify it.
					EGeometryScriptOutcomePins Outcome = EGeometryScriptOutcomePins::Failure;
					UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshToStaticMesh(const_cast<UDynamicMesh*>(Job.MeshData->GetDynamicMesh()),
						StaticMesh, AssetOptions, WriteLOD, Outcome);
					if (Outcome == EGeometryScriptOutcomePins::Failure)
					{
						PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("CopyFailed",
							"DynMesh | Save Static Mesh: input {0}: the mesh could not be written to LOD {1} of '{2}'."),
							FText::AsNumber(Job.InputIndex), FText::AsNumber(WriteLOD.LODIndex), FText::FromString(Object->GetPathName())), Context);
						return false;
					}
				}
				if (Asset.HiResSource.IsSet())
				{
					// After LOD 0, whose material slots the Hi-Res Source indexes. The engine refuses to replace
					// materials while writing it.
					const FSaveJob& Source = Asset.HiResSource.GetValue();
					FGeometryScriptCopyMeshToAssetOptions AssetOptions = Settings->CopyMeshToAssetOptions;
					AssetOptions.bEmitTransaction = false;
					AssetOptions.bDeferMeshPostEditChange = true;
					AssetOptions.bReplaceMaterials = false;
					FGeometryScriptMeshWriteLOD WriteLOD;
					WriteLOD.bWriteHiResSource = true;
					EGeometryScriptOutcomePins Outcome = EGeometryScriptOutcomePins::Failure;
					UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshToStaticMesh(const_cast<UDynamicMesh*>(Source.MeshData->GetDynamicMesh()),
						StaticMesh, AssetOptions, WriteLOD, Outcome);
					if (Outcome == EGeometryScriptOutcomePins::Failure)
					{
						PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("HiResCopyFailed",
							"DynMesh | Save Static Mesh: input {0}: the mesh could not be written to the Hi-Res Source Model of '{1}'."),
							FText::AsNumber(Source.InputIndex), FText::FromString(Object->GetPathName())), Context);
						return false;
					}
				}
				if (bLODs)
				{
					// An existing asset keeps the LODs it had past the last one written unless they are removed.
					if (StaticMesh->GetNumSourceModels() > Asset.LODs.Num())
					{
						StaticMesh->SetNumSourceModels(Asset.LODs.Num());
					}
					StaticMesh->PostEditChange();
				}
				SavedAsset = Object;
				return true;
			}, Context);

		if (!Package || !SavedAsset)
		{
			continue;
		}

		TArray<const FSaveJob*> Saved;
		for (const FSaveJob& Job : Asset.LODs)
		{
			Saved.Add(&Job);
		}
		if (Asset.HiResSource.IsSet())
		{
			Saved.Add(&Asset.HiResSource.GetValue());
		}
		for (const FSaveJob* JobPtr : Saved)
		{
			const FSaveJob& Job = *JobPtr;
			UPCGParamData* OutputData = PCGUtilsAsyncObjects::NewObject<UPCGParamData>(Context);
			OutputData->Metadata->CreateAttribute<FSoftObjectPath>(Settings->OutputAssetAttributeName, FSoftObjectPath(SavedAsset),
				/*bAllowsInterpolation=*/false, /*bOverrideParent=*/false);
			OutputData->Metadata->AddEntry();
			if (FPCGMetadataDomain* OutputDomain = PCGUtilsDataDomain::GetMutableDataDomain(OutputData))
			{
				PCGUtilsDataDomain::CopyDataAttributes(Job.MeshData, *OutputDomain);
			}

			OutputOfInput.Add(Job.InputIndex, OutputData);
		}
	}

	// Emitted in input order, whichever asset each mesh went into.
	for (int32 InputIndex = 0; InputIndex < Inputs.Num(); ++InputIndex)
	{
		if (UPCGParamData* const* OutputData = OutputOfInput.Find(InputIndex))
		{
			FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[InputIndex]);
			Output.Data = *OutputData;
			Output.Pin = PCGDynMeshSaveStaticMeshConstants::OutputLabel;
		}
	}
#else
	PCGLog::LogWarningOnGraph(LOCTEXT("EditorOnly", "DynMesh | Save Static Mesh saves assets, which only the editor can do; nothing was saved."), Context);
#endif

	return true;
}

#undef LOCTEXT_NAMESPACE
