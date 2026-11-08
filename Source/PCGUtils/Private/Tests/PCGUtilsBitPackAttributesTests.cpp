#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Elements/Metadata/PCGUtilsBitPackAttributes.h"
#include "Elements/Metadata/PCGMetadataStepOpElement.h"
#include "PCGContext.h"
#include "PCGParamData.h"
#include "Data/PCGPointArrayData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataDomain.h"
#include "Metadata/PCGMetadataAttributeTpl.h"

namespace BitPackAttributesTests
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

class FExecute : public FPCGUtilsBitPackAttributesElement
{
public:
	using FPCGUtilsBitPackAttributesElement::ExecuteInternal;
};

FPCGUtilsBitPackEntry Entry(const TCHAR* Name, int32 Bits, EPCGUtilsBitPackValueMode Mode = EPCGUtilsBitPackValueMode::Integer)
{
	FPCGUtilsBitPackEntry Result;
	Result.AttributeName = Name;
	Result.BitCount = Bits;
	Result.ValueMode = Mode;
	return Result;
}

/** Four points: Id (int32) 0..3 and 15 on the last, Weight (float) in [0, 1], Flag (bool), plus an untouched attribute. */
UPCGPointArrayData* MakePoints(int32 LastId = 15)
{
	auto* Points = NewObject<UPCGPointArrayData>();
	Points->SetNumPoints(4);
	auto* Elements = Points->Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements);
	auto* Id = Elements->CreateAttribute<int32>(TEXT("Id"), 0, false);
	auto* Weight = Elements->CreateAttribute<float>(TEXT("Weight"), 0.f, true);
	auto* Flag = Elements->CreateAttribute<bool>(TEXT("Flag"), false, false);
	auto* Other = Elements->CreateAttribute<int32>(TEXT("Other"), -1, false);
	Points->Metadata->GetMetadataDomain(PCGMetadataDomainID::Data)->CreateAttribute<int32>(TEXT("DataValue"), 9, false);
	auto Keys = Points->GetMetadataEntryValueRange();
	const float Weights[4] = {0.f, 1.f / 15.f, 0.5f, 1.f};
	for (int32 I = 0; I < 4; ++I)
	{
		Keys[I] = Elements->AddEntry();
		Id->SetValue(Keys[I], I == 3 ? LastId : I);
		Weight->SetValue(Keys[I], Weights[I]);
		Flag->SetValue(Keys[I], I % 2 == 1);
		Other->SetValue(Keys[I], 100 + I);
	}
	return Points;
}

void Run(UPCGUtilsBitPackAttributesSettings* Settings, UPCGData* Input, FPCGContext& Context)
{
	Context.InputData.TaggedData.Emplace_GetRef().Data = Settings;
	auto& In = Context.InputData.TaggedData.Emplace_GetRef();
	In.Data = Input; In.Pin = PCGPinConstants::DefaultInputLabel; In.Tags.Add(TEXT("PreservedTag"));
	FExecute().ExecuteInternal(&Context);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsMultiStepScalarTest, "PCGUtils.Metadata.Step.MultiStep", BitPackAttributesTests::Flags)
bool FPCGUtilsMultiStepScalarTest::RunTest(const FString&)
{
	using PCGMetadataStepOp::MultiStepScalar;
	// 5 steps over [0, 1]: 0, .2, .4, .6, .8 inside, 1 at Max.
	const double Inputs[]   = {-1.0, 0.0, 0.1, 0.2, 0.39, 0.4, 0.6, 0.79, 0.8, 0.99, 1.0, 2.0};
	const double Expected[] = { 0.0, 0.0, 0.0, 0.2, 0.2,  0.4, 0.6, 0.6,  0.8, 0.8,  1.0, 1.0};
	for (int32 I = 0; I < UE_ARRAY_COUNT(Inputs); ++I)
	{
		TestEqual(FString::Printf(TEXT("MultiStep(0, 1, %g, 5)"), Inputs[I]), MultiStepScalar(0.0, 1.0, Inputs[I], 5), Expected[I], 1e-12);
	}
	// Offset range, boundary values that are inexact in binary.
	TestEqual(TEXT("Range [10, 20], value 16 -> 0.6"), MultiStepScalar(10.0, 20.0, 16.0, 5), 0.6, 1e-12);
	TestEqual(TEXT("Range [0, 0.3], value 0.18 -> 0.6"), MultiStepScalar(0.0, 0.3, 0.18, 5), 0.6, 1e-12);
	TestEqual(TEXT("StepCount 1 is a step at Max (below)"), MultiStepScalar(0.0, 1.0, 0.99, 1), 0.0, 1e-12);
	TestEqual(TEXT("StepCount 1 is a step at Max (at)"), MultiStepScalar(0.0, 1.0, 1.0, 1), 1.0, 1e-12);
	TestEqual(TEXT("StepCount 0 is treated as 1"), MultiStepScalar(0.0, 1.0, 0.5, 0), 0.0, 1e-12);
	TestEqual(TEXT("Degenerate range below"), MultiStepScalar(1.0, 1.0, 0.5, 5), 0.0, 1e-12);
	TestEqual(TEXT("Degenerate range at"), MultiStepScalar(1.0, 1.0, 1.0, 5), 1.0, 1e-12);
	TestEqual(TEXT("Inverted range mirrors"), MultiStepScalar(1.0, 0.0, 0.3, 5), 0.6, 1e-12);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsBitPackQuantizeTest, "PCGUtils.Metadata.BitPack.Quantize", BitPackAttributesTests::Flags)
bool FPCGUtilsBitPackQuantizeTest::RunTest(const FString&)
{
	using namespace PCGUtilsBitPack;
	bool bClamped = false;
	TestEqual(TEXT("Integer rounds"), Quantize(6.6, 4, EPCGUtilsBitPackValueMode::Integer, bClamped), uint64(7)); TestFalse(TEXT("6.6 fits"), bClamped);
	TestEqual(TEXT("Integer clamps high"), Quantize(16.0, 4, EPCGUtilsBitPackValueMode::Integer, bClamped), uint64(15)); TestTrue(TEXT("16 clamped"), bClamped);
	TestEqual(TEXT("Integer clamps low"), Quantize(-2.0, 4, EPCGUtilsBitPackValueMode::Integer, bClamped), uint64(0)); TestTrue(TEXT("-2 clamped"), bClamped);
	TestEqual(TEXT("Max fits"), Quantize(15.0, 4, EPCGUtilsBitPackValueMode::Integer, bClamped), uint64(15)); TestFalse(TEXT("15 fits"), bClamped);
	TestEqual(TEXT("Normalized 1 -> max"), Quantize(1.0, 8, EPCGUtilsBitPackValueMode::Normalized, bClamped), uint64(255)); TestFalse(TEXT("1 fits"), bClamped);
	TestEqual(TEXT("Normalized 0.5 -> 128"), Quantize(0.5, 8, EPCGUtilsBitPackValueMode::Normalized, bClamped), uint64(128));
	TestEqual(TEXT("Normalized 1.5 clamps"), Quantize(1.5, 8, EPCGUtilsBitPackValueMode::Normalized, bClamped), uint64(255)); TestTrue(TEXT("1.5 clamped"), bClamped);
	TestEqual(TEXT("NaN packs 0"), Quantize(NAN, 8, EPCGUtilsBitPackValueMode::Integer, bClamped), uint64(0)); TestTrue(TEXT("NaN flagged"), bClamped);
	TestEqual(TEXT("Float output limit"), MaxBits(EPCGUtilsBitPackOutputType::Float), 24);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsBitPackNodeTest, "PCGUtils.Metadata.BitPack.PacksAndPreserves", BitPackAttributesTests::Flags)
bool FPCGUtilsBitPackNodeTest::RunTest(const FString&)
{
	using namespace BitPackAttributesTests;
	auto* Settings = NewObject<UPCGUtilsBitPackAttributesSettings>();
	Settings->AttributesToPack = {Entry(TEXT("Id"), 4), Entry(TEXT("Weight"), 4, EPCGUtilsBitPackValueMode::Normalized), Entry(TEXT("Flag"), 1), Entry(TEXT("@Data.DataValue"), 4)};

	FPCGContext Context;
	Run(Settings, MakePoints(), Context);
	if (!TestEqual(TEXT("One output"), Context.OutputData.TaggedData.Num(), 1)) return false;
	const FPCGTaggedData& Out = Context.OutputData.TaggedData[0];
	const auto* Points = Cast<UPCGPointArrayData>(Out.Data);
	if (!TestNotNull(TEXT("Output is point data"), Points)) return false;
	TestTrue(TEXT("Tag preserved"), Out.Tags.Contains(TEXT("PreservedTag")));
	TestEqual(TEXT("Output pin"), Out.Pin, PCGPinConstants::DefaultOutputLabel);

	const auto* Packed = Points->Metadata->GetConstTypedAttribute<float>(TEXT("BitPacked"));
	const auto* Other = Points->Metadata->GetConstTypedAttribute<int32>(TEXT("Other"));
	const auto* DataValue = Points->Metadata->GetConstTypedAttribute<int32>(FPCGAttributeIdentifier(TEXT("DataValue"), PCGMetadataDomainID::Data));
	if (!TestNotNull(TEXT("Float BitPacked written"), Packed) || !TestNotNull(TEXT("Other kept"), Other) || !TestNotNull(TEXT("@Data kept"), DataValue)) return false;
	TestFalse(TEXT("Packed attribute does not interpolate"), Packed->AllowsInterpolation());
	TestEqual(TEXT("@Data value kept"), DataValue->GetValue(PCGFirstEntryKey), 9);

	// Id bits 0-3, Weight*15 bits 4-7, Flag bit 8, DataValue (9) bits 9-12.
	const int32 Ids[4] = {0, 1, 2, 15}, Weights[4] = {0, 1, 8, 15}, FlagBits[4] = {0, 1, 0, 1};
	const auto Keys = Points->GetConstMetadataEntryValueRange();
	for (int32 I = 0; I < 4; ++I)
	{
		const float ExpectedValue = static_cast<float>(Ids[I] | (Weights[I] << 4) | (FlagBits[I] << 8) | (9 << 9));
		TestEqual(FString::Printf(TEXT("Packed value point %d"), I), Packed->GetValueFromItemKey(Keys[I]), ExpectedValue);
		TestEqual(FString::Printf(TEXT("Other point %d"), I), Other->GetValueFromItemKey(Keys[I]), 100 + I);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsBitPackOutputTypesTest, "PCGUtils.Metadata.BitPack.OutputTypes", BitPackAttributesTests::Flags)
bool FPCGUtilsBitPackOutputTypesTest::RunTest(const FString&)
{
	using namespace BitPackAttributesTests;
	// Full 32 bits into Integer 32: the top bit set reads negative, the bit pattern is exact.
	auto* Params = NewObject<UPCGParamData>();
	auto* Low = Params->Metadata->CreateAttribute<int64>(TEXT("Low"), 0, false, false);
	auto* High = Params->Metadata->CreateAttribute<int64>(TEXT("High"), 0, false, false);
	const PCGMetadataEntryKey Key = Params->Metadata->AddEntry();
	Low->SetValue(Key, 0xFFFF); High->SetValue(Key, 0x8001);

	auto* Settings = NewObject<UPCGUtilsBitPackAttributesSettings>();
	Settings->OutputType = EPCGUtilsBitPackOutputType::Integer32;
	Settings->OutputAttributeName = TEXT("Packed32");
	Settings->AttributesToPack = {Entry(TEXT("Low"), 16), Entry(TEXT("High"), 16)};
	FPCGContext Context;
	Run(Settings, Params, Context);
	if (!TestEqual(TEXT("One output"), Context.OutputData.TaggedData.Num(), 1)) return false;
	const auto* Out = Cast<UPCGParamData>(Context.OutputData.TaggedData[0].Data);
	const auto* Packed = Out ? Out->Metadata->GetConstTypedAttribute<int32>(TEXT("Packed32")) : nullptr;
	if (!TestNotNull(TEXT("Int32 attribute written on attribute set"), Packed)) return false;
	TestEqual(TEXT("Bit pattern"), static_cast<uint32>(Packed->GetValueFromItemKey(Key)), 0x8001FFFFu);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsBitPackErrorsTest, "PCGUtils.Metadata.BitPack.Errors", BitPackAttributesTests::Flags)
bool FPCGUtilsBitPackErrorsTest::RunTest(const FString&)
{
	using namespace BitPackAttributesTests;
	{
		AddExpectedError(TEXT("add up to 25, but Output Type Float stores at most 24"), EAutomationExpectedErrorFlags::Contains, 1, false);
		auto* Settings = NewObject<UPCGUtilsBitPackAttributesSettings>();
		Settings->AttributesToPack = {Entry(TEXT("Id"), 24), Entry(TEXT("Flag"), 1)};
		FPCGContext Context;
		Run(Settings, MakePoints(), Context);
		TestEqual(TEXT("Too many bits: no output"), Context.OutputData.TaggedData.Num(), 0);
	}
	{
		AddExpectedError(TEXT("input 0 has no attribute or property 'Missing' (Attributes To Pack[1])"), EAutomationExpectedErrorFlags::Contains, 1, false);
		auto* Settings = NewObject<UPCGUtilsBitPackAttributesSettings>();
		Settings->AttributesToPack = {Entry(TEXT("Id"), 4), Entry(TEXT("Missing"), 4)};
		FPCGContext Context;
		Run(Settings, MakePoints(), Context);
		TestEqual(TEXT("Missing attribute: no output"), Context.OutputData.TaggedData.Num(), 0);
	}
	{
		AddExpectedError(TEXT("Attributes To Pack is empty"), EAutomationExpectedErrorFlags::Contains, 1, false);
		auto* Settings = NewObject<UPCGUtilsBitPackAttributesSettings>();
		FPCGContext Context;
		Run(Settings, MakePoints(), Context);
	}
	{
		// Clamping is a warning, and the output still gets written.
		auto* Settings = NewObject<UPCGUtilsBitPackAttributesSettings>();
		Settings->AttributesToPack = {Entry(TEXT("Id"), 3)};
		FPCGContext Context;
		Run(Settings, MakePoints(), Context);
		if (TestEqual(TEXT("Clamped: still one output"), Context.OutputData.TaggedData.Num(), 1))
		{
			const auto* Points = CastChecked<UPCGPointArrayData>(Context.OutputData.TaggedData[0].Data);
			const auto* Packed = Points->Metadata->GetConstTypedAttribute<float>(TEXT("BitPacked"));
			TestEqual(TEXT("15 clamped to 7"), Packed->GetValueFromItemKey(Points->GetConstMetadataEntryValueRange()[3]), 7.f);
		}
	}
	return true;
}

#endif
