import copy
import datetime as dt
import json
import math
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SITUATIONS = json.loads((ROOT / 'Contracts/situations.json').read_text(encoding='utf-8'))['situations']
METRICS = json.loads((ROOT / 'Contracts/metrics.json').read_text(encoding='utf-8'))['metrics']
METRIC_IDS = {m['id'] for m in METRICS}
ACTORS = {f'passenger_{i:02d}': ['speak', 'set_emotion', 'set_task_marker', 'look_at'] for i in range(1, 11)}


class ApiError(Exception):
    def __init__(self, status, code, message):
        super().__init__(message)
        self.status, self.code = status, code


def utcnow():
    return dt.datetime.now(dt.timezone.utc)


def iso(value=None):
    return (value or utcnow()).isoformat().replace('+00:00', 'Z')


def number(value):
    return type(value) in (int, float) and math.isfinite(value) and 0 <= value <= 100


def validate_decision(value):
    def require(ok, message):
        if not ok:
            raise ApiError(502, 'invalid_ai_decision', message)
    def text(item, limit=8000):
        return isinstance(item, str) and len(item) <= limit
    require(isinstance(value, dict), 'ИИ должен вернуть объект.')
    required = {'status', 'actorId', 'text', 'deadlineSeconds', 'state', 'commands', 'assessments', 'debrief'}
    require(set(value) == required, 'Неверные поля решения ИИ.')
    require(value['status'] in ('active', 'completed', 'failed'), 'Неизвестный статус.')
    require(isinstance(value['actorId'], str) and value['actorId'] in ACTORS, 'Неизвестный пассажир.')
    require(text(value['text']) and text(value['debrief']), 'Неверный текст.')
    require(type(value['deadlineSeconds']) is int and 0 <= value['deadlineSeconds'] <= 300, 'Недопустимый таймер.')
    state = value['state']
    require(isinstance(state, dict) and set(state) == {'safety', 'loyalty', 'competencies'}, 'Неверная структура состояния.')
    require(number(state['safety']), 'Недопустимая безопасность.')
    for field, keys in [('loyalty', set(ACTORS)), ('competencies', METRIC_IDS)]:
        scores = state[field]
        require(isinstance(scores, dict) and set(scores) <= keys and all(number(x) for x in scores.values()), 'Недопустимые показатели.')
    require(value['actorId'] in state['loyalty'], 'Нет лояльности активного пассажира.')
    commands = value['commands']
    require(isinstance(commands, list) and len(commands) <= 32, 'Слишком много команд.')
    for command in commands:
        require(isinstance(command, dict) and set(command) == {'actorId', 'type', 'value', 'targetActorId'}, 'Неверная команда.')
        require(isinstance(command['actorId'], str) and command['actorId'] in ACTORS, 'Неизвестный актор команды.')
        require(command['type'] in ACTORS[command['actorId']], 'Команда не поддерживается актором.')
        require(text(command['value']) and isinstance(command['targetActorId'], str), 'Неверные аргументы команды.')
        require(not command['targetActorId'] or command['targetActorId'] in ACTORS, 'Неизвестная цель команды.')
        if command['type'] == 'look_at':
            require(command['targetActorId'] in ACTORS, 'look_at требует цель.')
        if command['type'] == 'set_emotion':
            require(command['value'] in ('neutral', 'satisfied', 'irritated', 'angry', 'concerned'), 'Неизвестная эмоция.')
        if command['type'] == 'set_task_marker':
            require(command['value'] in ('true', 'false'), 'Неверное состояние маркера.')
    assessments = value['assessments']
    require(isinstance(assessments, list) and len(assessments) <= 3, 'Неверные оценки.')
    seen = set()
    for metric in assessments:
        require(isinstance(metric, dict) and set(metric) == {'metricId', 'score', 'reason', 'evidence'}, 'Неверная оценка.')
        require(isinstance(metric['metricId'], str) and metric['metricId'] in METRIC_IDS and metric['metricId'] not in seen, 'Неизвестная или повторная метрика.')
        require(number(metric['score']) and text(metric['reason'],4000) and bool(metric['reason'].strip()) and text(metric['evidence'],4000) and bool(metric['evidence'].strip()), 'Нужны балл, объяснение и основание.')
        seen.add(metric['metricId'])
    if value['status'] != 'active':
        require(seen == METRIC_IDS and bool(value['debrief']), 'Итог требует все оценки и разбор.')
    return copy.deepcopy(value)


def snapshot(decision, run_id, revision):
    value = validate_decision(decision)
    deadline = iso(utcnow() + dt.timedelta(seconds=value['deadlineSeconds'])) if value['status']=='active' and value['deadlineSeconds'] else None
    commands = []
    for command in value['commands']:
        command['commandId'] = str(uuid.uuid4())
        if not command['targetActorId']:
            del command['targetActorId']
        commands.append(command)
    return {'runId': run_id, 'revision': revision, 'status': value['status'], 'serverTime': iso(),
            'state': value['state'], 'currentNode': {'id': str(uuid.uuid4()), 'actorId': value['actorId'],
                'text': value['text'], 'deadlineAt': deadline, 'choices': []},
            'commands': commands, 'assessments': value['assessments'], 'debrief': value['debrief'], 'isMock': False}


def context(situation, current, events, event_type, player_input):
    return {'situation': situation, 'metrics': METRICS, 'availableActors': ACTORS,
            'currentState': current, 'history': events[-20:], 'eventType': event_type,
            'playerInput': player_input, 'serverTime': iso()}
