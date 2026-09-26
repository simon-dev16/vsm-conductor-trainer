import tempfile
import unittest
import uuid
from pathlib import Path

from shift_store import ShiftStore


class FirstAllowedProvider:
    def classify(self, context):
        return {'situation_id': context['situation']['id'],
                'current_state': context['current_state'],
                'transition_id': context['allowed_transitions'][0]['id']}

    def assess(self, context):
        return {'decision_score': 100, 'communication_score': 100,
                'decision_reason': 'test', 'communication_reason': 'test',
                'safety_violation': False}


class ContentVersionTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.now = 1_000_000.0
        self.store = ShiftStore(Path(self.directory.name) / 'test.db',
                                FirstAllowedProvider(), lambda: self.now)
        self.user = self.store.register({'login': 'tester',
                                         'password': 'test-password'})['user']['id']

    def tearDown(self):
        self.directory.cleanup()

    def start(self):
        return self.store.start(self.user, {
            'clientActionId': str(uuid.uuid4()),
            'mode': 'training',
            'situation_id': 46,
        })

    def test_existing_shift_uses_start_snapshot_and_new_shift_uses_new_catalog(self):
        old = self.start()
        original_version = old['content_version']
        self.assertNotIn('content_snapshot', old)
        self.assertEqual(old['tasks'][0]['title'],
                         self.store.engine.catalog.situations[46]['situation_description'])

        catalog = self.store.engine.catalog
        catalog.situations[46]['situation_description'] = 'Обновлённое описание ситуации'
        transition = catalog.flows[46]['states']['offer_water']['allowed_transitions'][0]
        transition['id'] = 'replacement_water_action'
        changed_version = catalog.version()
        self.assertNotEqual(changed_version, original_version)

        old_after_catalog_change = self.store.get(self.user, old['id'])
        self.assertEqual(old_after_catalog_change['content_version'], original_version)
        self.assertEqual(old_after_catalog_change['tasks'][0]['title'], old['tasks'][0]['title'])
        self.assertNotIn('content_snapshot', old_after_catalog_change)

        old_after_action = self.store.act(self.user, old['id'], {
            'clientActionId': str(uuid.uuid4()),
            'revision': old_after_catalog_change['revision'],
            'kind': 'answer',
            'text': 'Принесу воду',
        })
        pending = old_after_action['tasks'][0]['pending_action']
        self.assertIsNotNone(pending)
        self.assertEqual(pending['transition']['id'], 'offer_water_action')
        self.assertEqual(old_after_action['content_version'], original_version)
        self.assertNotIn('content_snapshot', old_after_action)

        new = self.start()
        self.assertEqual(new['content_version'], changed_version)
        self.assertEqual(new['tasks'][0]['title'], 'Обновлённое описание ситуации')
        self.assertNotIn('content_snapshot', new)


if __name__ == '__main__':
    unittest.main()
