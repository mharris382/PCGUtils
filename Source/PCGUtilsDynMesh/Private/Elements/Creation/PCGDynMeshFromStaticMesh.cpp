// Copyright Max Harris

#include "Elements/Creation/PCGDynMeshFromStaticMesh.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGSpatialData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GeometryScript/MeshAssetFunctions.h"
#include "Helpers/PCGUtilsDataDomainHelpers.h"
#include "Materials/MaterialInterface.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"
#include "Metadata/PCGMetadataAttributeTraits.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#if WITH_EDITOR
#include "Helpers/PCGDynamicTrackingHelpers.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGDynMeshFromStaticMesh)

#define LOCTEXT_NAMESPACE "PCGDynMeshFromStaticMesh"

// Named, not anonymous: unity builds put this file beside others with helpers of the same names.
namespace PCGDynMeshFromStaticMeshPrivate
{
	/** One LOD of a mesh asset, read once however many elements name the asset. */
	struct FReadMesh
	{
		bool bRead = false;
		int32 LOD = 0;
		UE::Geometry::FDynamicMesh3 Mesh;
		TArray<UMaterialInterface*> Materials;
	};

	/** The LODs to read from Asset: the requested one, or with All LODs every LOD the asset has. */
	TArray<FGeometryScriptMeshReadLOD> GetLODsToRead(UObject* Asset, const UPCGDynMeshFromStaticMeshSettings& Settings)
	{
		const EGeometryScriptLODType Type = Settings.RequestedLODType;
		if (!Settings.bReadAllLODs || Type == EGeometryScriptLODType::HiResSourceModel)
		{
			return { FGeometryScriptMeshReadLOD{ Type, Settings.RequestedLODIndex } };
		}

		TArray<FGeometryScriptMeshReadLOD> LODs;
		if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Asset))
		{
			if (Type == EGeometryScriptLODType::RenderData)
			{
				for (int32 LOD = 0; LOD < StaticMesh->GetNumLODs(); ++LOD)
				{
					LODs.Add({ EGeometryScriptLODType::RenderData, LOD });
				}
				return LODs;
			}
#if WITH_EDITOR
			const int32 NumSource = StaticMesh->GetNumSourceModels();
			const int32 NumLODs = FMath::Max(NumSource, StaticMesh->GetNumLODs());
			for (int32 LOD = 0; LOD < NumLODs; ++LOD)
			{
				if (LOD < NumSource && StaticMesh->IsSourceModelValid(LOD))
				{
					// Max Available's LOD 0 is the hi-res source where there is one.
					LODs.Add({ Type == EGeometryScriptLODType::MaxAvailable && LOD == 0 ? EGeometryScriptLODType::MaxAvailable
						: EGeometryScriptLODType::SourceModel, LOD });
				}
				else if (Type == EGeometryScriptLODType::MaxAvailable && LOD < StaticMesh->GetNumLODs())
				{
					LODs.Add({ EGeometryScriptLODType::RenderData, LOD });
				}
				else
				{
					// Source Model asks for stored LODs only; Read reports the missing one.
					LODs.Add({ EGeometryScriptLODType::SourceModel, LOD });
				}
			}
#else
			for (int32 LOD = 0; LOD < StaticMesh->GetNumLODs(); ++LOD)
			{
				LODs.Add({ EGeometryScriptLODType::RenderData, LOD });
			}
#endif
			return LODs;
		}
		if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Asset))
		{
			for (int32 LOD = 0; LOD < SkeletalMesh->GetLODNum(); ++LOD)
			{
				LODs.Add({ Type == EGeometryScriptLODType::MaxAvailable ? EGeometryScriptLODType::SourceModel : Type, LOD });
			}
		}
		return LODs;
	}

	/** The LOD number a read is recorded as: Max Available and Hi-Res read the most detailed mesh. */
	int32 GetRecordedLOD(const FGeometryScriptMeshReadLOD& ReadLOD)
	{
		return ReadLOD.LODType == EGeometryScriptLODType::SourceModel || ReadLOD.LODType == EGeometryScriptLODType::RenderData
			? ReadLOD.LODIndex : 0;
	}

	/**
	 * Reads the mesh path on every element Selector picks out. False, with the reason in OutError, when the
	 * attribute is missing or holds neither a soft object path nor a string.
	 */
	bool ReadMeshPaths(const UPCGData* Data, const FPCGAttributePropertyInputSelector& Selector, TArray<FSoftObjectPath>& OutPaths, FText& OutError)
	{
		const TUniquePtr<const IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(Data, Selector, /*bQuiet=*/true);
		const TUniquePtr<const IPCGAttributeAccessorKeys> Keys = PCGAttributeAccessorHelpers::CreateConstKeys(Data, Selector);
		if (!Accessor || !Keys)
		{
			OutError = FText::Format(LOCTEXT("MissingAttribute", "Mesh Attribute '{0}' was not found"), FText::FromString(Selector.ToString()));
			return false;
		}

		const int32 Count = Keys->GetNum();
		const uint16 Type = Accessor->GetUnderlyingType();
		if (Type == PCG::Private::MetadataTypes<FSoftObjectPath>::Id)
		{
			OutPaths.SetNum(Count);
			return Count == 0 || Accessor->GetRange<FSoftObjectPath>(OutPaths, 0, *Keys);
		}
		if (Type == PCG::Private::MetadataTypes<FString>::Id)
		{
			TArray<FString> Strings;
			Strings.SetNum(Count);
			if (Count > 0 && !Accessor->GetRange<FString>(Strings, 0, *Keys))
			{
				return false;
			}
			OutPaths.Reset(Count);
			for (const FString& String : Strings)
			{
				OutPaths.Emplace(String);
			}
			return true;
		}
		OutError = FText::Format(LOCTEXT("WrongType", "Mesh Attribute '{0}' is {1}; it must be a soft object path or a string"),
			FText::FromString(Selector.ToString()), StaticEnum<EPCGMetadataTypes>()->GetDisplayNameTextByValue(Type));
		return false;
	}

	/** True when Selector reads the data's @Data domain rather than its elements. */
	bool SelectsDataDomain(const UPCGData* Data, const FPCGAttributePropertyInputSelector& Selector)
	{
		FPCGMetadataDomainID Domain = Data->GetMetadataDomainIDFromSelector(Selector);
		if (Domain == PCGMetadataDomainID::Default)
		{
			Domain = Data->GetDefaultMetadataDomainID();
		}
		return Domain == PCGMetadataDomainID::Data;
	}

	void ReadMeshLOD(FPCGContext* Context, UObject* Asset, const UPCGDynMeshFromStaticMeshSettings& Settings,
		const FGeometryScriptMeshReadLOD& MeshReadLOD, FReadMesh& Read)
	{
		Read.LOD = GetRecordedLOD(MeshReadLOD);

		// The engine node's read, unchanged.
		UDynamicMesh* Converted = FPCGContext::NewObject_AnyThread<UDynamicMesh>(Context);
		FGeometryScriptCopyMeshFromAssetOptions Options{};
		EGeometryScriptOutcomePins Outcome = EGeometryScriptOutcomePins::Failure;
		TArray<FName> SlotNames;
		if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Asset))
		{
			UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshFromStaticMeshV2(StaticMesh, Converted, Options, MeshReadLOD, Outcome, /*bUseSectionMaterials=*/false);
			if (Outcome == EGeometryScriptOutcomePins::Success && Settings.bExtractMaterials)
			{
				UGeometryScriptLibrary_StaticMeshFunctions::GetMaterialListFromStaticMesh(StaticMesh, Read.Materials, SlotNames);
			}
		}
		else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Asset))
		{
			UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshFromSkeletalMesh(SkeletalMesh, Converted, Options, MeshReadLOD, Outcome);
			if (Outcome == EGeometryScriptOutcomePins::Success && Settings.bExtractMaterials)
			{
				UGeometryScriptLibrary_StaticMeshFunctions::GetMaterialListFromSkeletalMesh(SkeletalMesh, Read.Materials, SlotNames);
			}
		}
		if (Outcome == EGeometryScriptOutcomePins::Success)
		{
			Read.bRead = true;
			Read.Mesh = Converted->GetMeshRef();
		}
	}

	/** Every LOD of Asset this node reads, read once however many elements name it. */
	const TArray<FReadMesh>& ReadMeshAsset(FPCGContext* Context, UObject* Asset, const UPCGDynMeshFromStaticMeshSettings& Settings, TMap<UObject*, TArray<FReadMesh>>& Cache)
	{
		if (const TArray<FReadMesh>* Existing = Cache.Find(Asset))
		{
			return *Existing;
		}
		TArray<FReadMesh>& Reads = Cache.Add(Asset);
		for (const FGeometryScriptMeshReadLOD& ReadLOD : GetLODsToRead(Asset, Settings))
		{
			ReadMeshLOD(Context, Asset, Settings, ReadLOD, Reads.AddDefaulted_GetRef());
		}
		return Reads;
	}
}

#if WITH_EDITOR
FText UPCGDynMeshFromStaticMeshSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DynMesh | From Static Mesh");
}

FText UPCGDynMeshFromStaticMeshSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Makes one DynMesh per mesh named on In: the engine's Mesh To Dynamic Mesh, reading its meshes from a pin. "
		"Mesh Attribute names a static or skeletal mesh on every entry or point (or once on @Data). Each DynMesh keeps "
		"its input's tags and @Data, and takes the attributes of the entry that named it as @Data.");
}
#endif

TArray<FPCGPinProperties> UPCGDynMeshFromStaticMeshSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Any, /*bAllowMultipleConnections=*/true, /*bAllowMultipleData=*/true,
		LOCTEXT("InTooltip", "Data naming meshes in Mesh Attribute: an attribute set listing assets, points, or any data naming one mesh on @Data."))
		.SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGDynMeshFromStaticMeshSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::DynamicMesh, /*bAllowMultipleConnections=*/true, /*bAllowMultipleData=*/true,
		LOCTEXT("OutTooltip", "One DynMesh per mesh named, in input then element order, with its input's tags."));
	return Pins;
}

FPCGElementPtr UPCGDynMeshFromStaticMeshSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshFromStaticMeshElement>();
}

bool FPCGDynMeshFromStaticMeshElement::CanExecuteOnlyOnMainThread(FPCGContext* Context) const
{
	// Without a context there is no telling which phase this is, so stay on the game thread to be safe.
	return !Context || Context->CurrentPhase == EPCGExecutionPhase::PrepareData;
}

bool FPCGDynMeshFromStaticMeshElement::PrepareDataInternal(FPCGContext* InContext) const
{
	using namespace PCGDynMeshFromStaticMeshPrivate;
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGDynMeshFromStaticMeshElement::PrepareDataInternal);
	FPCGDynMeshFromStaticMeshContext* Context = static_cast<FPCGDynMeshFromStaticMeshContext*>(InContext);
	check(Context);
	const UPCGDynMeshFromStaticMeshSettings* Settings = Context->GetInputSettings<UPCGDynMeshFromStaticMeshSettings>();
	check(Settings);

	if (Context->WasLoadRequested())
	{
		return true;
	}

	// Errors wait for Execute, which reads the same paths again.
	TSet<FSoftObjectPath> ToLoad;
	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel))
	{
		if (!Input.Data)
		{
			continue;
		}
		TArray<FSoftObjectPath> Paths;
		FText Ignored;
		if (ReadMeshPaths(Input.Data, Settings->MeshAttribute.CopyAndFixLast(Input.Data), Paths, Ignored))
		{
			for (const FSoftObjectPath& Path : Paths)
			{
				if (!Path.IsNull())
				{
					ToLoad.Add(Path);
				}
			}
		}
	}
	if (Settings->bExtractMaterials)
	{
		for (const TSoftObjectPtr<UMaterialInterface>& Material : Settings->OverrideMaterials)
		{
			if (!Material.IsNull())
			{
				ToLoad.Add(Material.ToSoftObjectPath());
			}
		}
	}
	if (ToLoad.IsEmpty())
	{
		return true;
	}
	return Context->RequestResourceLoad(Context, ToLoad.Array(), !Settings->bSynchronousLoad);
}

bool FPCGDynMeshFromStaticMeshElement::ExecuteInternal(FPCGContext* Context) const
{
	using namespace PCGDynMeshFromStaticMeshPrivate;
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGDynMeshFromStaticMeshElement::ExecuteInternal);
	check(Context);
	const UPCGDynMeshFromStaticMeshSettings* Settings = Context->GetInputSettings<UPCGDynMeshFromStaticMeshSettings>();
	check(Settings);

	TArray<UMaterialInterface*> OverrideMaterials;
	if (Settings->bExtractMaterials && !Settings->OverrideMaterials.IsEmpty())
	{
		for (int32 MaterialIndex = 0; MaterialIndex < Settings->OverrideMaterials.Num(); ++MaterialIndex)
		{
			UMaterialInterface* Material = Settings->OverrideMaterials[MaterialIndex].LoadSynchronous();
			if (!Material)
			{
				PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("MaterialNotLoaded",
					"DynMesh | From Static Mesh: Override Materials [{0}] ('{1}') could not be loaded; no meshes made."),
					FText::AsNumber(MaterialIndex), FText::FromString(Settings->OverrideMaterials[MaterialIndex].ToString())), Context);
				return true;
			}
			OverrideMaterials.Add(Material);
		}
	}

	if (Settings->bWriteLODAttribute && Settings->LODAttribute.IsNone())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("LODNameNone",
			"DynMesh | From Static Mesh: LOD Attribute is None while its output is on; name it or turn it off. No meshes made."), Context);
		return true;
	}
	if (Settings->bWriteSourceMeshAttribute && Settings->SourceMeshAttribute.IsNone())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("SourceNameNone",
			"DynMesh | From Static Mesh: Source Mesh Attribute is None while its output is on; name it or turn it off. No meshes made."), Context);
		return true;
	}
	if (Settings->bWriteLODAttribute && Settings->bWriteSourceMeshAttribute && Settings->LODAttribute == Settings->SourceMeshAttribute)
	{
		PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("SameNames",
			"DynMesh | From Static Mesh: LOD Attribute and Source Mesh Attribute are both '{0}'; they must differ. No meshes made."),
			FText::FromName(Settings->LODAttribute)), Context);
		return true;
	}

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	TMap<UObject*, TArray<FReadMesh>> ReadMeshes;
#if WITH_EDITOR
	TSet<FSoftObjectPath> TrackedPaths;
#endif

	for (int32 InputIndex = 0; InputIndex < Inputs.Num(); ++InputIndex)
	{
		const UPCGData* Data = Inputs[InputIndex].Data;
		if (!Data)
		{
			continue;
		}
		const FPCGAttributePropertyInputSelector Selector = Settings->MeshAttribute.CopyAndFixLast(Data);
		TArray<FSoftObjectPath> Paths;
		FText ReadError;
		if (!ReadMeshPaths(Data, Selector, Paths, ReadError))
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("ReadFailed", "DynMesh | From Static Mesh: input {0}: {1}; skipped."),
				FText::AsNumber(InputIndex), ReadError), Context);
			continue;
		}
		const bool bElementsAreData = SelectsDataDomain(Data, Selector);
		const UPCGSpatialData* SpatialData = Cast<const UPCGSpatialData>(Data);

		for (int32 ElementIndex = 0; ElementIndex < Paths.Num(); ++ElementIndex)
		{
			const FSoftObjectPath& Path = Paths[ElementIndex];
			if (Path.IsNull())
			{
				PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("EmptyPath",
					"DynMesh | From Static Mesh: input {0}, element {1}: Mesh Attribute '{2}' is empty; no mesh made for it."),
					FText::AsNumber(InputIndex), FText::AsNumber(ElementIndex), FText::FromString(Selector.ToString())), Context);
				continue;
			}
#if WITH_EDITOR
			TrackedPaths.Add(Path);
#endif
			UObject* Asset = Path.ResolveObject();
			if (!Asset)
			{
				Asset = Path.TryLoad();
			}
			if (!Asset)
			{
				PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("NotLoaded",
					"DynMesh | From Static Mesh: input {0}, element {1}: '{2}' could not be loaded; no mesh made for it."),
					FText::AsNumber(InputIndex), FText::AsNumber(ElementIndex), FText::FromString(Path.ToString())), Context);
				continue;
			}
			if (!Asset->IsA<UStaticMesh>() && !Asset->IsA<USkeletalMesh>())
			{
				PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("NotMesh",
					"DynMesh | From Static Mesh: input {0}, element {1}: '{2}' is a {3}, not a static or skeletal mesh; no mesh made for it."),
					FText::AsNumber(InputIndex), FText::AsNumber(ElementIndex), FText::FromString(Path.ToString()),
					FText::FromString(Asset->GetClass()->GetName())), Context);
				continue;
			}

			const TArray<FReadMesh>& Reads = ReadMeshAsset(Context, Asset, *Settings, ReadMeshes);
			if (Reads.IsEmpty())
			{
				PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("NoLODs",
					"DynMesh | From Static Mesh: input {0}, element {1}: '{2}' has no LODs of the requested LOD type; no mesh made for it."),
					FText::AsNumber(InputIndex), FText::AsNumber(ElementIndex), FText::FromString(Path.ToString())), Context);
				continue;
			}
			for (const FReadMesh& Read : Reads)
			{
				if (!Read.bRead)
				{
					PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("NotRead",
						"DynMesh | From Static Mesh: input {0}, element {1}: the geometry of '{2}' could not be read at LOD {3}; no mesh made for it."),
						FText::AsNumber(InputIndex), FText::AsNumber(ElementIndex), FText::FromString(Path.ToString()), FText::AsNumber(Read.LOD)), Context);
					continue;
				}
				if (!OverrideMaterials.IsEmpty() && OverrideMaterials.Num() != Read.Materials.Num())
				{
					PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("MaterialCount",
						"DynMesh | From Static Mesh: input {0}, element {1}: '{2}' has {3} materials, and Override Materials has {4}; they must match; no mesh made for it."),
						FText::AsNumber(InputIndex), FText::AsNumber(ElementIndex), FText::FromString(Path.ToString()),
						FText::AsNumber(Read.Materials.Num()), FText::AsNumber(OverrideMaterials.Num())), Context);
					continue;
				}

				UPCGDynamicMeshData* OutputData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(Context);
				UE::Geometry::FDynamicMesh3 Mesh = Read.Mesh;
				OutputData->Initialize(MoveTemp(Mesh), !Settings->bExtractMaterials ? TArray<UMaterialInterface*>()
					: !OverrideMaterials.IsEmpty() ? OverrideMaterials : Read.Materials);
				if (SpatialData)
				{
					OutputData->TargetActor = SpatialData->TargetActor;
				}
				if (FPCGMetadataDomain* OutputDomain = PCGUtilsDataDomain::GetMutableDataDomain(OutputData))
				{
					if (Settings->bCopyDataAttributes)
					{
						PCGUtilsDataDomain::CopyDataAttributes(Data, *OutputDomain);
					}
					if (Settings->bCopyElementAttributes && !bElementsAreData)
					{
						PCGUtilsDataDomain::CopyElementAttributes(Data, Selector.GetDomainName(), ElementIndex, *OutputDomain);
					}
					if (Settings->bWriteLODAttribute)
					{
						PCGUtilsDataDomain::WriteValue<int32>(*OutputDomain, Settings->LODAttribute, Read.LOD);
					}
					if (Settings->bWriteSourceMeshAttribute)
					{
						PCGUtilsDataDomain::WriteValue<FSoftObjectPath>(*OutputDomain, Settings->SourceMeshAttribute, FSoftObjectPath(Asset));
					}
				}

				FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[InputIndex]);
				Output.Data = OutputData;
				Output.Pin = PCGPinConstants::DefaultOutputLabel;
			}
		}
	}

#if WITH_EDITOR
	// Regenerate when a mesh read here changes, as the engine node does for its overridden mesh.
	if (!TrackedPaths.IsEmpty())
	{
		FPCGDynamicTrackingHelper Tracking;
		Tracking.EnableAndInitialize(Context, TrackedPaths.Num());
		for (const FSoftObjectPath& Path : TrackedPaths)
		{
			Tracking.AddToTracking(FPCGSelectionKey::CreateFromPath(Path), /*bIsCulled=*/false);
		}
		Tracking.Finalize(Context);
	}
#endif

	return true;
}

#undef LOCTEXT_NAMESPACE
