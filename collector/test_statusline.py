import os, sys, tempfile, unittest
sys.path.insert(0, os.path.dirname(__file__))
import statusline_hook as sl
import usage_cache as uc


class TestStatusline(unittest.TestCase):
    def test_build_status_line(self):
        d = {'model': {'display_name': 'Opus 4.8'}, 'workspace': {'current_dir': 'C:/code/ClaudeMeter'},
             'rate_limits': {'five_hour': {'used_percentage': 67}}}
        line = sl.build_status_line(d)
        self.assertIn('Opus 4.8', line)
        self.assertIn('ClaudeMeter', line)
        self.assertIn('67%', line)

    def test_build_status_line_no_ratelimits(self):
        line = sl.build_status_line({'model': {'display_name': 'Sonnet'}})
        self.assertIn('Sonnet', line)

    def test_handle_writes_cache_when_no_oauth(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, 'usage.ini')
            payload = {'model': {'display_name': 'Opus'},
                       'rate_limits': {'five_hour': {'used_percentage': 67, 'resets_at': 1700000000},
                                       'seven_day': {'used_percentage': 10, 'resets_at': 1700500000}}}
            sl.handle(payload, cache_path=p)
            back = uc.read_cache(p)
            self.assertEqual(back['source'], 'statusline')
            self.assertEqual(back['five_hour_pct'], 67)
            self.assertEqual(back['seven_day_pct'], 10)

    def test_handle_does_not_clobber_fresh_oauth(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, 'usage.ini')
            import time as _t
            uc.write_cache({'ok': 1, 'source': 'oauth', 'updated_epoch': int(_t.time()),
                            'five_hour_pct': 3, 'sonnet_pct': 0}, p)
            payload = {'model': {'display_name': 'Opus'},
                       'rate_limits': {'five_hour': {'used_percentage': 99, 'resets_at': 1}}}
            sl.handle(payload, cache_path=p)
            back = uc.read_cache(p)
            self.assertEqual(back['source'], 'oauth')   # unchanged
            self.assertEqual(back['five_hour_pct'], 3)

    def test_handle_writes_when_oauth_stale(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, 'usage.ini')
            uc.write_cache({'ok': 1, 'source': 'oauth', 'updated_epoch': 1,
                            'five_hour_pct': 3, 'sonnet_pct': 5}, p)
            payload = {'model': {'display_name': 'Opus'},
                       'rate_limits': {'five_hour': {'used_percentage': 42, 'resets_at': 1700000000}}}
            sl.handle(payload, cache_path=p)
            back = uc.read_cache(p)
            self.assertEqual(back['source'], 'statusline')
            self.assertEqual(back['five_hour_pct'], 42)
            self.assertEqual(back['sonnet_pct'], 5)  # per-model preserved


if __name__ == '__main__':
    unittest.main()
