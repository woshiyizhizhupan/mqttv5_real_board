import unittest

from mqtt_tester.mqtt_topics import build_default_topics, validate_topics


class TopicTests(unittest.TestCase):
    def test_builds_five_business_topics(self):
        topics = build_default_topics("pk001", "hc32-dev")

        self.assertEqual(5, len(topics.as_list()))
        self.assertEqual("/pk001/hc32-dev/user/telemetry/up", topics.telemetry_up)
        self.assertEqual("/pk001/hc32-dev/user/event/up", topics.event_up)
        self.assertEqual("/pk001/hc32-dev/user/status/up", topics.status_up)
        self.assertEqual("/pk001/hc32-dev/user/cmd/down", topics.cmd_down)
        self.assertEqual("/pk001/hc32-dev/user/debug/up", topics.debug_up)

    def test_rejects_empty_or_duplicate_topics(self):
        with self.assertRaises(ValueError):
            validate_topics(["a", "", "c", "d", "e"])

        with self.assertRaises(ValueError):
            validate_topics(["a", "b", "b", "d", "e"])


if __name__ == "__main__":
    unittest.main()
