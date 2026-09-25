#include "BillboardComponent.h"

#include "Rendering/RenderInfo.h"

IMPLEMENT_CLASS(UBillboardComponent, UPrimitiveComponent);

UBillboardComponent::UBillboardComponent()
{
}

void UBillboardComponent::Initialize(FVector location, FRotator rotation, FVector scale3D)
{
	UPrimitiveComponent::Initialize(EPrimitive::EP_BillboardQuad, location, rotation, scale3D);
}

FRenderInfo UBillboardComponent::makeRenderInfo() const
{
	FRenderInfo renderInfo = UPrimitiveComponent::makeRenderInfo();
	renderInfo.eRenderFlags = renderInfo.eRenderFlags | ERenderFlags::RF_Billboard;
	return renderInfo;
}
