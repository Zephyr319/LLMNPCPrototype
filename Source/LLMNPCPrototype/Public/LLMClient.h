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

    UPROPERTY(BlueprintAssignable, Category = "LLM")
    FOnLLMResponseReceived OnLLMResponseReceived;

    UPROPERTY(BlueprintAssignable, Category = "LLM")
    FOnLLMDisplayTextReceived OnLLMDisplayTextReceived;

    UPROPERTY(BlueprintAssignable, Category = "LLM")
    FOnLLMErrorReceived OnLLMErrorReceived;

    UPROPERTY(BlueprintAssignable, Category = "LLM")
    FOnStrategyResultReceived OnStrategyResultReceived;

private:
    TArray<TSharedPtr<FJsonValue>> ConversationHistory;

    void OnDeepSeekResponseReceived(
        FHttpRequestPtr Request,
        FHttpResponsePtr Response,
        bool bWasSuccessful
    );

    void OnStrategyResponseReceived(
        FHttpRequestPtr Request,
        FHttpResponsePtr Response,
        bool bWasSuccessful
    );

    void HandleFinalReplyText(const FString& ReplyText);
    FString SanitizeReplyTextForSpeechAndDisplay(const FString& RawText) const;
    UVoicevoxTTSClient* ResolveVoicevoxTTSClient();
};
