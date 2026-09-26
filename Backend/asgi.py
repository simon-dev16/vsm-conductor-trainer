import os
import threading
import time
from collections import defaultdict, deque
from pathlib import Path

from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse
from starlette.concurrency import run_in_threadpool
from domain import ApiError
from server import load_env
from shift_store import ShiftStore
from speech import transcribe

load_env()
app = FastAPI(title='ВСМ API',version='2.0',docs_url='/api-docs')
store = ShiftStore(os.getenv('VSM_SHIFT_DATABASE',str(Path(__file__).parent/'data/shifts.sqlite3')))
requests = defaultdict(deque)
rate_lock = threading.Lock()


@app.middleware('http')
async def limits(request:Request, call_next):
    try:
        length=int(request.headers.get('content-length','0'))
    except ValueError:
        return JSONResponse({'error':{'code':'invalid_length','message':'Неверная длина запроса.'}},400)
    if length>(1300000 if request.url.path=='/v2/speech/transcribe' else 20000):
        return JSONResponse({'error':{'code':'body_too_large','message':'Запрос слишком большой.'}},413)
    with rate_lock:
        now=time.monotonic()
        bucket=requests[request.client.host]
        while bucket and bucket[0]<now-60:bucket.popleft()
        if len(bucket)>=120:
            return JSONResponse({'error':{'code':'rate_limited','message':'Слишком много запросов.'}},429)
        bucket.append(now)
    response=await call_next(request)
    response.headers['Cache-Control']='no-store'
    return response


@app.exception_handler(ApiError)
async def api_error(request,exc):
    return JSONResponse({'error':{'code':exc.code,'message':str(exc)}},exc.status)


def user(request):return store.authenticate(request.headers.get('authorization',''))


@app.get('/health')
def health():
    provider=store.engine.provider
    return {'status':'ok','apiVersion':2,'database':'postgresql' if store.postgres else 'sqlite',
            'aiConfigured':all(getattr(provider,k,'') for k in ('key','folder','model'))}


@app.post('/v2/auth/register',status_code=201)
def register(body:dict):return store.register(body)


@app.post('/v2/auth/login')
def login(body:dict):return store.login(body)


@app.get('/v2/catalog')
def catalog(request:Request):
    user(request)
    return store.catalog()


@app.get('/v2/profile')
def profile(request:Request):return store.profile(user(request))


@app.get('/v2/leaderboard/{scope}')
def leaderboard(scope:str,request:Request):return store.leaderboard(user(request),scope)


@app.post('/v2/shifts',status_code=201)
def start(body:dict,request:Request):return store.start(user(request),body)


@app.get('/v2/shifts/current')
def current_shift(request:Request):return store.current(user(request))


@app.get('/v2/shifts/{shift_id}')
def get_shift(shift_id:str,request:Request):return store.get(user(request),shift_id)


@app.post('/v2/shifts/{shift_id}/actions')
def action(shift_id:str,body:dict,request:Request):return store.act(user(request),shift_id,body)


@app.get('/v2/integrations/me/progress')
def integration_progress(request:Request):
    identity=user(request)
    return {'schema_version':1,'user_id':identity,'progress':store.profile(identity)}

@app.post('/v2/speech/transcribe')
def speech(body:dict,request:Request):
    user(request)
    return transcribe(body)
