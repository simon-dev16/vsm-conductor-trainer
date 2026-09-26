import os
import unittest
from unittest.mock import patch
from speech import transcribe
from domain import ApiError


class SpeechTests(unittest.TestCase):
    def test_no_key_and_invalid_audio(self):
        with patch.dict(os.environ,{'VSM_SPEECHKIT_API_KEY':''}):
            with self.assertRaises(ApiError) as context:
                transcribe({'audio_base64':'AAAAAA=='})
            self.assertEqual(context.exception.code,'speech_not_configured')
        for data in ('?', '', 'AA=='):
            with self.assertRaises(ApiError):transcribe({'audio_base64':data})
