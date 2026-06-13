import os, sys, json, unittest, urllib.error
sys.path.insert(0, os.path.dirname(__file__))
import cc_usage_collector as col

SAMPLE = {
    'five_hour': {'utilization': 3.0, 'resets_at': '1970-01-01T01:00:00+00:00'},
    'seven_day': {'utilization': 13.0, 'resets_at': '1970-01-01T02:00:00+00:00'},
    'seven_day_sonnet': {'utilization': 0.0, 'resets_at': '1970-01-01T02:00:00+00:00'},
    'seven_day_opus': None,
    'extra_usage': {'is_enabled': False},
}


class TestCollector(unittest.TestCase):
    def setUp(self):
        self._rt = col.read_token
        self._fu = col.fetch_usage
        self._cv = col.claude_version
        self._cred = col.read_credentials
        col.read_token = lambda: 'sk-ant-oat01-FAKE'
        col.claude_version = lambda: '2.1.174'
        col.read_credentials = lambda: {'claudeAiOauth': {'subscriptionType': 'max'}}

    def tearDown(self):
        col.read_token = self._rt
        col.fetch_usage = self._fu
        col.claude_version = self._cv
        col.read_credentials = self._cred

    def test_collect_success(self):
        col.fetch_usage = lambda token, version: SAMPLE
        f = col.collect()
        self.assertEqual(f['ok'], 1)
        self.assertEqual(f['five_hour_pct'], 3)
        self.assertEqual(f['seven_day_pct'], 13)
        self.assertEqual(f['sonnet_pct'], 0)
        self.assertEqual(f['opus_pct'], -1)
        self.assertEqual(f['sub'], 'max')

    def test_collect_401_token_expired(self):
        def boom(token, version):
            raise urllib.error.HTTPError('u', 401, 'unauth', {}, None)
        col.fetch_usage = boom
        f = col.collect()
        self.assertEqual(f['ok'], 0)
        self.assertEqual(f['error'], 'token_expired')

    def test_collect_500_http_error(self):
        def boom(token, version):
            raise urllib.error.HTTPError('u', 500, 'err', {}, None)
        col.fetch_usage = boom
        f = col.collect()
        self.assertEqual(f['ok'], 0)
        self.assertEqual(f['error'], 'http_500')

    def test_collect_network_error(self):
        def boom(token, version):
            raise urllib.error.URLError('no route')
        col.fetch_usage = boom
        f = col.collect()
        self.assertEqual(f['ok'], 0)
        self.assertEqual(f['error'], 'network')

    def test_collect_no_token(self):
        col.read_token = lambda: (_ for _ in ()).throw(FileNotFoundError())
        f = col.collect()
        self.assertEqual(f['ok'], 0)
        self.assertEqual(f['error'], 'no_token')


if __name__ == '__main__':
    unittest.main()
