#!/usr/bin/python
# -*- coding: utf-8 -*-

# GNU General Public License v3.0+ (see COPYING or https://www.gnu.org/licenses/gpl-3.0.txt)

from __future__ import absolute_import, division, print_function
__metaclass__ = type

ANSIBLE_METADATA = {'metadata_version': '1.1',
                    'status': ['preview'],
                    'supported_by': 'community'}

DOCUMENTATION = r'''
---
module: msc_label
short_description: Manage labels
description:
- Manage labels on Cisco ACI Multi-Site.
author:
- Dag Wieers (@dagwieers)
version_added: '2.8'
options:
  label_id:
    description:
    - The ID of the label.
    type: str
  label:
    description:
    - The name of the label.
    - Alternative to the name, you can use C(label_id).
    type: str
    required: yes
    aliases: [ label_name, name ]
  type:
    description:
    - The type of the label.
    type: str
    choices: [ site ]
    default: site
  state:
    description:
    - Use C(present) or C(absent) for adding or removing.
    - Use C(query) for listing an object or multiple objects.
    type: str
    choices: [ absent, present, query ]
    default: present
extends_documentation_fragment: msc
'''

EXAMPLES = r'''
- name: Add a new label
  msc_label:
    host: msc_host
    username: admin
    password: SomeSecretPassword
    label: Belgium
    type: site
    state: present
  delegate_to: localhost

- name: Remove a label
  msc_label:
    host: msc_host
    username: admin
    password: SomeSecretPassword
    label: Belgium
    state: absent
  delegate_to: localhost

- name: Query a label
  msc_label:
    host: msc_host
    username: admin
    password: SomeSecretPassword
    label: Belgium
    state: query
  delegate_to: localhost
  register: query_result

- name: Query all labels
  msc_label:
    host: msc_host
    username: admin
    password: SomeSecretPassword
    state: query
  delegate_to: localhost
  register: query_result
'''

RETURN = r'''
'''

from ansible.module_utils.basic import AnsibleModule
from ansible.module_utils.network.aci.msc import MSCModule, msc_argument_spec, issubset


def main():
    argument_spec = msc_argument_spec()
    argument_spec.update(
        label=dict(type='str', required=False, aliases=['name', 'label_name']),
        label_id=dict(type='str', required=False),
        type=dict(type='str', default='site', choices=['site']),
        state=dict(type='str', default='present', choices=['absent', 'present', 'query']),
    )

    module = AnsibleModule(
        argument_spec=argument_spec,
        supports_check_mode=True,
        required_if=[
            ['state', 'absent', ['label']],
            ['state', 'present', ['label']],
        ],
    )

    label = module.params['label']
    label_id = module.params['label_id']
    label_type = module.params['type']
    state = module.params['state']

    msc = MSCModule(module)

    path = 'labels'

    # Query for existing object(s)
    if label_id is None and label is None:
        msc.existing = msc.query_objs(path)
    elif label_id is None:
        msc.existing = msc.get_obj(path, displayName=label)
        if msc.existing:
            label_id = msc.existing['id']
    elif label is None:
        msc.existing = msc.get_obj(path, id=label_id)
    else:
        msc.existing = msc.get_obj(path, id=label_id)
        existing_by_name = msc.get_obj(path, displayName=label)
        if existing_by_name and label_id != existing_by_name['id']:
            msc.fail_json(msg="Provided label '{0}' with id '{1}' does not match existing id '{2}'.".format(label, label_id, existing_by_name['id']))

    # If we found an existing object, continue with it
    if label_id:
        path = 'labels/{id}'.format(id=label_id)

    if state == 'query':
        pass

    elif state == 'absent':
        msc.previous = msc.existing
        if msc.existing:
            if module.check_mode:
                msc.existing = {}
            else:
                msc.existing = msc.request(path, method='DELETE')

    elif state == 'present':
        msc.previous = msc.existing

        payload = dict(
            id=label_id,
            displayName=label,
            type=label_type,
        )

        msc.sanitize(payload, collate=True)

        if msc.existing:
            if not issubset(msc.sent, msc.existing):
                if module.check_mode:
                    msc.existing = msc.proposed
                else:
                    msc.existing = msc.request(path, method='PUT', data=msc.sent)
        else:
            if module.check_mode:
                msc.existing = msc.proposed
            else:
                msc.existing = msc.request(path, method='POST', data=msc.sent)

    msc.exit_json()


if __name__ == "__main__":
    main()
