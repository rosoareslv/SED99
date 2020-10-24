#!/usr/bin/python
# -*- coding: utf-8 -*-
#
# Copyright (c) 2017 F5 Networks Inc.
# GNU General Public License v3.0 (see COPYING or https://www.gnu.org/licenses/gpl-3.0.txt)

from __future__ import absolute_import, division, print_function
__metaclass__ = type


ANSIBLE_METADATA = {'metadata_version': '1.1',
                    'status': ['preview'],
                    'supported_by': 'community'}

DOCUMENTATION = r'''
---
module: bigip_command
short_description: Run arbitrary command on F5 devices
description:
  - Sends an arbitrary command to an BIG-IP node and returns the results
    read from the device. This module includes an argument that will cause
    the module to wait for a specific condition before returning or timing
    out if the condition is not met.
version_added: "2.4"
options:
  commands:
    description:
      - The commands to send to the remote BIG-IP device over the
        configured provider. The resulting output from the command
        is returned. If the I(wait_for) argument is provided, the
        module is not returned until the condition is satisfied or
        the number of retries as expired.
      - The I(commands) argument also accepts an alternative form
        that allows for complex values that specify the command
        to run and the output format to return. This can be done
        on a command by command basis. The complex argument supports
        the keywords C(command) and C(output) where C(command) is the
        command to run and C(output) is 'text' or 'one-line'.
    required: True
  wait_for:
    description:
      - Specifies what to evaluate from the output of the command
        and what conditionals to apply.  This argument will cause
        the task to wait for a particular conditional to be true
        before moving forward. If the conditional is not true
        by the configured retries, the task fails. See examples.
    aliases: ['waitfor']
  match:
    description:
      - The I(match) argument is used in conjunction with the
        I(wait_for) argument to specify the match policy. Valid
        values are C(all) or C(any). If the value is set to C(all)
        then all conditionals in the I(wait_for) must be satisfied. If
        the value is set to C(any) then only one of the values must be
        satisfied.
    default: all
  retries:
    description:
      - Specifies the number of retries a command should by tried
        before it is considered failed. The command is run on the
        target device every retry and evaluated against the I(wait_for)
        conditionals.
    default: 10
  interval:
    description:
      - Configures the interval in seconds to wait between retries
        of the command. If the command does not pass the specified
        conditional, the interval indicates how to long to wait before
        trying the command again.
    default: 1
  transport:
    description:
      - Configures the transport connection to use when connecting to the
        remote device. The transport argument supports connectivity to the
        device over cli (ssh) or rest.
    required: true
    choices:
        - rest
        - cli
    default: rest
    version_added: "2.5"
notes:
  - Requires the f5-sdk Python package on the host. This is as easy as pip
    install f5-sdk.
requirements:
  - f5-sdk >= 2.2.3
extends_documentation_fragment: f5
author:
  - Tim Rupp (@caphrim007)
'''

EXAMPLES = r'''
- name: run show version on remote devices
  bigip_command:
    commands: show sys version
    server: lb.mydomain.com
    password: secret
    user: admin
    validate_certs: no
  delegate_to: localhost

- name: run show version and check to see if output contains BIG-IP
  bigip_command:
    commands: show sys version
    wait_for: result[0] contains BIG-IP
    server: lb.mydomain.com
    password: secret
    user: admin
    validate_certs: no
  delegate_to: localhost

- name: run multiple commands on remote nodes
  bigip_command:
    commands:
      - show sys version
      - list ltm virtual
    server: lb.mydomain.com
    password: secret
    user: admin
    validate_certs: no
  delegate_to: localhost

- name: run multiple commands and evaluate the output
  bigip_command:
    commands:
      - show sys version
      - list ltm virtual
    wait_for:
      - result[0] contains BIG-IP
      - result[1] contains my-vs
    server: lb.mydomain.com
    password: secret
    user: admin
    validate_certs: no
  delegate_to: localhost

- name: tmsh prefixes will automatically be handled
  bigip_command:
    commands:
      - show sys version
      - tmsh list ltm virtual
    server: lb.mydomain.com
    password: secret
    user: admin
    validate_certs: no
  delegate_to: localhost
'''

RETURN = r'''
stdout:
  description: The set of responses from the commands
  returned: always
  type: list
  sample: ['...', '...']
stdout_lines:
  description: The value of stdout split into a list
  returned: always
  type: list
  sample: [['...', '...'], ['...'], ['...']]
failed_conditions:
  description: The list of conditionals that have failed
  returned: failed
  type: list
  sample: ['...', '...']
'''

import re
import time

from ansible.module_utils.f5_utils import AnsibleF5Client
from ansible.module_utils.f5_utils import AnsibleF5Parameters
from ansible.module_utils.f5_utils import HAS_F5SDK
from ansible.module_utils.f5_utils import F5ModuleError

try:
    from ansible.module_utils.f5_utils import run_commands
    HAS_CLI_TRANSPORT = True
except ImportError:
    HAS_CLI_TRANSPORT = False

from ansible.module_utils.six import string_types
from ansible.module_utils.network.common.parsing import FailedConditionsError
from ansible.module_utils.network.common.parsing import Conditional
from ansible.module_utils.network.common.utils import ComplexList
from ansible.module_utils.network.common.utils import to_list
from collections import deque

try:
    from ansible.module_utils.f5_utils import iControlUnexpectedHTTPError
except ImportError:
    HAS_F5SDK = False


class Parameters(AnsibleF5Parameters):
    returnables = ['stdout', 'stdout_lines', 'warnings']

    def to_return(self):
        result = {}
        for returnable in self.returnables:
            result[returnable] = getattr(self, returnable)
        result = self._filter_params(result)
        return result

    @property
    def commands(self):
        commands = deque(self._values['commands'])
        if self._values['transport'] != 'cli':
            commands.appendleft(
                'tmsh modify cli preference pager disabled'
            )
            commands = map(self._ensure_tmsh_prefix, list(commands))
        return list(commands)

    @property
    def user_commands(self):
        return map(self._ensure_tmsh_prefix, list(self._values['commands']))

    def _ensure_tmsh_prefix(self, cmd):
        cmd = cmd.strip()
        if cmd[0:5] != 'tmsh ':
            cmd = 'tmsh ' + cmd.strip()
        return cmd


class ModuleManager(object):
    def __init__(self, client):
        self.client = client
        self.want = Parameters(self.client.module.params)
        self.changes = Parameters()

    def _to_lines(self, stdout):
        lines = list()
        for item in stdout:
            if isinstance(item, string_types):
                item = str(item).split('\n')
            lines.append(item)
        return lines

    def _is_valid_mode(self, cmd):
        valid_configs = [
            'list', 'show',
            'modify cli preference pager disabled'
        ]
        if self.client.module.params['transport'] != 'cli':
            valid_configs = list(map(self.want._ensure_tmsh_prefix, valid_configs))
        if any(cmd.startswith(x) for x in valid_configs):
            return True
        return False

    def exec_module(self):
        result = dict()

        try:
            changed = self.execute()
        except iControlUnexpectedHTTPError as e:
            raise F5ModuleError(str(e))

        result.update(**self.changes.to_return())
        result.update(dict(changed=changed))
        return result

    def execute(self):
        warnings = list()
        changed = ('tmsh modify', 'tmsh create', 'tmsh delete')

        commands = self.parse_commands(warnings)

        wait_for = self.want.wait_for or list()
        retries = self.want.retries

        conditionals = [Conditional(c) for c in wait_for]

        if self.client.check_mode:
            return

        while retries > 0:
            if self.client.module.params['transport'] == 'cli' and HAS_CLI_TRANSPORT:
                responses = run_commands(self.client.module, self.want.commands)
            else:
                responses = self.execute_on_device(commands)

            for item in list(conditionals):
                if item(responses):
                    if self.want.match == 'any':
                        conditionals = list()
                        break
                    conditionals.remove(item)

            if not conditionals:
                break

            time.sleep(self.want.interval)
            retries -= 1
        else:
            failed_conditions = [item.raw for item in conditionals]
            errmsg = 'One or more conditional statements have not been satisfied'
            raise FailedConditionsError(errmsg, failed_conditions)

        self.changes = Parameters({
            'stdout': responses,
            'stdout_lines': self._to_lines(responses),
            'warnings': warnings
        })
        if any(x for x in self.want.user_commands if x.startswith(changed)):
            return True
        return False

    def parse_commands(self, warnings):
        results = []
        commands = list(deque(set(self.want.commands)))
        spec = dict(
            command=dict(key=True),
            output=dict(
                default='text',
                choices=['text', 'one-line']
            ),
        )

        transform = ComplexList(spec, self.client.module)
        commands = transform(commands)

        for index, item in enumerate(commands):
            if not self._is_valid_mode(item['command']) and self.client.module.params['transport'] != 'cli':
                warnings.append(
                    'Using "write" commands is not idempotent. You should use '
                    'a module that is specifically made for that. If such a '
                    'module does not exist, then please file a bug. The command '
                    'in question is "%s..."' % item['command'][0:40]
                )
            if item['output'] == 'one-line' and 'one-line' not in item['command']:
                item['command'] += ' one-line'
            elif item['output'] == 'text' and 'one-line' in item['command']:
                item['command'] = item['command'].replace('one-line', '')
            results.append(item)
        return results

    def execute_on_device(self, commands):
        responses = []
        escape_patterns = r'([$' + "'])"
        for item in to_list(commands):
            command = re.sub(escape_patterns, r'\\\1', item['command'])
            output = self.client.api.tm.util.bash.exec_cmd(
                'run',
                utilCmdArgs='-c "{0}"'.format(command)
            )
            if hasattr(output, 'commandResult'):
                responses.append(str(output.commandResult))
        return responses


class ArgumentSpec(object):
    def __init__(self):
        self.supports_check_mode = True
        self.argument_spec = dict(
            commands=dict(
                type='list',
                required=True
            ),
            wait_for=dict(
                type='list',
                aliases=['waitfor']
            ),
            match=dict(
                default='all',
                choices=['any', 'all']
            ),
            retries=dict(
                default=10,
                type='int'
            ),
            interval=dict(
                default=1,
                type='int'
            ),
            transport=dict(
                type='str',
                default='rest',
                choices=['cli', 'rest']
            )
        )
        self.f5_product_name = 'bigip'


def main():
    spec = ArgumentSpec()

    client = AnsibleF5Client(
        argument_spec=spec.argument_spec,
        supports_check_mode=spec.supports_check_mode,
        f5_product_name=spec.f5_product_name
    )

    if client.module.params['transport'] != 'cli' and not HAS_F5SDK:
        raise F5ModuleError("The python f5-sdk module is required to use the rest api")

    try:
        mm = ModuleManager(client)
        results = mm.exec_module()
        client.module.exit_json(**results)
    except (FailedConditionsError, AttributeError) as e:
        client.module.fail_json(msg=str(e))


if __name__ == '__main__':
    main()
