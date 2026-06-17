#include "TTSClient.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Sound/SoundWaveProcedural.h"
#include "Audio.h"
#include "Misc/Guid.h"


void UTTSClient::GenerateVoice(const FString& Text)
{
    UE_LOG(LogTemp, Warning, TEXT("TTS GenerateVoice called: %s"), *Text);

    if (Text.IsEmpty())
    {
        const FString ErrorMessage = TEXT("TTS text is empty.");
        UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMessage);
        OnTTSError.Broadcast(ErrorMessage);
        return;
    }

  
    FString TTSInput = Text;

    if (TTSInput.Len() > 90)
    {
        TTSInput = TTSInput.Left(90);
    }

    const FString EncodedText = FGenericPlatformHttp::UrlEncode(TTSInput);

    FString Url = FString::Printf(
        TEXT("http://127.0.0.1:5000/voice?text=%s&model_id=0&speaker_id=0&sdp_ratio=0.2&noise=0.6&noisew=0.8&length=1.0&language=JP&auto_split=true&split_interval=0.5&style=Neutral&style_weight=5.0"),
        *EncodedText
    );

    TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Accept"), TEXT("audio/wav"));

    Request->OnProcessRequestComplete().BindUObject(this, &UTTSClient::OnTTSResponseReceived);
    Request->ProcessRequest();

    UE_LOG(LogTemp, Warning, TEXT("TTS request sent."));
}

void UTTSClient::OnTTSResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
    if (!bWasSuccessful || !Response.IsValid())
    {
        const FString ErrorMessage = TEXT("TTS request failed.");
        UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMessage);
        OnTTSError.Broadcast(ErrorMessage);
        return;
    }

    const int32 ResponseCode = Response->GetResponseCode();
    UE_LOG(LogTemp, Warning, TEXT("TTS response code: %d"), ResponseCode);

    if (ResponseCode != 200)
    {
        const FString ErrorBody = Response->GetContentAsString();
        const FString ErrorMessage = FString::Printf(TEXT("TTS server error. Code: %d, Body: %s"), ResponseCode, *ErrorBody);
        UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMessage);
        OnTTSError.Broadcast(ErrorMessage);
        return;
    }

    const TArray<uint8>& AudioData = Response->GetContent();

    if (AudioData.Num() == 0)
    {
        const FString ErrorMessage = TEXT("TTS response audio data is empty.");
        UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMessage);
        OnTTSError.Broadcast(ErrorMessage);
        return;
    }

    const FString SaveDir = FPaths::ProjectSavedDir() / TEXT("TTS");
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

    if (!PlatformFile.DirectoryExists(*SaveDir))
    {
        PlatformFile.CreateDirectoryTree(*SaveDir);
    }

    LastGeneratedSound = CreateSoundWaveFromWavData(AudioData);

    if (LastGeneratedSound)
    {
        UE_LOG(LogTemp, Warning, TEXT("TTS SoundWave generated from response data."));
        OnTTSSoundGenerated.Broadcast(LastGeneratedSound);
    }
    else
    {
        const FString ErrorMessage = TEXT("Failed to create SoundWave from TTS wav data.");
        UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMessage);
        OnTTSError.Broadcast(ErrorMessage);
    }
}

USoundWave* UTTSClient::CreateSoundWaveFromWavData(const TArray<uint8>& WavData)
{
    FWaveModInfo WaveInfo;

    if (!WaveInfo.ReadWaveInfo(WavData.GetData(), WavData.Num()))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to read wav info."));
        return nullptr;
    }

    const int32 NumChannels = *WaveInfo.pChannels;
    const int32 SampleRate = *WaveInfo.pSamplesPerSec;
    const int32 BitsPerSample = *WaveInfo.pBitsPerSample;
    const int32 PCMDataSize = WaveInfo.SampleDataSize;

    if (BitsPerSample != 16)
    {
        UE_LOG(LogTemp, Error, TEXT("Only 16-bit PCM wav is supported. BitsPerSample: %d"), BitsPerSample);
        return nullptr;
    }

    USoundWaveProcedural* SoundWave = NewObject<USoundWaveProcedural>(this);

    SoundWave->SetSampleRate(SampleRate);
    SoundWave->NumChannels = NumChannels;
    SoundWave->Duration = static_cast<float>(PCMDataSize) / static_cast<float>(SampleRate * NumChannels * sizeof(int16));
    SoundWave->SoundGroup = SOUNDGROUP_Default;
    SoundWave->bLooping = false;

    SoundWave->QueueAudio(WaveInfo.SampleDataStart, PCMDataSize);

    return SoundWave;
}