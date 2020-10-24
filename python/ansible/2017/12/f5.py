# -*- coding: utf-8 -*-
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


class ModuleDocFragment(object):
    # Standard F5 documentation fragment
    DOCUMENTATION = '''
options:
  password:
    description:
      - The password for the user account used to connect to the BIG-IP.
        You can omit this option if the environment variable C(F5_PASSWORD)
        is set.
    required: true
  server:
    description:
      - The BIG-IP host. You can omit this option if the environment
        variable C(F5_SERVER) is set.
    required: true
  server_port:
    description:
      - The BIG-IP server port. You can omit this option if the environment
        variable C(F5_SERVER_PORT) is set.
    default: 443
    version_added: 2.2
  user:
    description:
      - The username to connect to the BIG-IP with. This user must have
        administrative privileges on the device. You can omit this option
        if the environment variable C(F5_USER) is set.
    required: true
  validate_certs:
    description:
      - If C(no), SSL certificates will not be validated. Use this only
        on personally controlled sites using self-signed certificates.
        You can omit this option if the environment variable
        C(F5_VALIDATE_CERTS) is set.
    default: yes
    choices:
      - yes
      - no
    version_added: 2.0
notes:
  - For more information on using Ansible to manage F5 Networks devices see U(https://www.ansible.com/integrations/networks/f5).
'''
