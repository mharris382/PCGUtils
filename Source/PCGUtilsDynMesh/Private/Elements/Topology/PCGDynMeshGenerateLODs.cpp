// Copyright Max Harris

#include "Elements/Topology/PCGDynMeshGenerateLODs.h"

#include "Helpers/PCGUtilsParallelInputs.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Helpers/PCGUtilsDataDomainHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataDomain.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshGenerateLODs"

namespace PCGDynMeshGenerateLODs
{
	FName DefaultLevelAttribute(int32 LODIndex)
	{
		return FName(*FString::Printf(TEXT("LOD%dRatio"), LODIndex));
	}

	/** What one LOD output needs done to its copy of the source. */
	struct FLODTarget
	{
		enum class EKind : uint8 { Keep, TriangleCount, Tolerance };
		EKind Kind = EKind::Keep;
		int32 TriangleCount = 0;
		double Tolerance = 0.0;
	};

	/** One output: an input copy, and what to do to it. */
	struct FJob
	{
		UPCGDynamicMeshData* OutputData = nullptr;
		FLODTarget Target;
	};

	/** One input's outputs, in emit order. */
	struct FInputOutputs
	{
		const FPCGTaggedData* Input = nullptr;
		int32 InputIndex = INDEX_NONE;
		/** Index into the job list of this input's first output, and how many it has. */
		int32 FirstJob = 0;
		int32 NumJobs = 0;
		/** Index among this input's jobs where its LODs start: 1 with the High Quality Source output, else 0. */
		int32 FirstLODJob = 0;
	};

	FLODTarget TargetFromKeptFraction(double KeptFraction, int32 SourceTriangles)
	{
		FLODTarget Target;
		const int32 Count = FMath::Max(1, FMath::RoundToInt32(KeptFraction * SourceTriangles));
		if (Count < SourceTriangles)
		{
			Target.Kind = FLODTarget::EKind::TriangleCount;
			Target.TriangleCount = Count;
		}
		return Target;
	}

	bool HasDataAttribute(const UPCGData* Data, FName Name)
	{
		const UPCGMetadata* Metadata = Data ? Data->ConstMetadata() : nullptr;
		const FPCGMetadataDomain* Domain = Metadata ? Metadata->GetConstMetadataDomain(PCGMetadataDomainID::Data) : nullptr;
		return Domain && Domain->HasAttribute(Name);
	}
}

UPCGDynMeshGenerateLODsSettings::UPCGDynMeshGenerateLODsSettings()
	: LOD0DecimationRatio(0.0, TEXT("LOD0Ratio"))
	, LOD0MaxTriangleCount(10000, TEXT("LOD0MaxTriangles"))
	, ReductionPerLOD(0.5, TEXT("LODReductionPerLOD"))
{
	LODs.Emplace(0.0, PCGDynMeshGenerateLODs::DefaultLevelAttribute(0));
	LODs.Emplace(0.5, PCGDynMeshGenerateLODs::DefaultLevelAttribute(1));
	LODs.Emplace(0.8, PCGDynMeshGenerateLODs::DefaultLevelAttribute(2));
}

#if WITH_EDITOR
FText UPCGDynMeshGenerateLODsSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DynMesh | Generate LODs");
}

FText UPCGDynMeshGenerateLODsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Makes a set of LODs from each high-detail DynMesh: one decimated copy per LOD, each marked with its LOD number "
		"on @Data for Paint Vertex Color and Save Static Mesh. List mode gives each LOD its own decimation ratio, max "
		"triangle count or tolerance; Automatic mode takes a LOD count and LOD 0's reduction and works out the rest. "
		"Every LOD is decimated from the source.");
}

FString UPCGDynMeshGenerateLODsSettings::GetAdditionalTitleInformation() const
{
	if (Mode == EPCGUtilsDynMeshLODMode::Automatic)
	{
		return FString::Printf(TEXT("Auto, %d LODs"), LODCount);
	}
	const TCHAR* MetricName = Metric == EPCGUtilsDynMeshLODMetric::DecimationRatio ? TEXT("ratio")
		: Metric == EPCGUtilsDynMeshLODMetric::MaxTriangleCount ? TEXT("max triangles") : TEXT("tolerance");
	return FString::Printf(TEXT("%d LODs by %s"), LODs.Num(), MetricName);
}

void UPCGDynMeshGenerateLODsSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// An entry added in the Details panel names no attribute; give it the conventional LOD<n>Ratio.
	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UPCGDynMeshGenerateLODsSettings, LODs))
	{
		for (int32 Index = 0; Index < LODs.Num(); ++Index)
		{
			FPCGAttributePropertyInputSelector& Selector = LODs[Index].Value.Attribute;
			if (Selector.GetSelection() == EPCGAttributePropertySelection::Attribute && Selector.GetAttributeName().IsNone())
			{
				Selector.SetAttributeName(PCGDynMeshGenerateLODs::DefaultLevelAttribute(Index));
			}
		}
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

TArray<FPCGSettingsOverridableParam> UPCGDynMeshGenerateLODsSettings::GatherOverridableParams() const
{
	TArray<FPCGSettingsOverridableParam> Params = Super::GatherOverridableParams();
	PCGUtilsInputShorthands::ApplyOverrideLabels(Params);
	return Params;
}
#endif

TArray<FPCGPinProperties> UPCGDynMeshGenerateLODsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::DynamicMesh, /*bAllowMultipleConnections=*/true,
		/*bAllowMultipleData=*/true, LOCTEXT("InTooltip", "The high-detail DynMeshes to make LODs of."))
		.SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGDynMeshGenerateLODsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::DynamicMesh, /*bAllowMultipleConnections=*/true,
		/*bAllowMultipleData=*/true, LOCTEXT("OutTooltip",
			"For each input, in input order: its unchanged source when Output High Quality Source Mesh is on, then its "
			"LODs, LOD 0 first. Each keeps its input's tags and @Data."));
	return Pins;
}

FPCGElementPtr UPCGDynMeshGenerateLODsSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshGenerateLODsElement>();
}

bool FPCGDynMeshGenerateLODsElement::ExecuteInternal(FPCGContext* Context) const
{
	using namespace PCGDynMeshGenerateLODs;
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGDynMeshGenerateLODsElement::Execute);
	check(Context);

	const UPCGDynMeshGenerateLODsSettings* Settings = Context->GetInputSettings<UPCGDynMeshGenerateLODsSettings>();
	check(Settings);

	// Settings checks, each on its own so the message names the exact setting.
	if (Settings->LODAttribute.IsNone())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("NoLODAttribute", "DynMesh | Generate LODs: LOD Attribute is empty; it must name the @Data attribute receiving each output's LOD number."), Context);
		return true;
	}
	if (Settings->bWriteGroupAttribute && Settings->GroupAttribute.IsNone())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("NoGroupAttribute", "DynMesh | Generate LODs: Group Attribute is on but empty; name an attribute, or turn it off."), Context);
		return true;
	}
	if (Settings->bOutputHighQualitySource && Settings->HighQualitySourceAttribute.IsNone())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("NoHQAttribute", "DynMesh | Generate LODs: Output High Quality Source Mesh is on, and High Quality Source Attribute is empty; it must name the @Data attribute marking the source output."), Context);
		return true;
	}
	if (Settings->bWriteGroupAttribute && Settings->GroupAttribute == Settings->LODAttribute)
	{
		PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("GroupIsLOD", "DynMesh | Generate LODs: Group Attribute and LOD Attribute are both '{0}'; they must differ."),
			FText::FromName(Settings->LODAttribute)), Context);
		return true;
	}
	if (Settings->bOutputHighQualitySource && Settings->HighQualitySourceAttribute == Settings->LODAttribute)
	{
		PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("HQIsLOD", "DynMesh | Generate LODs: High Quality Source Attribute and LOD Attribute are both '{0}'; they must differ."),
			FText::FromName(Settings->LODAttribute)), Context);
		return true;
	}
	if (Settings->bOutputHighQualitySource && Settings->bWriteGroupAttribute && Settings->HighQualitySourceAttribute == Settings->GroupAttribute)
	{
		PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("HQIsGroup", "DynMesh | Generate LODs: High Quality Source Attribute and Group Attribute are both '{0}'; they must differ."),
			FText::FromName(Settings->GroupAttribute)), Context);
		return true;
	}
	const bool bList = Settings->Mode == EPCGUtilsDynMeshLODMode::List;
	if (bList && Settings->LODs.IsEmpty())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("NoLODs", "DynMesh | Generate LODs: LODs has no entries; add one entry per LOD."), Context);
		return true;
	}
	if (!bList && Settings->LODCount < 1)
	{
		PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("BadLODCount", "DynMesh | Generate LODs: LOD Count is {0}; it must be at least 1."),
			FText::AsNumber(Settings->LODCount)), Context);
		return true;
	}

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	TArray<FJob> Jobs;
	TArray<FInputOutputs> PerInput;
	PerInput.Reserve(Inputs.Num());

	for (int32 InputIndex = 0; InputIndex < Inputs.Num(); ++InputIndex)
	{
		const FPCGTaggedData& Input = Inputs[InputIndex];
		const UPCGDynamicMeshData* SourceData = Cast<const UPCGDynamicMeshData>(Input.Data);
		const UDynamicMesh* SourceObject = SourceData ? SourceData->GetDynamicMesh() : nullptr;
		const UE::Geometry::FDynamicMesh3* SourceMesh = SourceObject ? SourceObject->GetMeshPtr() : nullptr;
		if (!SourceMesh)
		{
			PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("NoMesh", "DynMesh | Generate LODs: input {0} is not a DynMesh with a mesh; no LODs made for it."),
				FText::AsNumber(InputIndex)), Context);
			continue;
		}
		const int32 SourceTriangles = SourceMesh->TriangleCount();

		auto Read = [&](const auto& Shorthand, auto& OutValue, const FText& Label)
		{
			FText Error;
			if (!Shorthand.TryReadDataValue(SourceData, OutValue, Error, Label, InputIndex))
			{
				PCGLog::LogErrorOnGraph(Error, Context);
				return false;
			}
			return true;
		};

		// Work out every LOD's target before allocating anything, so a bad value skips the whole input.
		TArray<FLODTarget> Targets;
		bool bResolved = true;
		if (bList)
		{
			for (int32 LODIndex = 0; LODIndex < Settings->LODs.Num() && bResolved; ++LODIndex)
			{
				const FText Label = FText::Format(LOCTEXT("LevelLabel", "LODs[{0}] Value"), FText::AsNumber(LODIndex));
				double Value = 0.0;
				if (!Read(Settings->LODs[LODIndex].Value, Value, Label))
				{
					bResolved = false;
					break;
				}
				FLODTarget& Target = Targets.AddDefaulted_GetRef();
				switch (Settings->Metric)
				{
				case EPCGUtilsDynMeshLODMetric::DecimationRatio:
					if (Value > 1.0)
					{
						PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("RatioTooHigh",
							"DynMesh | Generate LODs: input {0}, LODs[{1}]: the decimation ratio is {2}; it must be between 0 and 1 (the fraction of triangles removed)."),
							FText::AsNumber(InputIndex), FText::AsNumber(LODIndex), FText::AsNumber(Value)), Context);
						bResolved = false;
						break;
					}
					Target = TargetFromKeptFraction(1.0 - Value, SourceTriangles);
					break;
				case EPCGUtilsDynMeshLODMetric::MaxTriangleCount:
				{
					const int32 Count = FMath::RoundToInt32(Value);
					if (Count < 1)
					{
						PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("CountTooLow",
							"DynMesh | Generate LODs: input {0}, LODs[{1}]: the max triangle count is {2}; it must be at least 1."),
							FText::AsNumber(InputIndex), FText::AsNumber(LODIndex), FText::AsNumber(Value)), Context);
						bResolved = false;
						break;
					}
					if (Count < SourceTriangles)
					{
						Target.Kind = FLODTarget::EKind::TriangleCount;
						Target.TriangleCount = Count;
					}
					break;
				}
				case EPCGUtilsDynMeshLODMetric::Tolerance:
					if (Value > 0.0)
					{
						Target.Kind = FLODTarget::EKind::Tolerance;
						Target.Tolerance = Value;
					}
					break;
				}
			}
		}
		else
		{
			double KeptFraction = 1.0;
			if (Settings->LOD0Target == EPCGUtilsDynMeshLOD0Target::DecimationRatio)
			{
				double Ratio = 0.0;
				bResolved = Read(Settings->LOD0DecimationRatio, Ratio, LOCTEXT("LOD0RatioLabel", "LOD 0 Decimation Ratio"));
				KeptFraction = 1.0 - Ratio;
			}
			else
			{
				int32 MaxTriangles = 0;
				bResolved = Read(Settings->LOD0MaxTriangleCount, MaxTriangles, LOCTEXT("LOD0MaxLabel", "LOD 0 Max Triangle Count"));
				if (bResolved && MaxTriangles < 1)
				{
					PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("LOD0MaxTooLow",
						"DynMesh | Generate LODs: input {0}: LOD 0 Max Triangle Count is {1}; it must be at least 1."),
						FText::AsNumber(InputIndex), FText::AsNumber(MaxTriangles)), Context);
					bResolved = false;
				}
				KeptFraction = SourceTriangles > 0 ? FMath::Min(1.0, static_cast<double>(MaxTriangles) / SourceTriangles) : 1.0;
			}
			double Step = 0.0;
			bResolved = bResolved && Read(Settings->ReductionPerLOD, Step, LOCTEXT("StepLabel", "Reduction Per LOD"));
			for (int32 LODIndex = 0; LODIndex < Settings->LODCount && bResolved; ++LODIndex)
			{
				Targets.Add(TargetFromKeptFraction(KeptFraction * FMath::Pow(1.0 - Step, LODIndex), SourceTriangles));
			}
		}
		if (!bResolved)
		{
			continue;
		}

		const bool bKeepGroupValue = Settings->bWriteGroupAttribute && HasDataAttribute(SourceData, Settings->GroupAttribute);
		auto MakeOutput = [&](int32 LODNumber, bool bIsSource) -> UPCGDynamicMeshData*
		{
			TArray<UMaterialInterface*> Materials;
			Materials.Reserve(SourceData->GetMaterials().Num());
			for (UMaterialInterface* Material : SourceData->GetMaterials())
			{
				Materials.Add(Material);
			}
			// Each output is the input, reduced: it starts from the input's metadata and target actor.
			UPCGDynamicMeshData* OutputData = PCGUtilsAsyncObjects::NewObject<UPCGDynamicMeshData>(Context);
			OutputData->InitializeFromData(SourceData);
			OutputData->Initialize(UE::Geometry::FDynamicMesh3(*SourceMesh), MoveTemp(Materials));
			if (FPCGMetadataDomain* Domain = PCGUtilsDataDomain::GetMutableDataDomain(OutputData))
			{
				PCGUtilsDataDomain::WriteValue<int32>(*Domain, Settings->LODAttribute, LODNumber);
				if (Settings->bOutputHighQualitySource)
				{
					PCGUtilsDataDomain::WriteValue<bool>(*Domain, Settings->HighQualitySourceAttribute, bIsSource);
				}
				if (Settings->bWriteGroupAttribute && !bKeepGroupValue)
				{
					PCGUtilsDataDomain::WriteValue<int32>(*Domain, Settings->GroupAttribute, InputIndex);
				}
			}
			return OutputData;
		};

		FInputOutputs& Outputs = PerInput.AddDefaulted_GetRef();
		Outputs.Input = &Input;
		Outputs.InputIndex = InputIndex;
		Outputs.FirstJob = Jobs.Num();
		if (Settings->bOutputHighQualitySource)
		{
			Jobs.Add({MakeOutput(-1, /*bIsSource=*/true), FLODTarget()});
			Outputs.FirstLODJob = 1;
		}
		for (int32 LODIndex = 0; LODIndex < Targets.Num(); ++LODIndex)
		{
			Jobs.Add({MakeOutput(LODIndex, /*bIsSource=*/false), Targets[LODIndex]});
		}
		Outputs.NumJobs = Jobs.Num() - Outputs.FirstJob;
	}

	// Every output owns its own mesh copy, so all of them, across inputs, decimate at once.
	const bool bParallel = !Settings->bDisableParallel && PCGUtilsParallelInputs::IsEnabledGlobally();
	const FGeometryScriptSimplifyMeshOptions Options = Settings->SimplifyOptions;
	PCGUtilsParallelInputs::ForEach(Context, Jobs.Num(), bParallel, [&Jobs, &Options](int32 JobIndex)
	{
		const FJob& Job = Jobs[JobIndex];
		if (Job.Target.Kind == FLODTarget::EKind::Keep)
		{
			return;
		}
		UDynamicMesh* Mesh = Job.OutputData->GetMutableDynamicMesh();
		if (Job.Target.Kind == FLODTarget::EKind::TriangleCount)
		{
			UGeometryScriptLibrary_MeshSimplifyFunctions::ApplySimplifyToTriangleCount(Mesh, Job.Target.TriangleCount, Options);
		}
		else
		{
			UGeometryScriptLibrary_MeshSimplifyFunctions::ApplySimplifyToTolerance(Mesh, static_cast<float>(Job.Target.Tolerance), Options);
		}
	});

	for (const FInputOutputs& Outputs : PerInput)
	{
		// A LOD with more triangles than the one before it is almost always a mistyped value.
		for (int32 Local = Outputs.FirstLODJob + 1; Local < Outputs.NumJobs; ++Local)
		{
			const int32 Previous = Jobs[Outputs.FirstJob + Local - 1].OutputData->GetDynamicMesh()->GetTriangleCount();
			const int32 Current = Jobs[Outputs.FirstJob + Local].OutputData->GetDynamicMesh()->GetTriangleCount();
			if (Current > Previous)
			{
				const int32 LODIndex = Local - Outputs.FirstLODJob;
				PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("NotDecreasing",
					"DynMesh | Generate LODs: input {0}: LOD {1} has {2} triangles, more than LOD {3}'s {4}. LODs usually get coarser; check their values."),
					FText::AsNumber(Outputs.InputIndex), FText::AsNumber(LODIndex), FText::AsNumber(Current),
					FText::AsNumber(LODIndex - 1), FText::AsNumber(Previous)), Context);
			}
		}

		for (int32 Local = 0; Local < Outputs.NumJobs; ++Local)
		{
			FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(*Outputs.Input);
			Output.Data = Jobs[Outputs.FirstJob + Local].OutputData;
			Output.Pin = PCGPinConstants::DefaultOutputLabel;
		}
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
