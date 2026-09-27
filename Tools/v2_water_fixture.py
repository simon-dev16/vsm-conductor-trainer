"""Local-only UE end-to-end fixture for authored scenario 46.

This is a test backend, not a production fallback. It binds only to loopback and
keeps its database in a TemporaryDirectory that is removed on exit.
"""
import argparse
import os
from pathlib import Path
import socket
import sys
from tempfile import TemporaryDirectory


BACKEND = Path(__file__).resolve().parents[1] / 'Backend'
sys.path.insert(0, str(BACKEND))

import server as backend_server  # noqa: E402
from shift_store import ShiftStore  # noqa: E402


class FixtureProvider:
    """Deterministic provider for scenario 46 only; labels are test-only."""

    key = ''
    folder = ''
    model = ''

    def classify(self, context):
        situation_id = context['situation']['id']
        if situation_id != 46:
            raise RuntimeError('Fixture provider supports scenario 46 only')
        allowed = context['allowed_transitions']
        transition = next((item for item in allowed if item['id'] == 'offer_water_action'), None)
        if transition is None and context['current_state'] == 'follow_up' and allowed:
            transition = allowed[0]
        return {
            'situation_id': situation_id,
            'current_state': context['current_state'],
            'transition_id': transition['id'] if transition else 'NO_MATCH',
        }

    def assess(self, context):
        return {
            'decision_score': 100,
            'communication_score': 100,
            'decision_reason': 'Test-only fixture assessment: valid deterministic score.',
            'communication_reason': 'Test-only fixture assessment: valid deterministic score.',
            'safety_violation': False,
        }

    def decide(self, context):
        raise RuntimeError('The fixture supports v2 authored shifts only')


class FixtureHandler(backend_server.Handler):
    def respond(self, status, value):
        # take_item is committed inside store.act before respond receives its
        # snapshot. A newly occupied water slot is the success signal; the
        # route guard limits this to shift action responses.
        if (self.command == 'POST'
                and '/v2/shifts/' in self.path
                and self.path.endswith('/actions')
                and self.server.drop_take_response
                and self._has_water(value)):
            self.server.drop_take_response = False
            self.close_connection = True
            try:
                self.connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            self.connection.close()
            return
        super().respond(status, value)

    @staticmethod
    def _has_water(value):
        inventory = value.get('inventory') if isinstance(value, dict) else None
        return isinstance(inventory, list) and any(
            isinstance(slot, dict) and slot.get('item_id') == 'water'
            for slot in inventory
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=18768)
    parser.add_argument('--drop-take-response', action='store_true',
                        help='commit the first successful water take, then drop its HTTP response')
    args = parser.parse_args()
    provider = FixtureProvider()

    with TemporaryDirectory(prefix='v2-water-fixture-') as temp_dir:
        root = Path(temp_dir)
        store = ShiftStore(root / 'v2.sqlite3', provider=provider)
        store.register({'login': 'fixture', 'password': 'fixture-password'})
        server = backend_server.make_server(store, port=args.port, host='127.0.0.1')
        server.RequestHandlerClass = FixtureHandler
        server.drop_take_response = args.drop_take_response
        print(f'FIXTURE http://127.0.0.1:{args.port} scenario=46', flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
        finally:
            server.server_close()


if __name__ == '__main__':
    main()
