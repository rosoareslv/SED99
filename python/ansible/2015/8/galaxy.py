########################################################################
#
# (C) 2013, James Cammarata <jcammarata@ansible.com>
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
#
########################################################################

import os
import os.path
import sys
import yaml

from collections import defaultdict
from distutils.version import LooseVersion
from jinja2 import Environment

import ansible.constants as C
import ansible.utils
import ansible.galaxy
from ansible.cli import CLI
from ansible.errors import AnsibleError, AnsibleOptionsError
from ansible.galaxy import Galaxy
from ansible.galaxy.api import GalaxyAPI
from ansible.galaxy.role import GalaxyRole
from ansible.playbook.role.requirement import RoleRequirement

class GalaxyCLI(CLI):

    VALID_ACTIONS = ("init", "info", "install", "list", "remove", "search")
    SKIP_INFO_KEYS = ("name", "description", "readme_html", "related", "summary_fields", "average_aw_composite", "average_aw_score", "url" )

    def __init__(self, args, display=None):

        self.api = None
        self.galaxy = None
        super(GalaxyCLI, self).__init__(args, display)

    def parse(self):
        ''' create an options parser for bin/ansible '''

        self.parser = CLI.base_parser(
            usage = "usage: %%prog [%s] [--help] [options] ..." % "|".join(self.VALID_ACTIONS),
            epilog = "\nSee '%s <command> --help' for more information on a specific command.\n\n" % os.path.basename(sys.argv[0])
        )


        self.set_action()

        # options specific to actions
        if self.action == "info":
            self.parser.set_usage("usage: %prog info [options] role_name[,version]")
        elif self.action == "init":
            self.parser.set_usage("usage: %prog init [options] role_name")
            self.parser.add_option('-p', '--init-path', dest='init_path', default="./",
                help='The path in which the skeleton role will be created. The default is the current working directory.')
            self.parser.add_option(
                '--offline', dest='offline', default=False, action='store_true',
                help="Don't query the galaxy API when creating roles")
        elif self.action == "install":
            self.parser.set_usage("usage: %prog install [options] [-r FILE | role_name(s)[,version] | scm+role_repo_url[,version] | tar_file(s)]")
            self.parser.add_option('-i', '--ignore-errors', dest='ignore_errors', action='store_true', default=False,
                help='Ignore errors and continue with the next specified role.')
            self.parser.add_option('-n', '--no-deps', dest='no_deps', action='store_true', default=False,
                help='Don\'t download roles listed as dependencies')
            self.parser.add_option('-r', '--role-file', dest='role_file',
                help='A file containing a list of roles to be imported')
        elif self.action == "remove":
            self.parser.set_usage("usage: %prog remove role1 role2 ...")
        elif self.action == "list":
            self.parser.set_usage("usage: %prog list [role_name]")
        elif self.action == "search":
            self.parser.add_option('-P', '--platforms', dest='platforms',
                help='list of OS platforms to filter by')
            self.parser.add_option('-C', '--categories', dest='categories',
                help='list of categories to filter by')
            self.parser.set_usage("usage: %prog search [<search_term>] [-C <category1,category2>] [-P platform]")

        # options that apply to more than one action
        if self.action != "init":
            self.parser.add_option('-p', '--roles-path', dest='roles_path', default=C.DEFAULT_ROLES_PATH,
                help='The path to the directory containing your roles. '
                     'The default is the roles_path configured in your '
                     'ansible.cfg file (/etc/ansible/roles if not configured)')

        if self.action in ("info","init","install","search"):
            self.parser.add_option('-s', '--server', dest='api_server', default="https://galaxy.ansible.com",
                help='The API server destination')

        if self.action in ("init","install"):
            self.parser.add_option('-f', '--force', dest='force', action='store_true', default=False,
                help='Force overwriting an existing role')

        # get options, args and galaxy object
        self.options, self.args =self.parser.parse_args()
        self.display.verbosity = self.options.verbosity
        self.galaxy = Galaxy(self.options, self.display)

        return True

    def run(self):

        super(GalaxyCLI, self).run()

        # if not offline, get connect to galaxy api
        if self.action in ("info","install", "search") or (self.action == 'init' and not self.options.offline):
            api_server = self.options.api_server
            self.api = GalaxyAPI(self.galaxy, api_server)
            if not self.api:
                raise AnsibleError("The API server (%s) is not responding, please try again later." % api_server)

        self.execute()

    def get_opt(self, k, defval=""):
        """
        Returns an option from an Optparse values instance.
        """
        try:
            data = getattr(self.options, k)
        except:
            return defval
        if k == "roles_path":
            if os.pathsep in data:
                data = data.split(os.pathsep)[0]
        return data

    def exit_without_ignore(self, rc=1):
        """
        Exits with the specified return code unless the
        option --ignore-errors was specified
        """
        if not self.get_opt("ignore_errors", False):
            raise AnsibleError('- you can use --ignore-errors to skip failed roles and finish processing the list.')

    def parse_requirements_files(self, role):
        if 'role' in role:
            # Old style: {role: "galaxy.role,version,name", other_vars: "here" }
            role_info = role_spec_parse(role['role'])
            if isinstance(role_info, dict):
                # Warning: Slight change in behaviour here.  name may be being
                # overloaded.  Previously, name was only a parameter to the role.
                # Now it is both a parameter to the role and the name that
                # ansible-galaxy will install under on the local system.
                if 'name' in role and 'name' in role_info:
                    del role_info['name']
                role.update(role_info)
        else:
            # New style: { src: 'galaxy.role,version,name', other_vars: "here" }
            if 'github.com' in role["src"] and 'http' in role["src"] and '+' not in role["src"] and not role["src"].endswith('.tar.gz'):
                role["src"] = "git+" + role["src"]

            if '+' in role["src"]:
                (scm, src) = role["src"].split('+')
                role["scm"] = scm
                role["src"] = src

            if 'name' not in role:
                role["name"] = GalaxyRole.url_to_spec(role["src"])

            if 'version' not in role:
                role['version'] = ''

            if 'scm' not in role:
                role['scm'] = None

        return role


    def _display_role_info(self, role_info):

        text = "\nRole: %s \n" % role_info['name']
        text += "\tdescription: %s \n" % role_info['description']

        for k in sorted(role_info.keys()):

            if k in self.SKIP_INFO_KEYS:
                continue

            if isinstance(role_info[k], dict):
                text += "\t%s: \n" % (k)
                for key in sorted(role_info[k].keys()):
                    if key in self.SKIP_INFO_KEYS:
                        continue
                    text += "\t\t%s: %s\n" % (key, role_info[k][key])
            else:
                text += "\t%s: %s\n" % (k, role_info[k])

        return text

############################
# execute actions
############################

    def execute_init(self):
        """
        Executes the init action, which creates the skeleton framework
        of a role that complies with the galaxy metadata format.
        """

        init_path  = self.get_opt('init_path', './')
        force      = self.get_opt('force', False)
        offline    = self.get_opt('offline', False)

        role_name = self.args.pop(0).strip()
        if role_name == "":
            raise AnsibleOptionsError("- no role name specified for init")
        role_path = os.path.join(init_path, role_name)
        if os.path.exists(role_path):
            if os.path.isfile(role_path):
                raise AnsibleError("- the path %s already exists, but is a file - aborting" % role_path)
            elif not force:
                raise AnsibleError("- the directory %s already exists." % role_path + \
                            "you can use --force to re-initialize this directory,\n" + \
                            "however it will reset any main.yml files that may have\n" + \
                                "been modified there already.")

        # create the default README.md
        if not os.path.exists(role_path):
            os.makedirs(role_path)
        readme_path = os.path.join(role_path, "README.md")
        f = open(readme_path, "wb")
        f.write(self.galaxy.default_readme)
        f.close

        for dir in GalaxyRole.ROLE_DIRS:
            dir_path = os.path.join(init_path, role_name, dir)
            main_yml_path = os.path.join(dir_path, 'main.yml')
            # create the directory if it doesn't exist already
            if not os.path.exists(dir_path):
                os.makedirs(dir_path)

            # now create the main.yml file for that directory
            if dir == "meta":
                # create a skeleton meta/main.yml with a valid galaxy_info
                # datastructure in place, plus with all of the available
                # tags/platforms included (but commented out) and the
                # dependencies section
                platforms = []
                if not offline and self.api:
                    platforms = self.api.get_list("platforms") or []
                categories = []
                if not offline and self.api:
                    categories = self.api.get_list("categories") or []

                # group the list of platforms from the api based
                # on their names, with the release field being
                # appended to a list of versions
                platform_groups = defaultdict(list)
                for platform in platforms:
                    platform_groups[platform['name']].append(platform['release'])
                    platform_groups[platform['name']].sort()

                inject = dict(
                    author = 'your name',
                    company = 'your company (optional)',
                    license = 'license (GPLv2, CC-BY, etc)',
                    issue_tracker_url = 'http://example.com/issue/tracker',
                    min_ansible_version = '1.2',
                    platforms = platform_groups,
                    categories = categories,
                )
                rendered_meta = Environment().from_string(self.galaxy.default_meta).render(inject)
                f = open(main_yml_path, 'w')
                f.write(rendered_meta)
                f.close()
                pass
            elif dir not in ('files','templates'):
                # just write a (mostly) empty YAML file for main.yml
                f = open(main_yml_path, 'w')
                f.write('---\n# %s file for %s\n' % (dir,role_name))
                f.close()
        self.display.display("- %s was created successfully" % role_name)

    def execute_info(self):
        """
        Executes the info action. This action prints out detailed
        information about an installed role as well as info available
        from the galaxy API.
        """

        if len(self.args) == 0:
            # the user needs to specify a role
            raise AnsibleOptionsError("- you must specify a user/role name")

        roles_path = self.get_opt("roles_path")

        data = ''
        for role in self.args:

            role_info = {}
            gr = GalaxyRole(self.galaxy, role)
            #self.galaxy.add_role(gr)

            install_info = gr.install_info
            if install_info:
                if 'version' in install_info:
                    install_info['intalled_version'] = install_info['version']
                    del install_info['version']
                role_info.update(install_info)

            remote_data = False
            if self.api:
                remote_data = self.api.lookup_role_by_name(role, False)

            if remote_data:
                role_info.update(remote_data)

            if gr.metadata:
                role_info.update(gr.metadata)

            req = RoleRequirement()
            __, __, role_spec= req.parse({'role': role})
            if role_spec:
                role_info.update(role_spec)

            data += self._display_role_info(role_info)
            if not data:
                data += "\n- the role %s was not found" % role

        self.pager(data)

    def execute_install(self):
        """
        Executes the installation action. The args list contains the
        roles to be installed, unless -f was specified. The list of roles
        can be a name (which will be downloaded via the galaxy API and github),
        or it can be a local .tar.gz file.
        """

        role_file  = self.get_opt("role_file", None)

        if len(self.args) == 0 and role_file is None:
            # the user needs to specify one of either --role-file
            # or specify a single user/role name
            raise AnsibleOptionsError("- you must specify a user/role name or a roles file")
        elif len(self.args) == 1 and not role_file is None:
            # using a role file is mutually exclusive of specifying
            # the role name on the command line
            raise AnsibleOptionsError("- please specify a user/role name, or a roles file, but not both")

        no_deps    = self.get_opt("no_deps", False)
        roles_path = self.get_opt("roles_path")

        roles_done = []
        roles_left = []
        if role_file:
            self.display.debug('Getting roles from %s' % role_file)
            try:
                self.display.debug('Processing role file: %s' % role_file)
                f = open(role_file, 'r')
                if role_file.endswith('.yaml') or role_file.endswith('.yml'):
                    try:
                        rolesparsed = map(self.parse_requirements_files, yaml.safe_load(f))
                    except Exception as e:
                       raise AnsibleError("%s does not seem like a valid yaml file: %s" % (role_file, str(e)))
                    roles_left = [GalaxyRole(self.galaxy, **r) for r in rolesparsed]
                else:
                    # roles listed in a file, one per line
                    self.display.deprecated("Non yaml files for role requirements")
                    for rname in f.readlines():
                        if rname.startswith("#") or rname.strip() == '':
                            continue
                        roles_left.append(GalaxyRole(self.galaxy, rname.strip()))
                f.close()
            except (IOError,OSError) as e:
                raise AnsibleError("Unable to read requirements file (%s): %s" % (role_file, str(e)))
        else:
            # roles were specified directly, so we'll just go out grab them
            # (and their dependencies, unless the user doesn't want us to).
            for rname in self.args:
                roles_left.append(GalaxyRole(self.galaxy, rname.strip()))

        while len(roles_left) > 0:
            # query the galaxy API for the role data
            role_data = None
            role = roles_left.pop(0)
            role_path = role.path


            if role_path:
                self.options.roles_path = role_path
            else:
                self.options.roles_path = roles_path

            self.display.debug('Installing role %s from %s' % (role.name, self.options.roles_path))

            tmp_file = None
            installed = False
            if role.src and os.path.isfile(role.src):
                # installing a local tar.gz
                tmp_file = role.src
            else:
                if role.scm:
                    # create tar file from scm url
                    tmp_file = GalaxyRole.scm_archive_role(role.scm, role.src, role.version, role.name)
                if role.src:
                    if '://' not in role.src:
                        role_data = self.api.lookup_role_by_name(role.src)
                        if not role_data:
                            self.display.warning("- sorry, %s was not found on %s." % (role.src, self.options.api_server))
                            self.exit_without_ignore()
                            continue

                        role_versions = self.api.fetch_role_related('versions', role_data['id'])
                        if not role.version:
                            # convert the version names to LooseVersion objects
                            # and sort them to get the latest version. If there
                            # are no versions in the list, we'll grab the head
                            # of the master branch
                            if len(role_versions) > 0:
                                loose_versions = [LooseVersion(a.get('name',None)) for a in role_versions]
                                loose_versions.sort()
                                role.version = str(loose_versions[-1])
                            else:
                                role.version = 'master'
                        elif role.version != 'master':
                            if role_versions and role.version not in [a.get('name', None) for a in role_versions]:
                                self.display.warning('role is %s' % role)
                                self.display.warning("- the specified version (%s) was not found in the list of available versions (%s)." % (role.version, role_versions))
                                self.exit_without_ignore()
                                continue

                    # download the role. if --no-deps was specified, we stop here,
                    # otherwise we recursively grab roles and all of their deps.
                    tmp_file = role.fetch(role_data)
            if tmp_file:
                installed = role.install(tmp_file)
                # we're done with the temp file, clean it up
                if tmp_file != role.src:
                    os.unlink(tmp_file)
                # install dependencies, if we want them
                if not no_deps and installed:
                    if not role_data:
                        role_data = gr.get_metadata(role.get("name"), options)
                        role_dependencies = role_data['dependencies']
                    else:
                        role_dependencies = role_data['summary_fields']['dependencies'] # api_fetch_role_related(api_server, 'dependencies', role_data['id'])
                    for dep in role_dependencies:
                        self.display.debug('Installing dep %s' % dep)
                        if isinstance(dep, basestring):
                            dep = ansible.utils.role_spec_parse(dep)
                        else:
                            dep = ansible.utils.role_yaml_parse(dep)
                        if not get_role_metadata(dep["name"], options):
                            if dep not in roles_left:
                                self.display.display('- adding dependency: %s' % dep["name"])
                                roles_left.append(dep)
                            else:
                                self.display.display('- dependency %s already pending installation.' % dep["name"])
                        else:
                            self.display.display('- dependency %s is already installed, skipping.' % dep["name"])

            if not tmp_file or not installed:
                self.display.warning("- %s was NOT installed successfully." % role.name)
                self.exit_without_ignore()
        return 0

    def execute_remove(self):
        """
        Executes the remove action. The args list contains the list
        of roles to be removed. This list can contain more than one role.
        """

        if len(self.args) == 0:
            raise AnsibleOptionsError('- you must specify at least one role to remove.')

        for role_name in self.args:
            role = GalaxyRole(self.galaxy, role_name)
            try:
                if role.remove():
                    self.display.display('- successfully removed %s' % role_name)
                else:
                    self.display.display('- %s is not installed, skipping.' % role_name)
            except Exception as e:
                raise AnsibleError("Failed to remove role %s: %s" % (role_name, str(e)))

        return 0

    def execute_list(self):
        """
        Executes the list action. The args list can contain zero
        or one role. If one is specified, only that role will be
        shown, otherwise all roles in the specified directory will
        be shown.
        """

        if len(self.args) > 1:
            raise AnsibleOptionsError("- please specify only one role to list, or specify no roles to see a full list")

        if len(self.args) == 1:
            # show only the request role, if it exists
            name = self.args.pop()
            gr = GalaxyRole(self.galaxy, name)
            if gr.metadata:
                install_info = gr.install_info
                version = None
                if install_info:
                    version = install_info.get("version", None)
                if not version:
                    version = "(unknown version)"
                # show some more info about single roles here
                self.display.display("- %s, %s" % (name, version))
            else:
                self.display.display("- the role %s was not found" % name)
        else:
            # show all valid roles in the roles_path directory
            roles_path = self.get_opt('roles_path')
            roles_path = os.path.expanduser(roles_path)
            if not os.path.exists(roles_path):
                raise AnsibleOptionsError("- the path %s does not exist. Please specify a valid path with --roles-path" % roles_path)
            elif not os.path.isdir(roles_path):
                raise AnsibleOptionsError("- %s exists, but it is not a directory. Please specify a valid path with --roles-path" % roles_path)
            path_files = os.listdir(roles_path)
            for path_file in path_files:
                gr = GalaxyRole(self.galaxy, path_file)
                if gr.metadata:
                    install_info = gr.metadata
                    version = None
                    if install_info:
                        version = install_info.get("version", None)
                    if not version:
                        version = "(unknown version)"
                    self.display.display("- %s, %s" % (path_file, version))
        return 0

    def execute_search(self):

        search = None
        if len(self.args) > 1:
            raise AnsibleOptionsError("At most a single search term is allowed.")
        elif len(self.args) == 1:
            search = self.args.pop()

        response = self.api.search_roles(search, self.options.platforms, self.options.categories)

        if 'count' in response:
            self.galaxy.display.display("Found %d roles matching your search:\n" % response['count'])

        data = ''
        if 'results' in response:
            for role in response['results']:
                data += self._display_role_info(role)

        self.pager(data)
