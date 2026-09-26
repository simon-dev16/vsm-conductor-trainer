import tempfile
import unittest
import uuid
from pathlib import Path
from shift_store import ShiftStore
from domain import ApiError


class FakeProvider:
    def classify(self, context):
        return {'situation_id':context['situation']['id'],'current_state':context['current_state'],
                'transition_id':context['allowed_transitions'][0]['id']}
    def assess(self, context):
        return {'decision_score':100,'communication_score':100,'decision_reason':'test',
                'communication_reason':'test','safety_violation':False}


class ShiftStoreTests(unittest.TestCase):
    def setUp(self):
        self.directory=tempfile.TemporaryDirectory()
        self.now=1000000.
        self.store=ShiftStore(Path(self.directory.name)/'test.db',FakeProvider(),lambda:self.now)
        self.user=self.store.register({'login':'tester','password':'test-password'})['user']['id']

    def tearDown(self):
        self.directory.cleanup()

    def start(self,mode='training'):
        return self.store.start(self.user,{'clientActionId':str(uuid.uuid4()),'mode':mode,'situation_id':46})

    def act(self,state,kind,**fields):
        return self.store.act(self.user,state['id'],{'clientActionId':str(uuid.uuid4()),'revision':state['revision'],'kind':kind,**fields})

    def test_full_water_cycle_requires_physical_delivery(self):
        state=self.start()
        state=self.act(state,'answer',text='Принесу воду')
        self.assertIsNotNone(state['tasks'][0]['pending_action'])
        state=self.act(state,'position',position=[-260,240,100])
        state=self.act(state,'take_item')
        state=self.act(state,'position',position=[120,118,90])
        state=self.act(state,'give_item',slot=0)
        self.assertIsNone(state['inventory'][0])
        self.assertEqual(state['tasks'][0]['state'],'follow_up')
        state=self.act(state,'answer',text='Как вы себя чувствуете?')
        self.assertEqual(state['tasks'][0]['status'],'completed')
        self.assertEqual(len(state['assessments']),1)

    def test_idempotency_ownership_and_restart_persistence(self):
        body={'clientActionId':str(uuid.uuid4()),'mode':'ranked','situation_id':46}
        state=self.store.start(self.user,body)
        self.assertEqual(self.store.start(self.user,body),state)
        self.assertEqual(self.store.profile(self.user)['attempts_remaining'],9)
        other=self.store.register({'login':'another','password':'test-password'})['user']['id']
        with self.assertRaises(ApiError):
            self.store.get(other,state['id'])
        store=ShiftStore(self.store.database,FakeProvider(),lambda:self.now)
        self.assertEqual(store.get(self.user,state['id'])['id'],state['id'])

    def test_attempt_limit_and_training_exemption(self):
        for _ in range(10): self.start('ranked')
        with self.assertRaises(ApiError): self.start('ranked')
        self.assertEqual(self.start()['mode'],'training')

    def test_ticket_timeout_is_once_and_check_remains_available(self):
        state=self.start()
        state=self.act(state,'open_ticket',actor_id='passenger_01')
        self.now+=91
        state=self.store.get(self.user,state['id'])
        loyalty=state['loyalty']
        self.assertEqual(self.store.get(self.user,state['id'])['loyalty'],loyalty)
        state=self.act(state,'check_ticket',actor_id='passenger_01',accept=True)
        self.assertTrue(state['tickets'][0]['checked'])


if __name__=='__main__': unittest.main()
