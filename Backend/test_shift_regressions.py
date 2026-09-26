import tempfile
import unittest
import uuid
from pathlib import Path

from scenario_machine import shift_score
from shift_engine import ShiftEngine
from shift_store import ShiftStore


class FirstTransitionProvider:
    def classify(self, context):
        return {
            'situation_id': context['situation']['id'],
            'current_state': context['current_state'],
            'transition_id': context['allowed_transitions'][0]['id'],
        }

    def assess(self, context):
        return {
            'decision_score': 100,
            'communication_score': 100,
            'decision_reason': 'test',
            'communication_reason': 'test',
            'safety_violation': False,
        }


class ShiftRegressionTests(unittest.TestCase):
    def setUp(self):
        self.now = 1_000_000.0
        self.engine = ShiftEngine(FirstTransitionProvider())
        self.state = self.engine.begin('training', self.now)

    def act(self, kind, **fields):
        return self.engine.act(self.state, {'revision': self.state['revision'], 'kind': kind, **fields}, self.now)

    def test_take_and_give_validate_the_position_supplied_with_the_action(self):
        self.state = self.act('answer', text='Принесу воду')
        # The saved position is far away, but the supplied position is within range.
        self.state = self.act('take_item', position=[-260, 240, 100])
        # The saved position is now by the station. A far position supplied with
        # give must be checked instead of reusing that stale nearby position.
        with self.assertRaisesRegex(ValueError, 'Подойдите к пассажиру'):
            self.act('give_item', slot=0, position=[-260, 240, 100])
        self.state = self.act('give_item', slot=0, position=[120, 118, 90])
        self.assertIsNone(self.state['inventory'][0])

    def test_take_checks_new_position_in_both_directions(self):
        self.state = self.act('answer', text='Принесу воду')
        # A nearby position supplied with take succeeds despite a distant saved one.
        self.state = self.act('take_item', position=[-260, 240, 100])
        self.assertIsNotNone(self.state['inventory'][0])

    def test_ticket_assessment_does_not_dilute_communication_average(self):
        regular = {'task_type': 'сервисная', 'decision': 100, 'response': 100, 'communication': 80}
        ticket = {'task_type': 'приоритетная', 'decision': 100, 'response': 100,
                  'communication': None, 'source': 'ticket'}
        self.assertEqual(shift_score([regular], 100, 100)['communication'], 80)
        self.assertEqual(shift_score([regular, ticket], 100, 100)['communication'], 80)

    def test_mutating_selected_task_preserves_selection(self):
        self.engine.spawn(self.state, 15, self.now)
        _, second = self.state['tasks']
        self.state = self.act('select_task', task_id=second['id'])
        self.state = self.act('answer', text='Выполняю действие')
        self.assertEqual(self.state['selected_task'], second['id'])

    def test_replayed_take_action_does_not_duplicate_item(self):
        with tempfile.TemporaryDirectory() as directory:
            store = ShiftStore(Path(directory) / 'test.db', FirstTransitionProvider(), lambda: self.now)
            user = store.register({'login': 'regression', 'password': 'test-password'})['user']['id']
            state = store.start(user, {'clientActionId': str(uuid.uuid4()), 'mode': 'training', 'situation_id': 46})

            def call(state, kind, action_id=None, **fields):
                action_id = action_id or str(uuid.uuid4())
                body = {'clientActionId': action_id, 'revision': state['revision'], 'kind': kind, **fields}
                return store.act(user, state['id'], body)

            state = call(state, 'answer', text='Принесу воду')
            state = call(state, 'position', position=[-260, 240, 100])
            action_id = str(uuid.uuid4())
            body_state = call(state, 'take_item', action_id=action_id)
            replay = call(state, 'take_item', action_id=action_id)
            self.assertEqual(replay, body_state)
            self.assertEqual(sum(slot is not None for slot in replay['inventory']), 1)


if __name__ == '__main__':
    unittest.main()
