"""Local REST foundation. Put a hardened application server/TLS gateway in front for deployment."""
import json
import logging
import os
import threading
import time
from collections import defaultdict, deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlsplit

from domain import ApiError
from service import Service
from shift_store import ShiftStore
from speech import transcribe


def load_env():
    env=Path(__file__).with_name('.env')
    if env.exists():
        for line in env.read_text(encoding='utf-8-sig').splitlines():
            if line.strip() and not line.lstrip().startswith('#') and '=' in line:
                key,value=line.split('=',1)
                if key.strip().startswith('VSM_'):
                    os.environ.setdefault(key.strip(),value.strip().strip('"').strip("'"))


class Handler(BaseHTTPRequestHandler):
    def setup(self):
        super().setup()
        self.connection.settimeout(60)

    def log_message(self,*args):
        pass

    def respond(self,status,value):
        self.status_code=status
        if isinstance(value,dict):
            code=(value.get('error') or {}).get('code')
            if code: self.error_code=code
        raw=json.dumps(value,ensure_ascii=False).encode()
        self.send_response(status)
        self.send_header('Content-Type','application/json; charset=utf-8')
        self.send_header('Cache-Control','no-store')
        self.send_header('Content-Length',str(len(raw)))
        self.end_headers()
        try:
            self.wfile.write(raw)
        except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError):
            pass

    def dispatch(self):
        started=time.monotonic()
        path=urlsplit(self.path).path
        self.status_code=0
        self.error_code=''
        try:
            with self.server.rate_lock:
                now=time.monotonic()
                bucket=self.server.requests[self.client_address[0]]
                while bucket and bucket[0]<now-60:
                    bucket.popleft()
                if len(bucket)>=60:
                    raise ApiError(429,'rate_limited','Слишком много запросов. Повторите позже.')
                bucket.append(now)
            body={}
            if self.command=='POST':
                try:
                    length=int(self.headers.get('Content-Length','0'))
                except ValueError:
                    raise ApiError(400,'invalid_length','Неверная длина запроса.') from None
                if not 0<length<=(1300000 if path=='/v2/speech/transcribe' else 20000):
                    raise ApiError(413,'body_too_large','Недопустимый размер запроса.')
                try:
                    body=json.loads(self.rfile.read(length))
                    if not isinstance(body,dict):
                        raise ValueError()
                except (ValueError,UnicodeError):
                    raise ApiError(400,'invalid_json','Требуется JSON-объект.') from None
            service=self.server.service
            if self.command=='GET' and path=='/health':
                return self.respond(200,{'status':'ok','service':'vsm-backend-foundation','aiConfigured':all(getattr(service.provider,k,'') for k in ('key','folder','model'))})
            if self.command=='POST' and path=='/v1/auth/login':
                return self.respond(200,service.login(body))
            if path.startswith('/v2/'):
                store=self.server.shift_store
                if self.command=='POST' and path=='/v2/auth/register':
                    return self.respond(201,store.register(body))
                if self.command=='POST' and path=='/v2/auth/login':
                    return self.respond(200,store.login(body))
                user_id=store.authenticate(self.headers.get('Authorization',''))
                if self.command=='POST' and path=='/v2/speech/transcribe':
                    return self.respond(200,transcribe(body))
                if self.command=='GET' and path=='/v2/catalog':
                    return self.respond(200,store.catalog())
                if self.command=='GET' and path=='/v2/profile':
                    return self.respond(200,store.profile(user_id))
                if self.command=='GET' and path.startswith('/v2/leaderboard/'):
                    return self.respond(200,store.leaderboard(user_id,path.rsplit('/',1)[1]))
                if self.command=='POST' and path=='/v2/shifts':
                    return self.respond(201,store.start(user_id,body))
                parts=path.strip('/').split('/')
                if len(parts)==3 and parts[:2]==['v2','shifts'] and self.command=='GET':
                    return self.respond(200,store.get(user_id,parts[2]))
                if len(parts)==4 and parts[:2]==['v2','shifts'] and parts[3]=='actions' and self.command=='POST':
                    return self.respond(200,store.act(user_id,parts[2],body))
                raise ApiError(404,'not_found','Маршрут не найден.')
            user=service.authenticate(self.headers.get('Authorization',''))
            if self.command=='GET' and path=='/v1/scenarios':
                return self.respond(200,service.catalog())
            if self.command=='GET' and path=='/v1/profile':
                return self.respond(200,service.profile(user))
            if self.command=='POST' and path=='/v1/runs':
                return self.respond(201,service.start(user,body))
            parts=path.strip('/').split('/')
            if len(parts)>=3 and parts[:2]==['v1','runs']:
                run_id=unquote(parts[2])
                if self.command=='GET' and len(parts)==3:
                    return self.respond(200,service.get_run(user,run_id))
                if self.command=='POST' and len(parts)==4:
                    return self.respond(200,service.turn(user,run_id,parts[3],body))
            raise ApiError(404,'not_found','Маршрут не найден.')
        except ApiError as exc:
            self.respond(exc.status,{'error':{'code':exc.code,'message':str(exc)}})
        except (TimeoutError,ConnectionError):
            return
        except Exception as exc:
            # Тело ответа провайдера сюда не попадает, но ключ может лежать в аргументах исключения,
            # поэтому трейсбек печатается только по явному запросу.
            logging.error('Internal error in %s %s: %s',self.command,path,type(exc).__name__)
            if os.getenv('VSM_LOG_TRACEBACK'):
                logging.exception('Traceback for %s %s',self.command,path)
            self.respond(500,{'error':{'code':'internal_error','message':'Внутренняя ошибка сервера.'}})
        finally:
            logging.info('%s %s -> %d%s %.3fs %s',self.command,path,self.status_code or 0,
                         f' {self.error_code}' if self.error_code else '',
                         time.monotonic()-started,self.client_address[0])

    do_GET=dispatch
    do_POST=dispatch


def make_server(service,port=18767):
    server=ThreadingHTTPServer(('127.0.0.1',port),Handler)
    server.service=service
    server.shift_store=ShiftStore(os.getenv('VSM_SHIFT_DATABASE',service.path+'.shifts'))
    server.rate_lock=threading.Lock()
    server.requests=defaultdict(deque)
    return server


if __name__=='__main__':
    load_env()
    logging.basicConfig(level=logging.INFO,format='%(levelname)s %(message)s')
    root=Path(__file__).resolve().parents[1]
    database=Path(os.getenv('VSM_DATABASE',str(root/'Backend/data/vsm.sqlite3')))
    if not database.is_absolute():
        database=root/database
    service=Service(database)
    if os.getenv('VSM_DEMO_PASSWORD'):
        service.create_demo_user(os.environ['VSM_DEMO_PASSWORD'])
    server=make_server(service,int(os.getenv('VSM_PORT','18767')))
    logging.info('VSM backend: http://127.0.0.1:%d. No provider calls until a run is requested.',server.server_address[1])
    server.serve_forever()
