import unittest
from scenario_machine import Catalog, ScenarioError, response_score, shift_score


class ScenarioTests(unittest.TestCase):
    def setUp(self):
        self.catalog = Catalog()

    def classify(self, run, transition):
        return {'situation_id': run['situation_id'], 'current_state': run['state'], 'transition_id': transition}

    def test_catalog_preserves_unwritten_situations(self):
        self.assertEqual(len(self.catalog.situations), 20)
        self.assertEqual(len(self.catalog.flows), 9)
        with self.assertRaises(ScenarioError):
            self.catalog.begin(19)

    def test_only_allowed_transition_and_revision(self):
        run = self.catalog.begin(44)
        with self.assertRaises(ScenarioError):
            self.catalog.apply(run, self.classify(run, 'invent_event'), {}, 0)
        with self.assertRaises(ScenarioError):
            self.catalog.apply(run, self.classify(run, 'NO_MATCH'), {}, 2)
        unchanged = self.catalog.apply(run, self.classify(run, 'NO_MATCH'), {}, 0)
        self.assertEqual(unchanged['state'], run['state'])
        self.assertEqual(run['revision'], 0)

    def test_unknown_and_ambiguous_facts_do_not_guess(self):
        run = self.catalog.begin(44)
        transition = self.classify(run, 'ask_complaint_reason')
        with self.assertRaises(ScenarioError):
            self.catalog.apply(run, transition, {}, 0)
        state = self.catalog.flows[44]['states']['reason_known']
        facts = {b['game_state_condition']: True for b in state['engine_branches']}
        with self.assertRaises(ScenarioError):
            self.catalog.apply(run, transition, facts, 0)
        self.assertEqual(run['events'], [])

    def test_source_gap_is_not_success(self):
        run = self.catalog.begin(44)
        facts = {'Пассажир настроен на урегулирование ситуации на борту.': True}
        result = self.catalog.apply(run, self.classify(run, 'ask_complaint_reason'), facts, 0)
        self.assertEqual(result['status'], 'content_gap')

    def test_promise_waits_for_verified_physical_action(self):
        run = self.catalog.begin(46)
        run['state'] = 'offer_water'
        required = {'action': 'give_item', 'item': 'water', 'quantity': 1}
        facts = {'Лимона нет в наличии.': True}
        result = self.catalog.apply(run, self.classify(run, 'offer_water_action'), facts, 0, {'46:offer_water_action': required})
        self.assertEqual(result['state'], 'offer_water')
        with self.assertRaises(ScenarioError):
            self.catalog.complete_action(result, facts, 1, {'action': 'say_water'})
        completed = self.catalog.complete_action(result, facts, 1, required)
        self.assertEqual(completed['state'], 'follow_up')
        self.assertIsNone(completed['pending_action'])

    def test_speed_requires_correctness_and_deadline(self):
        self.assertEqual(response_score(1, 20, 0), 0)
        self.assertEqual(response_score(5, 20, 50), 50)
        self.assertEqual(response_score(20, 20, 100), 0)
        row = {'task_type': 'сервисная', 'decision': 100, 'response': 100, 'communication': 100}
        self.assertEqual(shift_score([row], 100, 100)['rating'], 300)
        self.assertEqual(shift_score([row], 0, 0)['rating'], 150)


if __name__ == '__main__':
    unittest.main()
