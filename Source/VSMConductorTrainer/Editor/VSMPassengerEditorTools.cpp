#include "Editor/VSMPassengerEditorTools.h"
#if WITH_EDITOR
#include "Passengers/VSMPassengerFaceAnimInstance.h"
#include "Passengers/VSMPassengerCharacter.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/Skeleton.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_LookAt.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

void UVSMPassengerEditorTools::RebuildPassenger(AActor* Passenger)
{
    if(auto* NPC=Cast<AVSMPassengerCharacter>(Passenger)) { NPC->Modify(); NPC->RerunConstructionScripts(); }
}

bool UVSMPassengerEditorTools::AddPassengerGaze(UObject* AnimationBlueprint)
{
    auto* BP=Cast<UAnimBlueprint>(AnimationBlueprint);
    if(!BP || !BP->TargetSkeleton) return false;
    TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
    UAnimGraphNode_Root* Root=nullptr;
    for(auto* Graph:Graphs) for(UEdGraphNode* Node:Graph->Nodes)
        if(auto* Candidate=Cast<UAnimGraphNode_Root>(Node)) Root=Candidate;
    if(!Root || Root->FindPin(TEXT("Result"))->LinkedTo.Num()!=1) return false;
    if(BP->ParentClass==UVSMPassengerFaceAnimInstance::StaticClass()) return BP->Status!=BS_Error;
    BP->Modify(); BP->ParentClass=UVSMPassengerFaceAnimInstance::StaticClass();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);
    UEdGraph* Graph=Root->GetGraph(); const UEdGraphSchema* Schema=Graph->GetSchema();
    auto* Result=Root->FindPin(TEXT("Result")); auto* Source=Result->LinkedTo[0];
    Result->BreakAllPinLinks();
    FGraphNodeCreator<UAnimGraphNode_LocalToComponentSpace> ToComponentCreator(*Graph);
    auto* ToComponent=ToComponentCreator.CreateNode(); ToComponentCreator.Finalize();
    bool bConnected=Schema->TryCreateConnection(Source,ToComponent->FindPin(TEXT("LocalPose")));
    UEdGraphPin* Last=ToComponent->FindPin(TEXT("ComponentPose"));
    auto AddGetter=[&](FName Name)->UEdGraphPin*
    {
        FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph);
        auto* Node=Creator.CreateNode(); Node->VariableReference.SetSelfMember(Name); Creator.Finalize();
        return Node->FindPin(Name);
    };
    UEdGraphPin* Location=AddGetter(TEXT("GazeLocation"));
    UEdGraphPin* Alpha=AddGetter(TEXT("GazeAlpha"));
    const FReferenceSkeleton& Reference=BP->TargetSkeleton->GetReferenceSkeleton();
    for(const TCHAR* Bone:{TEXT("FACIAL_L_Eye"),TEXT("FACIAL_R_Eye")})
    {
        FGraphNodeCreator<UAnimGraphNode_LookAt> Creator(*Graph);
        auto* Node=Creator.CreateNode(); Node->Node.BoneToModify.BoneName=Bone;
        Node->Node.LookAtClamp=25.f;
        Node->Node.InterpolationTime=0.f;
        const int32 BoneIndex=Reference.FindBoneIndex(Bone);
        if(BoneIndex==INDEX_NONE) return false;
        FTransform ReferenceTransform=Reference.GetRefBonePose()[BoneIndex];
        for(int32 Parent=Reference.GetParentIndex(BoneIndex);Parent!=INDEX_NONE;Parent=Reference.GetParentIndex(Parent))
            ReferenceTransform=ReferenceTransform*Reference.GetRefBonePose()[Parent];
        Node->Node.LookAt_Axis.Axis=ReferenceTransform.InverseTransformVectorNoScale(FVector(0,1,0));
        Creator.Finalize();
        for(auto& Pin:Node->ShowPinForProperties)
            if(Pin.PropertyName==TEXT("LookAtLocation") || Pin.PropertyName==TEXT("Alpha")) Pin.bShowPin=true;
        Node->ReconstructNode();
        if(!Location || !Alpha || !Node->FindPin(TEXT("LookAtLocation")) || !Node->FindPin(TEXT("Alpha")))
        {
            UE_LOG(LogTemp,Error,TEXT("Gaze pins: Location=%d Alpha=%d"),Location!=nullptr,Alpha!=nullptr);
            for(auto* Pin:Node->Pins) UE_LOG(LogTemp,Error,TEXT("Gaze node pin: %s"),*Pin->PinName.ToString());
            return false;
        }
        bConnected &= Schema->TryCreateConnection(Last,Node->FindPin(TEXT("ComponentPose")));
        bConnected &= Schema->TryCreateConnection(Location,Node->FindPin(TEXT("LookAtLocation")));
        bConnected &= Schema->TryCreateConnection(Alpha,Node->FindPin(TEXT("Alpha")));
        Last=Node->FindPin(TEXT("Pose"));
    }
    FGraphNodeCreator<UAnimGraphNode_ComponentToLocalSpace> ToLocalCreator(*Graph);
    auto* ToLocal=ToLocalCreator.CreateNode(); ToLocalCreator.Finalize();
    bConnected &= Schema->TryCreateConnection(Last,ToLocal->FindPin(TEXT("ComponentPose")));
    bConnected &= Schema->TryCreateConnection(ToLocal->FindPin(TEXT("Pose")),Result);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);
    return bConnected && BP->Status!=BS_Error;
}
#endif
