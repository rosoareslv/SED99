"""Test Home Assistant yaml loader."""
import io
import unittest
import os
import tempfile
from homeassistant.exceptions import HomeAssistantError
from homeassistant.util import yaml
import homeassistant.config as config_util
from tests.common import get_test_config_dir


class TestYaml(unittest.TestCase):
    """Test util.yaml loader."""

    def test_simple_list(self):
        """Test simple list."""
        conf = "config:\n  - simple\n  - list"
        with io.StringIO(conf) as f:
            doc = yaml.yaml.safe_load(f)
        assert doc['config'] == ["simple", "list"]

    def test_simple_dict(self):
        """Test simple dict."""
        conf = "key: value"
        with io.StringIO(conf) as f:
            doc = yaml.yaml.safe_load(f)
        assert doc['key'] == 'value'

    def test_duplicate_key(self):
        """Test simple dict."""
        conf = "key: thing1\nkey: thing2"
        try:
            with io.StringIO(conf) as f:
                yaml.yaml.safe_load(f)
        except Exception:
            pass
        else:
            assert 0

    def test_enviroment_variable(self):
        """Test config file with enviroment variable."""
        os.environ["PASSWORD"] = "secret_password"
        conf = "password: !env_var PASSWORD"
        with io.StringIO(conf) as f:
            doc = yaml.yaml.safe_load(f)
        assert doc['password'] == "secret_password"
        del os.environ["PASSWORD"]

    def test_invalid_enviroment_variable(self):
        """Test config file with no enviroment variable sat."""
        conf = "password: !env_var PASSWORD"
        try:
            with io.StringIO(conf) as f:
                yaml.yaml.safe_load(f)
        except Exception:
            pass
        else:
            assert 0

    def test_include_yaml(self):
        """Test include yaml."""
        with tempfile.NamedTemporaryFile() as include_file:
            include_file.write(b"value")
            include_file.seek(0)
            conf = "key: !include {}".format(include_file.name)
            with io.StringIO(conf) as f:
                doc = yaml.yaml.safe_load(f)
                assert doc["key"] == "value"

    def test_include_dir_list(self):
        """Test include dir list yaml."""
        with tempfile.TemporaryDirectory() as include_dir:
            file_1 = tempfile.NamedTemporaryFile(dir=include_dir,
                                                 suffix=".yaml", delete=False)
            file_1.write(b"one")
            file_1.close()
            file_2 = tempfile.NamedTemporaryFile(dir=include_dir,
                                                 suffix=".yaml", delete=False)
            file_2.write(b"two")
            file_2.close()
            conf = "key: !include_dir_list {}".format(include_dir)
            with io.StringIO(conf) as f:
                doc = yaml.yaml.safe_load(f)
                assert sorted(doc["key"]) == sorted(["one", "two"])

    def test_include_dir_named(self):
        """Test include dir named yaml."""
        with tempfile.TemporaryDirectory() as include_dir:
            file_1 = tempfile.NamedTemporaryFile(dir=include_dir,
                                                 suffix=".yaml", delete=False)
            file_1.write(b"one")
            file_1.close()
            file_2 = tempfile.NamedTemporaryFile(dir=include_dir,
                                                 suffix=".yaml", delete=False)
            file_2.write(b"two")
            file_2.close()
            conf = "key: !include_dir_named {}".format(include_dir)
            correct = {}
            correct[os.path.splitext(os.path.basename(file_1.name))[0]] = "one"
            correct[os.path.splitext(os.path.basename(file_2.name))[0]] = "two"
            with io.StringIO(conf) as f:
                doc = yaml.yaml.safe_load(f)
                assert doc["key"] == correct

    def test_include_dir_merge_list(self):
        """Test include dir merge list yaml."""
        with tempfile.TemporaryDirectory() as include_dir:
            file_1 = tempfile.NamedTemporaryFile(dir=include_dir,
                                                 suffix=".yaml", delete=False)
            file_1.write(b"- one")
            file_1.close()
            file_2 = tempfile.NamedTemporaryFile(dir=include_dir,
                                                 suffix=".yaml", delete=False)
            file_2.write(b"- two\n- three")
            file_2.close()
            conf = "key: !include_dir_merge_list {}".format(include_dir)
            with io.StringIO(conf) as f:
                doc = yaml.yaml.safe_load(f)
                assert sorted(doc["key"]) == sorted(["one", "two", "three"])

    def test_include_dir_merge_named(self):
        """Test include dir merge named yaml."""
        with tempfile.TemporaryDirectory() as include_dir:
            file_1 = tempfile.NamedTemporaryFile(dir=include_dir,
                                                 suffix=".yaml", delete=False)
            file_1.write(b"key1: one")
            file_1.close()
            file_2 = tempfile.NamedTemporaryFile(dir=include_dir,
                                                 suffix=".yaml", delete=False)
            file_2.write(b"key2: two\nkey3: three")
            file_2.close()
            conf = "key: !include_dir_merge_named {}".format(include_dir)
            with io.StringIO(conf) as f:
                doc = yaml.yaml.safe_load(f)
                assert doc["key"] == {
                    "key1": "one",
                    "key2": "two",
                    "key3": "three"
                }


def load_yaml(fname, string):
    """Write a string to file and return the parsed yaml."""
    with open(fname, 'w') as file:
        file.write(string)
    return config_util.load_yaml_config_file(fname)


class FakeKeyring():
    """Fake a keyring class."""

    def __init__(self, secrets_dict):
        """Store keyring dictionary."""
        self._secrets = secrets_dict

    # pylint: disable=protected-access
    def get_password(self, domain, name):
        """Retrieve password."""
        assert domain == yaml._SECRET_NAMESPACE
        return self._secrets.get(name)


class TestSecrets(unittest.TestCase):
    """Test the secrets parameter in the yaml utility."""

    def setUp(self):  # pylint: disable=invalid-name
        """Create & load secrets file."""
        config_dir = get_test_config_dir()
        yaml.clear_secret_cache()
        self._yaml_path = os.path.join(config_dir,
                                       config_util.YAML_CONFIG_FILE)
        self._secret_path = os.path.join(config_dir, yaml._SECRET_YAML)
        self._sub_folder_path = os.path.join(config_dir, 'subFolder')
        if not os.path.exists(self._sub_folder_path):
            os.makedirs(self._sub_folder_path)
        self._unrelated_path = os.path.join(config_dir, 'unrelated')
        if not os.path.exists(self._unrelated_path):
            os.makedirs(self._unrelated_path)

        load_yaml(self._secret_path,
                  'http_pw: pwhttp\n'
                  'comp1_un: un1\n'
                  'comp1_pw: pw1\n'
                  'stale_pw: not_used\n'
                  'logger: debug\n')
        self._yaml = load_yaml(self._yaml_path,
                               'http:\n'
                               '  api_password: !secret http_pw\n'
                               'component:\n'
                               '  username: !secret comp1_un\n'
                               '  password: !secret comp1_pw\n'
                               '')

    def tearDown(self):  # pylint: disable=invalid-name
        """Clean up secrets."""
        yaml.clear_secret_cache()
        for path in [self._yaml_path, self._secret_path,
                     os.path.join(self._sub_folder_path, 'sub.yaml'),
                     os.path.join(self._sub_folder_path, yaml._SECRET_YAML),
                     os.path.join(self._unrelated_path, yaml._SECRET_YAML)]:
            if os.path.isfile(path):
                os.remove(path)

    def test_secrets_from_yaml(self):
        """Did secrets load ok."""
        expected = {'api_password': 'pwhttp'}
        self.assertEqual(expected, self._yaml['http'])

        expected = {
            'username': 'un1',
            'password': 'pw1'}
        self.assertEqual(expected, self._yaml['component'])

    def test_secrets_from_parent_folder(self):
        """Test loading secrets from parent foler."""
        expected = {'api_password': 'pwhttp'}
        self._yaml = load_yaml(os.path.join(self._sub_folder_path, 'sub.yaml'),
                               'http:\n'
                               '  api_password: !secret http_pw\n'
                               'component:\n'
                               '  username: !secret comp1_un\n'
                               '  password: !secret comp1_pw\n'
                               '')

        self.assertEqual(expected, self._yaml['http'])

    def test_secret_overrides_parent(self):
        """Test loading current directory secret overrides the parent."""
        expected = {'api_password': 'override'}
        load_yaml(os.path.join(self._sub_folder_path, yaml._SECRET_YAML),
                  'http_pw: override')
        self._yaml = load_yaml(os.path.join(self._sub_folder_path, 'sub.yaml'),
                               'http:\n'
                               '  api_password: !secret http_pw\n'
                               'component:\n'
                               '  username: !secret comp1_un\n'
                               '  password: !secret comp1_pw\n'
                               '')

        self.assertEqual(expected, self._yaml['http'])

    def test_secrets_from_unrelated_fails(self):
        """Test loading secrets from unrelated folder fails."""
        load_yaml(os.path.join(self._unrelated_path, yaml._SECRET_YAML),
                  'test: failure')
        with self.assertRaises(HomeAssistantError):
            load_yaml(os.path.join(self._sub_folder_path, 'sub.yaml'),
                      'http:\n'
                      '  api_password: !secret test')

    def test_secrets_keyring(self):
        """Test keyring fallback & get_password."""
        yaml.keyring = None  # Ensure its not there
        yaml_str = 'http:\n  api_password: !secret http_pw_keyring'
        with self.assertRaises(yaml.HomeAssistantError):
            load_yaml(self._yaml_path, yaml_str)

        yaml.keyring = FakeKeyring({'http_pw_keyring': 'yeah'})
        _yaml = load_yaml(self._yaml_path, yaml_str)
        self.assertEqual({'http': {'api_password': 'yeah'}}, _yaml)

    def test_secrets_logger_removed(self):
        """Ensure logger: debug was removed."""
        with self.assertRaises(yaml.HomeAssistantError):
            load_yaml(self._yaml_path, 'api_password: !secret logger')
