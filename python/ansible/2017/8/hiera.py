# (c) 2017, Juan Manuel Parrilla <jparrill@redhat.com>
#
# This file is part of Ansible
#
# Ansible is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Ansible is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Ansible.  If not, see <http://www.gnu.org/licenses/>.
'''
DOCUMENTATION:
    author:
        - Juan Manuel Parrilla (@jparrill)
    lookup: hiera
    version_added: "2.4"
    short_description: get info from hiera data
    description:
        - Retrieves data from an Puppetmaster node using
          Hiera as ENC
    options:
        _hiera_key:
            description:
                - The list of keys to lookup on the Puppetmaster
            type: list
            element_type: string
            required: True
        _bin_file:
            description:
                - Binary file to execute Hiera
            default: '/usr/bin/hiera'
            env_vars:
                - name: ANSIBLE_HIERA_BIN
        _hierarchy_file:
            description:
                - File that describes the hierarchy of Hiera
            default: '/etc/hiera.yaml'
            env_vars:
                - name: ANSIBLE_HIERA_CFG
EXAMPLES:
    All this examples depends on hiera.yml  that describes the
    hierarchy

    - name: "a value from Hiera 'DB'"
      debug: msg={{ lookup('hiera', 'foo') }}

    - name: "a value from a Hiera 'DB' on other environment"
      debug: msg={{ lookup('hiera', 'foo environment=production') }}

    - name: "a value from a Hiera 'DB' for a concrete node"
      debug: msg={{ lookup('hiera', 'foo fqdn=puppet01.localdomain') }}
RETURN:
    _list:
        description:
            - a value associated with input key
        type: strings
'''
from __future__ import (absolute_import, division, print_function)
__metaclass__ = type

import os

from ansible.plugins.lookup import LookupBase
from ansible.utils.cmd_functions import run_cmd


ANSIBLE_HIERA_CFG = os.getenv('ANSIBLE_HIERA_CFG', '/etc/hiera.yaml')
ANSIBLE_HIERA_BIN = os.getenv('ANSIBLE_HIERA_BIN', '/usr/bin/hiera')


class Hiera(object):
    def get(self, hiera_key):
        pargs = [ANSIBLE_HIERA_BIN]
        pargs.extend(['-c', ANSIBLE_HIERA_CFG])

        pargs.extend(hiera_key)

        rc, output, err = run_cmd("{} -c {} {}".format(
            ANSIBLE_HIERA_BIN, ANSIBLE_HIERA_CFG, hiera_key[0]))

        return output.strip()


class LookupModule(LookupBase):
    def run(self, terms, variables=''):
        hiera = Hiera()
        ret = []

        ret.append(hiera.get(terms))
        return ret
