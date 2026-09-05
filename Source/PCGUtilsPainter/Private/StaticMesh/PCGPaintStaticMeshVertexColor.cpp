// Copyright Max Harris

#include "StaticMesh/PCGPaintStaticMeshVertexColor.h"

#include "PCGUtilsPainter.h"
#include "Factories/PCGUtilsDynMeshPainterFactory.h"
#include "StaticMesh/PCGUtilsPainterStaticMeshBackend.h"
#include "StaticMesh/PCGUtilsPainterStaticMeshTarget.h"
#include "Target/PCGUtilsPainterTarget.h"

#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGGraphExecutionStateInterface.h"
#include "Elements/PCGAddComponent.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Utils/PCGLogErrors.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif

#define LOCTEXT_NAMESPACE "PCGPaintStaticMeshVertexColor"

namespace
{
	const FName StaticMeshTargetPin = TEXT("Target");
	const FName StaticMeshPainterPin = TEXT("Painter");

	EPCGUtilsDynMeshPainterColorChannel GetStaticMeshWriteChannels(const FGeometryScriptColorFlags& Flags)
	{
		EPCGUtilsDynMeshPainterColorChannel Channels = EPCGUtilsDynMeshPainterColorChannel::None;
		if (Flags.bRed) Channels |= EPCGUtilsDynMeshPainterColorChannel::Red;
		if (Flags.bGreen) Channels |= EPCGUtilsDynMeshPainterColorChannel::Green;
		if (Flags.bBlue) Channels |= EPCGUtilsDynMeshPainterColorChannel::Blue;
		if (Flags.bAlpha) Channels |= EPCGUtilsDynMeshPainterColorChannel::Alpha;
		return Channels;
	}

	PCGUtilsPainterStaticMeshBackend::EBaseColorMode ToStaticMeshBackendBaseColor(EPCGPaintStaticMeshBaseColor Mode)
	{
		using EBackend = PCGUtilsPainterStaticMeshBackend::EBaseColorMode;
		switch (Mode)
		{
		case EPCGPaintStaticMeshBaseColor::AssetVertexColors: return EBackend::AssetVertexColors;
		case EPCGPaintStaticMeshBaseColor::White:             return EBackend::White;
		case EPCGPaintStaticMeshBaseColor::Black:             return EBackend::Black;
		case EPCGPaintStaticMeshBaseColor::Existing:
		default:                                              return EBackend::Existing;
		}
	}
}

UPCGPaintStaticMeshVertexColorSettings::UPCGPaintStaticMeshVertexColorSettings()
{
	TargetComponentAttribute.SetAttributeName(PCGAddComponentConstants::ComponentReferenceAttribute);
}

#if WITH_EDITOR
FText UPCGPaintStaticMeshVertexColorSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Paint Static Mesh Vertex Colors");
}

FText UPCGPaintStaticMeshVertexColorSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Applies a Painter to the per-component override vertex colors of existing Static Mesh Components referenced "
		"by a soft-object-path attribute. Does not modify the Static Mesh asset. The complete Painter graph is "
		"evaluated exactly once, against a canonical Dynamic Mesh of LOD0; the result is transferred to lower LODs "
		"by surface projection. Editor-authoring only. Nanite and ISM/HISM components are skipped with a warning.");
}
#endif

TArray<FPCGPinProperties> UPCGPaintStaticMeshVertexColorSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(StaticMeshTargetPin, EPCGDataType::Point | EPCGDataType::Param).SetRequiredPin();
	Pins.Emplace_GetRef(StaticMeshPainterPin, FPCGUtilsDynMeshPainterFactoryDataTypeInfo::AsId(), false, false).SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGPaintStaticMeshVertexColorSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	FPCGPinProperties& DependencyPin = Pins.Emplace_GetRef(PCGPinConstants::DefaultExecutionDependencyLabel, EPCGDataType::Any);
#if WITH_EDITOR
	DependencyPin.Tooltip = PCGPinConstants::Tooltips::ExecutionDependencyTooltip;
#endif
	DependencyPin.Usage = EPCGPinUsage::DependencyOnly;
	return Pins;
}

FPCGElementPtr UPCGPaintStaticMeshVertexColorSettings::CreateElement() const
{
	return MakeShared<FPCGPaintStaticMeshVertexColorElement>();
}

bool FPCGPaintStaticMeshVertexColorElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGPaintStaticMeshVertexColorElement::Execute);
	check(Context);

	const UPCGPaintStaticMeshVertexColorSettings* Settings =
		Context->GetInputSettings<UPCGPaintStaticMeshVertexColorSettings>();
	check(Settings);

	// Resolve the single Painter.
	const UPCGUtilsDynMeshPainterFactoryData* PainterFactory = nullptr;
	if (!PCGUtilsDynMeshPainterFactories::GetSinglePainter(Context, StaticMeshPainterPin, PainterFactory, /*bRequired=*/true))
	{
		return true;
	}

	// Collect unique component soft-object-paths from the Target input.
	TArray<FSoftObjectPath> ComponentPaths;
	{
		TSet<FSoftObjectPath> Seen;
		for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(StaticMeshTargetPin))
		{
			if (!Input.Data)
			{
				continue;
			}

			TArray<FSoftObjectPath> DataPaths;
			if (!PCGAttributeAccessorHelpers::ExtractAllValues(Input.Data, Settings->TargetComponentAttribute, DataPaths, Context))
			{
				continue;
			}

			for (FSoftObjectPath& Path : DataPaths)
			{
				if (!Path.IsNull() && !Seen.Contains(Path))
				{
					Seen.Add(Path);
					ComponentPaths.Add(MoveTemp(Path));
				}
			}
		}
	}

	if (ComponentPaths.IsEmpty())
	{
		PCGLog::LogWarningOnGraph(
			LOCTEXT("NoTargets", "Paint Static Mesh Vertex Colors resolved no component references from its Target input."),
			Context);
		return true;
	}

	// Fail fast — before touching any component — if the Painter cannot target a non-DynMesh canonical mesh.
	// The Static Mesh canonical mesh carries no backing UPCGDynamicMeshData, so this mesh-less probe context is
	// exactly what EvaluatePainterGraphOntoTarget will build per component.
	{
		TSharedPtr<FPCGUtilsDynMeshPainterOperation> Probe = PainterFactory->CreateOperation(Context);
		const FPCGUtilsDynMeshPainterEvaluationContext ProbeContext(FTransform::Identity);
		if (!Probe || !Probe->Initialize(ProbeContext))
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("PainterInitFailed", "Paint Static Mesh Vertex Colors could not initialize its Painter. Painter by Vertex ID is Dynamic Mesh-only and cannot target a Static Mesh Component."),
				Context);
			return true;
		}
	}

	FPCGUtilsPainterGraphEvaluation Evaluation;
	Evaluation.Painter = PainterFactory;
	Evaluation.WriteChannels = GetStaticMeshWriteChannels(Settings->WriteChannels);
	// The Static Mesh canonical mesh always pre-seeds its color overlay with the resolved Base Color, so the
	// shared traversal reads "existing" and Replace/Modify/From Asset/White/Black are all decided by the seed.
	Evaluation.BaseColorSource = EPCGUtilsPainterBaseColorSource::CanonicalExisting;

	FPCGUtilsPainterStaticMeshTarget::FConfig TargetConfig;
	TargetConfig.BaseColorMode = ToStaticMeshBackendBaseColor(Settings->BaseColor);
	TargetConfig.WrittenChannels = Evaluation.WriteChannels;
	TargetConfig.bConvertToSRGB = Settings->bConvertToSRGB;
	TargetConfig.bTransferToLowerLODs = (Settings->LODMode == EPCGPaintStaticMeshLODMode::AllLODs);
	TargetConfig.WeldTolerance = Settings->CanonicalWeldTolerance;

#if WITH_EDITOR
	const bool bUseTransactions = Context->ExecutionSource.Get()
		&& Context->ExecutionSource->GetExecutionState().UseTransactions();
	FScopedTransaction Transaction(LOCTEXT("PaintSMTransaction", "Paint Static Mesh Vertex Colors"), bUseTransactions);
#endif

	int32 UnresolvedCount = 0;
	int32 PaintedComponentCount = 0;

	for (const FSoftObjectPath& Path : ComponentPaths)
	{
		UObject* Object = Path.ResolveObject();
		if (!Object)
		{
			Object = Path.TryLoad();
		}
		if (!Object)
		{
			++UnresolvedCount;
			continue;
		}

		UStaticMeshComponent* Component = Cast<UStaticMeshComponent>(Object);
		if (!Component)
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("NotStaticMeshComponent", "'{0}' is not a Static Mesh Component and was skipped."),
				FText::FromString(Path.ToString())), Context);
			continue;
		}

		if (Component->IsA<UInstancedStaticMeshComponent>())
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("InstancedComponent", "'{0}' is an Instanced Static Mesh component. Override vertex colors are one buffer shared by every instance, so this node cannot paint individual instances — use Per Instance Custom Data. Skipped."),
				FText::FromString(Path.ToString())), Context);
			continue;
		}

		if (!Component->GetStaticMesh())
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("NoStaticMesh", "'{0}' has no Static Mesh assigned and was skipped."),
				FText::FromString(Path.ToString())), Context);
			continue;
		}

		if (Component->HasValidNaniteData())
		{
			if (!Settings->bPaintNaniteComponents)
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("NaniteComponentSkipped", "'{0}' renders with Nanite. The Nanite raster path ignores per-component override vertex colors — use a Mesh Paint Texture for Nanite. Skipped; enable 'Paint Nanite Components' to paint anyway."),
					FText::FromString(Path.ToString())), Context);
				continue;
			}
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("NaniteComponentPainted", "'{0}' renders with Nanite: the painted override vertex colors affect ray tracing, the non-Nanite fallback mesh, and Nanite debug views, but not the Nanite raster render."),
				FText::FromString(Path.ToString())), Context);
		}

		FPCGUtilsPainterStaticMeshTarget Target(Component, TargetConfig);
		if (!Target.Prepare(Context))
		{
			continue;
		}

		if (!PCGUtilsPainter::EvaluatePainterGraphOntoTarget(Target, Evaluation, Context))
		{
			// Already logged; the up-front probe should have caught Painter incompatibility.
			continue;
		}

		Component->Modify();
		if (Target.Commit(Context))
		{
			++PaintedComponentCount;
		}
	}

	if (!Settings->bSilenceUnresolvedPathWarning && UnresolvedCount > 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("UnresolvedPaths", "{0} component reference(s) were empty or their owning actor is not loaded, and were skipped."),
			FText::AsNumber(UnresolvedCount)), Context);
	}

	UE_LOG(LogPCGUtilsPainter, Verbose,
		TEXT("Paint Static Mesh Vertex Colors: painted %d component(s)."), PaintedComponentCount);
	return true;
}

#undef LOCTEXT_NAMESPACE
