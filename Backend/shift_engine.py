from copy import deepcopy
from datetime import datetime, timezone
from pathlib import Path
import json
import uuid
import math

from inventory import acquire, empty_inventory, transfer
from scenario_machine import Catalog, ScenarioError, response_score, shift_score


ROOT = Path(__file__).resolve().parents[1]
RULES = json.loads((ROOT / 'Contracts/shift-rules.json').read_text(encoding='utf-8'))
WORLD_ACTIONS = json.loads((ROOT / 'Contracts/world-actions.json').read_text(encoding='utf-8-sig'))


def validate_world_actions(world):
    try:
        objects = [world['station']] + world['passengers']
        if not world['passengers'] or len({o['id'] for o in objects}) != len(objects):
            raise ValueError()
        for obj in objects:
            if not isinstance(obj['id'],str) or not obj['id'] or len(obj['position'])!=3:
                raise ValueError()
            if any(type(n) not in (int,float) or not math.isfinite(n) for n in obj['position']):
                raise ValueError()
            if type(obj['radius']) not in (int,float) or not 0<obj['radius']<=10000:
                raise ValueError()
        for binding in world['bindings'].values():
            if binding['action']!='give_item' or binding['quantity']!=1 or not isinstance(binding['item'],str) or not binding['item']:
                raise ValueError()
    except (KeyError,TypeError,ValueError):
        raise ScenarioError('Некорректная конфигурация предметных взаимодействий') from None


validate_world_actions(WORLD_ACTIONS)


class ShiftEngine:
    def __init__(self, provider, catalog=None):
        self.provider = provider
        self.catalog = catalog or Catalog()

    def begin(self, mode, now, situation_id=46):
        if mode not in ('training', 'ranked'):
            raise ScenarioError('Unknown mode')
        if situation_id not in self.catalog.flows:
            raise ScenarioError('Схема этой ситуации ещё не написана')
        value = {'id': str(uuid.uuid4()), 'revision': 0, 'mode': mode, 'status': 'active',
                 'started_at': now, 'deadline': now + RULES['duration_seconds'], 'next_spawn': now + RULES['task_spawn_interval_seconds'],
                 'rules': deepcopy(RULES), 'safety': RULES['initial_safety'], 'loyalty': RULES['initial_loyalty'], 'inventory': empty_inventory(),
                 'tasks': [], 'events': [], 'assessments': [], 'position': [-180, 0, 100],
                 'tickets': self.tickets(), 'selected_task': '', 'rating': None,
                 'content_version': self.catalog.version(), 'content_snapshot': self.catalog.snapshot(),
                 'world_actions': deepcopy(WORLD_ACTIONS)}
        self.spawn(value, situation_id, now)
        return value

    @staticmethod
    def tickets():
        values = []
        for i in range(10):
            person = {'name': f'Учебный Пассажир {i+1}', 'birth_date': f'199{ i % 10 }-01-15',
                      'sex': 'М', 'citizenship': 'РФ', 'document': f'0000 {100000+i}', 'portrait': f'portrait_{i}'}
            passport = deepcopy(person)
            if i in (2, 7):
                passport['document'] = f'0000 {900000+i}'
            values.append({'actor_id': f'passenger_{i+1:02}', 'passport': passport, 'ticket': deepcopy(person),
                           'terminal': deepcopy(person), 'on_manifest': True, 'checked': False,
                           'started_at': None, 'timed_out': False, 'issue': None})
        return values

    def spawn(self, value, situation_id, now):
        task = self.catalog.begin(situation_id)
        definition = self.catalog.situations[situation_id]
        passengers = value.get('world_actions',WORLD_ACTIONS)['passengers']
        task.update({'id': str(uuid.uuid4()), 'actor_id': passengers[len(value['tasks']) % len(passengers)]['id'],
                     'created_at': now, 'deadline': now + definition['timer_seconds'],
                     'reaction_seconds': None, 'timed_out': False, 'history': [], 'facts': {},
                     'text': definition['situation_description'], 'emotion': 'neutral'})
        # Demonstration world facts are explicit and fixed, never inferred from player text.
        for state in self.catalog.flows[situation_id]['states'].values():
            branches = state.get('engine_branches', [])
            for index, branch in enumerate(branches):
                task['facts'][branch['game_state_condition']] = index == 0
            for transition in state.get('allowed_transitions', []):
                if transition.get('guard'):
                    task['facts'][transition['guard']] = True
        if situation_id == 46:
            task['facts'].update({'Пассажир едет против хода движения.': False,
                                 'Условие поездки против хода движения не подтверждено.': True,
                                 'Лимон есть в наличии.': False, 'Лимона нет в наличии.': True})
        self.catalog.resolve(task, task['facts'])
        value['tasks'].append(task)
        value['selected_task'] = task['id']

    def tick(self, value, now):
        if value['status'] != 'active':
            return
        for task in value['tasks']:
            if task['status'] == 'active' and not task['timed_out'] and now >= task['deadline']:
                task['timed_out'] = True
                task['reaction_seconds'] = task['reaction_seconds'] if task['reaction_seconds'] is not None else task['deadline'] - task['created_at']
                kind = self.catalog.situations[task['situation_id']]['task_type']
                penalty = value['rules']['weights'][kind]
                value['safety'] = max(0, value['safety'] - penalty['timeout_safety_loss'])
                value['loyalty'] = max(0, value['loyalty'] - penalty['timeout_loyalty_loss'])
                value['events'].append({'kind': 'timeout', 'task_id': task['id'], 'at': now})
        for ticket in value['tickets']:
            if ticket['started_at'] is not None and not ticket['checked'] and not ticket['timed_out'] and now >= ticket['started_at'] + value['rules']['ticket_check_seconds']:
                ticket['timed_out'] = True
                value['loyalty'] = max(0, value['loyalty'] - 3)
                value['events'].append({'kind': 'ticket_timeout', 'actor_id': ticket['actor_id'], 'at': now})
        if value['safety'] < value['rules']['failure_below_safety'] or value['loyalty'] < value['rules']['failure_below_loyalty']:
            self.finish(value, 'failed')
        elif now >= value['deadline']:
            self.finish(value, 'completed')
        elif value['mode'] == 'ranked' and now >= value['next_spawn']:
            active = [t for t in value['tasks'] if t['status'] == 'active']
            if len(active) < value['rules']['max_active_tasks']:
                used = {t['situation_id'] for t in value['tasks']}
                critical = sum(self.catalog.situations[t['situation_id']]['task_type'] == 'критическая' for t in active)
                available = [s for s in self.catalog.flows if s not in used and
                             (critical < value['rules']['max_active_critical_tasks'] or self.catalog.situations[s]['task_type'] != 'критическая')]
                if available:
                    self.spawn(value, available[0], now)
            value['next_spawn'] = now + value['rules']['task_spawn_interval_seconds']

    @staticmethod
    def finish(value, status):
        value['status'] = status
        value['rating'] = shift_score(value['assessments'], value['safety'], value['loyalty'])

    def act(self, current, body, now):
        if body.get('revision') != current['revision']:
            raise ScenarioError('Состояние обновилось. Повторно получите смену.')
        value = deepcopy(current)
        self.tick(value, now)
        if value['status'] != 'active':
            return value
        kind = body.get('kind')
        task = next((t for t in value['tasks'] if t['id'] == body.get('task_id', value['selected_task'])), None)
        if 'position' in body:
            position = body.get('position')
            if not isinstance(position, list) or len(position) != 3 or any(type(n) not in (int, float) or not -10000 < n < 10000 for n in position):
                raise ScenarioError('Некорректная позиция')
            value['position'] = position
        if task and kind not in ('position', 'open_ticket', 'check_ticket'):
            value['selected_task'] = task['id']
        if kind == 'position':
            if 'position' not in body:
                raise ScenarioError('Некорректная позиция')
        elif kind == 'select_task':
            if not task:
                raise ScenarioError('Задача не найдена')
            value['selected_task'] = task['id']
        elif kind == 'answer':
            if not task or task['status'] != 'active':
                raise ScenarioError('Задача недоступна')
            text = body.get('text', '').strip()
            if not text or len(text) > 4000:
                raise ScenarioError('Введите ответ до 4000 символов')
            context = self.catalog.classify_context(task, task['facts'], task['history'], text)
            decision = self.provider.classify(context)
            updated = self.catalog.apply(task, decision, task['facts'], task['revision'], value.get('world_actions',WORLD_ACTIONS)['bindings'])
            updated['history'].append({'speaker': 'player', 'text': text, 'at': now, 'transition': decision['transition_id']})
            if decision['transition_id'] != 'NO_MATCH' and updated['reaction_seconds'] is None:
                updated['reaction_seconds'] = max(0, now - task['created_at'])
            if updated['pending_action']:
                updated['text'] = 'Получите предмет на станции проводника и передайте пассажиру.'
            elif decision['transition_id'] == 'NO_MATCH':
                updated['text'] = 'Ваш ответ не соответствует доступному шагу. Уточните действие.'
            else:
                state = self.catalog.state(updated)
                updated['text'] = state.get('result', self.catalog.situations[updated['situation_id']]['situation_description'])
            turn_assessment = self.provider.assess({'situation': self.catalog.situations[updated['situation_id']],
                'history': updated['history'], 'events': updated['events'], 'stage': updated['status']})
            updated.setdefault('turn_assessments', []).append({'turn': len(updated['history']),
                'at': now, 'assessment': turn_assessment})
            value['tasks'][value['tasks'].index(task)] = updated
            self.assess_finished(value, updated, turn_assessment)
        elif kind == 'take_item':
            if not task or not task.get('pending_action'):
                raise ScenarioError('Нет запроса на предмет')
            station = value.get('world_actions',WORLD_ACTIONS)['station']
            if body.get('world_id',station['id']) != station['id']:
                raise ScenarioError('Неверная станция')
            if sum((a-b)**2 for a,b in zip(value['position'], station['position'])) > station['radius']**2:
                raise ScenarioError('Подойдите к станции проводника')
            item = task['pending_action']['requirement']['item']
            if any(slot and slot['item_id']==item for slot in value['inventory']):
                raise ScenarioError('Этот предмет уже в инвентаре')
            value['inventory'] = acquire(value['inventory'], item, str(uuid.uuid4()))
        elif kind == 'give_item':
            if not task or not task.get('pending_action'):
                raise ScenarioError('Нет ожидаемой передачи')
            if body.get('actor_id',task['actor_id']) != task['actor_id']:
                raise ScenarioError('Предмет предназначен другому пассажиру')
            passenger = next(p for p in value.get('world_actions',WORLD_ACTIONS)['passengers'] if p['id']==task['actor_id'])
            if sum((a-b)**2 for a,b in zip(value['position'], passenger['position'])) > passenger['radius']**2:
                raise ScenarioError('Подойдите к пассажиру')
            required = task['pending_action']['requirement']
            slot = body.get('slot')
            if type(slot) is not int or not 0 <= slot < 8 or not value['inventory'][slot]:
                raise ScenarioError('Выберите предмет')
            instance = value['inventory'][slot]['instance_id']
            value['inventory'] = transfer(value['inventory'], slot, required['item'], instance)
            updated = self.catalog.complete_action(task, task['facts'], task['revision'], required)
            updated['history'].append({'speaker': 'action', 'text': 'Передан предмет: '+required['item'], 'at': now})
            updated['text'] = 'Предмет передан. Уточните самочувствие или комфорт пассажира.'
            value['tasks'][value['tasks'].index(task)] = updated
            self.assess_finished(value, updated)
        elif kind in ('open_ticket', 'check_ticket'):
            ticket = next((t for t in value['tickets'] if t['actor_id'] == body.get('actor_id')), None)
            if not ticket or ticket['checked']:
                raise ScenarioError('Проверка недоступна')
            if ticket['started_at'] is None:
                ticket['started_at'] = now
            if kind == 'check_ticket':
                if type(body.get('accept')) is not bool:
                    raise ScenarioError('Выберите результат проверки')
                matches = ticket['passport'] == ticket['ticket'] == ticket['terminal'] and ticket['on_manifest']
                correct = body['accept'] == matches
                ticket['checked'] = True
                ticket['issue'] = None if matches and correct else ('document_mismatch' if not matches else 'false_rejection')
                value['loyalty'] = min(100, max(0, value['loyalty'] + (2 if correct and matches else -2 if correct else -8)))
                value['assessments'].append({'task_type':'приоритетная', 'decision':100 if correct else 0,
                    'communication':None, 'response':response_score(now-ticket['started_at'],90,100 if correct else 0),
                    'reason':'Данные сопоставлены верно.' if correct else 'Результат проверки не соответствует документам.', 'source':'ticket'})
                value['events'].append({'kind':'ticket_result','actor_id':ticket['actor_id'],'correct':correct,'follow_up':'content_gap' if ticket['issue'] and not body['accept'] else None})
        elif kind == 'finish':
            self.finish(value, 'completed' if value['mode'] == 'training' else 'cancelled')
        else:
            raise ScenarioError('Неизвестное действие')
        value['revision'] += 1
        value['events'].append({'kind':kind, 'at':now})
        return value

    def assess_finished(self, value, task, assessment=None):
        if task['status'] != 'completed' or task.get('assessment'):
            return
        definition = self.catalog.situations[task['situation_id']]
        assessment = assessment or self.provider.assess({'situation': definition, 'history': task['history'], 'events': task['events']})
        task['assessment'] = assessment
        decision = assessment['decision_score']
        row = {'task_type': definition['task_type'], 'decision': decision,
               'communication': assessment['communication_score'],
               'response': response_score(task['reaction_seconds'] if task['reaction_seconds'] is not None else definition['timer_seconds'], definition['timer_seconds'], decision),
               'task_id':task['id'], 'reason':assessment['decision_reason'], 'communication_reason':assessment['communication_reason']}
        value['assessments'].append(row)
        value['loyalty'] = min(100, max(0, value['loyalty'] + (4 if decision >= 75 else -5)))
        task['emotion'] = 'satisfied' if decision >= 75 else 'dissatisfied'
        if assessment['safety_violation'] and definition['task_type'] != 'сервисная':
            value['safety'] = max(0, value['safety'] - 15)

    def snapshot(self, current, now):
        value = deepcopy(current)
        value.pop('content_snapshot', None)
        world = value.pop('world_actions',deepcopy(WORLD_ACTIONS))
        value['world_interactions'] = []
        for passenger in world['passengers']:
            tasks = [t for t in value['tasks'] if t['actor_id']==passenger['id'] and t['status']=='active']
            pending = next((t for t in tasks if t.get('pending_action')), None)
            value['world_interactions'].append({'id':passenger['id'],'kind':'passenger','available':value['status']=='active',
                'marker':bool(tasks),'task_id':tasks[0]['id'] if tasks else '',
                'item':pending['pending_action']['requirement']['item'] if pending else '',
                'position':passenger['position']})
        waiting = [t for t in value['tasks'] if t.get('pending_action') and t['status']=='active']
        pending = next((t for t in waiting if t['id']==value['selected_task']),waiting[0] if waiting else None)
        item = pending['pending_action']['requirement']['item'] if pending else ''
        available = bool(pending) and value['status']=='active' and not any(s and s['item_id']==item for s in value['inventory'])
        value['world_interactions'].append({'id':world['station']['id'],'kind':'station','available':available,'marker':available,
            'task_id':pending['id'] if pending else '', 'item':item,'position':world['station']['position']})
        value['server_time'] = now
        value['remaining_seconds'] = max(0, value['deadline'] - now)
        for task in value['tasks']:
            task['remaining_seconds'] = max(0, task['deadline'] - now)
            task['title'] = self.catalog.situations[task['situation_id']]['situation_description']
            task['task_type'] = self.catalog.situations[task['situation_id']]['task_type']
            task.pop('facts', None)
        return value

