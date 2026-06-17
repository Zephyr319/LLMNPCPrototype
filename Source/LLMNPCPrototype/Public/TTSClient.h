#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Http.h"
#include "Sound/SoundWave.h"
#include "TTSClient.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTTSGenerated, const FString&, SavedFilePath);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTTSSoundGenerated, USoundWave*, SoundWave);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTTSError, const FString&, ErrorMessage);

UCLASS(Blueprintable)
class LLMNPCPROTOTYPE_API UTTSClient : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "TTS")
    void GenerateVoice(const FString& Text);

    UPROPERTY(BlueprintAssignable, Category = "TTS")
    FOnTTSGenerated OnTTSGenerated;

    UPROPERTY(BlueprintAssignable, Category = "TTS")
    FOnTTSSoundGenerated OnTTSSoundGenerated;

    UPROPERTY(BlueprintAssignable, Category = "TTS")
    FOnTTSError OnTTSError;

private:
    UPROPERTY()
    USoundWave* LastGeneratedSound;

    void OnTTSResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

    USoundWave* CreateSoundWaveFromWavData(const TArray<uint8>& WavData);
};