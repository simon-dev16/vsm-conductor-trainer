"""Проверка готовности backend перед вписыванием ключей.

    python Backend/doctor.py             что настроено и чего не хватает
    python Backend/doctor.py --live      плюс реальный запрос к модели (тратит квоту)
    python Backend/doctor.py --template  напечатать .env с пояснением к каждой строке

Коды возврата: 0 — можно начинать смену, 1 — есть блокирующие проблемы,
2 — сервер не отвечает (это не поломка настроек, просто не запущен).
"""
import json
import os
import socket
import sys
import urllib.error
import urllib.request
from pathlib import Path

BACKEND = Path(__file__).resolve().parent
sys.path.insert(0, str(BACKEND))

from ai_provider import YandexProvider, http_failure  # noqa: E402
from server import load_env  # noqa: E402

# (имя, значение по умолчанию, пояснение, нужно ли для работы ИИ)
KEYS = [
    ('VSM_AI_PROVIDER', 'yandex', 'Провайдер оценки ответов. Реализован только yandex.', False),
    ('VSM_YANDEX_API_KEY', '', 'Ключ сервисного аккаунта Yandex Cloud с доступом к YandexGPT.', True),
    ('VSM_YANDEX_FOLDER_ID', '', 'ID каталога, в котором открыт доступ к модели.', True),
    ('VSM_YANDEX_MODEL_URI', '', 'URI модели вида gpt://<folder>/<model>/latest.', True),
    ('VSM_AI_TIMEOUT', '45', 'Таймаут запроса к модели, секунды (5..90).', False),
    ('VSM_SPEECHKIT_API_KEY', '', 'Ключ SpeechKit. Без него доступен только текстовый ответ.', False),
    ('VSM_PORT', '18767', 'Порт локального сервера.', False),
    ('VSM_BIND', '127.0.0.1', 'Адрес прослушивания. 0.0.0.0 нужен только для телефона.', False),
    ('VSM_DEMO_PASSWORD', '', 'Пароль демо-пользователя.', False),
    ('VSM_DATABASE', 'Backend/data/vsm.sqlite3', 'Файл базы смен.', False),
]

PLACEHOLDERS = ('change-this', 'changeme', 'your-', 'your_', 'example',
                'placeholder', 'xxxx', 'todo', '<', 'впишите', 'заглушка')

problems = []
warnings = []


def out(line=''):
    print(line)


def fail(text):
    problems.append(text)


def warn(text):
    warnings.append(text)


def reset():
    problems.clear()
    warnings.clear()


def looks_filled(value):
    low = value.strip().lower()
    if not low:
        return False
    return not any(mark in low for mark in PLACEHOLDERS)


def check_shape(name, value):
    """Локальная проверка формата. Сеть не трогаем."""
    if name == 'VSM_YANDEX_FOLDER_ID':
        if not value.isalnum() or len(value) < 6:
            fail(f'{name}="{value}" — это не похоже на ID каталога. Скопируйте его из консоли Yandex Cloud.')
        elif not value.isdigit():
            warn(f'{name}="{value}" — обычно ID каталога состоит из цифр. Проверьте, что скопировали верно.')
    elif name == 'VSM_YANDEX_MODEL_URI':
        if not value.startswith('gpt://'):
            fail(f'{name}="{value}" — должно начинаться с gpt:// . Скопируйте точную строку со страницы модели.')
        elif value.endswith('/'):
            warn(f'{name} оканчивается на слэш — лишний символ, провайдер может ответить 404.')
    elif name in ('VSM_AI_TIMEOUT', 'VSM_PORT'):
        try:
            number = int(value)
        except ValueError:
            fail(f'{name}="{value}" — нужно целое число.')
            return
        low, high = (5, 90) if name == 'VSM_AI_TIMEOUT' else (1, 65535)
        if not low <= number <= high:
            fail(f'{name}={number} — вне диапазона {low}..{high}.')
    elif name == 'VSM_AI_PROVIDER' and value != 'yandex':
        fail(f'{name}="{value}" — реализован только yandex.')


def probe_model(provider):
    """Один минимальный запрос к модели: проверяет ключ, каталог и URI, но не контракт ответа."""
    payload = {'modelUri': provider.model,
               'completionOptions': {'stream': False, 'temperature': 0, 'maxTokens': '16'},
               'messages': [{'role': 'user', 'text': 'Ответь одним словом: ok'}]}
    request = urllib.request.Request(
        'https://llm.api.cloud.yandex.net/foundationModels/v1/completion',
        data=json.dumps(payload).encode(),
        headers={'Authorization': 'Api-Key ' + provider.key,
                 'Content-Type': 'application/json',
                 'x-folder-id': provider.folder,
                 'x-data-logging-enabled': 'false'},
        method='POST')
    try:
        with urllib.request.urlopen(request, timeout=provider.timeout) as response:
            response.read(65536)
        return True, 'модель ответила, ключ и каталог в порядке', ''
    except urllib.error.HTTPError as exc:
        code, message = http_failure(exc.code, 'ai', 'Модель')
        exc.close()
        return False, message, code
    except (urllib.error.URLError, TimeoutError):
        return False, 'провайдер не ответил — нет сети или сбил лимит времени', 'ai_unavailable'


def lan_address():
    """Адрес этого компьютера в локальной сети. Пакеты не уходят: connect() у UDP только выбирает маршрут."""
    probe=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
    try:
        probe.connect(('192.0.2.1',1))
        return probe.getsockname()[0]
    except OSError:
        return None
    finally:
        probe.close()


def health(port):
    try:
        with urllib.request.urlopen(f'http://127.0.0.1:{port}/health', timeout=4) as response:
            return json.load(response)
    except Exception:
        return None


def main():
    live = '--live' in sys.argv
    reset()
    if '--template' in sys.argv:
        out('Скопируй это в Backend/.env и впиши свои значения:')
        out()
        for name, default, note, needed in KEYS:
            marker = ' (нужен для ИИ)' if needed else ''
            out(f'# {note}{marker}')
            out(f'{name}={default}')
            out()
        return 0

    out('=' * 72)
    out('ПРОВЕРКА ГОТОВНОСТИ BACKEND')
    out('=' * 72)

    load_env()
    out()
    out('--- 1. КЛЮЧИ В Backend/.env ---')
    if not (BACKEND / '.env').exists():
        out('  файла .env нет')
        fail('Создай Backend/.env: скопируй Backend/.env.example и заполни строки.')
    ai_ready = True
    for name, default, note, needed in KEYS:
        value = os.getenv(name, default)
        if not value:
            if needed:
                fail(f'{name} — не задан. {note}')
                ai_ready = False
            else:
                out(f'  [--] {name:<24} не задан, {note}')
            continue
        if not looks_filled(value):
            fail(f'{name}="{value}" — это заглушка из примера, а не настоящее значение.')
            if needed:
                ai_ready = False
            continue
        check_shape(name, value)
        if needed or name == 'VSM_SPEECHKIT_API_KEY':
            out(f'  [OK] {name:<24} задан')
        else:
            out(f'  [ok] {name:<24} {value[:38]}')

    out()
    out('--- 2. ЖИВОЙ СЕРВЕР ---')
    port = os.getenv('VSM_PORT', '18767')
    state = health(port)
    if state is None:
        out(f'  на порту {port} никто не отвечает')
        warn(f'Запусти сервер: python Backend/server.py   (он читает .env только при старте)')
        ai_ready = False
    else:
        out(f'  отвечает: {state.get("service")}, aiConfigured={state.get("aiConfigured")}')
        if state.get('aiConfigured') and not ai_ready:
            warn('Сервер видит ключи, которых нет в .env — значит ключи попали в окружение иначе. Проверь, откуда.')
        elif not state.get('aiConfigured') and ai_ready:
            fail('Ключи в .env есть, но сервер их не видит. Перезапусти сервер: .env читается только при старте.')

    out()
    out('--- 3. ТЕЛЕФОН ---')
    bind=os.getenv('VSM_BIND','127.0.0.1')
    lan=lan_address()
    if bind!='0.0.0.0' and bind!='::':
        out(f'  сервер слушает {bind} — с телефона сюда не достучаться')
        if lan:
            out(f'  адрес в локальной сети: http://{lan}:{port}')
            out('  если это не тот адрес, посмотри ipconfig и возьми адрес Wi-Fi')
            out('  чтобы проверить на телефоне:')
            out('    1. добавь VSM_BIND=0.0.0.0 в Backend/.env и перезапусти сервер')
            out(f'    2. в Config/DefaultGame.ini поставь ApiBaseUrlV2="http://{lan}:{port}"')
            out('    3. телефон и компьютер в одной сети, и файрвол пускает порт')
        else:
            out('  адрес в локальной сети определить не удалось — компьютер не в сети')
    else:
        out(f'  сервер слушает {bind}, с телефона доступен' + (f' как http://{lan}:{port}' if lan else ''))
        out(f'  в клиенте должно быть ApiBaseUrlV2="http://{lan}:{port}"' if lan else '  в клиенте должен быть адрес этого компьютера')

    out()
    out('--- 4. РЕАЛЬНЫЙ ЗАПРОС К МОДЕЛИ ---')
    if not live:
        out('  пропущен. Запусти с --live, когда ключи будут вписаны (тратит немного квоты).')
    elif not ai_ready:
        out('  пропущен: ключи не заполнены или сервер не перезапущен.')
    else:
        ok, message, code = probe_model(YandexProvider())
        out(f'  {"[OK]" if ok else "[!!]"} {message}' + (f'  ({code})' if code else ''))
        if not ok:
            fail(f'Модель не отвечает: {message}')

    out()
    out('=' * 72)
    for text in warnings:
        out(f'! {text}')
    for text in problems:
        out(f'X {text}')
    out('=' * 72)
    if problems:
        out(f'НЕ ГОТОВО: проблем {len(problems)}.')
        out('Сначала исправь их, потом запусти python Backend/verify_v2_contract.py')
        return 1
    if not state:
        out('Ключи в порядке, но сервер не запущен.')
        return 2
    out('ГОТОВО. Дальше: python Backend/verify_v2_contract.py')
    return 0


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    sys.exit(main())
