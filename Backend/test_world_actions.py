import copy
import unittest
import uuid
from unittest.mock import patch

import shift_engine
from scenario_machine import ScenarioError
from shift_engine import ShiftEngine
from test_shift_store import FakeProvider


class WorldActionTests(unittest.TestCase):
    def setUp(self):
        self.engine = ShiftEngine(FakeProvider())
        self.now = 1_000_000.0
        self.state = self.engine.begin('training', self.now, 46)

    def act(self, state, kind, **fields):
        return self.engine.act(state, {
            'revision': state['revision'],
            'kind': kind,
            'clientActionId': str(uuid.uuid4()),
            **fields,
        }, self.now)

    @staticmethod
    def interactions(snapshot):
        return {item['id']: item for item in snapshot['world_interactions']}

    def promise_water(self):
        return self.act(self.state, 'answer', text='Принесу воду')

    def test_water_marker_and_inventory_follow_physical_actions(self):
        state = self.promise_water()
        station = self.interactions(self.engine.snapshot(state, self.now))['conductor_station']
        self.assertTrue(station['available'])
        self.assertTrue(station['marker'])
        self.assertTrue(all(slot is None for slot in state['inventory']))

        state = self.act(state, 'position', position=[-260, 240, 100])
        state = self.act(state, 'take_item')
        station = self.interactions(self.engine.snapshot(state, self.now))['conductor_station']
        self.assertFalse(station['available'])
        self.assertFalse(station['marker'])
        self.assertEqual(state['inventory'][0]['item_id'], 'water')
        self.assertIsNotNone(state['inventory'][0]['instance_id'])

    def test_water_cannot_be_taken_twice_with_a_new_action_id(self):
        state = self.act(self.promise_water(), 'position', position=[-260, 240, 100])
        state = self.act(state, 'take_item')
        duplicate = {
            'revision': state['revision'],
            'kind': 'take_item',
            'clientActionId': str(uuid.uuid4()),
        }
        with self.assertRaises(ScenarioError):
            self.engine.act(state, duplicate, self.now)
        self.assertEqual(sum(slot is not None for slot in state['inventory']), 1)

    def test_wrong_passenger_cannot_receive_water(self):
        state = self.act(self.promise_water(), 'position', position=[-260, 240, 100])
        state = self.act(state, 'take_item')
        state = self.act(state, 'position', position=[120, 118, 90])
        before = copy.deepcopy(state['inventory'])
        with self.assertRaises(ScenarioError):
            self.act(state, 'give_item', actor_id='passenger_02', slot=0)
        self.assertEqual(state['inventory'], before)

    def test_correct_delivery_and_follow_up_complete_without_marker(self):
        state = self.act(self.promise_water(), 'position', position=[-260, 240, 100])
        state = self.act(state, 'take_item')
        state = self.act(state, 'position', position=[120, 118, 90])
        state = self.act(state, 'give_item', actor_id='passenger_01', slot=0)
        self.assertIsNone(state['inventory'][0])
        self.assertEqual(state['tasks'][0]['state'], 'follow_up')

        state = self.act(state, 'answer', text='Как вы себя чувствуете?')
        self.assertEqual(state['tasks'][0]['status'], 'completed')
        interactions = self.interactions(self.engine.snapshot(state, self.now))
        self.assertFalse(interactions['passenger_01']['marker'])
        self.assertFalse(interactions['conductor_station']['marker'])

    def test_snapshot_hides_world_actions_and_shift_keeps_its_copy(self):
        original = copy.deepcopy(self.state['world_actions'])
        self.assertIn('world_actions', self.state)
        changed_global = copy.deepcopy(shift_engine.WORLD_ACTIONS)
        changed_global['station']['id'] = 'changed_global_station'
        changed_global['station']['position'] = [9000, 9000, 9000]
        with patch('shift_engine.WORLD_ACTIONS', changed_global):
            snapshot = self.engine.snapshot(self.state, self.now)
            self.assertNotIn('world_actions', snapshot)
            by_id = self.interactions(snapshot)
            self.assertIn('conductor_station', by_id)
            self.assertEqual(by_id['conductor_station']['position'], original['station']['position'])
            self.assertEqual(len(by_id), len(original['passengers']) + 1)
            self.assertTrue(all(item['kind'] in ('passenger', 'station') for item in by_id.values()))

            state = self.promise_water()
            state = self.act(state, 'position', position=original['station']['position'])
            state = self.act(state, 'take_item')
            self.assertEqual(state['inventory'][0]['item_id'], 'water')


class WorldConfigurationTests(unittest.TestCase):
    def test_invalid_world_configuration_is_rejected(self):
        for change in ('duplicate','radius','position','action'):
            world=copy.deepcopy(shift_engine.WORLD_ACTIONS)
            if change=='duplicate':world['passengers'][0]['id']=world['station']['id']
            elif change=='radius':world['station']['radius']=-1
            elif change=='position':world['station']['position'][0]=float('nan')
            else:next(iter(world['bindings'].values()))['action']='arbitrary_command'
            with self.subTest(change=change),self.assertRaises(ScenarioError):
                shift_engine.validate_world_actions(world)


if __name__ == '__main__':
    unittest.main()
