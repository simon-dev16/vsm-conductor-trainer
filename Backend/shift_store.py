from contextlib import contextmanager
from datetime import datetime, timezone, timedelta
import hashlib
import hmac
import json
import os
from pathlib import Path
import secrets
import sqlite3
import threading
import time
import uuid

from domain import ApiError
from scenario_machine import Catalog, ScenarioError
from inventory import InventoryError
from authored_ai import AuthoredYandexProvider
from ai_provider import ProviderError
from shift_engine import ShiftEngine


def encode(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':'))


class ShiftStore:
    def __init__(self, database, provider=None, clock=time.time):
        self.database = str(database)
        self.postgres = self.database.startswith(('postgresql://', 'postgres://'))
        self.clock = clock
        self.lock = threading.RLock()
        self.engine = ShiftEngine(provider or AuthoredYandexProvider())
        if not self.postgres:
            Path(self.database).parent.mkdir(parents=True, exist_ok=True)
        with self.connect() as db:
            for statement in (
                'CREATE TABLE IF NOT EXISTS v2_migrations(version INTEGER PRIMARY KEY)',
                'CREATE TABLE IF NOT EXISTS v2_users(id TEXT PRIMARY KEY, login TEXT UNIQUE NOT NULL, salt TEXT NOT NULL, digest TEXT NOT NULL, profile TEXT NOT NULL)',
                'CREATE TABLE IF NOT EXISTS v2_sessions(token TEXT PRIMARY KEY, user_id TEXT NOT NULL REFERENCES v2_users(id), expires DOUBLE PRECISION NOT NULL)',
                'CREATE TABLE IF NOT EXISTS v2_shifts(id TEXT PRIMARY KEY, user_id TEXT NOT NULL REFERENCES v2_users(id), day TEXT NOT NULL, mode TEXT NOT NULL, state TEXT NOT NULL)',
                'CREATE TABLE IF NOT EXISTS v2_actions(user_id TEXT NOT NULL, action_id TEXT NOT NULL, fingerprint TEXT NOT NULL, response TEXT NOT NULL, PRIMARY KEY(user_id,action_id))',
                'CREATE INDEX IF NOT EXISTS v2_shifts_by_user ON v2_shifts(user_id)',
                'CREATE TABLE IF NOT EXISTS v2_current_shifts(user_id TEXT PRIMARY KEY REFERENCES v2_users(id), shift_id TEXT NOT NULL)',
                'INSERT INTO v2_migrations(version) VALUES(1) ON CONFLICT DO NOTHING',
                'INSERT INTO v2_migrations(version) VALUES(2) ON CONFLICT DO NOTHING'):
                db.execute(statement)

    @contextmanager
    def connect(self):
        if self.postgres:
            import psycopg
            from psycopg.rows import dict_row
            raw = psycopg.connect(self.database, row_factory=dict_row)
        else:
            raw = sqlite3.connect(self.database, timeout=60)
            raw.row_factory = sqlite3.Row
            raw.execute('PRAGMA foreign_keys=ON')
            raw.execute('BEGIN IMMEDIATE')
        store = self
        class Connection:
            def execute(self, query, parameters=()):
                return raw.execute(query.replace('?', '%s') if store.postgres else query, parameters)
        try:
            with raw:
                yield Connection()
        finally:
            raw.close()

    def register(self, body):
        login, password = body.get('login'), body.get('password')
        if not isinstance(login,str) or not 3 <= len(login.strip()) <= 64 or not isinstance(password,str) or not 8 <= len(password) <= 256:
            raise ApiError(422,'invalid_registration','Логин 3–64 символа, пароль 8–256 символов.')
        login = login.strip().lower()
        salt = secrets.token_hex(16)
        digest = hashlib.pbkdf2_hmac('sha256',password.encode(),bytes.fromhex(salt),600000).hex()
        profile = {'display_name':login, 'brigade':'Учебная бригада', 'depot':'Учебное депо', 'company':'ВСМ',
                   'completed_shifts':0, 'xp':0, 'level':1, 'best_rating':0, 'achievements':[], 'history':[],
                   'notifications':[{'id':'welcome','text':'Доступны учебные ситуации и тренировка.'}]}
        with self.lock, self.connect() as db:
            row = db.execute('INSERT INTO v2_users VALUES(?,?,?,?,?) ON CONFLICT(login) DO NOTHING RETURNING id',
                             (str(uuid.uuid4()),login,salt,digest,encode(profile))).fetchone()
            if not row:
                raise ApiError(409,'login_exists','Этот логин уже занят.')
        return self.login(body)

    def login(self, body):
        login, password = body.get('login'), body.get('password')
        if not isinstance(login,str) or not isinstance(password,str) or len(password)>256:
            raise ApiError(422,'invalid_login','Введите логин и пароль.')
        with self.connect() as db:
            user = db.execute('SELECT * FROM v2_users WHERE login=?',(login.strip().lower(),)).fetchone()
            salt = bytes.fromhex(user['salt']) if user else bytes(16)
            digest = hashlib.pbkdf2_hmac('sha256',password.encode(),salt,600000).hex()
            if not user or not hmac.compare_digest(digest,user['digest']):
                raise ApiError(401,'invalid_login','Логин или пароль неверный.')
            token=secrets.token_urlsafe(32)
            db.execute('DELETE FROM v2_sessions WHERE expires<?',(self.clock(),))
            db.execute('INSERT INTO v2_sessions VALUES(?,?,?)',(hashlib.sha256(token.encode()).hexdigest(),user['id'],self.clock()+86400))
            return {'accessToken':token,'user':{'id':user['id'],'displayName':json.loads(user['profile'])['display_name']}}

    def authenticate(self, authorization):
        if not authorization.startswith('Bearer '):
            raise ApiError(401,'unauthorized','Требуется вход.')
        digest=hashlib.sha256(authorization[7:].encode()).hexdigest()
        with self.connect() as db:
            row=db.execute('SELECT user_id FROM v2_sessions WHERE token=? AND expires>?',(digest,self.clock())).fetchone()
        if not row:
            raise ApiError(401,'unauthorized','Войдите снова.')
        return row['user_id']

    def _user_lock(self, db, user):
        query='SELECT profile FROM v2_users WHERE id=?'+(' FOR UPDATE' if self.postgres else '')
        row=db.execute(query,(user,)).fetchone()
        if not row:
            raise ApiError(401,'unauthorized','Пользователь не найден.')
        return json.loads(row['profile'])

    def _mutation(self, user, body, operation, callback):
        action_id=body.get('clientActionId')
        try:
            uuid.UUID(action_id)
        except (ValueError,TypeError,AttributeError):
            raise ApiError(422,'action_id_required','Требуется UUID действия.') from None
        fingerprint=hashlib.sha256(encode({'operation':operation,'body':body}).encode()).hexdigest()
        with self.lock,self.connect() as db:
            profile=self._user_lock(db,user)
            previous=db.execute('SELECT * FROM v2_actions WHERE user_id=? AND action_id=?',(user,action_id)).fetchone()
            if previous:
                if previous['fingerprint']!=fingerprint:
                    raise ApiError(409,'action_conflict','UUID уже использован для другого действия.')
                return json.loads(previous['response'])
            try:
                response=callback(db,profile)
            except (ScenarioError,InventoryError) as exc:
                raise ApiError(409,'invalid_action',str(exc)) from None
            except ProviderError as exc:
                raise ApiError(503,exc.code,str(exc)) from None
            if not isinstance(response, ApiError):
                db.execute('INSERT INTO v2_actions VALUES(?,?,?,?)',(user,action_id,fingerprint,encode(response)))
        if isinstance(response, ApiError):
            raise response
        return response

    def day(self):
        return datetime.fromtimestamp(self.clock(),timezone(timedelta(hours=3))).date().isoformat()

    def start(self,user,body):
        def operation(db,profile):
            mode=body.get('mode','training')
            count=db.execute("SELECT COUNT(*) AS count FROM v2_shifts WHERE user_id=? AND day=? AND mode='ranked'",(user,self.day())).fetchone()['count']
            if mode=='ranked' and count>=10:
                raise ApiError(429,'daily_attempts_exhausted','На сегодня попытки закончились.')
            value=self.engine.begin(mode,self.clock(),body.get('situation_id',46))
            self._settle_current(db,user,profile,replace=True)
            db.execute('INSERT INTO v2_shifts VALUES(?,?,?,?,?)',(value['id'],user,self.day(),mode,encode(value)))
            self._set_current(db,user,value['id'])
            return self.engine.snapshot(value,self.clock())
        return self._mutation(user,body,'start',operation)

    def _settle_current(self, db, user, profile, replace=False):
        pointer = db.execute('SELECT shift_id FROM v2_current_shifts WHERE user_id=?',(user,)).fetchone()
        if pointer:
            rows = db.execute('SELECT state FROM v2_shifts WHERE user_id=? AND id=?',(user,pointer['shift_id'])).fetchall()
        else:
            rows = db.execute('SELECT state FROM v2_shifts WHERE user_id=?',(user,)).fetchall()
        values = [json.loads(row['state']) for row in rows]
        values.sort(key=lambda value: (value['started_at'], value['id']), reverse=True)
        current = None
        now = self.clock()
        for value in values:
            if value.get('recorded'):
                continue
            engine = self._engine_for(value)
            before = encode(value)
            engine.tick(value, now)
            if value['status'] == 'active':
                if replace or current is not None:
                    engine.finish(value, 'cancelled')
                    value['events'].append({'kind':'replaced_by_new_shift','at':now})
                else:
                    current = value
            self._record_result(db, user, profile, value)
            if encode(value) != before:
                value['revision'] += 1
                db.execute('UPDATE v2_shifts SET state=? WHERE id=?', (encode(value),value['id']))
        self._set_current(db,user,current['id'] if current else '')
        return current

    @staticmethod
    def _set_current(db,user,shift_id):
        db.execute('INSERT INTO v2_current_shifts VALUES(?,?) ON CONFLICT(user_id) DO UPDATE SET shift_id=excluded.shift_id',
                   (user,shift_id))

    def current(self, user):
        with self.lock, self.connect() as db:
            profile = self._user_lock(db, user)
            value = self._settle_current(db, user, profile)
            return {'shift': self._engine_for(value).snapshot(value,self.clock()) if value else None}

    def _load(self,db,user,shift_id):
        row=db.execute('SELECT state FROM v2_shifts WHERE id=? AND user_id=?',(shift_id,user)).fetchone()
        if not row:
            raise ApiError(404,'shift_not_found','Смена не найдена.')
        return json.loads(row['state'])

    def _engine_for(self, value):
        snapshot = value.get('content_snapshot')
        return ShiftEngine(self.engine.provider, Catalog.from_snapshot(snapshot)) if snapshot else self.engine

    def _record_result(self,db,user,profile,value):
        if value['status']=='active' or value.get('recorded'):
            return
        value['recorded']=True
        profile['history'].append({'id':value['id'],'status':value['status'],'mode':value['mode'],'rating':value['rating'], 'at':self.clock()})
        profile['history']=profile['history'][-100:]
        if value['mode']=='ranked' and value['status']=='completed':
            profile['completed_shifts']+=1
            profile['xp']+=100
            profile['level']=1+profile['xp']//500
            profile['best_rating']=max(profile['best_rating'],value['rating']['rating'])
            for achievement in value['rules']['achievements']:
                qualifies=(profile['completed_shifts']>=achievement.get('completed_shifts',0) and
                           value['safety']>=achievement.get('minimum_final_safety',0) and
                           value['rating']['communication']>=achievement.get('minimum_communication',0))
                if qualifies and achievement['id'] not in profile['achievements']:
                    profile['achievements'].append(achievement['id'])
                    profile['notifications'].append({'id':achievement['id'],'text':achievement['title']})
        db.execute('UPDATE v2_users SET profile=? WHERE id=?',(encode(profile),user))

    def get(self,user,shift_id):
        with self.lock,self.connect() as db:
            profile=self._user_lock(db,user)
            value=self._load(db,user,shift_id)
            engine=self._engine_for(value)
            before=encode(value)
            engine.tick(value,self.clock())
            self._record_result(db,user,profile,value)
            if encode(value)!=before:
                value['revision']+=1
                db.execute('UPDATE v2_shifts SET state=? WHERE id=?',(encode(value),shift_id))
            return engine.snapshot(value,self.clock())

    def act(self,user,shift_id,body):
        def operation(db,profile):
            current=self._load(db,user,shift_id)
            engine=self._engine_for(current)
            before=time.monotonic()
            try:
                value=engine.act(current,body,self.clock())
            except ProviderError as exc:
                self._exclude_processing(current,time.monotonic()-before)
                db.execute('UPDATE v2_shifts SET state=? WHERE id=?',(encode(current),shift_id))
                return ApiError(503,exc.code,str(exc))
            processing=time.monotonic()-before
            if body.get('kind') in ('answer','give_item'):
                self._exclude_processing(value,processing)
            self._record_result(db,user,profile,value)
            db.execute('UPDATE v2_shifts SET state=? WHERE id=?',(encode(value),shift_id))
            return engine.snapshot(value,self.clock())
        return self._mutation(user,body,shift_id,operation)

    @staticmethod
    def _exclude_processing(value, seconds):
        value['deadline']+=seconds
        value['next_spawn']+=seconds
        for task in value['tasks']:
            if task['status']=='active':
                task['deadline']+=seconds
                task['created_at']+=seconds
        for ticket in value['tickets']:
            if ticket['started_at'] is not None and not ticket['checked']:
                ticket['started_at']+=seconds

    def profile(self,user):
        with self.connect() as db:
            profile=self._user_lock(db,user)
            count=db.execute("SELECT COUNT(*) AS count FROM v2_shifts WHERE user_id=? AND day=? AND mode='ranked'",(user,self.day())).fetchone()['count']
            profile['attempts_remaining']=max(0,10-count)
            return profile

    def leaderboard(self,user,scope='company'):
        if scope not in ('company','depot','brigade'):
            raise ApiError(422,'invalid_scope','Неизвестная группа рейтинга.')
        with self.connect() as db:
            own=self._user_lock(db,user)
            rows=[]
            for row in db.execute('SELECT id,profile FROM v2_users').fetchall():
                profile=json.loads(row['profile'])
                if profile[scope]==own[scope]:
                    rows.append({'id':row['id'],'name':profile['display_name'],'rating':profile['best_rating'],'is_self':row['id']==user})
        rows.sort(key=lambda r:(-r['rating'],r['id']))
        for i,row in enumerate(rows):
            row['rank']=i+1
        return {'scope':scope,'items':rows}

    def catalog(self):
        return {'items':[{'id':key,'title':s['situation_description'],'type':s['task_type'],
                          'playable':key in self.engine.catalog.flows} for key,s in self.engine.catalog.situations.items()]}
