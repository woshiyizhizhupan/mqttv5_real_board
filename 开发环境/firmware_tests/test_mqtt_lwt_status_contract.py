import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
FIRMWARES = {
    "real": ROOT / "mqttv5_real_board",
    "sim": ROOT / "mqttv5_sim_board",
}


class MqttLwtStatusContractTests(unittest.TestCase):
    def test_status_topic_is_generated_without_expanding_business_topics(self):
        for name, firmware in FIRMWARES.items():
            with self.subTest(firmware=name):
                config_h = (firmware / "src" / "config.h").read_text(encoding="utf-8")
                main_c = (firmware / "src" / "main.c").read_text(encoding="utf-8")

                self.assertRegex(config_h, r"#define\s+MQTT_TOPIC_COUNT\s+5\b")
                self.assertRegex(config_h, r"char\s+topics\s*\[\s*5\s*\]\s*\[\s*96\s*\]")
                self.assertIn('#define MQTT_SERVER_STATUS_TOPIC_PREFIX "v1/devices/status/"', main_c)
                self.assertIn('"%s%s"', main_c)
                self.assertIn("MQTT_SERVER_STATUS_TOPIC_PREFIX", main_c)
                self.assertIn("mqtt_get_device_name()", main_c)
                self.assertNotIn("v1/devices/status/+", main_c)

    def test_connect_uses_retained_qos0_lwt_and_online_status(self):
        for name, firmware in FIRMWARES.items():
            with self.subTest(firmware=name):
                main_c = (firmware / "src" / "main.c").read_text(encoding="utf-8")

                self.assertRegex(main_c, r"data\.willFlag\s*=\s*1\s*;")
                self.assertRegex(main_c, r"data\.will\.qos\s*=\s*0\s*;")
                self.assertRegex(main_c, r"data\.will\.retained\s*=\s*1\s*;")
                self.assertIn("data.will.topicName.cstring = (char *)mqtt_get_status_topic();", main_c)
                self.assertIn('data.will.message.cstring = mqtt_make_status_payload("offline", "mqtt_lwt");', main_c)
                self.assertIn('mqtt_publish_lifecycle_status("online", "mqtt_connected");', main_c)
                self.assertIn('mqtt_publish_json_to_topic_ex(mqtt_get_status_topic(), payload, 0U, 1U);', main_c)
                self.assertRegex(main_c, r"(?s)MQTTV5Serialize_publish\s*\([^;]*retain_flag")

    def test_status_payload_shape_matches_server_contract(self):
        for name, firmware in FIRMWARES.items():
            with self.subTest(firmware=name):
                main_c = (firmware / "src" / "main.c").read_text(encoding="utf-8")

                self.assertIn('\\"schema\\":\\"emqx-gateway.status.v1\\"', main_c)
                self.assertIn('\\"device_id\\":\\"%s\\"', main_c)
                self.assertIn('\\"status\\":\\"%s\\"', main_c)
                self.assertIn('\\"reason\\":\\"%s\\"', main_c)


if __name__ == "__main__":
    unittest.main()
