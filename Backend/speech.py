import base64
import binascii
import json
import os
import urllib.error
import urllib.request
from ai_provider import http_failure
from domain import ApiError


def transcribe(body):
    encoded=body.get('audio_base64')
    if not isinstance(encoded,str) or not 1<=len(encoded)<=1280000:
        raise ApiError(422,'invalid_audio','Запишите до 30 секунд речи.')
    try:
        audio=base64.b64decode(encoded,validate=True)
    except (ValueError,binascii.Error):
        raise ApiError(422,'invalid_audio','Некорректное аудио.') from None
    if not 2<=len(audio)<=960000 or len(audio)%2:
        raise ApiError(422,'invalid_audio','Ожидается mono PCM16 16000 Гц, до 30 секунд.')
    key=os.getenv('VSM_SPEECHKIT_API_KEY','')
    if not key:
        raise ApiError(503,'speech_not_configured','Ключ распознавания не настроен. Используйте текстовый ответ.')
    request=urllib.request.Request('https://stt.api.cloud.yandex.net/speech/v1/stt:recognize?lang=ru-RU&format=lpcm&sampleRateHertz=16000',
        data=audio,headers={'Authorization':'Api-Key '+key,'Content-Type':'application/octet-stream','x-data-logging-enabled':'false'},method='POST')
    try:
        with urllib.request.urlopen(request,timeout=45) as response:
            raw=response.read(65537)
        if len(raw)>65536:raise ValueError()
        text=json.loads(raw)['result']
        if not isinstance(text,str) or len(text)>4000:raise ValueError()
        return {'text':text}
    except urllib.error.HTTPError as exc:
        status=exc.code;exc.close()
        code,message=http_failure(status,'speech','Распознавание')
        raise ApiError(503,code,message) from None
    except (urllib.error.URLError,TimeoutError):
        raise ApiError(503,'speech_unavailable','Распознавание недоступно. Используйте текст.') from None
    except (ValueError,KeyError,TypeError):
        raise ApiError(503,'speech_invalid_response','Ответ распознавания отклонён.') from None
