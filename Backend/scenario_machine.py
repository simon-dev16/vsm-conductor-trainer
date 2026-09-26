"""Deterministic execution of authored transitions; language models only classify."""
from copy import deepcopy
from pathlib import Path
import json
from hashlib import sha256


class ScenarioError(ValueError):
    pass


class Catalog:
    def __init__(self, directory=None):
        directory = Path(directory or Path(__file__).resolve().parents[1] / 'Contracts' / 'Authored')
        self.situations = self._index(json.loads((directory / 'situations.json').read_text(encoding='utf-8-sig'))['situations'], 'id')
        flow = json.loads((directory / 'scenario_flow.json').read_text(encoding='utf-8-sig'))
        self.flows = self._index(flow['scenarios'], 'situation_id')
        self.rules = flow['llm_rules']
        self.validate()

    def validate(self):
        for key, scenario in self.flows.items():
            if key not in self.situations:
                raise ScenarioError('Unknown situation')
            states = scenario['states']
            if scenario['entry_state'] not in states:
                raise ScenarioError('Missing entry state')
            for state in states.values():
                transitions = state.get('allowed_transitions', []) + state.get('engine_branches', [])
                if len({t['id'] for t in transitions}) != len(transitions):
                    raise ScenarioError('Duplicate transition')
                if any(t['next_state'] not in states for t in transitions):
                    raise ScenarioError('Missing target state')

    def snapshot(self):
        return deepcopy({'situations': list(self.situations.values()),
                         'scenarios': list(self.flows.values()), 'llm_rules': self.rules})

    @classmethod
    def from_snapshot(cls, value):
        catalog = cls.__new__(cls)
        catalog.situations = cls._index(deepcopy(value['situations']), 'id')
        catalog.flows = cls._index(deepcopy(value['scenarios']), 'situation_id')
        catalog.rules = deepcopy(value['llm_rules'])
        catalog.validate()
        return catalog

    def version(self):
        return sha256(json.dumps(self.snapshot(), sort_keys=True, ensure_ascii=False).encode()).hexdigest()

    @staticmethod
    def _index(items, field):
        result = {item[field]: item for item in items}
        if len(result) != len(items):
            raise ScenarioError('Duplicate identifier')
        return result

    def begin(self, situation_id):
        if situation_id not in self.flows:
            raise ScenarioError('Scenario flow has not been authored')
        return {'situation_id': situation_id, 'state': self.flows[situation_id]['entry_state'],
                'revision': 0, 'status': 'active', 'events': [], 'pending_action': None}

    def state(self, run):
        return self.flows[run['situation_id']]['states'][run['state']]

    def classify_context(self, run, facts, history, text):
        allowed = [t for t in self.state(run).get('allowed_transitions', [])
                   if not t.get('guard') or facts.get(t['guard']) is True]
        return {'situation': deepcopy(self.situations[run['situation_id']]),
                'current_state': run['state'], 'allowed_transitions': deepcopy(allowed),
                'history': deepcopy(history), 'player_text': text, 'rules': self.rules}

    def apply(self, run, classification, facts, revision, action_bindings=None):
        if revision != run['revision'] or run['status'] != 'active' or run['pending_action']:
            raise ScenarioError('Stale or unavailable turn')
        if classification.get('situation_id') != run['situation_id'] or classification.get('current_state') != run['state']:
            raise ScenarioError('Classification belongs to another state')
        selected = classification.get('transition_id')
        result = deepcopy(run)
        if selected == 'NO_MATCH':
            result['events'].append({'kind': 'no_match', 'state': run['state']})
        else:
            allowed = self.classify_context(run, facts, [], '')['allowed_transitions']
            transition = next((t for t in allowed if t['id'] == selected), None)
            if transition is None:
                raise ScenarioError('Transition is not allowed')
            binding = (action_bindings or {}).get(f"{run['situation_id']}:{selected}")
            if binding:
                result['pending_action'] = {'transition': deepcopy(transition), 'requirement': deepcopy(binding)}
                result['events'].append({'kind': 'action_requested', 'transition_id': selected})
            else:
                self._advance(result, transition, facts)
        result['revision'] += 1
        return result

    def complete_action(self, run, facts, revision, verified_action):
        if revision != run['revision'] or not run['pending_action'] or run['status'] != 'active':
            raise ScenarioError('No pending action at this revision')
        pending = run['pending_action']
        if verified_action != pending['requirement']:
            raise ScenarioError('Action does not satisfy the requirement')
        result = deepcopy(run)
        result['pending_action'] = None
        result['events'].append({'kind': 'action_completed', 'action': deepcopy(verified_action)})
        self._advance(result, pending['transition'], facts)
        result['revision'] += 1
        return result

    def _advance(self, result, transition, facts):
        result['events'].append({'kind': 'transition', 'from': result['state'], 'transition_id': transition['id'], 'to': transition['next_state']})
        result['state'] = transition['next_state']
        self.resolve(result, facts)

    def resolve(self, result, facts):
        visited = set()
        while True:
            if result['state'] in visited:
                raise ScenarioError('Automatic transition cycle')
            visited.add(result['state'])
            state = self.state(result)
            if state.get('source_gap'):
                result['status'] = 'content_gap'
                return
            if state.get('terminal'):
                result['status'] = 'completed'
                return
            branches = state.get('engine_branches', [])
            if not branches:
                return
            matches = [t for t in branches if facts.get(t['game_state_condition']) is True]
            if len(matches) != 1:
                raise ScenarioError('World facts must select exactly one branch')
            transition = matches[0]
            result['events'].append({'kind': 'world_branch', 'from': result['state'], 'transition_id': transition['id'], 'to': transition['next_state']})
            result['state'] = transition['next_state']


def response_score(elapsed, timer, decision):
    if timer <= 0 or elapsed < 0 or decision not in (0, 25, 50, 75, 100):
        raise ScenarioError('Invalid scoring input')
    fraction = elapsed / timer
    base = 100 if fraction <= .25 else 80 if fraction <= .5 else 60 if fraction <= .75 else 40 if fraction < 1 else 0
    return base * decision / 100


def shift_score(assessments, safety, loyalty):
    if not 0 <= safety <= 100 or not 0 <= loyalty <= 100:
        raise ScenarioError('Invalid shift gauges')
    weights = {'сервисная': (1, .7), 'приоритетная': (1.5, 1), 'критическая': (2, 1.5)}
    totals = [0., 0., 0.]
    denominators = [0., 0., 0.]
    for row in assessments:
        weight, speed = weights[row['task_type']]
        for i, name in enumerate(('decision', 'response', 'communication')):
            value = row[name]
            if name == 'communication' and value is None and row.get('source') == 'ticket':
                continue
            if not isinstance(value, (int, float)) or isinstance(value, bool) or not 0 <= value <= 100:
                raise ScenarioError('Invalid assessment')
            factor = weight * (speed if i == 1 else 1)
            totals[i] += value * factor
            denominators[i] += factor
    values = [n / d if d else 0 for n, d in zip(totals, denominators)]
    return {'decision': values[0], 'response': values[1], 'communication': values[2],
            'rating': sum(values) * (100 + (safety + loyalty) / 2) / 200}
