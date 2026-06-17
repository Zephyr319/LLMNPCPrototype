#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Http.h"
#include "STTClient.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSTTResultReceived, const FString&, TranscribedText);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSTTErrorReceived, const FString&, ErrorMessage);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnSTTResultReceivedWithId,
    int32,
    RequestId,
    const FString&,
    TranscribedText
);

UCLASS(Blueprintable)
class LLMNPCPROTOTYPE_API USTTClient : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "STT")
    void TranscribeAudioFile(const FString& FilePath);

    UFUNCTION(BlueprintCallable, Category = "STT")
    void TranscribeAudioFileWhenReady(
        const FString& FilePath,
        float TimeoutSeconds = 2.0f,
        float PollIntervalSeconds = 0.05f
    );

    UFUNCTION(BlueprintCallable, Category = "STT")
    void TranscribeAudioFileWithId(
        const FString& FilePath,
        int32 RequestId
    );

    UFUNCTION(BlueprintCallable, Category = "STT")
    void TranscribeReadyWithId(
        const FString& FilePath,
        int32 RequestId,
        float TimeoutSeconds = 2.0f,
        float PollIntervalSeconds = 0.05f
    );

    UFUNCTION(BlueprintCallable, Category = "STT")
    void DeleteFileIfExists(const FString& FilePath);

    UPROPERTY(BlueprintAssignable, Category = "STT")
    FOnSTTResultReceived OnSTTResultReceived;

    UPROPERTY(BlueprintAssignable, Category = "STT")
    FOnSTTResultReceivedWithId OnSTTResultReceivedWithId;

    UPROPERTY(BlueprintAssignable, Category = "STT")
    FOnSTTErrorReceived OnSTTErrorReceived;

private:
    void OnSTTResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

    void OnSTTResponseReceivedWithId(
        FHttpRequestPtr Request,
        FHttpResponsePtr Response,
        bool bWasSuccessful,
        int32 RequestId
    );

    void AppendStringToBody(TArray<uint8>& Body, const FString& Text);
};