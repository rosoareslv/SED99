# -*- coding: utf-8 -*-
#
# Copyright (c) 2017 F5 Networks Inc.
# GNU General Public License v3.0 (see COPYING or https://www.gnu.org/licenses/gpl-3.0.txt)

from __future__ import (absolute_import, division, print_function)
__metaclass__ = type

import os
import json
import sys

from nose.plugins.skip import SkipTest
if sys.version_info < (2, 7):
    raise SkipTest("F5 Ansible modules require Python >= 2.7")

from ansible.compat.tests import unittest
from ansible.compat.tests.mock import Mock
from ansible.compat.tests.mock import patch
from ansible.module_utils.f5_utils import AnsibleF5Client

try:
    from library.bigip_snmp import Parameters
    from library.bigip_snmp import ModuleManager
    from library.bigip_snmp import ArgumentSpec
    from ansible.module_utils.f5_utils import iControlUnexpectedHTTPError
    from test.unit.modules.utils import set_module_args
except ImportError:
    try:
        from ansible.modules.network.f5.bigip_snmp import Parameters
        from ansible.modules.network.f5.bigip_snmp import ModuleManager
        from ansible.modules.network.f5.bigip_snmp import ArgumentSpec
        from ansible.module_utils.f5_utils import iControlUnexpectedHTTPError
        from units.modules.utils import set_module_args
    except ImportError:
        raise SkipTest("F5 Ansible modules require the f5-sdk Python library")

fixture_path = os.path.join(os.path.dirname(__file__), 'fixtures')
fixture_data = {}


def load_fixture(name):
    path = os.path.join(fixture_path, name)

    if path in fixture_data:
        return fixture_data[path]

    with open(path) as f:
        data = f.read()

    try:
        data = json.loads(data)
    except Exception:
        pass

    fixture_data[path] = data
    return data


class TestParameters(unittest.TestCase):
    def test_module_parameters(self):
        args = dict(
            agent_status_traps='enabled',
            agent_authentication_traps='enabled',
            contact='Alice@foo.org',
            device_warning_traps='enabled',
            location='Lunar orbit',
            password='password',
            server='localhost',
            user='admin'
        )
        p = Parameters(args)
        assert p.agent_status_traps == 'enabled'
        assert p.agent_authentication_traps == 'enabled'
        assert p.device_warning_traps == 'enabled'
        assert p.location == 'Lunar orbit'
        assert p.contact == 'Alice@foo.org'

    def test_module_parameters_disabled(self):
        args = dict(
            agent_status_traps='disabled',
            agent_authentication_traps='disabled',
            device_warning_traps='disabled',
            password='password',
            server='localhost',
            user='admin'
        )
        p = Parameters(args)
        assert p.agent_status_traps == 'disabled'
        assert p.agent_authentication_traps == 'disabled'
        assert p.device_warning_traps == 'disabled'

    def test_api_parameters(self):
        args = dict(
            agentTrap='enabled',
            authTrap='enabled',
            bigipTraps='enabled',
            sysLocation='Lunar orbit',
            sysContact='Alice@foo.org',
        )
        p = Parameters(args)
        assert p.agent_status_traps == 'enabled'
        assert p.agent_authentication_traps == 'enabled'
        assert p.device_warning_traps == 'enabled'
        assert p.location == 'Lunar orbit'
        assert p.contact == 'Alice@foo.org'

    def test_api_parameters_disabled(self):
        args = dict(
            agentTrap='disabled',
            authTrap='disabled',
            bigipTraps='disabled',
        )
        p = Parameters(args)
        assert p.agent_status_traps == 'disabled'
        assert p.agent_authentication_traps == 'disabled'
        assert p.device_warning_traps == 'disabled'


@patch('ansible.module_utils.f5_utils.AnsibleF5Client._get_mgmt_root',
       return_value=True)
class TestManager(unittest.TestCase):

    def setUp(self):
        self.spec = ArgumentSpec()

    def test_update_agent_status_traps(self, *args):
        set_module_args(dict(
            agent_status_traps='enabled',
            password='passsword',
            server='localhost',
            user='admin'
        ))

        # Configure the parameters that would be returned by querying the
        # remote device
        current = Parameters(
            dict(
                agent_status_traps='disabled'
            )
        )

        client = AnsibleF5Client(
            argument_spec=self.spec.argument_spec,
            supports_check_mode=self.spec.supports_check_mode,
            f5_product_name=self.spec.f5_product_name
        )
        mm = ModuleManager(client)

        # Override methods to force specific logic in the module to happen
        mm.update_on_device = Mock(return_value=True)
        mm.read_current_from_device = Mock(return_value=current)

        results = mm.exec_module()

        assert results['changed'] is True
        assert results['agent_status_traps'] == 'enabled'
