"""Живая проверка контракта /v2 ровно по форме запросов, которую шлёт UE-клиент.

Форма тела скопирована из VSMShiftSubsystem.cpp:
  POST /v2/shifts        { clientActionId, mode, situation_id }
  POST /v2/shifts/{id}/actions
                         { clientActionId, revision, kind, task_id, actor_id,
                           text, slot, accept, position:[x,y,z] }

Запуск (сервер должен быть уже поднят на 127.0.0.1:18767):
    python Backend/verify_v2_contract.py

Файл НЕ подпадает под шаблон test_*.py, поэтому unittest его не подхватывает.

ВАЖНО: шаги предмета (взятие/передача) зависят от того, что ИИ-классификатор создал
pending_action. Без ключа YandexGPT они недостижимы, и скрипт честно сообщает
«требует ключ», а не выдаёт ложную ошибку.
"""
import base64
import json
import sys
import urllib.error
import urllib.request
import uuid

BASE = "http://127.0.0.1:18767"
results: list[tuple[str, int, bool, str]] = []


def call(method, path, body=None, token=None):
    raw = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(BASE + path, data=raw, method=method)
    req.add_header("Content-Type", "application/json")
    if token:
        req.add_header("Authorization", "Bearer " + token)
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            code, data = r.status, r.read().decode("utf-8")
    except urllib.error.HTTPError as e:
        code, data = e.code, e.read().decode("utf-8")
    except Exception as e:
        code, data = 0, f"{type(e).__name__}: {e}"
    try:
        return code, json.loads(data)
    except Exception:
        return code, data[:300]


def check(name, code, data, expect, note=""):
    """expect: множество допустимых кодов, либо None = любой 2xx."""
    if expect is None:
        ok = 200 <= code < 300
    else:
        ok = code in expect
    results.append((name, code, ok, note))
    flag = "OK " if ok else "ERR"
    tag = f"  <- {note}" if note else ""
    print(f"  [{flag}] {code:3} {name}{tag}")
    body = json.dumps(data, ensure_ascii=False) if not isinstance(data, str) else data
    print(f"         {body[:250]}")
    return data


def err_code(data):
    if isinstance(data, dict):
        return (data.get("error") or {}).get("code")
    return None


def action(shift_id, token, revision, kind, **extra):
    body = {
        "clientActionId": str(uuid.uuid4()),
        "revision": revision,
        "kind": kind,
        "task_id": extra.get("task_id", ""),
        "actor_id": extra.get("actor_id", "passenger_01"),
        "text": extra.get("text", ""),
        "slot": extra.get("slot", 0),
        "accept": extra.get("accept", True),
        "position": extra.get("position", [0, 0, 100]),
    }
    return call("POST", f"/v2/shifts/{shift_id}/actions", body, token)


print("=" * 74)
print("ЖИВАЯ ПРОВЕРКА КОНТРАКТА /v2")
print("=" * 74)

print("\n--- 1. ВХОД (auth/register 201, auth/login 200) ---")
login = "verify_" + uuid.uuid4().hex[:8]
c, d = call("POST", "/v2/auth/register", {"login": login, "password": "verify-pass-123"})
token = check("регистрация", c, d, {201, 200})
token = (token or {}).get("accessToken") if isinstance(token, dict) else None
c, d = call("POST", "/v2/auth/login", {"login": login, "password": "verify-pass-123"})
d = check("вход", c, d, {200})
token = token or ((d.get("accessToken")) if isinstance(d, dict) else None)
if not token:
    print("\nНЕТ ТОКЕНА — дальше проверять нечего.")
    sys.exit(1)
print(f"         токен получен, {len(token)} символов")

print("\n--- 2. ПРОФИЛЬ И РЕЙТИНГ ---")
check("профиль", *call("GET", "/v2/profile", token=token)[:1], call("GET", "/v2/profile", token=token)[1], {200})
check("рейтинг", *call("GET", "/v2/leaderboard/company", token=token)[:1], call("GET", "/v2/leaderboard/company", token=token)[1], {200})

print("\n--- 3. СТАРТ СМЕНЫ (StartShift: clientActionId + mode=training) ---")
c, shift = call("POST", "/v2/shifts",
                {"clientActionId": str(uuid.uuid4()), "mode": "training", "situation_id": 46}, token)
check("старт смены", c, shift, {201, 200})
shift_id = shift.get("id") if isinstance(shift, dict) else None
revision = shift.get("revision") if isinstance(shift, dict) else None
tasks = shift.get("tasks", []) if isinstance(shift, dict) else []
print(f"         shift_id={shift_id} revision={revision} правила={shift.get('rules') if isinstance(shift, dict) else None}")
print(f"         задач: {len(tasks)}")
active = [t for t in tasks if t.get("status") == "active"]
for t in tasks[:8]:
    print(f"           id={t.get('id')} actor={t.get('actor_id')} ситуация={t.get('situation_id')} [{t.get('status')}]")
    print(f"             {str(t.get('text'))[:120]}")
task_id = active[0]["id"] if active else (tasks[0]["id"] if tasks else "")

print("\n--- 4. ЗАЩИТА ОТ ГОНКИ: заведомо старая ревизия ---")
c, d = action(shift_id, token, 1, "position", position=[0, 0, 100])
check("старая ревизия отклонена", c, d, {409}, "ожидаем 409 invalid_action")

print("\n--- 5. ОТВЕТ ИГРОКА — здесь определяется, настроен ли ИИ ---")
c, d = action(shift_id, token, revision, "answer", task_id=task_id,
              text="да, сейчас принесу воду", position=[-180, 0, 100])
ai_ready = c == 200
check("ответ игрока", c, d, {200, 503}, "200 = ИИ настроен, 503 = нет ключа")
print(f"         ИИ {'НАСТРОЕН' if ai_ready else 'НЕ НАСТРОЕН (нет ключа YandexGPT)'}")
if isinstance(d, dict):
    if "revision" in d:
        revision = d["revision"]
    for t in d.get("tasks", []):
        if t.get("id") == task_id:
            print(f"         текст задачи: {str(t.get('text'))[:200]}")
            print(f"         pending_action: {t.get('pending_action')}")

print("\n--- 6. ПРЕДМЕТ (нужен ИИ: он создаёт pending_action) ---")
if not ai_ready:
    c, d = action(shift_id, token, revision, "take_item", task_id=task_id, position=[-260, 240, 100])
    check("взятие предмета", c, d, {409, 422}, "ожидаемо без ключа: нет запроса на предмет")
    c, d = action(shift_id, token, revision, "give_item", task_id=task_id, slot=0, position=[120, 118, 90])
    check("передача предмета", c, d, {409, 422}, "ожидаемо без ключа: нет ожидаемой передачи")
    print("         >>> ПРЕДМЕТНЫЙ ПОТОК НЕ ПРОВЕРЕН. Нужен VSM_YANDEX_API_KEY в Backend/.env.")
    print("         >>> Логика покрыта модульными тестами (test_shift_store), но не вживую.")
else:
    c, d = action(shift_id, token, revision, "take_item", task_id=task_id, position=[1200, 100, 100])
    check("взятие издалека отклонено", c, d, {422, 409}, "ждём «Подойдите к станции»")
    c, d = action(shift_id, token, revision, "take_item", task_id=task_id, position=[-260, 240, 100])
    check("взятие у станции", c, d, {200}, "ждём 200 и предмет в слоте")
    if isinstance(d, dict):
        revision = d.get("revision", revision)
        print(f"         инвентарь: {d.get('inventory')}")
    c, d = action(shift_id, token, revision, "give_item", task_id=task_id, slot=0, position=[120, 118, 90])
    check("передача пассажиру", c, d, {200}, "ждём 200 и освобождение слота")
    if isinstance(d, dict):
        revision = d.get("revision", revision)
        print(f"         инвентарь после передачи: {d.get('inventory')}")

print("\n--- 7. РЕЧЬ: валидный PCM16 mono 16 кГц, 1 секунда тишины ---")
pcm = base64.b64encode(b"\x00\x00" * 16000).decode()
c, d = call("POST", "/v2/speech/transcribe", {"audio_base64": pcm}, token)
code_name = err_code(d)
check("STT", c, d, {200, 503},
      "200 = ключ есть, 503 = нет ключа (сервер сам предлагает текст)")

print("\n" + "=" * 74)
passed = sum(1 for _, _, ok, _ in results if ok)
print(f"ИТОГ: {passed}/{len(results)} проверок соответствуют ожиданию")
for name, code, ok, note in results:
    if not ok:
        print(f"  НЕОЖИДАННО: {name} -> {code} {note}")
print("=" * 74)
sys.exit(0 if passed == len(results) else 1)
