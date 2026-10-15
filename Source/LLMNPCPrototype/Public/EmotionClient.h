#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Http.h"
#include "EmotionClient.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEmotionResultReceived, const FString&, Emotion);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEmotionErrorReceived, const FString&, ErrorMessage);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPlayerVoiceEmotionNative, const FString&);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnEmotionResultReceivedWithId,
    int32,
    RequestId,
    const FString&,
    Emotion
);

UCLASS(Blueprintable)
class LLMNPCPROTOTYPE_API UEmotionClient : public UObject
{
    GENERATED_BODY()

public:
	static FOnPlayerVoiceEmotionNative OnAnyPlayerVoiceEmotion;

	static FString GetLatestPlayerVoiceEmotion(float MaximumAgeSeconds = 30.0f);

    UFUNCTION(BlueprintCallable, Category = "Emotion")
    void AnalyzeAudioFile(const FString& FilePath);

    UFUNCTION(BlueprintCallable, Category = "Emotion")
    void AnalyzeAudioFileWhenReady(
        const FString& FilePath,
        float TimeoutSeconds = 2.0f,
        float PollIntervalSeconds = 0.05f
    );

    UFUNCTION(BlueprintCallable, Category = "Emotion")
    void AnalyzeAudioFileWithId(
        const FString& FilePath,
        int32 RequestId
    );

    UFUNCTION(BlueprintCallable, Category = "Emotion")
    void AnalyzeAudioFileWhenReadyWithId(
        const FString& FilePath,
        int32 RequestId,
        float TimeoutSeconds = 2.0f,
        float PollIntervalSeconds = 0.05f
    );

    UPROPERTY(BlueprintAssignable, Category = "Emotion")
    FOnEmotionResultReceived OnEmotionResultReceived;

    UPROPERTY(BlueprintAssignable, Category = "Emotion")
    FOnEmotionResultReceivedWithId OnEmotionResultReceivedWithId;

    UPROPERTY(BlueprintAssignable, Category = "Emotion")
    FOnEmotionErrorReceived OnEmotionErrorReceived;

private:
	static FString LatestPlayerVoiceEmotion;
	static double LatestPlayerVoiceEmotionTimeSeconds;

    void OnEmotionResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

    void OnEmotionResponseReceivedWithId(
        FHttpRequestPtr Request,
        FHttpResponsePtr Response,
        bool bWasSuccessful,
        int32 RequestId
    );

    void AppendStringToBody(TArray<uint8>& Body, const FString& Text);
};
