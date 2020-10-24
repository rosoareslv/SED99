# -*- coding: utf-8 -*-
#
# Copyright 2012-2015 Spotify AB
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

import os
import subprocess
import tempfile
import mock
from helpers import unittest

import luigi
import luigi.lock
import luigi.notifications

luigi.notifications.DEBUG = True


class TestCmd(unittest.TestCase):

    def test_getpcmd(self):
        p = subprocess.Popen(["sleep", "1"])
        self.assertTrue(
            luigi.lock.getpcmd(p.pid) in ["sleep 1", '[sleep]']
        )
        p.kill()


class LockTest(unittest.TestCase):

    def setUp(self):
        self.pid_dir = tempfile.mkdtemp()
        self.pid, self.cmd, self.pid_file = luigi.lock.get_info(self.pid_dir)

    def tearDown(self):
        if os.path.exists(self.pid_file):
            os.remove(self.pid_file)
        os.rmdir(self.pid_dir)

    def test_get_info(self):
        p = subprocess.Popen(["yes", "à我ф"], stdout=subprocess.PIPE)
        pid, cmd, pid_file = luigi.lock.get_info(self.pid_dir, p.pid)
        p.kill()
        self.assertEqual(cmd, 'yes à我ф')

    def test_acquiring_free_lock(self):
        acquired = luigi.lock.acquire_for(self.pid_dir)
        self.assertTrue(acquired)

    def test_acquiring_taken_lock(self):
        with open(self.pid_file, 'w') as f:
            f.write('%d\n' % (self.pid, ))

        acquired = luigi.lock.acquire_for(self.pid_dir)
        self.assertFalse(acquired)

    def test_acquiring_partially_taken_lock(self):
        with open(self.pid_file, 'w') as f:
            f.write('%d\n' % (self.pid, ))

        acquired = luigi.lock.acquire_for(self.pid_dir, 2)
        self.assertTrue(acquired)

        s = os.stat(self.pid_file)
        self.assertEqual(s.st_mode & 0o777, 0o777)

    def test_acquiring_lock_from_missing_process(self):
        fake_pid = 99999
        with open(self.pid_file, 'w') as f:
            f.write('%d\n' % (fake_pid, ))

        acquired = luigi.lock.acquire_for(self.pid_dir)
        self.assertTrue(acquired)

        s = os.stat(self.pid_file)
        self.assertEqual(s.st_mode & 0o777, 0o777)

    @mock.patch('os.kill')
    def test_take_lock_with_kill(self, kill_fn):
        with open(self.pid_file, 'w') as f:
            f.write('%d\n' % (self.pid,))

        kill_signal = 77777
        acquired = luigi.lock.acquire_for(self.pid_dir, kill_signal=kill_signal)
        self.assertTrue(acquired)
        kill_fn.assert_called_once_with(self.pid, kill_signal)
