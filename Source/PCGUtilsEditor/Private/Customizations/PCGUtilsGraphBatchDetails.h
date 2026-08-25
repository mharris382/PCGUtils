#pragma once

#include "IDetailCustomization.h"

class UPCGUtilsGraphBatch;

class FPCGUtilsGraphBatchDetails final : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	TWeakObjectPtr<UPCGUtilsGraphBatch> Batch;
};
