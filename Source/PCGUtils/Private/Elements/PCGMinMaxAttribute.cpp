// Fill out your copyright notice in the Description page of Project Settings.


#include "Elements/PCGMinMaxAttribute.h"

FPCGElementPtr UPCGMinMaxAttribute::CreateElement() const
{
	return MakeShared<FPCGMinMaxAttribute>();
}

bool FPCGMinMaxAttribute::ExecuteInternal(FPCGContext* Context) const
{
	return true;
}
