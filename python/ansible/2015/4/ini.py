# (c) 2012-2014, Michael DeHaan <michael.dehaan@gmail.com>
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

#############################################
from __future__ import (absolute_import, division, print_function)
__metaclass__ = type

import ast
import shlex
import re

from ansible import constants as C
from ansible.errors import *
from ansible.inventory.host import Host
from ansible.inventory.group import Group
from ansible.inventory.expand_hosts import detect_range
from ansible.inventory.expand_hosts import expand_hostname_range
from ansible.utils.unicode import to_unicode

class InventoryParser(object):
    """
    Host inventory for ansible.
    """

    def __init__(self, filename=C.DEFAULT_HOST_LIST):
        self.filename = filename
        with open(filename) as fh:
            self.lines = fh.readlines()
            self.groups = {}
            self.hosts = {}
            self._parse()

    def _parse(self):

        self._parse_base_groups()
        self._parse_group_children()
        self._add_allgroup_children()
        self._parse_group_variables()
        return self.groups

    @staticmethod
    def _parse_value(v):
        if "#" not in v:
            try:
                v = ast.literal_eval(v)
            # Using explicit exceptions.
            # Likely a string that literal_eval does not like. We wil then just set it.
            except ValueError:
                # For some reason this was thought to be malformed.
                pass
            except SyntaxError:
                # Is this a hash with an equals at the end?
                pass
        return to_unicode(v, nonstring='passthru', errors='strict')

    # [webservers]
    # alpha
    # beta:2345
    # gamma sudo=True user=root
    # delta asdf=jkl favcolor=red

    def _add_allgroup_children(self):

        for group in self.groups.values():
            if group.depth == 0 and group.name != 'all':
                self.groups['all'].add_child_group(group)


    def _parse_base_groups(self):
        # FIXME: refactor

        ungrouped = Group(name='ungrouped')
        all = Group(name='all')
        all.add_child_group(ungrouped)

        self.groups = dict(all=all, ungrouped=ungrouped)
        active_group_name = 'ungrouped'

        for line in self.lines:
            line = self._before_comment(line).strip()
            if line.startswith("[") and line.endswith("]"):
                active_group_name = line.replace("[","").replace("]","")
                if ":vars" in line or ":children" in line:
                    active_group_name = active_group_name.rsplit(":", 1)[0]
                    if active_group_name not in self.groups:
                        new_group = self.groups[active_group_name] = Group(name=active_group_name)
                    active_group_name = None
                elif active_group_name not in self.groups:
                    new_group = self.groups[active_group_name] = Group(name=active_group_name)
            elif line.startswith(";") or line == '':
                pass
            elif active_group_name:
                tokens = shlex.split(line)
                if len(tokens) == 0:
                    continue
                hostname = tokens[0]
                port = C.DEFAULT_REMOTE_PORT
                # Three cases to check:
                # 0. A hostname that contains a range pesudo-code and a port
                # 1. A hostname that contains just a port
                if hostname.count(":") > 1:
                    # Possible an IPv6 address, or maybe a host line with multiple ranges
                    # IPv6 with Port  XXX:XXX::XXX.port
                    # FQDN            foo.example.com
                    if hostname.count(".") == 1:
                        (hostname, port) = hostname.rsplit(".", 1)
                elif ("[" in hostname and
                    "]" in hostname and
                    ":" in hostname and
                    (hostname.rindex("]") < hostname.rindex(":")) or
                    ("]" not in hostname and ":" in hostname)):
                        (hostname, port) = hostname.rsplit(":", 1)

                hostnames = []
                if detect_range(hostname):
                    hostnames = expand_hostname_range(hostname)
                else:
                    hostnames = [hostname]

                for hn in hostnames:
                    host = None
                    if hn in self.hosts:
                        host = self.hosts[hn]
                    else:
                        host = Host(name=hn, port=port)
                        self.hosts[hn] = host
                    if len(tokens) > 1:
                        for t in tokens[1:]:
                            if t.startswith('#'):
                                break
                            try:
                                (k,v) = t.split("=", 1)
                            except ValueError, e:
                                raise AnsibleError("Invalid ini entry in %s: %s - %s" % (self.filename, t, str(e)))
                            if k == 'ansible_ssh_host':
                                host.ipv4_address = self._parse_value(v)
                            else:
                                host.set_variable(k, self._parse_value(v))
                    self.groups[active_group_name].add_host(host)

    # [southeast:children]
    # atlanta
    # raleigh

    def _parse_group_children(self):
        group = None

        for line in self.lines:
            line = line.strip()
            if line is None or line == '':
                continue
            if line.startswith("[") and ":children]" in line:
                line = line.replace("[","").replace(":children]","")
                group = self.groups.get(line, None)
                if group is None:
                    group = self.groups[line] = Group(name=line)
            elif line.startswith("#") or line.startswith(";"):
                pass
            elif line.startswith("["):
                group = None
            elif group:
                kid_group = self.groups.get(line, None)
                if kid_group is None:
                    raise AnsibleError("child group is not defined: (%s)" % line)
                else:
                    group.add_child_group(kid_group)


    # [webservers:vars]
    # http_port=1234
    # maxRequestsPerChild=200

    def _parse_group_variables(self):
        group = None
        for line in self.lines:
            line = line.strip()
            if line.startswith("[") and ":vars]" in line:
                line = line.replace("[","").replace(":vars]","")
                group = self.groups.get(line, None)
                if group is None:
                    raise AnsibleError("can't add vars to undefined group: %s" % line)
            elif line.startswith("#") or line.startswith(";"):
                pass
            elif line.startswith("["):
                group = None
            elif line == '':
                pass
            elif group:
                if "=" not in line:
                    raise AnsibleError("variables assigned to group must be in key=value form")
                else:
                    (k, v) = [e.strip() for e in line.split("=", 1)]
                    group.set_variable(k, self._parse_value(v))

    def get_host_variables(self, host):
        return {}

    def _before_comment(self, msg):
        ''' what's the part of a string before a comment? '''
        msg = msg.replace("\#","**NOT_A_COMMENT**")
        msg = msg.split("#")[0]
        msg = msg.replace("**NOT_A_COMMENT**","#")
        return msg

