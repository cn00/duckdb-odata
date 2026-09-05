import importlib.util
import os
import unittest
import urllib.error
from unittest.mock import patch


MODULE_PATH = os.path.join(os.path.dirname(__file__), "odata_http_test.py")
SPEC = importlib.util.spec_from_file_location("odata_http_test", MODULE_PATH)
ODATA_HTTP_TEST = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ODATA_HTTP_TEST)


class WaitForServerTest(unittest.TestCase):
    def test_retries_connection_refusals_until_server_is_ready(self):
        responses = [
            urllib.error.URLError(ConnectionRefusedError()),
            urllib.error.URLError(ConnectionRefusedError()),
            (200, '{"status": "ok"}'),
        ]

        def get(*_args, **_kwargs):
            response = responses.pop(0)
            if isinstance(response, Exception):
                raise response
            return response

        with patch.object(ODATA_HTTP_TEST, "get", side_effect=get), \
             patch.object(ODATA_HTTP_TEST.time, "monotonic", side_effect=[0, 0, 0]), \
             patch.object(ODATA_HTTP_TEST.time, "sleep") as sleep:
            self.assertEqual(ODATA_HTTP_TEST.wait_for_server(), (200, '{"status": "ok"}'))

        self.assertEqual(sleep.call_count, 2)
