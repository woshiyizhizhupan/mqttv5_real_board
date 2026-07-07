import unittest

from mqtt_tester.log_buffer import LogBuffer


class LogBufferTests(unittest.TestCase):
    def test_keeps_latest_entries_only(self):
        log = LogBuffer(max_entries=3)

        for index in range(5):
            log.append("INFO", f"message-{index}")

        entries = log.snapshot()
        self.assertEqual(3, len(entries))
        self.assertTrue(entries[0].endswith("INFO message-2"))
        self.assertTrue(entries[-1].endswith("INFO message-4"))

    def test_rejects_invalid_capacity(self):
        with self.assertRaises(ValueError):
            LogBuffer(max_entries=0)


if __name__ == "__main__":
    unittest.main()
