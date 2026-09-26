import tempfile
import unittest
import uuid
import json
import threading
import urllib.request
import urllib.error
from pathlib import Path

from domain import ApiError
from shift_store import ShiftStore, encode
from test_shift_store import FakeProvider
from server import make_server
from service import Service


class ShiftRecoveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.now = 1000000.
        self.store = ShiftStore(Path(self.temp.name)/'state.db', FakeProvider(), lambda:self.now)
        self.user = self.store.register({'login':'recovery','password':'test-password'})['user']['id']

    def tearDown(self):
        self.temp.cleanup()

    def start(self, mode='ranked', action_id=None):
        return self.store.start(self.user, {'clientActionId':action_id or str(uuid.uuid4()),'mode':mode,'situation_id':46})

    def test_recovery_after_restart_does_not_spend_an_attempt(self):
        self.assertIsNone(self.store.current(self.user)['shift'])
        original = self.start()
        self.now += 20
        restarted = ShiftStore(self.store.database, FakeProvider(), lambda:self.now)
        for _ in range(2):
            recovered = restarted.current(self.user)['shift']
            self.assertEqual(recovered['id'], original['id'])
            self.assertEqual(recovered['remaining_seconds'], 400)
        self.assertEqual(restarted.profile(self.user)['attempts_remaining'], 9)

    def test_new_start_cancels_previous_and_replay_does_not_cancel_current(self):
        first = self.start()
        action_id = str(uuid.uuid4())
        second = self.start(action_id=action_id)
        self.assertEqual(self.store.get(self.user,first['id'])['status'], 'cancelled')
        self.assertEqual(self.start(action_id=action_id), second)
        self.assertEqual(self.store.current(self.user)['shift']['id'], second['id'])
        self.assertEqual(self.store.profile(self.user)['attempts_remaining'], 8)

    def test_expiry_records_result_once_and_other_user_cannot_recover_it(self):
        state = self.start()
        other = self.store.register({'login':'another','password':'test-password'})['user']['id']
        self.assertIsNone(self.store.current(other)['shift'])
        self.now += 421
        self.assertIsNone(self.store.current(self.user)['shift'])
        self.assertIsNone(self.store.current(self.user)['shift'])
        history = self.store.profile(self.user)['history']
        self.assertEqual(len([x for x in history if x['id']==state['id']]), 1)

    def test_failed_start_does_not_cancel_active_shift(self):
        for _ in range(10):
            latest = self.start()
        with self.assertRaises(ApiError):
            self.start()
        self.assertEqual(self.store.current(self.user)['shift']['id'],latest['id'])
        self.assertEqual(self.store.profile(self.user)['attempts_remaining'],0)

    def test_legacy_multiple_active_shifts_are_reconciled_once(self):
        first = self.start()
        self.now += 1
        newer = self.store.engine.begin('ranked',self.now)
        with self.store.connect() as db:
            db.execute('DELETE FROM v2_current_shifts WHERE user_id=?',(self.user,))
            db.execute('INSERT INTO v2_shifts VALUES(?,?,?,?,?)',
                (newer['id'],self.user,self.store.day(),'ranked',encode(newer)))
        self.assertEqual(self.store.current(self.user)['shift']['id'],newer['id'])
        self.assertEqual(self.store.get(self.user,first['id'])['status'],'cancelled')
        self.assertEqual(self.store.current(self.user)['shift']['id'],newer['id'])
        history = self.store.profile(self.user)['history']
        self.assertEqual(len([x for x in history if x['id']==first['id']]),1)

    def test_http_current_requires_auth_and_returns_the_snapshot(self):
        state = self.start()
        token = self.store.login({'login':'recovery','password':'test-password'})['accessToken']
        server = make_server(Service(Path(self.temp.name)/'http.db'),0,host='127.0.0.1')
        server.shift_store = self.store
        thread = threading.Thread(target=server.serve_forever,daemon=True)
        thread.start()
        url = f'http://127.0.0.1:{server.server_address[1]}/v2/shifts/current'
        try:
            with self.assertRaises(urllib.error.HTTPError) as error:
                urllib.request.urlopen(url,timeout=5)
            self.assertEqual(error.exception.code,401)
            request = urllib.request.Request(url,headers={'Authorization':'Bearer '+token})
            with urllib.request.urlopen(request,timeout=5) as response:
                recovered = json.load(response)['shift']
            self.assertEqual(recovered['id'],state['id'])
            self.assertNotIn('content_snapshot',recovered)
            self.assertEqual(self.store.profile(self.user)['attempts_remaining'],9)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(5)

if __name__=='__main__':
    unittest.main()
