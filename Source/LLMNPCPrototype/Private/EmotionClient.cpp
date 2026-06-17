#include "EmotionClient.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "Async/Async.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"

void UEmotionClient::AppendStringToBody(TArray<uint8>& Body, const FString& Text)
{
    FTCHARToUTF8 Converter(*Text);
    Body.Append(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length());
}

void UEmotionClient::AnalyzeAudioFile(const FString& FilePath)
{
    AnalyzeAudioFileWithId(FilePath, -1);
}

void UEmotionClient::AnalyzeAudioFileWithId(const FString& FilePath, int32 RequestId)
{
    UE_LOG(LogTemp, Warning, TEXT("Emotion AnalyzeAudioFileWithId called. RequestId=%d, FilePath=%s"), RequestId, *FilePath);

    TArray<uint8> AudioData;
    if (!FFileHelper::LoadFileToArray(AudioData, *FilePath))
    {
        const FString ErrorMessage = FString::Printf(TEXT("Failed to load audio file for emotion analysis: %s"), *FilePath);
        UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMessage);
        OnEmotionErrorReceived.Broadcast(ErrorMessage);
        return;
    }

    const FString Url = TEXT("http://127.0.0.1:8002/analyze");
    const FString Boundary = TEXT("----UE5EmotionBoundary7MA4YWxkTrZu0gW");

    TArray<uint8> Body;

    const FString FileName = FPaths::GetCleanFilename(FilePath);

    AppendStringToBody(Body, TEXT("--") + Boundary + TEXT("\r\n"));
    AppendStringToBody(
        Body,
        FString::Printf(
            TEXT("Content-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"),
            *FileName
        )
    );
    AppendStringToBody(Body, TEXT("Content-Type: audio/wav\r\n\r\n"));

    Body.Append(AudioData);

    AppendStringToBody(Body, TEXT("\r\n"));
    AppendStringToBody(Body, TEXT("--") + Boundary + TEXT("--\r\n"));

    TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("multipart/form-data; boundary=") + Boundary);
    Request->SetContent(Body);
    Request->SetTimeout(60.0f);

    Request->OnProcessRequestComplete().BindUObject(
        this,
        &UEmotionClient::OnEmotionResponseReceivedWithId,
        RequestId
    );

    Request->ProcessRequest();

    UE_LOG(LogTemp, Warning, TEXT("Emotion request sent. RequestId=%d"), RequestId);
}

void UEmotionClient::AnalyzeAudioFileWhenReady(
    const FString& FilePath,
    float TimeoutSeconds,
    float PollIntervalSeconds
)
{
    AnalyzeAudioFileWhenReadyWithId(FilePath, -1, TimeoutSeconds, PollIntervalSeconds);
}

void UEmotionClient::AnalyzeAudioFileWhenReadyWithId(
    const FString& FilePath,
    int32 RequestId,
    float TimeoutSeconds,
    float PollIntervalSeconds
)
{
    UE_LOG(LogTemp, Warning, TEXT("Waiting for emotion audio file. RequestId=%d, FilePath=%s"), RequestId, *FilePath);

    TWeakObjectPtr<UEmotionClient> WeakThis(this);

    Async(EAsyncExecution::ThreadPool, [WeakThis, FilePath, RequestId, TimeoutSeconds, PollIntervalSeconds]()
        {
            IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

            const double StartTime = FPlatformTime::Seconds();
            int64 LastFileSize = -1;
            int32 StableCount = 0;

            while (FPlatformTime::Seconds() - StartTime < TimeoutSeconds)
            {
                if (PlatformFile.FileExists(*FilePath))
                {
                    const int64 CurrentFileSize = PlatformFile.FileSize(*FilePath);

                    if (CurrentFileSize > 44)
                    {
                        if (CurrentFileSize == LastFileSize)
                        {
                            StableCount++;
                        }
                        else
                        {
                            StableCount = 0;
                            LastFileSize = CurrentFileSize;
                        }

                        if (StableCount >= 1)
                        {
                            AsyncTask(ENamedThreads::GameThread, [WeakThis, FilePath, RequestId]()
                                {
                                    if (WeakThis.IsValid())
                                    {
                                        UE_LOG(
                                            LogTemp,
                                            Warning,
                                            TEXT("Emotion audio file is ready. Start emotion analysis. RequestId=%d, FilePath=%s"),
                                            RequestId,
                                            *FilePath
                                        );

                                        WeakThis->AnalyzeAudioFileWithId(FilePath, RequestId);
                                    }
                                });

                            return;
                        }
                    }
                }

                FPlatformProcess::Sleep(FMath::Max(0.01f, PollIntervalSeconds));
            }

            AsyncTask(ENamedThreads::GameThread, [WeakThis, FilePath, RequestId]()
                {
                    if (WeakThis.IsValid())
                    {
                        const FString ErrorMessage = FString::Printf(
                            TEXT("Timeout waiting for emotion audio file. RequestId=%d, FilePath=%s"),
                            RequestId,
                            *FilePath
                        );

                        UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMessage);
                        WeakThis->OnEmotionErrorReceived.Broadcast(ErrorMessage);
                    }
                });
        });
}

void UEmotionClient::OnEmotionResponseReceived(
    FHttpRequestPtr Request,
    FHttpResponsePtr Response,
    bool bWasSuccessful
)
{
    OnEmotionResponseReceivedWithId(Request, Response, bWasSuccessful, -1);
}

void UEmotionClient::OnEmotionResponseReceivedWithId(
    FHttpRequestPtr Request,
    FHttpResponsePtr Response,
    bool bWasSuccessful,
    int32 RequestId
)
{
    if (!bWasSuccessful || !Response.IsValid())
    {
        const FString ErrorMessage = FString::Printf(
            TEXT("Emotion request failed. RequestId=%d, bWasSuccessful=%s, ResponseValid=%s, RequestStatus=%d"),
            RequestId,
            bWasSuccessful ? TEXT("true") : TEXT("false"),
            Response.IsValid() ? TEXT("true") : TEXT("false"),
            Request.IsValid() ? static_cast<int32>(Request->GetStatus()) : -1
        );

        UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMessage);
        OnEmotionErrorReceived.Broadcast(ErrorMessage);
        return;
    }

    const int32 ResponseCode = Response->GetResponseCode();
    const FString ResponseString = Response->GetContentAsString();

    UE_LOG(LogTemp, Warning, TEXT("Emotion response code: %d, RequestId=%d"), ResponseCode, RequestId);
    UE_LOG(LogTemp, Warning, TEXT("Emotion raw response: %s"), *ResponseString);

    if (ResponseCode != 200)
    {
        const FString ErrorMessage = FString::Printf(
            TEXT("Emotion server error. RequestId=%d, Code=%d, Body=%s"),
            RequestId,
            ResponseCode,
            *ResponseString
        );

        UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMessage);
        OnEmotionErrorReceived.Broadcast(ErrorMessage);
        return;
    }

    TSharedPtr<FJsonObject> RootObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseString);

    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
    {
        const FString ErrorMessage = FString::Printf(
            TEXT("Failed to parse emotion response JSON. RequestId=%d"),
            RequestId
        );

        UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMessage);
        OnEmotionErrorReceived.Broadcast(ErrorMessage);
        return;
    }

    FString Emotion;
    if (!RootObject->TryGetStringField(TEXT("emotion"), Emotion))
    {
        const FString ErrorMessage = FString::Printf(
            TEXT("No emotion field found in emotion response. RequestId=%d"),
            RequestId
        );

        UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMessage);
        OnEmotionErrorReceived.Broadcast(ErrorMessage);
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("Emotion result. RequestId=%d, Emotion=%s"), RequestId, *Emotion);

    // Old event, kept for compatibility.
    OnEmotionResultReceived.Broadcast(Emotion);

    // New event with RequestId.
    OnEmotionResultReceivedWithId.Broadcast(RequestId, Emotion);
}