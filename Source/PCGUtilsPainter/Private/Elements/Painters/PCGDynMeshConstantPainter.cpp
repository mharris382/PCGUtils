// Copyright Max Harris

#include "Elements/Painters/PCGDynMeshConstantPainter.h"
#include "Helpers/PCGUtilsParallelInputs.h"
#include "Data/PCGDynamicMeshData.h"

#include "Elements/Painters/PCGUtilsPainterShorthandHelpers.h"
#include "PCGContext.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshConstantPainter"

namespace
{
	EPCGUtilsDynMeshPainterColorChannel ToPainterChannels(const FGeometryScriptColorFlags& Flags)
	{
		EPCGUtilsDynMeshPainterColorChannel Channels = EPCGUtilsDynMeshPainterColorChannel::None;
		if (Flags.bRed) Channels |= EPCGUtilsDynMeshPainterColorChannel::Red;
		if (Flags.bGreen) Channels |= EPCGUtilsDynMeshPainterColorChannel::Green;
		if (Flags.bBlue) Channels |= EPCGUtilsDynMeshPainterColorChannel::Blue;
		if (Flags.bAlpha) Channels |= EPCGUtilsDynMeshPainterColorChannel::Alpha;
		return Channels;
	}

	class FConstantPainterOperation final : public FPCGUtilsDynMeshPainterOperation
	{
	public:
		explicit FConstantPainterOperation(const UPCGDynMeshConstantPainterFactoryData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual bool Initialize(const FPCGUtilsDynMeshPainterEvaluationContext& InPainterContext) override
		{
			if (!FPCGUtilsDynMeshPainterOperation::Initialize(InPainterContext) || !Factory)
			{
				return false;
			}
			if (Factory->ValueType == EPCGUtilsDynMeshPainterValueType::Color
				&& ToPainterChannels(Factory->ColorChannels) == EPCGUtilsDynMeshPainterColorChannel::None)
			{
				PCGLog::LogErrorOnGraph(LOCTEXT("NoChannels",
					"Painter | Constant: Value Type is Color but Color Channels has no channel enabled; enable at least one of R, G, B or A."),
					Context);
				return false;
			}
			return true;
		}

		/**
		 * The @Data read happens here, not in Initialize: Prepare is the first point that sees the real target, and
		 * Paint Static Mesh Vertex Colors initializes every Painter once against an empty probe before any target.
		 */
		virtual bool Prepare(const FPCGUtilsDynMeshPainterEvaluationContext& InPainterContext) override
		{
			FText Error;
			if (Factory->ValueType == EPCGUtilsDynMeshPainterValueType::Scalar)
			{
				float Scalar = 0.0f;
				if (!Factory->Value.TryReadDataValue(InPainterContext.MeshData, Scalar, Error,
					LOCTEXT("ValueLabel", "Painter | Constant: Value"), InPainterContext.DataSetIndex))
				{
					PCGLog::LogErrorOnGraph(Error, Context);
					return false;
				}
				if (!FMath::IsFinite(Scalar))
				{
					PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("NonFiniteValue",
						"Painter | Constant: Value (input {0}) is not a finite number."),
						FText::AsNumber(InPainterContext.DataSetIndex)), Context);
					return false;
				}
				Result = FPCGUtilsDynMeshPainterValue::MakeScalar(Scalar);
				return true;
			}

			FVector4 ColorValue = FVector4::Zero();
			if (!Factory->Color.TryReadDataValue(InPainterContext.MeshData, ColorValue, Error,
				LOCTEXT("ColorLabel", "Painter | Constant: Color"), InPainterContext.DataSetIndex))
			{
				PCGLog::LogErrorOnGraph(Error, Context);
				return false;
			}
			const EPCGUtilsDynMeshPainterColorChannel Channels = ToPainterChannels(Factory->ColorChannels);
			for (int32 Channel = 0; Channel < 4; ++Channel)
			{
				if (EnumHasAnyFlags(Channels, static_cast<EPCGUtilsDynMeshPainterColorChannel>(1 << Channel))
					&& !FMath::IsFinite(ColorValue[Channel]))
				{
					PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("NonFiniteColor",
						"Painter | Constant: Color (input {0}) channel {1} is not a finite number."),
						FText::AsNumber(InPainterContext.DataSetIndex), FText::FromString(FString::Chr(TEXT("RGBA")[Channel]))),
						Context);
					return false;
				}
			}
			Result = FPCGUtilsDynMeshPainterValue::MakeColor(FVector4f(ColorValue), Channels);
			return true;
		}

		virtual EPCGUtilsDynMeshPainterValueType GetOutputType() const override
		{
			return Factory->ValueType;
		}

		virtual FPCGUtilsDynMeshPainterValue Evaluate(const FPCGUtilsDynMeshPainterSample& /*Sample*/) const override
		{
			return Result;
		}

	private:
		TObjectPtr<const UPCGDynMeshConstantPainterFactoryData> Factory;
		FPCGUtilsDynMeshPainterValue Result;
	};
}

TSharedPtr<FPCGUtilsDynMeshPainterOperation> UPCGDynMeshConstantPainterFactoryData::CreateOperationInternal() const
{
	return MakeShared<FConstantPainterOperation>(this);
}

void UPCGDynMeshConstantPainterFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc)
	{
		return;
	}
	uint8 TypeValue = static_cast<uint8>(ValueType);
	Ar << TypeValue;
	PCGUtilsPainterShorthands::AddToCrc(Ar, Value, Value.Constant);
	FVector4 ColorConstant = Color.Constant;
	PCGUtilsPainterShorthands::AddToCrc(Ar, Color, ColorConstant);
	uint8 ChannelValue = static_cast<uint8>(ToPainterChannels(ColorChannels));
	Ar << ChannelValue;
}

UPCGDynMeshConstantPainterProviderSettings::UPCGDynMeshConstantPainterProviderSettings()
	: Value(1.0f, TEXT("Value"))
	, Color(FVector4(1.0, 1.0, 1.0, 1.0), TEXT("PathColor"))
{
}

#if WITH_EDITOR
FText UPCGDynMeshConstantPainterProviderSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Painter | Constant");
}

FText UPCGDynMeshConstantPainterProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Paints one value over the whole target: a scalar, or a color that defines the chosen channels. The value "
		"is a constant, or is read from each target mesh's own @Data domain (e.g. @Data.PathColor.R), so one "
		"Painter gives every mesh its own value. A target without the attribute fails with an error.");
}

FString UPCGDynMeshConstantPainterProviderSettings::GetAdditionalTitleInformation() const
{
	const FPCGUtilsInputShorthandBase& Active = ValueType == EPCGUtilsDynMeshPainterValueType::Scalar
		? static_cast<const FPCGUtilsInputShorthandBase&>(Value)
		: static_cast<const FPCGUtilsInputShorthandBase&>(Color);
	if (Active.IsAttribute())
	{
		return Active.Attribute.ToString();
	}
	if (ValueType == EPCGUtilsDynMeshPainterValueType::Scalar)
	{
		return FString::SanitizeFloat(Value.Constant);
	}
	return FString::Printf(TEXT("(%g, %g, %g, %g)"), Color.Constant.X, Color.Constant.Y, Color.Constant.Z, Color.Constant.W);
}

TArray<FPCGSettingsOverridableParam> UPCGDynMeshConstantPainterProviderSettings::GatherOverridableParams() const
{
	TArray<FPCGSettingsOverridableParam> Params = Super::GatherOverridableParams();
	PCGUtilsInputShorthands::ApplyOverrideLabels(Params);
	return Params;
}
#endif

FName UPCGDynMeshConstantPainterProviderSettings::GetMainOutputPin() const
{
	return PCGUtilsDynMeshPainterConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGDynMeshConstantPainterProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsDynMeshPainterFactoryDataTypeInfo::AsId();
}

UPCGUtilsDynMeshFactoryData* UPCGDynMeshConstantPainterProviderSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	UPCGDynMeshConstantPainterFactoryData* Factory = InFactory
		? Cast<UPCGDynMeshConstantPainterFactoryData>(InFactory)
		: PCGUtilsAsyncObjects::NewObject<UPCGDynMeshConstantPainterFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Priority = Priority;
	Factory->ValueType = ValueType;
	Factory->Value = Value;
	Factory->Color = Color;
	Factory->ColorChannels = ColorChannels;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE
