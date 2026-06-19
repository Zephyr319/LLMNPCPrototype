#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Http.h"
#include "Dom/JsonValue.h"
#include "LLMClient.generated.h"

class UVoicevoxTTSClient;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnLLMResponseReceived,
    const FString&,
    ResponseText
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnLLMDisplayTextReceived,
    const FString&,
    DisplayText
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnLLMErrorReceived,
    const FString&,
    ErrorMessage
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_NineParams(
    FOnStrategyResultReceived,
    const FString&,
    Reply,
    const FString&,
    StrategyTone,
    const FString&,
    StrategyLength,
    const FString&,
    StrategyPriority,
    const FString&,
    StrategyReason,
    const FString&,
    ActionName,
    const FString&,
    ActionItem,
    int32,
    ActionQuantity,
    const FString&,
    RawJson
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
    FOnNPCActionRequested,
    const FString&,
    ActionType,
    const FString&,
    Timing,
    const FString&,
    Item,
    int32,
    Quantity
);

UCLASS(Blueprintable)
class LLMNPCPROTOTYPE_API ULLMClient : public UObject
{
    GENERATED_BODY()

public:
    ULLMClient();

    UFUNCTION(BlueprintCallable, Category = "LLM")
    void SendDeepSeekMessage(const FString& UserMessage);

    UFUNCTION(BlueprintCallable, Category = "LLM")
    void SendStrategyMessage(const FString& UserMessage);

    UFUNCTION(BlueprintCallable, Category = "TTS")
    void SetVoicevoxTTSClient(UVoicevoxTTSClient* InVoicevoxTTSClient);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TTS")
    bool bUseVoicevoxTTS;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TTS")
    UVoicevoxTTSClient* VoicevoxTTSClient;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenAI")
    FString OpenAIModelName;

    UPROPERTY(BlueprintAssignable, Category = "LLM")
    FOnLLMResponseReceived OnLLMResponseReceived;

    UPROPERTY(BlueprintAssignable, Category = "LLM")
    FOnLLMDisplayTextReceived OnLLMDisplayTextReceived;

    UPROPERTY(BlueprintAssignable, Category = "LLM")
    FOnLLMErrorReceived OnLLMErrorReceived;

    UPROPERTY(BlueprintAssignable, Category = "LLM")
    FOnStrategyResultReceived OnStrategyResultReceived;

    UPROPERTY(BlueprintAssignable, Category = "LLM")
    FOnNPCActionRequested OnNPCActionRequested;

private:
    TArray<TSharedPtr<FJsonValue>> ConversationHistory;
    int32 ActiveLLMRequestId;

    void OnOpenAINormalResponseReceived(
        FHttpRequestPtr Request,
        FHttpResponsePtr Response,
        bool bWasSuccessful,
        int32 RequestId
    );

    void OnStrategyResponseReceived(
        FHttpRequestPtr Request,
        FHttpResponsePtr Response,
        bool bWasSuccessful,
        int32 RequestId
    );

    void InterruptCurrentVoicevoxSpeech();
    void HandleFinalReplyText(const FString& ReplyText);
    FString SanitizeReplyTextForSpeechAndDisplay(const FString& RawText) const;
    UVoicevoxTTSClient* ResolveVoicevoxTTSClient();
};
