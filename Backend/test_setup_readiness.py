import io
import json
import logging
import os
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import patch

import doctor
from ai_provider import http_failure
from server import make_server
from service import Service


class HttpReasonTests(unittest.TestCase):
    def test_status_becomes_a_code_that_names_the_culprit(self):
        for status,code in ((400,'ai_bad_request'),(401,'ai_unauthorized'),(403,'ai_forbidden'),
                            (404,'ai_model_not_found'),(408,'ai_timeout'),(429,'ai_rate_limited')):
            self.assertEqual(http_failure(status,'ai')[0],code)
    def test_server_side_and_unknown_statuses(self):
        self.assertEqual(http_failure(500,'ai')[0],'ai_unavailable')
        self.assertEqual(http_failure(503,'ai')[0],'ai_unavailable')
        self.assertEqual(http_failure(418,'ai')[0],'ai_http_error')
        self.assertIn('503',http_failure(503,'ai','Модель')[1])
    def test_prefix_is_reused_by_the_speech_endpoint(self):
        self.assertEqual(http_failure(401,'speech')[0],'speech_unauthorized')
        self.assertEqual(http_failure(429,'speech')[0],'speech_rate_limited')
    def test_hint_points_at_the_setting_to_fix(self):
        self.assertIn('VSM_YANDEX_MODEL_URI',http_failure(404,'ai')[1])
        self.assertIn('квоту',http_failure(429,'ai')[1])
        self.assertIn('прав',http_failure(403,'ai')[1])
    def test_no_provider_body_is_echoed(self):
        for status in (400,401,403,404,429,500):
            self.assertNotIn('{"',http_failure(status,'ai')[1])


class DoctorCheckTests(unittest.TestCase):
    def setUp(self):
        doctor.reset()
    def test_values_from_the_example_file_are_not_mistaken_for_keys(self):
        self.assertFalse(doctor.looks_filled('change-this-local-password'))
        self.assertFalse(doctor.looks_filled('впишите ключ сюда'))
        self.assertFalse(doctor.looks_filled('   '))
        self.assertTrue(doctor.looks_filled('AQVNBBCCDDEEFF0011223344'))
    def test_model_uri_must_look_like_a_model_uri(self):
        doctor.check_shape('VSM_YANDEX_MODEL_URI','https://yandex.ru/promo')
        self.assertTrue(any('gpt://' in problem for problem in doctor.problems))
        doctor.reset()
        doctor.check_shape('VSM_YANDEX_MODEL_URI','gpt://b1g1234567890abcd00/yandexgpt/latest')
        self.assertEqual(doctor.problems,[])
    def test_folder_id_is_checked_without_rejecting_valid_ones(self):
        doctor.check_shape('VSM_YANDEX_FOLDER_ID','1234567890123')
        self.assertEqual(doctor.problems,[])
        doctor.reset()
        doctor.check_shape('VSM_YANDEX_FOLDER_ID','b1gfolder')
        self.assertEqual(doctor.problems,[])
        self.assertTrue(doctor.warnings)
        doctor.reset()
        doctor.check_shape('VSM_YANDEX_FOLDER_ID','папка')
        self.assertTrue(doctor.problems)
    def test_numbers_are_range_checked(self):
        for name,value in (('VSM_AI_TIMEOUT','500'),('VSM_AI_TIMEOUT','soon'),('VSM_PORT','eight')):
            doctor.reset()
            doctor.check_shape(name,value)
            self.assertTrue(doctor.problems,name)
        doctor.reset()
        doctor.check_shape('VSM_AI_TIMEOUT','45')
        self.assertEqual(doctor.problems,[])
    def test_only_yandex_is_implemented(self):
        doctor.check_shape('VSM_AI_PROVIDER','openai')
        self.assertTrue(doctor.problems)
    def test_template_mentions_every_setting(self):
        argv=sys.argv
        buffer=io.StringIO()
        try:
            sys.argv=['doctor.py','--template']
            with redirect_stdout(buffer): self.assertEqual(doctor.main(),0)
        finally:
            sys.argv=argv
        printed=buffer.getvalue()
        for name,_,_,_ in doctor.KEYS:
            self.assertIn(name,printed)
        self.assertIn('VSM_LOG_TRACEBACK',(doctor.BACKEND/'.env.example').read_text(encoding='utf-8'))


class _Collector(logging.Handler):
    def __init__(self):
        super().__init__(logging.INFO)
        self.lines=[]
    def emit(self,record):
        self.lines.append(record.getMessage())


class RequestLogTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.service=Service(Path(self.temp.name)/'test.db')
        self.server=make_server(self.service,0)
        self.thread=threading.Thread(target=self.server.serve_forever,daemon=True)
        self.thread.start()
        self.base=f'http://127.0.0.1:{self.server.server_address[1]}'
    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        self.temp.cleanup()
    def request(self,path,body=None,token=None):
        request=urllib.request.Request(self.base+path,
            data=None if body is None else json.dumps(body).encode(),
            headers={'Content-Type':'application/json',**({'Authorization':'Bearer '+token} if token else {})},
            method='POST' if body is not None else 'GET')
        try:
            with urllib.request.urlopen(request,timeout=5) as response:
                return response.status,json.load(response)
        except urllib.error.HTTPError as error:
            return error.code,json.loads(error.read().decode('utf-8'))
    def token(self):
        status,body=self.request('/v2/auth/register',{'login':'logtest','password':'synthetic-pass'})
        self.assertEqual(status,201)
        return body['accessToken']
    def capture(self,call):
        """Лог пишется в finally после отправки ответа, поэтому ждём его появления."""
        collector=_Collector()
        root=logging.getLogger()
        previous=root.level
        root.addHandler(collector)
        root.setLevel(logging.INFO)
        try:
            call()
            for _ in range(200):
                if collector.lines: break
                time.sleep(0.01)
        finally:
            root.removeHandler(collector)
            root.setLevel(previous)
        return '\n'.join(collector.lines)
    def test_successful_call_is_logged_with_its_status(self):
        lines=self.capture(lambda: self.request('/health'))
        self.assertIn('GET /health -> 200',lines)
    def test_failed_call_keeps_its_error_code(self):
        lines=self.capture(lambda: self.request('/v2/profile'))
        self.assertIn('GET /v2/profile -> 401 unauthorized',lines)
    def test_unknown_route_is_logged_by_its_code(self):
        token=self.token()
        status,_=self.request('/v2/nothing',token=token)
        self.assertEqual(status,404)
        lines=self.capture(lambda: self.request('/v2/nothing',token=token))
        self.assertIn('404 not_found',lines)
    def test_client_address_is_logged(self):
        lines=self.capture(lambda: self.request('/health'))
        self.assertTrue(lines.endswith('127.0.0.1'),lines)


class BindTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.service=Service(Path(self.temp.name)/'test.db')
    def tearDown(self):
        self.temp.cleanup()
    def test_loopback_only_by_default(self):
        with patch.dict(os.environ,{},clear=False):
            os.environ.pop('VSM_BIND',None)
            server=make_server(self.service,0)
        try:
            self.assertEqual(server.server_address[0],'127.0.0.1')
        finally:
            server.server_close()
    def test_bind_setting_is_honoured(self):
        with patch.dict(os.environ,{'VSM_BIND':'0.0.0.0'}):
            server=make_server(self.service,0)
        try:
            self.assertEqual(server.server_address[0],'0.0.0.0')
        finally:
            server.server_close()
    def test_explicit_host_wins_over_setting(self):
        with patch.dict(os.environ,{'VSM_BIND':'0.0.0.0'}):
            server=make_server(self.service,0,'127.0.0.1')
        try:
            self.assertEqual(server.server_address[0],'127.0.0.1')
        finally:
            server.server_close()


class HealthTests(unittest.TestCase):
    def test_health_reports_whether_ai_is_configured(self):
        temp=tempfile.TemporaryDirectory()
        try:
            service=Service(Path(temp.name)/'test.db')
            server=make_server(service,0)
            thread=threading.Thread(target=server.serve_forever,daemon=True)
            thread.start()
            try:
                with urllib.request.urlopen(f'http://127.0.0.1:{server.server_address[1]}/health',timeout=5) as response:
                    body=json.load(response)
                self.assertEqual(body['status'],'ok')
                self.assertIn('aiConfigured',body)
            finally:
                server.shutdown(); server.server_close(); thread.join()
        finally:
            temp.cleanup()


if __name__=='__main__':
    unittest.main()
