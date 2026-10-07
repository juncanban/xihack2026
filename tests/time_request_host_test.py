import sys
import threading
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'host'))
import vibe_pet_host as host


class EndSession(Exception):
    pass


class FakeSerial:
    def __init__(self):
        self.lines = [b'READY:vibe_pet\n', b'Q:TIME\n', b'OK:TIME\n']
        self.written = []

    @property
    def in_waiting(self):
        if not self.lines:
            raise EndSession()
        return True

    def readline(self):
        return self.lines.pop(0)

    def write(self, value):
        self.written.append(value)

    def flush(self):
        pass


class TimeRequestTest(unittest.TestCase):
    def test_respond_only_to_request(self):
        serial = FakeSerial()
        with patch.object(host.time, 'time', return_value=1790956800):
            with self.assertRaises(EndSession):
                host.serve_serial(serial, [], threading.Lock())
        self.assertEqual(serial.written, [b'D:1790956800\n'])


if __name__ == '__main__':
    unittest.main()
