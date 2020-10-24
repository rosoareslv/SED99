# Copyright (c) 2018 Ansible Project
# GNU General Public License v3.0+ (see COPYING or https://www.gnu.org/licenses/gpl-3.0.txt)
import json

import pytest

from ansible.modules.cloud.docker import docker_volume
from ansible.module_utils import docker_common

pytestmark = pytest.mark.usefixtures('patch_ansible_module')

TESTCASE_DOCKER_VOLUME = [
    {
        'name': 'daemon_config',
        'state': 'present'
    }
]


@pytest.mark.parametrize('patch_ansible_module', TESTCASE_DOCKER_VOLUME, indirect=['patch_ansible_module'])
def test_create_volume_on_invalid_docker_version(mocker, capfd):
    mocker.patch.object(docker_common, 'HAS_DOCKER_PY', True)
    mocker.patch.object(docker_common, 'docker_version', '1.8.0')

    with pytest.raises(SystemExit):
        docker_volume.main()

    out, dummy = capfd.readouterr()
    results = json.loads(out)
    assert results['failed']
    assert 'Error: docker / docker-py version is 1.8.0. Minimum version required is 1.10.0.' in results['msg']
