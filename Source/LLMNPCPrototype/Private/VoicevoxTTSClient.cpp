#include "VoicevoxTTSClient.h"

#include "Audio.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Sound/SoundWaveProcedural.h"

UVoicevoxTTSClient::UVoicevoxTTSClient()
{
    PrimaryComponentTick.bCanEverTick = false;

    BaseUrl = TEXT("http://127.0.0.1:50021");
    SpeakerId = 17;
    LastGeneratedSound = nullptr;
    LastWarmupSound = nullptr;
}

void UVoicevoxTTSClient::GenerateVoice(const FString& Text)
{
    StartAudioQuery(Text, true, false);
}

void UVoicevoxTTSClient::WarmupVoicevox()
{
    StartAudioQuery(TEXT("\u3053\u3093\u306B\u3061\u306F\u3002"), false, true);
}

FString UVoicevoxTTSClient::BuildVoicevoxUrl(const FString& PathAndQuery) const
{
    FString SanitizedBaseUrl = BaseUrl;
    SanitizedBaseUrl.RemoveFromEnd(TEXT("/"));

    return SanitizedBaseUrl + PathAndQuery;
}

void UVoicevoxTTSClient::StartAudioQuery(const FString& Text, bool bBroadcastSound, bool bIsWarmup)
{
    if (Text.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("[VOICEVOX] audio_query failed: text is empty"));
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("[VOICEVOX] audio_query start"));

    const FString EncodedText = FGenericPlatformHttp::UrlEncode(Text);
    const FString PathAndQuery = FString::Printf(
        TEXT("/audio_query?text=%s&speaker=%d"),
        *EncodedText,
        SpeakerId
    );

    TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(BuildVoicevoxUrl(PathAndQuery));
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->OnProcessRequestComplete().BindUObject(
        this,
        &UVoicevoxTTSClient::OnAudioQueryResponseReceived,
        bBroadcastSound,
        bIsWarmup
    );
    Request->ProcessRequest();
}

void UVoicevoxTTSClient::OnAudioQueryResponseReceived(
    FHttpRequestPtr Request,
    FHttpResponsePtr Response,
    bool bWasSuccessful,
    bool bBroadcastSound,
    bool bIsWarmup
)
{
    if (!bWasSuccessful || !Response.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[VOICEVOX] audio_query failed"));
        return;
    }

    const int32 ResponseCode = Response->GetResponseCode();
    if (ResponseCode != 200)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[VOICEVOX] audio_query failed: Code=%d, Body=%s"),
            ResponseCode,
            *Response->GetContentAsString()
        );
        return;
    }

    const FString AudioQueryJson = Response->GetContentAsString();
    if (AudioQueryJson.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("[VOICEVOX] audio_query failed: response body is empty"));
        return;
    }

    StartSynthesis(AudioQueryJson, bBroadcastSound, bIsWarmup);
}

void UVoicevoxTTSClient::StartSynthesis(const FString& AudioQueryJson, bool bBroadcastSound, bool bIsWarmup)
{
    UE_LOG(LogTemp, Warning, TEXT("[VOICEVOX] synthesis start"));

    const FString PathAndQuery = FString::Printf(TEXT("/synthesis?speaker=%d"), SpeakerId);

    TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(BuildVoicevoxUrl(PathAndQuery));
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Accept"), TEXT("audio/wav"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetContentAsString(AudioQueryJson);
    Request->OnProcessRequestComplete().BindUObject(
        this,
        &UVoicevoxTTSClient::OnSynthesisResponseReceived,
        bBroadcastSound,
        bIsWarmup
    );
    Request->ProcessRequest();
}

void UVoicevoxTTSClient::OnSynthesisResponseReceived(
    FHttpRequestPtr Request,
    FHttpResponsePtr Response,
    bool bWasSuccessful,
    bool bBroadcastSound,
    bool bIsWarmup
)
{
    if (!bWasSuccessful || !Response.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[VOICEVOX] synthesis failed"));
        return;
    }

    const int32 ResponseCode = Response->GetResponseCode();
    if (ResponseCode != 200)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[VOICEVOX] synthesis failed: Code=%d, Body=%s"),
            ResponseCode,
            *Response->GetContentAsString()
        );
        return;
    }

    const TArray<uint8>& WavBytes = Response->GetContent();
    UE_LOG(LogTemp, Warning, TEXT("[VOICEVOX] wav bytes: %d"), WavBytes.Num());

    if (WavBytes.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("[VOICEVOX] synthesis failed: wav bytes are empty"));
        return;
    }

    USoundWave* SoundWave = CreateSoundWaveFromWavData(WavBytes);
    if (!SoundWave)
    {
        UE_LOG(LogTemp, Error, TEXT("[VOICEVOX] SoundWave creation failed"));
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("[VOICEVOX] SoundWave created"));

    if (bIsWarmup)
    {
        LastWarmupSound = SoundWave;
        UE_LOG(LogTemp, Warning, TEXT("[VOICEVOX] Warm-up completed"));
        return;
    }

    LastGeneratedSound = SoundWave;

    if (bBroadcastSound)
    {
        OnVoicevoxSoundGenerated.Broadcast(LastGeneratedSound);
    }
}

USoundWave* UVoicevoxTTSClient::CreateSoundWaveFromWavData(const TArray<uint8>& WavData)
{
    FWaveModInfo WaveInfo;

    if (!WaveInfo.ReadWaveInfo(WavData.GetData(), WavData.Num()))
    {
        UE_LOG(LogTemp, Error, TEXT("[VOICEVOX] Failed to read wav info"));
        return nullptr;
    }

    const int32 NumChannels = *WaveInfo.pChannels;
    const int32 SampleRate = *WaveInfo.pSamplesPerSec;
    const int32 BitsPerSample = *WaveInfo.pBitsPerSample;
    const int32 PCMDataSize = WaveInfo.SampleDataSize;

    if (BitsPerSample != 16)
    {
        UE_LOG(LogTemp, Error, TEXT("[VOICEVOX] Only 16-bit PCM wav is supported. BitsPerSample=%d"), BitsPerSample);
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
