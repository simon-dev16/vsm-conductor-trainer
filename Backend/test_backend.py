import copy
import json
import tempfile
import threading
import unittest
from unittest.mock import patch
from io import BytesIO
import urllib.error
import urllib.request
import uuid
from pathlib import Path

from ai_provider import ProviderError, YandexProvider
from domain import ApiError, validate_decision
from server import make_server
from service import Service


def decision(completed=False):
    return {'status':'completed' if completed else 'active','actorId':'passenger_01','text':'Учебная реплика','deadlineSeconds':120,
            'state':{'safety':100,'loyalty':{'passenger_01':50},'competencies':{}},'commands':[],
            'assessments':[{'metricId':m,'score':75,'reason':'Synthetic test assessment','evidence':'Test only'}
                           for m in ('decision_making','response_speed','customer_care')] if completed else [],
            'debrief':'Synthetic test debrief' if completed else ''}


class FakeProvider:
    def __init__(self):
        self.calls=0
    def decide(self,context):
        self.calls+=1
        return decision(context['eventType']!='start')


class BackendTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.provider=FakeProvider()
        self.service=Service(Path(self.temp.name)/'test.db',self.provider)
        self.service.create_demo_user('synthetic-password')
        login=self.service.login({'login':'demo','password':'synthetic-password'})
        self.token=login['accessToken']
        self.user=self.service.authenticate('Bearer '+self.token)
    def tearDown(self):
        self.temp.cleanup()
    def start(self):
        return self.service.start(self.user,{'scenarioKey':'luggage-help','scenarioVersion':1,'mode':'ai_text'})
    def body(self,run):
        return {'clientActionId':str(uuid.uuid4()),'expectedRevision':run['revision'],'turnId':run['currentNode']['id'],'text':'test response'}
    def test_persistence_and_idempotency(self):
        run=self.start(); body=self.body(run)
        result=self.service.turn(self.user,run['runId'],'ai-turn',body)
        repeated=self.service.turn(self.user,run['runId'],'ai-turn',body)
        self.assertEqual(result['revision'],repeated['revision'])
        self.assertEqual(self.provider.calls,2)
        reloaded=Service(self.service.path,self.provider)
        self.assertEqual(reloaded.get_run(self.user,run['runId'])['status'],'completed')
        self.assertEqual(reloaded.profile(self.user)['completedRuns'],1)
        with self.assertRaises(ApiError) as error:
            self.service.turn(self.user,run['runId'],'ai-turn',{**body,'text':'changed'})
        self.assertEqual(error.exception.code,'action_id_reused')
    def test_concurrent_same_action_only_calls_provider_once(self):
        run=self.start(); body=self.body(run); results=[]
        threads=[threading.Thread(target=lambda:results.append(self.service.turn(self.user,run['runId'],'ai-turn',body))) for _ in range(2)]
        for thread in threads: thread.start()
        for thread in threads: thread.join()
        self.assertEqual(len(results),2); self.assertEqual(self.provider.calls,2)
    def test_timeout_and_revision_authority(self):
        run=self.start(); body=self.body(run)
        for endpoint,payload,code in [('timeout',body,'deadline_not_elapsed'),('ai-turn',{**body,'expectedRevision':5},'stale_revision')]:
            with self.assertRaises(ApiError) as error:
                self.service.turn(self.user,run['runId'],endpoint,payload)
            self.assertEqual(error.exception.code,code)
        self.assertEqual(self.provider.calls,1)
    def test_user_cannot_read_other_run(self):
        run=self.start()
        with self.assertRaises(ApiError) as error:
            self.service.get_run({'id':'someone-else'},run['runId'])
        self.assertEqual(error.exception.status,404)
    def test_provider_failure_does_not_write_turn(self):
        run=self.start(); body=self.body(run)
        def fail(_): raise ProviderError('unavailable','Test outage')
        self.provider.decide=fail
        with self.assertRaises(ApiError): self.service.turn(self.user,run['runId'],'ai-turn',body)
        self.assertEqual(self.service.get_run(self.user,run['runId'])['revision'],0)
    def test_invalid_output_is_atomic(self):
        run=self.start(); body=self.body(run)
        value=decision(True); value['state']['safety']=999
        self.provider.decide=lambda _:value
        with self.assertRaises(ApiError): self.service.turn(self.user,run['runId'],'ai-turn',body)
        self.assertEqual(self.service.get_run(self.user,run['runId'])['revision'],0)
    def test_model_cannot_invent_capabilities(self):
        value=decision()
        value['commands']=[{'actorId':'passenger_01','type':'execute_console','value':'anything','targetActorId':''}]
        with self.assertRaises(ApiError): validate_decision(value)
        value=decision(True); value['assessments'][1]['metricId']='decision_making'
        with self.assertRaises(ApiError): validate_decision(value)
    def test_unconfigured_yandex_is_explicit(self):
        provider=YandexProvider(); provider.key=''
        with self.assertRaises(ProviderError) as error: provider.decide({})
        self.assertEqual(error.exception.code,'ai_not_configured')
    def test_real_http_contract(self):
        server=make_server(self.service,0)
        thread=threading.Thread(target=server.serve_forever,daemon=True); thread.start()
        base=f'http://127.0.0.1:{server.server_address[1]}'
        def call(path,body=None,auth=True):
            request=urllib.request.Request(base+path,data=None if body is None else json.dumps(body).encode(),
                headers={'Authorization':'Bearer '+self.token if auth else '', 'Content-Type':'application/json'})
            with urllib.request.urlopen(request,timeout=5) as response: return json.load(response)
        try:
            self.assertEqual(len(call('/v1/scenarios')['items']),10)
            run=call('/v1/runs',{'scenarioKey':'luggage-help','scenarioVersion':1,'mode':'ai_text'})
            result=call('/v1/runs/'+run['runId']+'/ai-turn',self.body(run))
            self.assertEqual(result['status'],'completed')
            with self.assertRaises(urllib.error.HTTPError) as error: call('/v1/scenarios',auth=False)
            self.assertEqual(error.exception.code,401)
        finally:
            server.shutdown(); server.server_close(); thread.join()

    def test_yandex_adapter_accepts_envelope_and_rejects_bad_output(self):
        provider=YandexProvider()
        provider.key='synthetic'; provider.folder='synthetic'; provider.model='gpt://synthetic/test'
        valid={'result':{'alternatives':[{'status':'ALTERNATIVE_STATUS_FINAL','message':{'text':json.dumps(decision())}}]}}
        with patch('urllib.request.urlopen',return_value=BytesIO(json.dumps(valid).encode())):
            self.assertEqual(provider.decide({})['status'],'active')
        for value in ([],{}, {'result':None}, {'alternatives':[{'status':'ALTERNATIVE_STATUS_TRUNCATED_FINAL'}]}):
            with patch('urllib.request.urlopen',return_value=BytesIO(json.dumps(value).encode())):
                with self.assertRaises(ProviderError): provider.decide({})


if __name__=='__main__':
    unittest.main()
