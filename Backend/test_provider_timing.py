import tempfile
import unittest
import uuid
from pathlib import Path
from unittest.mock import patch
from ai_provider import ProviderError
from domain import ApiError
from shift_store import ShiftStore
from test_shift_store import FakeProvider


class ProviderTimingTests(unittest.TestCase):
    def test_failed_provider_time_is_persisted_without_consuming_action(self):
        with tempfile.TemporaryDirectory() as directory:
            provider = FakeProvider()
            store = ShiftStore(Path(directory)/'state.db', provider, lambda:1000000.)
            user = store.register({'login':'timing','password':'test-password'})['user']['id']
            state = store.start(user, {'clientActionId':str(uuid.uuid4()),'mode':'training'})
            body = {'clientActionId':str(uuid.uuid4()), 'kind':'answer', 'text':'Принесу воду', 'revision':state['revision']}
            with patch.object(provider,'classify',side_effect=ProviderError('test_unavailable','test')), patch('shift_store.time.monotonic',side_effect=[100.,112.]):
                with self.assertRaises(ApiError) as error:
                    store.act(user,state['id'],body)
            self.assertEqual(error.exception.status,503)
            refreshed = store.get(user,state['id'])
            self.assertEqual(refreshed['deadline'],state['deadline']+12)
            self.assertEqual(refreshed['revision'],state['revision'])
            completed = store.act(user,state['id'],body)
            self.assertIsNotNone(completed['tasks'][0]['pending_action'])
            self.assertEqual(len(completed['tasks'][0]['turn_assessments']),1)
            self.assertEqual(completed['assessments'],[])
            self.assertEqual(store.act(user,state['id'],body),completed)

if __name__=='__main__':
    unittest.main()
