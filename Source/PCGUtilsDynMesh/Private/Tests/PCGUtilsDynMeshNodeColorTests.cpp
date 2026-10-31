// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EDITOR

#include "Algo/Find.h"
#include "PCGSettings.h"
#include "PCGUtilsDynMeshNodeColor.h"
#include "UObject/UObjectIterator.h"

/**
 * Every node in the DynMesh family must draw in the family color. PCGUtils nodes report PCGUtils category types, for
 * which the PCG editor has no color, so a node that forgets the override is colorless rather than obviously wrong.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshNodeColorTest,
	"PCGUtils.DynMesh.NodeColor", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshNodeColorTest::RunTest(const FString&)
{
	static const TCHAR* const Packages[] = { TEXT("/Script/PCGUtilsDynMesh"), TEXT("/Script/PCGUtilsPainter") };

	int32 Checked = 0;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		const UClass* Class = *It;
		if (!Class->IsChildOf(UPCGSettings::StaticClass())
			|| Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			continue;
		}
		const FString Package = Class->GetOutermost()->GetName();
		if (!Algo::Find(Packages, Package))
		{
			continue;
		}

		const UPCGSettings* Settings = GetDefault<UPCGSettings>(const_cast<UClass*>(Class));
		const FLinearColor Color = Settings->GetNodeTitleColor();
		TestTrue(FString::Printf(TEXT("%s (%s) title color is the DynMesh family color, got %s"),
			*Class->GetName(), *Package, *Color.ToString()), Color.Equals(PCGUtilsDynMeshNodeColor::Title));
		++Checked;
	}

	TestTrue(TEXT("Found DynMesh node classes to check"), Checked > 0);
	return true;
}

#endif
