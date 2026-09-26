import unittest
from unittest.mock import patch
from authored_ai import AuthoredYandexProvider
from ai_provider import ProviderError


class AuthoredAITests(unittest.TestCase):
    def setUp(self):
        self.provider = AuthoredYandexProvider()
        self.context = {'situation': {'id': 46}, 'current_state': 'offer_water',
                        'allowed_transitions': [{'id': 'offer_water_action'}]}

    def test_missing_key_does_not_fabricate_a_response(self):
        self.provider.key = ''
        with self.assertRaises(ProviderError) as raised:
            self.provider.classify(self.context)
        self.assertEqual(raised.exception.code, 'ai_not_configured')

    def test_out_of_scope_and_malformed_transition_rejected(self):
        for transition in ('invented', ['offer_water_action']):
            with patch.object(self.provider, '_request', return_value={'situation_id': 46, 'current_state': 'offer_water', 'transition_id': transition}):
                with self.assertRaises(ProviderError):
                    self.provider.classify(self.context)

    def test_assessment_requires_exact_scale_and_reasons(self):
        good = {'decision_score': 75, 'communication_score': 100, 'decision_reason': 'Выполнено действие из журнала.',
                'communication_reason': 'Игрок объяснил порядок помощи.', 'safety_violation': False}
        with patch.object(self.provider, '_request', return_value=good):
            self.assertEqual(self.provider.assess({}), good)
        for patch_value in ({'decision_score': True}, {'communication_score': 99}, {'decision_reason': ' '}, {'safety_violation': 'false'}):
            with patch.object(self.provider, '_request', return_value={**good, **patch_value}):
                with self.assertRaises(ProviderError):
                    self.provider.assess({})


if __name__ == '__main__':
    unittest.main()
