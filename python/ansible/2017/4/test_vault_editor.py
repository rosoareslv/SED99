# (c) 2014, James Tanner <tanner.jc@gmail.com>
# (c) 2014, James Cammarata, <jcammarata@ansible.com>
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

# Make coding more python3-ish
from __future__ import (absolute_import, division, print_function)
__metaclass__ = type

import os
import tempfile
from nose.plugins.skip import SkipTest

from ansible.compat.tests import unittest
from ansible.compat.tests.mock import patch

from ansible import errors
from ansible.parsing import vault
from ansible.module_utils._text import to_bytes, to_text


# Counter import fails for 2.0.1, requires >= 2.6.1 from pip
try:
    from Crypto.Util import Counter
    HAS_COUNTER = True
except ImportError:
    HAS_COUNTER = False

# KDF import fails for 2.0.1, requires >= 2.6.1 from pip
try:
    from Crypto.Protocol.KDF import PBKDF2
    HAS_PBKDF2 = True
except ImportError:
    HAS_PBKDF2 = False

# AES IMPORTS
try:
    from Crypto.Cipher import AES as AES
    HAS_AES = True
except ImportError:
    HAS_AES = False

v10_data = """$ANSIBLE_VAULT;1.0;AES
53616c7465645f5fd0026926a2d415a28a2622116273fbc90e377225c12a347e1daf4456d36a77f9
9ad98d59f61d06a4b66718d855f16fb7bdfe54d1ec8aeaa4d06c2dc1fa630ae1846a029877f0eeb1
83c62ffb04c2512995e815de4b4d29ed"""

v11_data = """$ANSIBLE_VAULT;1.1;AES256
62303130653266653331306264616235333735323636616539316433666463323964623162386137
3961616263373033353631316333623566303532663065310a393036623466376263393961326530
64336561613965383835646464623865663966323464653236343638373165343863623638316664
3631633031323837340a396530313963373030343933616133393566366137363761373930663833
3739"""


class TestVaultEditor(unittest.TestCase):

    def setUp(self):
        self._test_dir = None

    def tearDown(self):
        if self._test_dir:
            pass
            #shutil.rmtree(self._test_dir)
        self._test_dir = None

    def test_methods_exist(self):
        v = vault.VaultEditor(None)
        slots = ['create_file',
                 'decrypt_file',
                 'edit_file',
                 'encrypt_file',
                 'rekey_file',
                 'read_data',
                 'write_data']
        for slot in slots:
            assert hasattr(v, slot), "VaultLib is missing the %s method" % slot

    def _create_test_dir(self):
        suffix = '_ansible_unit_test_%s_' % (self.__class__.__name__)
        return tempfile.mkdtemp(suffix=suffix)

    def _create_file(self, test_dir, name, content=None, symlink=False):
        file_path = os.path.join(test_dir, name)
        opened_file = open(file_path, 'wb')
        if content:
            opened_file.write(content)
        opened_file.close()
        return file_path

    @patch('ansible.parsing.vault.call')
    def test_edit_file_helper_empty_target(self, mock_sp_call):
        self._test_dir = self._create_test_dir()

        src_contents = to_bytes("some info in a file\nyup.")
        src_file_path = self._create_file(self._test_dir, 'src_file', content=src_contents)

        mock_sp_call.side_effect = self._faux_command
        ve = vault.VaultEditor('password')

        b_ciphertext = ve._edit_file_helper(src_file_path)

        self.assertNotEqual(src_contents, b_ciphertext)

    @patch('ansible.parsing.vault.call')
    def test_edit_file_helper_call_exception(self, mock_sp_call):
        self._test_dir = self._create_test_dir()

        src_contents = to_bytes("some info in a file\nyup.")
        src_file_path = self._create_file(self._test_dir, 'src_file', content=src_contents)

        error_txt = 'calling editor raised an exception'
        mock_sp_call.side_effect = errors.AnsibleError(error_txt)

        ve = vault.VaultEditor('password')

        self.assertRaisesRegexp(errors.AnsibleError,
                                error_txt,
                                ve._edit_file_helper,
                                src_file_path)

    @patch('ansible.parsing.vault.call')
    def test_edit_file_helper_symlink_target(self, mock_sp_call):
        self._test_dir = self._create_test_dir()

        src_file_contents = to_bytes("some info in a file\nyup.")
        src_file_path = self._create_file(self._test_dir, 'src_file', content=src_file_contents)

        src_file_link_path = os.path.join(self._test_dir, 'a_link_to_dest_file')

        os.symlink(src_file_path, src_file_link_path)

        mock_sp_call.side_effect = self._faux_command
        ve = vault.VaultEditor('password')

        b_ciphertext = ve._edit_file_helper(src_file_link_path)

        self.assertNotEqual(src_file_contents, b_ciphertext,
                            'b_ciphertext should be encrypted and not equal to src_contents')

    def _faux_editor(self, editor_args, new_src_contents=None):
        if editor_args[0] == 'shred':
            return

        tmp_path = editor_args[-1]

        # simulate the tmp file being editted
        tmp_file = open(tmp_path, 'wb')
        if new_src_contents:
            tmp_file.write(new_src_contents)
        tmp_file.close()

    def _faux_command(self, tmp_path):
        pass

    @patch('ansible.parsing.vault.call')
    def test_edit_file_helper_no_change(self, mock_sp_call):
        self._test_dir = self._create_test_dir()

        src_file_contents = to_bytes("some info in a file\nyup.")
        src_file_path = self._create_file(self._test_dir, 'src_file', content=src_file_contents)

        # editor invocation doesnt change anything
        def faux_editor(editor_args):
            self._faux_editor(editor_args, src_file_contents)

        mock_sp_call.side_effect = faux_editor
        ve = vault.VaultEditor('password')

        ve._edit_file_helper(src_file_path, existing_data=src_file_contents)

        new_target_file = open(src_file_path, 'rb')
        new_target_file_contents = new_target_file.read()
        self.assertEqual(src_file_contents, new_target_file_contents)

    def _assert_file_is_encrypted(self, vault_editor, src_file_path, src_contents):
        new_src_file = open(src_file_path, 'rb')
        new_src_file_contents = new_src_file.read()

        # TODO: assert that it is encrypted
        self.assertTrue(vault.is_encrypted(new_src_file_contents))

        src_file_plaintext = vault_editor.vault.decrypt(new_src_file_contents)

        # the plaintext should not be encrypted
        self.assertFalse(vault.is_encrypted(src_file_plaintext))

        # and the new plaintext should match the original
        self.assertEqual(src_file_plaintext, src_contents)

    def _assert_file_is_link(self, src_file_link_path, src_file_path):
        self.assertTrue(os.path.islink(src_file_link_path),
                        'The dest path (%s) should be a symlink to (%s) but is not' % (src_file_link_path, src_file_path))

    def test_rekey_file(self):
        self._test_dir = self._create_test_dir()

        src_file_contents = to_bytes("some info in a file\nyup.")
        src_file_path = self._create_file(self._test_dir, 'src_file', content=src_file_contents)

        ve = vault.VaultEditor('password')
        ve.encrypt_file(src_file_path)

        new_password = 'password2:electricbugaloo'
        ve.rekey_file(src_file_path, new_password)

        new_ve = vault.VaultEditor(new_password)
        self._assert_file_is_encrypted(new_ve, src_file_path, src_file_contents)

    def test_rekey_file_no_new_password(self):
        self._test_dir = self._create_test_dir()

        src_file_contents = to_bytes("some info in a file\nyup.")
        src_file_path = self._create_file(self._test_dir, 'src_file', content=src_file_contents)

        ve = vault.VaultEditor('password')
        ve.encrypt_file(src_file_path)

        self.assertRaisesRegexp(errors.AnsibleError,
                                'The value for the new_password to rekey',
                                ve.rekey_file,
                                src_file_path,
                                None)

    def test_rekey_file_not_encrypted(self):
        self._test_dir = self._create_test_dir()

        src_file_contents = to_bytes("some info in a file\nyup.")
        src_file_path = self._create_file(self._test_dir, 'src_file', content=src_file_contents)

        ve = vault.VaultEditor('password')

        new_password = 'password2:electricbugaloo'
        self.assertRaisesRegexp(errors.AnsibleError,
                                'input is not vault encrypted data',
                                ve.rekey_file,
                                src_file_path, new_password)

    def test_plaintext(self):
        self._test_dir = self._create_test_dir()

        src_file_contents = to_bytes("some info in a file\nyup.")
        src_file_path = self._create_file(self._test_dir, 'src_file', content=src_file_contents)

        ve = vault.VaultEditor('password')
        ve.encrypt_file(src_file_path)

        res = ve.plaintext(src_file_path)
        self.assertEquals(src_file_contents, res)

    def test_plaintext_not_encrypted(self):
        self._test_dir = self._create_test_dir()

        src_file_contents = to_bytes("some info in a file\nyup.")
        src_file_path = self._create_file(self._test_dir, 'src_file', content=src_file_contents)

        ve = vault.VaultEditor('password')
        self.assertRaisesRegexp(errors.AnsibleError,
                                'input is not vault encrypted data',
                                ve.plaintext,
                                src_file_path)

    def test_encrypt_file(self):
        self._test_dir = self._create_test_dir()
        src_file_contents = to_bytes("some info in a file\nyup.")
        src_file_path = self._create_file(self._test_dir, 'src_file', content=src_file_contents)

        ve = vault.VaultEditor('password')
        ve.encrypt_file(src_file_path)

        self._assert_file_is_encrypted(ve, src_file_path, src_file_contents)

    def test_encrypt_file_symlink(self):
        self._test_dir = self._create_test_dir()

        src_file_contents = to_bytes("some info in a file\nyup.")
        src_file_path = self._create_file(self._test_dir, 'src_file', content=src_file_contents)

        src_file_link_path = os.path.join(self._test_dir, 'a_link_to_dest_file')
        os.symlink(src_file_path, src_file_link_path)

        ve = vault.VaultEditor('password')
        ve.encrypt_file(src_file_link_path)

        self._assert_file_is_encrypted(ve, src_file_path, src_file_contents)
        self._assert_file_is_encrypted(ve, src_file_link_path, src_file_contents)

        self._assert_file_is_link(src_file_link_path, src_file_path)

    @patch('ansible.parsing.vault.call')
    def test_edit_file(self, mock_sp_call):
        self._test_dir = self._create_test_dir()
        src_contents = to_bytes("some info in a file\nyup.")

        src_file_path = self._create_file(self._test_dir, 'src_file', content=src_contents)

        new_src_contents = to_bytes("The info is different now.")

        def faux_editor(editor_args):
            self._faux_editor(editor_args, new_src_contents)

        mock_sp_call.side_effect = faux_editor

        ve = vault.VaultEditor('password')

        ve.encrypt_file(src_file_path)
        ve.edit_file(src_file_path)

        new_src_file = open(src_file_path, 'rb')
        new_src_file_contents = new_src_file.read()

        src_file_plaintext = ve.vault.decrypt(new_src_file_contents)
        self.assertEqual(src_file_plaintext, new_src_contents)

        new_stat = os.stat(src_file_path)
        print(new_stat)

    @patch('ansible.parsing.vault.call')
    def test_edit_file_symlink(self, mock_sp_call):
        self._test_dir = self._create_test_dir()
        src_contents = to_bytes("some info in a file\nyup.")

        src_file_path = self._create_file(self._test_dir, 'src_file', content=src_contents)

        new_src_contents = to_bytes("The info is different now.")

        def faux_editor(editor_args):
            self._faux_editor(editor_args, new_src_contents)

        mock_sp_call.side_effect = faux_editor

        ve = vault.VaultEditor('password')

        ve.encrypt_file(src_file_path)

        src_file_link_path = os.path.join(self._test_dir, 'a_link_to_dest_file')

        os.symlink(src_file_path, src_file_link_path)

        ve.edit_file(src_file_link_path)

        new_src_file = open(src_file_path, 'rb')
        new_src_file_contents = new_src_file.read()

        src_file_plaintext = ve.vault.decrypt(new_src_file_contents)

        self._assert_file_is_link(src_file_link_path, src_file_path)

        self.assertEqual(src_file_plaintext, new_src_contents)

        #self.assertEqual(src_file_plaintext, new_src_contents,
        #                 'The decrypted plaintext of the editted file is not the expected contents.')

    @patch('ansible.parsing.vault.call')
    def test_edit_file_not_encrypted(self, mock_sp_call):
        self._test_dir = self._create_test_dir()
        src_contents = to_bytes("some info in a file\nyup.")

        src_file_path = self._create_file(self._test_dir, 'src_file', content=src_contents)

        new_src_contents = to_bytes("The info is different now.")

        def faux_editor(editor_args):
            self._faux_editor(editor_args, new_src_contents)

        mock_sp_call.side_effect = faux_editor

        ve = vault.VaultEditor('password')
        self.assertRaisesRegexp(errors.AnsibleError,
                                'input is not vault encrypted data',
                                ve.edit_file,
                                src_file_path)

    def test_create_file_exists(self):
        self._test_dir = self._create_test_dir()
        src_contents = to_bytes("some info in a file\nyup.")
        src_file_path = self._create_file(self._test_dir, 'src_file', content=src_contents)

        ve = vault.VaultEditor('password')
        self.assertRaisesRegexp(errors.AnsibleError,
                                'please use .edit. instead',
                                ve.create_file,
                                src_file_path)

    def test_decrypt_file_exception(self):
        self._test_dir = self._create_test_dir()
        src_contents = to_bytes("some info in a file\nyup.")
        src_file_path = self._create_file(self._test_dir, 'src_file', content=src_contents)

        ve = vault.VaultEditor('password')
        self.assertRaisesRegexp(errors.AnsibleError,
                                'input is not vault encrypted data',
                                ve.decrypt_file,
                                src_file_path)

    @patch.object(vault.VaultEditor, '_editor_shell_command')
    def test_create_file(self, mock_editor_shell_command):

        def sc_side_effect(filename):
            return ['touch', filename]
        mock_editor_shell_command.side_effect = sc_side_effect

        tmp_file = tempfile.NamedTemporaryFile()
        os.unlink(tmp_file.name)

        ve = vault.VaultEditor("ansible")
        ve.create_file(tmp_file.name)

        self.assertTrue(os.path.exists(tmp_file.name))

    def test_decrypt_1_0(self):
        # Skip testing decrypting 1.0 files if we don't have access to AES, KDF or Counter.
        if not HAS_AES or not HAS_COUNTER or not HAS_PBKDF2:
            raise SkipTest

        v10_file = tempfile.NamedTemporaryFile(delete=False)
        with v10_file as f:
            f.write(to_bytes(v10_data))

        ve = vault.VaultEditor("ansible")

        # make sure the password functions for the cipher
        error_hit = False
        try:
            ve.decrypt_file(v10_file.name)
        except errors.AnsibleError:
            error_hit = True

        # verify decrypted content
        f = open(v10_file.name, "rb")
        fdata = to_text(f.read())
        f.close()

        os.unlink(v10_file.name)

        assert error_hit is False, "error decrypting 1.0 file"
        self.assertEquals(fdata.strip(), "foo")
        assert fdata.strip() == "foo", "incorrect decryption of 1.0 file: %s" % fdata.strip()

    def test_decrypt_1_1(self):
        if not HAS_AES or not HAS_COUNTER or not HAS_PBKDF2:
            raise SkipTest

        v11_file = tempfile.NamedTemporaryFile(delete=False)
        with v11_file as f:
            f.write(to_bytes(v11_data))

        ve = vault.VaultEditor("ansible")

        # make sure the password functions for the cipher
        error_hit = False
        try:
            ve.decrypt_file(v11_file.name)
        except errors.AnsibleError:
            error_hit = True

        # verify decrypted content
        f = open(v11_file.name, "rb")
        fdata = to_text(f.read())
        f.close()

        os.unlink(v11_file.name)

        assert error_hit is False, "error decrypting 1.0 file"
        assert fdata.strip() == "foo", "incorrect decryption of 1.0 file: %s" % fdata.strip()

    def test_rekey_migration(self):
        # Skip testing rekeying files if we don't have access to AES, KDF or Counter.
        if not HAS_AES or not HAS_COUNTER or not HAS_PBKDF2:
            raise SkipTest

        v10_file = tempfile.NamedTemporaryFile(delete=False)
        with v10_file as f:
            f.write(to_bytes(v10_data))

        ve = vault.VaultEditor("ansible")

        # make sure the password functions for the cipher
        error_hit = False
        try:
            ve.rekey_file(v10_file.name, 'ansible2')
        except errors.AnsibleError:
            error_hit = True

        # verify decrypted content
        f = open(v10_file.name, "rb")
        fdata = f.read()
        f.close()

        assert error_hit is False, "error rekeying 1.0 file to 1.1"

        # ensure filedata can be decrypted, is 1.1 and is AES256
        vl = vault.VaultLib("ansible2")
        dec_data = None
        error_hit = False
        try:
            dec_data = vl.decrypt(fdata)
        except errors.AnsibleError:
            error_hit = True

        os.unlink(v10_file.name)

        assert vl.cipher_name == "AES256", "wrong cipher name set after rekey: %s" % vl.cipher_name
        assert error_hit is False, "error decrypting migrated 1.0 file"
        assert dec_data.strip() == b"foo", "incorrect decryption of rekeyed/migrated file: %s" % dec_data

    def test_real_path_dash(self):
        filename = '-'
        ve = vault.VaultEditor('password')

        res = ve._real_path(filename)
        self.assertEqual(res, '-')

    def test_real_path_dev_null(self):
        filename = '/dev/null'
        ve = vault.VaultEditor('password')

        res = ve._real_path(filename)
        self.assertEqual(res, '/dev/null')

    def test_real_path_symlink(self):
        self._test_dir = self._create_test_dir()
        file_path = self._create_file(self._test_dir, 'test_file', content=b'this is a test file')
        file_link_path = os.path.join(self._test_dir, 'a_link_to_test_file')

        os.symlink(file_path, file_link_path)

        ve = vault.VaultEditor('password')

        res = ve._real_path(file_link_path)
        self.assertEqual(res, file_path)
