import datetime as dt
import hashlib
import hmac
import json
import secrets
import sqlite3
import threading
import time
import uuid
from contextlib import contextmanager
from pathlib import Path

from ai_provider import ProviderError, YandexProvider
from domain import ACTORS, ApiError, SITUATIONS, context, iso, snapshot, utcnow


def encoded(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':'))


def token_hash(token):
    return hashlib.sha256(token.encode()).hexdigest()


class Service:
    """Single-process server foundation. Persisted decisions are committed atomically."""
    def __init__(self, database, provider=None):
        self.path = str(database)
        Path(self.path).parent.mkdir(parents=True, exist_ok=True)
        self.provider = provider or YandexProvider()
        self.mutation_lock = threading.Lock()
        with self.connect() as db:
            db.executescript('''
                CREATE TABLE IF NOT EXISTS users(id TEXT PRIMARY KEY, login TEXT UNIQUE NOT NULL,
                    display_name TEXT NOT NULL, salt BLOB NOT NULL, password_hash BLOB NOT NULL);
                CREATE TABLE IF NOT EXISTS sessions(token_hash TEXT PRIMARY KEY, user_id TEXT NOT NULL,
                    expires REAL NOT NULL, FOREIGN KEY(user_id) REFERENCES users(id));
                CREATE TABLE IF NOT EXISTS runs(id TEXT PRIMARY KEY, user_id TEXT NOT NULL,
                    scenario_key TEXT NOT NULL, scenario_version INTEGER NOT NULL, snapshot TEXT NOT NULL,
                    definition TEXT NOT NULL, created TEXT NOT NULL, FOREIGN KEY(user_id) REFERENCES users(id));
                CREATE TABLE IF NOT EXISTS events(run_id TEXT NOT NULL, action_id TEXT NOT NULL,
                    request_hash TEXT NOT NULL, event TEXT NOT NULL, response TEXT NOT NULL,
                    PRIMARY KEY(run_id, action_id), FOREIGN KEY(run_id) REFERENCES runs(id));
            ''')

    @contextmanager
    def connect(self):
        db = sqlite3.connect(self.path, timeout=60)
        db.row_factory = sqlite3.Row
        db.execute('PRAGMA foreign_keys=ON')
        try:
            with db:
                yield db
        finally:
            db.close()

    def create_demo_user(self, password):
        if not password or len(password) < 8:
            raise ValueError('VSM_DEMO_PASSWORD must contain at least 8 characters')
        with self.connect() as db:
            if db.execute('SELECT 1 FROM users WHERE login=?', ('demo',)).fetchone():
                return
            salt = secrets.token_bytes(16)
            digest = hashlib.pbkdf2_hmac('sha256', password.encode(), salt, 600000)
            db.execute('INSERT INTO users VALUES(?,?,?,?,?)',
                       (str(uuid.uuid4()), 'demo', 'Учебный проводник', salt, digest))

    def login(self, body):
        name, password = body.get('login'), body.get('password')
        if not isinstance(name,str) or not isinstance(password,str) or not 0<len(name)<=256 or not 0<len(password)<=4096:
            raise ApiError(422, 'invalid_credentials', 'Введите логин и пароль.')
        with self.connect() as db:
            user = db.execute('SELECT * FROM users WHERE login=?', (name,)).fetchone()
            # Unknown users still pay the hash cost to avoid a cheap account-existence oracle.
            salt = user['salt'] if user else bytes(16)
            digest = hashlib.pbkdf2_hmac('sha256', password.encode(), salt, 600000)
            if not user or not hmac.compare_digest(digest, user['password_hash']):
                raise ApiError(401, 'invalid_credentials', 'Неверный логин или пароль.')
            token = secrets.token_urlsafe(32)
            db.execute('DELETE FROM sessions WHERE expires<?', (time.time(),))
            db.execute('INSERT INTO sessions VALUES(?,?,?)', (token_hash(token), user['id'], time.time()+86400))
            return {'accessToken':token, 'user':{'id':user['id'], 'displayName':user['display_name']}}

    def authenticate(self, authorization):
        if not authorization.startswith('Bearer '):
            raise ApiError(401, 'unauthorized', 'Требуется вход.')
        with self.connect() as db:
            user = db.execute('SELECT users.* FROM sessions JOIN users ON users.id=sessions.user_id WHERE token_hash=? AND expires>?',
                              (token_hash(authorization[7:]),time.time())).fetchone()
            if not user:
                raise ApiError(401, 'unauthorized', 'Сессия истекла. Войдите снова.')
            return dict(user)

    def catalog(self):
        return {'items':[{k:s[k] for k in ('key','version','title','type')} for s in SITUATIONS]}

    def decide(self, value):
        try:
            return self.provider.decide(value)
        except ProviderError as exc:
            raise ApiError(503, exc.code, str(exc)) from None

    def start(self, user, body):
        if body.get('mode') != 'ai_text':
            raise ApiError(422,'unsupported_mode','Серверная основа поддерживает свободный ответ ИИ; рейтинговые смены добавляются отдельно.')
        situation = next((s for s in SITUATIONS if s['key']==body.get('scenarioKey') and s['version']==body.get('scenarioVersion')),None)
        if not situation:
            raise ApiError(404,'scenario_not_found','Ситуация или версия не найдена.')
        run_id = str(uuid.uuid4())
        with self.mutation_lock:
            decision = self.decide(context(situation,None,[],'start',''))
            result = snapshot(decision,run_id,0)
            if result['status'] != 'active':
                raise ApiError(502,'invalid_ai_decision','Первый ход должен открыть ситуацию.')
            with self.connect() as db:
                db.execute('INSERT INTO runs VALUES(?,?,?,?,?,?,?)',
                           (run_id,user['id'],situation['key'],situation['version'],encoded(result),encoded(situation),iso()))
            return result

    def owned_run(self, db, user, run_id):
        row = db.execute('SELECT * FROM runs WHERE id=? AND user_id=?',(run_id,user['id'])).fetchone()
        if not row:
            raise ApiError(404,'run_not_found','Прохождение не найдено.')
        return row

    def get_run(self,user,run_id):
        with self.connect() as db:
            result = json.loads(self.owned_run(db,user,run_id)['snapshot'])
            result['serverTime']=iso()
            return result

    def turn(self,user,run_id,endpoint,body):
        try:
            action_id=str(uuid.UUID(body.get('clientActionId','')))
        except (ValueError,TypeError,AttributeError):
            raise ApiError(422,'invalid_action_id','Требуется UUID clientActionId.') from None
        if endpoint not in ('ai-turn','timeout'):
            raise ApiError(422,'unsupported_action','Используйте свободный ответ.')
        text=body.get('text','') if endpoint=='ai-turn' else ''
        if endpoint=='ai-turn' and (not isinstance(text,str) or not 1<=len(text.strip())<=4000):
            raise ApiError(422,'invalid_answer','Ответ должен содержать от 1 до 4000 символов.')
        digest=hashlib.sha256(encoded({'endpoint':endpoint,'body':body}).encode()).hexdigest()
        with self.mutation_lock, self.connect() as db:
            row=self.owned_run(db,user,run_id)
            previous=db.execute('SELECT * FROM events WHERE run_id=? AND action_id=?',(run_id,action_id)).fetchone()
            if previous:
                if previous['request_hash'] != digest:
                    raise ApiError(409,'action_id_reused','Этот ID уже принадлежит другому действию.')
                result=json.loads(previous['response']); result['serverTime']=iso(); return result
            current=json.loads(row['snapshot'])
            if current['status'] != 'active':
                raise ApiError(409,'run_finished','Прохождение уже завершено.')
            if type(body.get('expectedRevision')) is not int or body['expectedRevision'] != current['revision'] or body.get('turnId') != current['currentNode']['id']:
                raise ApiError(409,'stale_revision','Сначала обновите состояние прохождения.')
            if current['revision'] >= 30:
                raise ApiError(409,'turn_limit','Достигнут лимит учебного прохождения.')
            deadline=current['currentNode'].get('deadlineAt')
            expired=bool(deadline and utcnow() >= dt.datetime.fromisoformat(deadline.replace('Z','+00:00')))
            if endpoint=='timeout' and not expired:
                raise ApiError(409,'deadline_not_elapsed','Серверный таймер ещё не истёк.')
            if endpoint=='ai-turn' and expired:
                raise ApiError(409,'deadline_elapsed','Время истекло. Отправьте событие timeout.')
            events=[json.loads(e['event']) for e in db.execute('SELECT event FROM events WHERE run_id=? ORDER BY rowid',(run_id,))]
            event={'type':'timeout' if endpoint=='timeout' else 'player_turn','text':text,'receivedAt':iso()}
            decision=self.decide(context(json.loads(row['definition']),current,events,event['type'],text))
            result=snapshot(decision,run_id,current['revision']+1)
            if not set(current['state']['loyalty']) <= set(result['state']['loyalty']):
                raise ApiError(502,'invalid_ai_decision','ИИ потерял показатели существующих пассажиров.')
            event['decision']=result
            db.execute('UPDATE runs SET snapshot=? WHERE id=?',(encoded(result),run_id))
            db.execute('INSERT INTO events VALUES(?,?,?,?,?)',(run_id,action_id,digest,encoded(event),encoded(result)))
            return result

    def profile(self,user):
        with self.connect() as db:
            runs=[json.loads(row['snapshot']) for row in db.execute('SELECT snapshot FROM runs WHERE user_id=?',(user['id'],))]
        completed=[r for r in runs if r['status']=='completed']
        return {'user':{'id':user['id'],'displayName':user['display_name']},'completedRuns':len(completed),
                'recentResults':[{'runId':r['runId'],'assessments':r['assessments'],'debrief':r['debrief']} for r in completed[-10:]],
                'rankedModeAvailable':False}
