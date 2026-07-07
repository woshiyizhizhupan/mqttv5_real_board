import unittest

from mqtt_tester.mqtt_loopback import is_success_reason_code


class FakeReasonCode:
    value = 0

    def __int__(self):
        raise TypeError("Paho v2 ReasonCode does not support int()")


class MqttLoopbackTests(unittest.TestCase):
    def test_accepts_paho_v2_reason_code_objects(self):
        self.assertTrue(is_success_reason_code(FakeReasonCode()))

    def test_rejects_nonzero_reason_codes(self):
        self.assertFalse(is_success_reason_code(1))


if __name__ == "__main__":
    unittest.main()
