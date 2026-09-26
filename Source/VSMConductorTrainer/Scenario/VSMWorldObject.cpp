#include "Scenario/VSMWorldObject.h"
#include "Scenario/VSMWorldPresenter.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AVSMWorldObject::AVSMWorldObject()
{
    Body=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));SetRootComponent(Body);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    Body->SetStaticMesh(Cube.Object);
    Presenter=CreateDefaultSubobject<UVSMWorldPresenter>(TEXT("Presenter"));Presenter->WorldId=TEXT("conductor_station");
    Marker=CreateDefaultSubobject<UTextRenderComponent>(TEXT("Marker"));Marker->SetupAttachment(Body);
    Marker->SetRelativeLocation(FVector(0,0,100));Marker->SetWorldSize(30);Marker->SetTextRenderColor(FColor::Cyan);
    Marker->SetHorizontalAlignment(EHTA_Center);Marker->SetVisibility(false);
}
void AVSMWorldObject::BeginPlay()
{
    Super::BeginPlay();Presenter->OnViewChanged.AddDynamic(this,&AVSMWorldObject::Present);Present(Presenter->View);
}
void AVSMWorldObject::Present_Implementation(const FVSMWorldView& View)
{
    Marker->SetVisibility(View.bMarker);Marker->SetText(FText::FromString(View.Item));
}
bool AVSMWorldObject::CanInteract_Implementation(AActor* Interactor) const{return Presenter->View.bAvailable;}
FText AVSMWorldObject::GetInteractionLabel_Implementation() const{return NSLOCTEXT("VSM","StationInteract","Получить предмет");}
void AVSMWorldObject::Interact_Implementation(AActor* Interactor){Presenter->Interact();}
