"""Server-only model adapter. No credentials or provider calls belong in Unreal."""
import json
import os
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SYSTEM_PROMPT = (ROOT / 'Contracts' / 'ai-system-prompt.txt').read_text(encoding='utf-8')
OUTPUT_SCHEMA = json.loads((ROOT / 'Contracts' / 'ai-decision.schema.json').read_text(encoding='utf-8'))


class ProviderError(Exception):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


class YandexProvider:
    def __init__(self):
        if os.getenv('VSM_AI_PROVIDER', 'yandex') != 'yandex':
            raise ValueError('Only VSM_AI_PROVIDER=yandex is implemented')
        self.key = os.getenv('VSM_YANDEX_API_KEY', '')
        self.folder = os.getenv('VSM_YANDEX_FOLDER_ID', '')
        self.model = os.getenv('VSM_YANDEX_MODEL_URI', '')
        self.timeout = min(90, max(5, int(os.getenv('VSM_AI_TIMEOUT', '45'))))

    def decide(self, context):
        if not self.key or not self.folder or not self.model:
            raise ProviderError('ai_not_configured', 'Задайте API-ключ, folder ID и URI модели в окружении backend.')
        payload = {
            'modelUri': self.model,
            'completionOptions': {'stream': False, 'temperature': .3, 'maxTokens': '4000'},
            'messages': [{'role': 'system', 'text': SYSTEM_PROMPT},
                         {'role': 'user', 'text': json.dumps(context, ensure_ascii=False)}],
            'jsonSchema': {'schema': OUTPUT_SCHEMA},
        }
        request = urllib.request.Request(
            'https://llm.api.cloud.yandex.net/foundationModels/v1/completion',
            data=json.dumps(payload).encode(),
            headers={'Authorization': 'Api-Key ' + self.key, 'Content-Type': 'application/json',
                     'x-folder-id': self.folder, 'x-data-logging-enabled': 'false'},
            method='POST')
        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as response:
                raw = response.read(1024 * 1024 + 1)
                if len(raw) > 1024 * 1024:
                    raise ProviderError('ai_invalid_response', 'Ответ модели слишком большой.')
                envelope = json.loads(raw)
            result = envelope.get('result', envelope)
            alternative = result['alternatives'][0]
            if alternative.get('status') not in ('ALTERNATIVE_STATUS_FINAL', None):
                raise ProviderError('ai_incomplete', 'Модель не завершила ответ. Состояние не изменено.')
            return json.loads(alternative['message']['text'])
        except urllib.error.HTTPError as exc:
            # Provider bodies can contain request context; only normalize status, never echo secrets.
            status = exc.code
            exc.close()
            raise ProviderError('ai_http_error', f'Провайдер ИИ вернул HTTP {status}.') from None
        except (urllib.error.URLError, TimeoutError):
            raise ProviderError('ai_unavailable', 'Провайдер ИИ недоступен или не ответил вовремя.') from None
        except (KeyError, IndexError, ValueError, TypeError, AttributeError):
            raise ProviderError('ai_invalid_response', 'Ответ ИИ не соответствует контракту.') from None
