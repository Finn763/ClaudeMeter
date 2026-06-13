import os, sys, tempfile, unittest
sys.path.insert(0, os.path.dirname(__file__))
import usage_cache as uc


class TestUsageCache(unittest.TestCase):
    def test_iso_to_epoch(self):
        self.assertEqual(uc.iso_to_epoch('1970-01-01T00:00:00+00:00'), 0)
        self.assertEqual(uc.iso_to_epoch('1970-01-01T01:00:00+00:00'), 3600)
        self.assertEqual(uc.iso_to_epoch('2026-06-12T15:50:00.355853+00:00'), 1781279400)
        self.assertEqual(uc.iso_to_epoch(''), 0)
        self.assertEqual(uc.iso_to_epoch('not-a-date'), 0)

    def test_normalize_oauth(self):
        data = {
            'five_hour': {'utilization': 3.0, 'resets_at': '1970-01-01T01:00:00+00:00'},
            'seven_day': {'utilization': 13.0, 'resets_at': '1970-01-01T02:00:00+00:00'},
            'seven_day_sonnet': {'utilization': 0.0, 'resets_at': '1970-01-01T02:00:00+00:00'},
            'seven_day_opus': None,
            'extra_usage': {'is_enabled': False},
        }
        f = uc.normalize_oauth(data)
        self.assertEqual(f['ok'], 1)
        self.assertEqual(f['source'], 'oauth')
        self.assertEqual(f['five_hour_pct'], 3)
        self.assertEqual(f['five_hour_reset_epoch'], 3600)
        self.assertEqual(f['seven_day_pct'], 13)
        self.assertEqual(f['sonnet_pct'], 0)
        self.assertEqual(f['opus_pct'], -1)   # null -> -1

    def test_normalize_statusline(self):
        rl = {
            'five_hour': {'used_percentage': 67, 'resets_at': 1700000000},
            'seven_day': {'used_percentage': 10, 'resets_at': 1700500000},
        }
        f = uc.normalize_statusline(rl)
        self.assertEqual(f['source'], 'statusline')
        self.assertEqual(f['five_hour_pct'], 67)
        self.assertEqual(f['five_hour_reset_epoch'], 1700000000)
        self.assertEqual(f['seven_day_pct'], 10)

    def test_render_and_read_roundtrip(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, 'usage.ini')
            fields = {'ok': 1, 'source': 'oauth', 'updated_epoch': 123,
                      'five_hour_pct': 67, 'seven_day_pct': 10, 'opus_pct': -1, 'sub': 'max'}
            uc.write_cache(fields, p)
            self.assertTrue(os.path.exists(p))
            back = uc.read_cache(p)
            self.assertEqual(back['ok'], 1)
            self.assertEqual(back['source'], 'oauth')
            self.assertEqual(back['five_hour_pct'], 67)
            self.assertEqual(back['opus_pct'], -1)
            self.assertEqual(back['sub'], 'max')
            self.assertEqual(back['seven_day_reset_epoch'], 0)  # default applied

    def test_read_missing_file(self):
        back = uc.read_cache(os.path.join(tempfile.gettempdir(), 'nope_missing.ini'))
        self.assertEqual(back['ok'], 0)
        self.assertEqual(back['five_hour_pct'], -1)


if __name__ == '__main__':
    unittest.main()
