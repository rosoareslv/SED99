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

# Make coding more python3-ish
from __future__ import (absolute_import, division, print_function)
__metaclass__ = type

import operator
import optparse
import os
import time
import yaml
import getpass

from ansible import __version__
from ansible import constants as C
from ansible.errors import AnsibleError
from ansible.utils.unicode import to_bytes

# FIXME: documentation for methods here, which have mostly been
#        copied directly over from the old utils/__init__.py

class SortedOptParser(optparse.OptionParser):
    '''Optparser which sorts the options by opt before outputting --help'''

    def format_help(self, formatter=None):
        self.option_list.sort(key=operator.methodcaller('get_opt_string'))
        return optparse.OptionParser.format_help(self, formatter=None)

#TODO: move many cli only functions in this file into the CLI class
class CLI(object):
    ''' code behind bin/ansible* programs '''

    VALID_ACTIONS = ['No Actions']

    def __init__(self, args, display=None):
        """
        Base init method for all command line programs
        """

        self.args = args
        self.options = None
        self.parser = None
        self.action = None

        if display is None:
            self.display = Display()
        else:
            self.display = display

    def set_action(self):
        """
        Get the action the user wants to execute from the sys argv list.
        """
        for i in range(0,len(self.args)):
            arg = self.args[i]
            if arg in self.VALID_ACTIONS:
                self.action = arg
                del self.args[i]
                break

        if not self.action:
            self.parser.print_help()
            raise AnsibleError("Missing required action")

    def execute(self):
        """
        Actually runs a child defined method using the execute_<action> pattern
        """
        fn = getattr(self, "execute_%s" % self.action)
        fn()

    def parse(self):
        raise Exception("Need to implement!")

    def run(self):
        raise Exception("Need to implement!")

    @staticmethod
    def ask_vault_passwords(ask_vault_pass=False, ask_new_vault_pass=False, confirm_vault=False, confirm_new=False):

        vault_pass = None
        new_vault_pass = None

        if ask_vault_pass:
            vault_pass = getpass.getpass(prompt="Vault password: ")

        if ask_vault_pass and confirm_vault:
            vault_pass2 = getpass.getpass(prompt="Confirm Vault password: ")
            if vault_pass != vault_pass2:
                raise errors.AnsibleError("Passwords do not match")

        if ask_new_vault_pass:
            new_vault_pass = getpass.getpass(prompt="New Vault password: ")

        if ask_new_vault_pass and confirm_new:
            new_vault_pass2 = getpass.getpass(prompt="Confirm New Vault password: ")
            if new_vault_pass != new_vault_pass2:
                raise errors.AnsibleError("Passwords do not match")

        # enforce no newline chars at the end of passwords
        if vault_pass:
            vault_pass = to_bytes(vault_pass, errors='strict', nonstring='simplerepr').strip()
        if new_vault_pass:
            new_vault_pass = to_bytes(new_vault_pass, errors='strict', nonstring='simplerepr').strip()

        return vault_pass, new_vault_pass


    def ask_passwords(self):

        op = self.options
        sshpass = None
        becomepass = None
        become_prompt = ''

        if op.ask_pass:
            sshpass = getpass.getpass(prompt="SSH password: ")
            become_prompt = "%s password[defaults to SSH password]: " % op.become_method.upper()
            if sshpass:
                sshpass = to_bytes(sshpass, errors='strict', nonstring='simplerepr')
        else:
            become_prompt = "%s password: " % op.become_method.upper()

        if op.become_ask_pass:
            becomepass = getpass.getpass(prompt=become_prompt)
            if op.ask_pass and becomepass == '':
                becomepass = sshpass
            if becomepass:
                becomepass = to_bytes(becomepass)

        return (sshpass, becomepass)


    def normalize_become_options(self):
        ''' this keeps backwards compatibility with sudo/su self.options '''
        self.options.become_ask_pass = self.options.become_ask_pass or self.options.ask_sudo_pass or self.options.ask_su_pass or C.DEFAULT_BECOME_ASK_PASS
        self.options.become_user     = self.options.become_user or self.options.sudo_user or self.options.su_user or C.DEFAULT_BECOME_USER

        if self.options.become:
            pass
        elif self.options.sudo:
            self.options.become = True
            self.options.become_method = 'sudo'
        elif self.options.su:
            self.options.become = True
            options.become_method = 'su'


    def validate_conflicts(self):

        op = self.options

        # Check for vault related conflicts
        if (op.ask_vault_pass and op.vault_password_file):
            self.parser.error("--ask-vault-pass and --vault-password-file are mutually exclusive")


        # Check for privilege escalation conflicts
        if (op.su or op.su_user or op.ask_su_pass) and \
                    (op.sudo or op.sudo_user or op.ask_sudo_pass) or \
            (op.su or op.su_user or op.ask_su_pass) and \
                    (op.become or op.become_user or op.become_ask_pass) or \
            (op.sudo or op.sudo_user or op.ask_sudo_pass) and \
                    (op.become or op.become_user or op.become_ask_pass):

            self.parser.error("Sudo arguments ('--sudo', '--sudo-user', and '--ask-sudo-pass') "
                              "and su arguments ('-su', '--su-user', and '--ask-su-pass') "
                              "and become arguments ('--become', '--become-user', and '--ask-become-pass')"
                              " are exclusive of each other")

    @staticmethod
    def base_parser(usage="", output_opts=False, runas_opts=False, meta_opts=False,
        async_opts=False, connect_opts=False, subset_opts=False, check_opts=False, diff_opts=False):
        ''' create an options parser for any ansible script '''

        parser = SortedOptParser(usage, version=CLI.version("%prog"))

        parser.add_option('-u', '--user', default=C.DEFAULT_REMOTE_USER, dest='remote_user',
            help='connect as this user (default=%s)' % C.DEFAULT_REMOTE_USER)
        parser.add_option('-v','--verbose', dest='verbosity', default=0, action="count",
            help="verbose mode (-vvv for more, -vvvv to enable connection debugging)")
        parser.add_option('-f','--forks', dest='forks', default=C.DEFAULT_FORKS, type='int',
            help="specify number of parallel processes to use (default=%s)" % C.DEFAULT_FORKS)
        parser.add_option('-i', '--inventory-file', dest='inventory',
            help="specify inventory host file (default=%s)" % C.DEFAULT_HOST_LIST,
            default=C.DEFAULT_HOST_LIST)
        parser.add_option('-k', '--ask-pass', default=False, dest='ask_pass', action='store_true',
            help='ask for connection password')
        parser.add_option('--private-key', default=C.DEFAULT_PRIVATE_KEY_FILE, dest='private_key_file',
            help='use this file to authenticate the connection')
        parser.add_option('--ask-vault-pass', default=False, dest='ask_vault_pass', action='store_true',
            help='ask for vault password')
        parser.add_option('--vault-password-file', default=C.DEFAULT_VAULT_PASSWORD_FILE,
            dest='vault_password_file', help="vault password file")
        parser.add_option('--list-hosts', dest='listhosts', action='store_true',
            help='outputs a list of matching hosts; does not execute anything else')
        parser.add_option('-M', '--module-path', dest='module_path',
            help="specify path(s) to module library (default=%s)" % C.DEFAULT_MODULE_PATH,
            default=None)
        parser.add_option('-e', '--extra-vars', dest="extra_vars", action="append",
            help="set additional variables as key=value or YAML/JSON", default=[])

        if subset_opts:
            parser.add_option('-l', '--limit', default=C.DEFAULT_SUBSET, dest='subset',
                help='further limit selected hosts to an additional pattern')
            parser.add_option('-t', '--tags', dest='tags', default='all',
                help="only run plays and tasks tagged with these values")
            parser.add_option('--skip-tags', dest='skip_tags',
                help="only run plays and tasks whose tags do not match these values")

        if output_opts:
            parser.add_option('-o', '--one-line', dest='one_line', action='store_true',
                help='condense output')
            parser.add_option('-t', '--tree', dest='tree', default=None,
                help='log output to this directory')

        if runas_opts:
            # priv user defaults to root later on to enable detecting when this option was given here
            parser.add_option('-K', '--ask-sudo-pass', default=False, dest='ask_sudo_pass', action='store_true',
                help='ask for sudo password (deprecated, use become)')
            parser.add_option('--ask-su-pass', default=False, dest='ask_su_pass', action='store_true',
                help='ask for su password (deprecated, use become)')
            parser.add_option("-s", "--sudo", default=C.DEFAULT_SUDO, action="store_true", dest='sudo',
                help="run operations with sudo (nopasswd) (deprecated, use become)")
            parser.add_option('-U', '--sudo-user', dest='sudo_user', default=None,
                              help='desired sudo user (default=root) (deprecated, use become)')
            parser.add_option('-S', '--su', default=C.DEFAULT_SU, action='store_true',
                help='run operations with su (deprecated, use become)')
            parser.add_option('-R', '--su-user', default=None,
                help='run operations with su as this user (default=%s) (deprecated, use become)' % C.DEFAULT_SU_USER)

            # consolidated privilege escalation (become)
            parser.add_option("-b", "--become", default=C.DEFAULT_BECOME, action="store_true", dest='become',
                help="run operations with become (nopasswd implied)")
            parser.add_option('--become-method', dest='become_method', default=C.DEFAULT_BECOME_METHOD, type='string',
                help="privilege escalation method to use (default=%s), valid choices: [ %s ]" % (C.DEFAULT_BECOME_METHOD, ' | '.join(C.BECOME_METHODS)))
            parser.add_option('--become-user', default=None, dest='become_user', type='string',
                help='run operations as this user (default=%s)' % C.DEFAULT_BECOME_USER)
            parser.add_option('--ask-become-pass', default=False, dest='become_ask_pass', action='store_true',
                help='ask for privilege escalation password')


        if connect_opts:
            parser.add_option('-c', '--connection', dest='connection', default=C.DEFAULT_TRANSPORT,
                help="connection type to use (default=%s)" % C.DEFAULT_TRANSPORT)
            parser.add_option('-T', '--timeout', default=C.DEFAULT_TIMEOUT, type='int', dest='timeout',
                help="override the connection timeout in seconds (default=%s)" % C.DEFAULT_TIMEOUT)


        if async_opts:
            parser.add_option('-P', '--poll', default=C.DEFAULT_POLL_INTERVAL, type='int',
                dest='poll_interval',
                help="set the poll interval if using -B (default=%s)" % C.DEFAULT_POLL_INTERVAL)
            parser.add_option('-B', '--background', dest='seconds', type='int', default=0,
                help='run asynchronously, failing after X seconds (default=N/A)')

        if check_opts:
            parser.add_option("-C", "--check", default=False, dest='check', action='store_true',
                help="don't make any changes; instead, try to predict some of the changes that may occur")
            parser.add_option('--syntax-check', dest='syntax', action='store_true',
                help="perform a syntax check on the playbook, but do not execute it")

        if diff_opts:
            parser.add_option("-D", "--diff", default=False, dest='diff', action='store_true',
                help="when changing (small) files and templates, show the differences in those files; works great with --check"
            )

        if meta_opts:
            parser.add_option('--force-handlers', dest='force_handlers', action='store_true',
                help="run handlers even if a task fails")
            parser.add_option('--flush-cache', dest='flush_cache', action='store_true',
                help="clear the fact cache")

        return parser

    @staticmethod
    def version(prog):
        result = "{0} {1}".format(prog, __version__)
        gitinfo = _gitinfo()
        if gitinfo:
            result = result + " {0}".format(gitinfo)
        result = result + "\n  configured module search path = %s" % C.DEFAULT_MODULE_PATH
        return result

    @staticmethod
    def version_info(gitinfo=False):
        if gitinfo:
            # expensive call, user with care
            ansible_version_string = version('')
        else:
            ansible_version_string = __version__
        ansible_version = ansible_version_string.split()[0]
        ansible_versions = ansible_version.split('.')
        for counter in range(len(ansible_versions)):
            if ansible_versions[counter] == "":
                ansible_versions[counter] = 0
            try:
                ansible_versions[counter] = int(ansible_versions[counter])
            except:
                pass
        if len(ansible_versions) < 3:
            for counter in range(len(ansible_versions), 3):
                ansible_versions.append(0)
        return {'string':      ansible_version_string.strip(),
                'full':        ansible_version,
                'major':       ansible_versions[0],
                'minor':       ansible_versions[1],
                'revision':    ansible_versions[2]}

def _git_repo_info(repo_path):
    ''' returns a string containing git branch, commit id and commit date '''
    result = None
    if os.path.exists(repo_path):
        # Check if the .git is a file. If it is a file, it means that we are in a submodule structure.
        if os.path.isfile(repo_path):
            try:
                gitdir = yaml.safe_load(open(repo_path)).get('gitdir')
                # There is a possibility the .git file to have an absolute path.
                if os.path.isabs(gitdir):
                    repo_path = gitdir
                else:
                    repo_path = os.path.join(repo_path[:-4], gitdir)
            except (IOError, AttributeError):
                return ''
        f = open(os.path.join(repo_path, "HEAD"))
        branch = f.readline().split('/')[-1].rstrip("\n")
        f.close()
        branch_path = os.path.join(repo_path, "refs", "heads", branch)
        if os.path.exists(branch_path):
            f = open(branch_path)
            commit = f.readline()[:10]
            f.close()
        else:
            # detached HEAD
            commit = branch[:10]
            branch = 'detached HEAD'
            branch_path = os.path.join(repo_path, "HEAD")

        date = time.localtime(os.stat(branch_path).st_mtime)
        if time.daylight == 0:
            offset = time.timezone
        else:
            offset = time.altzone
        result = "({0} {1}) last updated {2} (GMT {3:+04d})".format(branch, commit,
            time.strftime("%Y/%m/%d %H:%M:%S", date), int(offset / -36))
    else:
        result = ''
    return result

def _gitinfo():
    basedir = os.path.join(os.path.dirname(__file__), '..', '..', '..')
    repo_path = os.path.join(basedir, '.git')
    result = _git_repo_info(repo_path)
    submodules = os.path.join(basedir, '.gitmodules')
    if not os.path.exists(submodules):
       return result
    f = open(submodules)
    for line in f:
        tokens = line.strip().split(' ')
        if tokens[0] == 'path':
            submodule_path = tokens[2]
            submodule_info =_git_repo_info(os.path.join(basedir, submodule_path, '.git'))
            if not submodule_info:
                submodule_info = ' not found - use git submodule update --init ' + submodule_path
            result += "\n  {0}: {1}".format(submodule_path, submodule_info)
    f.close()
    return result
