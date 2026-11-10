// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGBakeSettings.h"
#include "Engine/StaticMesh.h"
#include "PCGUtilsHelpers.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGResolveBakeSettingsTest, "PCGUtils.Helpers.ResolveBakeSettingsToStaticMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPCGResolveBakeSettingsTest::RunTest(const FString&)
{
	auto MakeSettings = [](const FString& Path, const FString& SaveName)
	{
		FPCGUtilsBakeSettings Settings;
		Settings.BakedAssetSavePath.Path = Path;
		Settings.BakedAssetSaveName = SaveName;
		return Settings;
	};

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Engine cube exists"), Cube)) return false;

	TestEqual(TEXT("Object path is {Path}/{SaveName}{Suffix}.{SaveName}{Suffix}"),
		MakeSettings(TEXT("/Game/_Generated"), TEXT("DefaultGroup_ABC")).GetBakedAssetObjectPath(TEXT("Green")).ToString(),
		FString(TEXT("/Game/_Generated/DefaultGroup_ABCGreen.DefaultGroup_ABCGreen")));

	TestTrue(TEXT("Resolves with no suffix"),
		UPCGUtilsHelpers::ResolveBakeSettingsToStaticMesh(MakeSettings(TEXT("/Engine/BasicShapes"), TEXT("Cube"))) == Cube);
	TestTrue(TEXT("Suffix is appended to the save name"),
		UPCGUtilsHelpers::ResolveBakeSettingsToStaticMesh(MakeSettings(TEXT("/Engine/BasicShapes"), TEXT("Cu")), TEXT("be")) == Cube);
	TestTrue(TEXT("Trailing slash and backslashes in the save path are tolerated"),
		UPCGUtilsHelpers::ResolveBakeSettingsToStaticMesh(MakeSettings(TEXT("\\Engine\\BasicShapes\\"), TEXT("Cube"))) == Cube);

	TestNull(TEXT("Empty save name gives null"),
		UPCGUtilsHelpers::ResolveBakeSettingsToStaticMesh(MakeSettings(TEXT("/Engine/BasicShapes"), TEXT(""))));
	TestNull(TEXT("Empty save path gives null"),
		UPCGUtilsHelpers::ResolveBakeSettingsToStaticMesh(MakeSettings(TEXT(""), TEXT("Cube"))));
	TestNull(TEXT("Missing asset gives null"),
		UPCGUtilsHelpers::ResolveBakeSettingsToStaticMesh(MakeSettings(TEXT("/Engine/BasicShapes"), TEXT("Cube")), TEXT("_DoesNotExist")));
	TestNull(TEXT("A non-mesh asset gives null"),
		UPCGUtilsHelpers::ResolveBakeSettingsToStaticMesh(MakeSettings(TEXT("/Engine/BasicShapes"), TEXT("BasicShapeMaterial"))));
	return true;
}

#endif
