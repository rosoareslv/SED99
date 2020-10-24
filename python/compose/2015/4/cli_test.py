from __future__ import absolute_import
import sys
import os

from six import StringIO
from mock import patch

from .testcases import DockerClientTestCase
from compose.cli.main import TopLevelCommand


class CLITestCase(DockerClientTestCase):
    def setUp(self):
        super(CLITestCase, self).setUp()
        self.old_sys_exit = sys.exit
        sys.exit = lambda code=0: None
        self.command = TopLevelCommand()
        self.command.base_dir = 'tests/fixtures/simple-composefile'

    def tearDown(self):
        sys.exit = self.old_sys_exit
        self.project.kill()
        self.project.remove_stopped()

    @property
    def project(self):
        # Hack: allow project to be overridden. This needs refactoring so that
        # the project object is built exactly once, by the command object, and
        # accessed by the test case object.
        if hasattr(self, '_project'):
            return self._project

        return self.command.get_project(self.command.get_config_path())

    def test_help(self):
        old_base_dir = self.command.base_dir
        self.command.base_dir = 'tests/fixtures/no-composefile'
        with self.assertRaises(SystemExit) as exc_context:
            self.command.dispatch(['help', 'up'], None)
            self.assertIn('Usage: up [options] [SERVICE...]', str(exc_context.exception))
        # self.project.kill() fails during teardown
        # unless there is a composefile.
        self.command.base_dir = old_base_dir

    # TODO: address the "Inappropriate ioctl for device" warnings in test output
    @patch('sys.stdout', new_callable=StringIO)
    def test_ps(self, mock_stdout):
        self.project.get_service('simple').create_container()
        self.command.dispatch(['ps'], None)
        self.assertIn('simplecomposefile_simple_1', mock_stdout.getvalue())

    @patch('sys.stdout', new_callable=StringIO)
    def test_ps_default_composefile(self, mock_stdout):
        self.command.base_dir = 'tests/fixtures/multiple-composefiles'
        self.command.dispatch(['up', '-d'], None)
        self.command.dispatch(['ps'], None)

        output = mock_stdout.getvalue()
        self.assertIn('multiplecomposefiles_simple_1', output)
        self.assertIn('multiplecomposefiles_another_1', output)
        self.assertNotIn('multiplecomposefiles_yetanother_1', output)

    @patch('sys.stdout', new_callable=StringIO)
    def test_ps_alternate_composefile(self, mock_stdout):
        self.command.base_dir = 'tests/fixtures/multiple-composefiles'
        self.command.dispatch(['-f', 'compose2.yml', 'up', '-d'], None)
        self.command.dispatch(['-f', 'compose2.yml', 'ps'], None)

        output = mock_stdout.getvalue()
        self.assertNotIn('multiplecomposefiles_simple_1', output)
        self.assertNotIn('multiplecomposefiles_another_1', output)
        self.assertIn('multiplecomposefiles_yetanother_1', output)

    @patch('compose.service.log')
    def test_pull(self, mock_logging):
        self.command.dispatch(['pull'], None)
        mock_logging.info.assert_any_call('Pulling simple (busybox:latest)...')
        mock_logging.info.assert_any_call('Pulling another (busybox:latest)...')

    @patch('sys.stdout', new_callable=StringIO)
    def test_build_no_cache(self, mock_stdout):
        self.command.base_dir = 'tests/fixtures/simple-dockerfile'
        self.command.dispatch(['build', 'simple'], None)

        mock_stdout.truncate(0)
        cache_indicator = 'Using cache'
        self.command.dispatch(['build', 'simple'], None)
        output = mock_stdout.getvalue()
        self.assertIn(cache_indicator, output)

        mock_stdout.truncate(0)
        self.command.dispatch(['build', '--no-cache', 'simple'], None)
        output = mock_stdout.getvalue()
        self.assertNotIn(cache_indicator, output)

    def test_up(self):
        self.command.dispatch(['up', '-d'], None)
        service = self.project.get_service('simple')
        another = self.project.get_service('another')
        self.assertEqual(len(service.containers()), 1)
        self.assertEqual(len(another.containers()), 1)

        # Ensure containers don't have stdin and stdout connected in -d mode
        config = service.containers()[0].inspect()['Config']
        self.assertFalse(config['AttachStderr'])
        self.assertFalse(config['AttachStdout'])
        self.assertFalse(config['AttachStdin'])

    def test_up_with_links(self):
        self.command.base_dir = 'tests/fixtures/links-composefile'
        self.command.dispatch(['up', '-d', 'web'], None)
        web = self.project.get_service('web')
        db = self.project.get_service('db')
        console = self.project.get_service('console')
        self.assertEqual(len(web.containers()), 1)
        self.assertEqual(len(db.containers()), 1)
        self.assertEqual(len(console.containers()), 0)

    def test_up_with_no_deps(self):
        self.command.base_dir = 'tests/fixtures/links-composefile'
        self.command.dispatch(['up', '-d', '--no-deps', 'web'], None)
        web = self.project.get_service('web')
        db = self.project.get_service('db')
        console = self.project.get_service('console')
        self.assertEqual(len(web.containers()), 1)
        self.assertEqual(len(db.containers()), 0)
        self.assertEqual(len(console.containers()), 0)

    def test_up_with_recreate(self):
        self.command.dispatch(['up', '-d'], None)
        service = self.project.get_service('simple')
        self.assertEqual(len(service.containers()), 1)

        old_ids = [c.id for c in service.containers()]

        self.command.dispatch(['up', '-d'], None)
        self.assertEqual(len(service.containers()), 1)

        new_ids = [c.id for c in service.containers()]

        self.assertNotEqual(old_ids, new_ids)

    def test_up_with_keep_old(self):
        self.command.dispatch(['up', '-d'], None)
        service = self.project.get_service('simple')
        self.assertEqual(len(service.containers()), 1)

        old_ids = [c.id for c in service.containers()]

        self.command.dispatch(['up', '-d', '--no-recreate'], None)
        self.assertEqual(len(service.containers()), 1)

        new_ids = [c.id for c in service.containers()]

        self.assertEqual(old_ids, new_ids)

    @patch('dockerpty.start')
    def test_run_service_without_links(self, mock_stdout):
        self.command.base_dir = 'tests/fixtures/links-composefile'
        self.command.dispatch(['run', 'console', '/bin/true'], None)
        self.assertEqual(len(self.project.containers()), 0)

        # Ensure stdin/out was open
        container = self.project.containers(stopped=True, one_off=True)[0]
        config = container.inspect()['Config']
        self.assertTrue(config['AttachStderr'])
        self.assertTrue(config['AttachStdout'])
        self.assertTrue(config['AttachStdin'])

    @patch('dockerpty.start')
    def test_run_service_with_links(self, __):
        self.command.base_dir = 'tests/fixtures/links-composefile'
        self.command.dispatch(['run', 'web', '/bin/true'], None)
        db = self.project.get_service('db')
        console = self.project.get_service('console')
        self.assertEqual(len(db.containers()), 1)
        self.assertEqual(len(console.containers()), 0)

    @patch('dockerpty.start')
    def test_run_with_no_deps(self, __):
        self.command.base_dir = 'tests/fixtures/links-composefile'
        self.command.dispatch(['run', '--no-deps', 'web', '/bin/true'], None)
        db = self.project.get_service('db')
        self.assertEqual(len(db.containers()), 0)

    @patch('dockerpty.start')
    def test_run_does_not_recreate_linked_containers(self, __):
        self.command.base_dir = 'tests/fixtures/links-composefile'
        self.command.dispatch(['up', '-d', 'db'], None)
        db = self.project.get_service('db')
        self.assertEqual(len(db.containers()), 1)

        old_ids = [c.id for c in db.containers()]

        self.command.dispatch(['run', 'web', '/bin/true'], None)
        self.assertEqual(len(db.containers()), 1)

        new_ids = [c.id for c in db.containers()]

        self.assertEqual(old_ids, new_ids)

    @patch('dockerpty.start')
    def test_run_without_command(self, __):
        self.command.base_dir = 'tests/fixtures/commands-composefile'
        self.check_build('tests/fixtures/simple-dockerfile', tag='composetest_test')

        for c in self.project.containers(stopped=True, one_off=True):
            c.remove()

        self.command.dispatch(['run', 'implicit'], None)
        service = self.project.get_service('implicit')
        containers = service.containers(stopped=True, one_off=True)
        self.assertEqual(
            [c.human_readable_command for c in containers],
            [u'/bin/sh -c echo "success"'],
        )

        self.command.dispatch(['run', 'explicit'], None)
        service = self.project.get_service('explicit')
        containers = service.containers(stopped=True, one_off=True)
        self.assertEqual(
            [c.human_readable_command for c in containers],
            [u'/bin/true'],
        )

    @patch('dockerpty.start')
    def test_run_service_with_entrypoint_overridden(self, _):
        self.command.base_dir = 'tests/fixtures/dockerfile_with_entrypoint'
        name = 'service'
        self.command.dispatch(
            ['run', '--entrypoint', '/bin/echo', name, 'helloworld'],
            None
        )
        service = self.project.get_service(name)
        container = service.containers(stopped=True, one_off=True)[0]
        self.assertEqual(
            container.human_readable_command,
            u'/bin/echo helloworld'
        )

    @patch('dockerpty.start')
    def test_run_service_with_user_overridden(self, _):
        self.command.base_dir = 'tests/fixtures/user-composefile'
        name = 'service'
        user = 'sshd'
        args = ['run', '--user={}'.format(user), name]
        self.command.dispatch(args, None)
        service = self.project.get_service(name)
        container = service.containers(stopped=True, one_off=True)[0]
        self.assertEqual(user, container.get('Config.User'))

    @patch('dockerpty.start')
    def test_run_service_with_user_overridden_short_form(self, _):
        self.command.base_dir = 'tests/fixtures/user-composefile'
        name = 'service'
        user = 'sshd'
        args = ['run', '-u', user, name]
        self.command.dispatch(args, None)
        service = self.project.get_service(name)
        container = service.containers(stopped=True, one_off=True)[0]
        self.assertEqual(user, container.get('Config.User'))

    @patch('dockerpty.start')
    def test_run_service_with_environement_overridden(self, _):
        name = 'service'
        self.command.base_dir = 'tests/fixtures/environment-composefile'
        self.command.dispatch(
            ['run', '-e', 'foo=notbar', '-e', 'allo=moto=bobo',
             '-e', 'alpha=beta', name],
            None
        )
        service = self.project.get_service(name)
        container = service.containers(stopped=True, one_off=True)[0]
        # env overriden
        self.assertEqual('notbar', container.environment['foo'])
        # keep environement from yaml
        self.assertEqual('world', container.environment['hello'])
        # added option from command line
        self.assertEqual('beta', container.environment['alpha'])
        # make sure a value with a = don't crash out
        self.assertEqual('moto=bobo', container.environment['allo'])

    @patch('dockerpty.start')
    def test_run_service_without_map_ports(self, __):
        # create one off container
        self.command.base_dir = 'tests/fixtures/ports-composefile'
        self.command.dispatch(['run', '-d', 'simple'], None)
        container = self.project.get_service('simple').containers(one_off=True)[0]

        # get port information
        port_random = container.get_local_port(3000)
        port_assigned = container.get_local_port(3001)

        # close all one off containers we just created
        container.stop()

        # check the ports
        self.assertEqual(port_random, None)
        self.assertEqual(port_assigned, None)

    @patch('dockerpty.start')
    def test_run_service_with_map_ports(self, __):

        # create one off container
        self.command.base_dir = 'tests/fixtures/ports-composefile'
        self.command.dispatch(['run', '-d', '--service-ports', 'simple'], None)
        container = self.project.get_service('simple').containers(one_off=True)[0]

        # get port information
        port_random = container.get_local_port(3000)
        port_assigned = container.get_local_port(3001)

        # close all one off containers we just created
        container.stop()

        # check the ports
        self.assertNotEqual(port_random, None)
        self.assertIn("0.0.0.0", port_random)
        self.assertEqual(port_assigned, "0.0.0.0:49152")

    def test_rm(self):
        service = self.project.get_service('simple')
        service.create_container()
        service.kill()
        self.assertEqual(len(service.containers(stopped=True)), 1)
        self.command.dispatch(['rm', '--force'], None)
        self.assertEqual(len(service.containers(stopped=True)), 0)
        service = self.project.get_service('simple')
        service.create_container()
        service.kill()
        self.assertEqual(len(service.containers(stopped=True)), 1)
        self.command.dispatch(['rm', '-f'], None)
        self.assertEqual(len(service.containers(stopped=True)), 0)

    def test_stop(self):
        self.command.dispatch(['up', '-d'], None)
        service = self.project.get_service('simple')
        self.assertEqual(len(service.containers()), 1)
        self.assertTrue(service.containers()[0].is_running)

        self.command.dispatch(['stop', '-t', '1'], None)

        self.assertEqual(len(service.containers(stopped=True)), 1)
        self.assertFalse(service.containers(stopped=True)[0].is_running)

    def test_kill(self):
        self.command.dispatch(['up', '-d'], None)
        service = self.project.get_service('simple')
        self.assertEqual(len(service.containers()), 1)
        self.assertTrue(service.containers()[0].is_running)

        self.command.dispatch(['kill'], None)

        self.assertEqual(len(service.containers(stopped=True)), 1)
        self.assertFalse(service.containers(stopped=True)[0].is_running)

    def test_kill_signal_sigint(self):
        self.command.dispatch(['up', '-d'], None)
        service = self.project.get_service('simple')
        self.assertEqual(len(service.containers()), 1)
        self.assertTrue(service.containers()[0].is_running)

        self.command.dispatch(['kill', '-s', 'SIGINT'], None)

        self.assertEqual(len(service.containers()), 1)
        # The container is still running. It has been only interrupted
        self.assertTrue(service.containers()[0].is_running)

    def test_kill_interrupted_service(self):
        self.command.dispatch(['up', '-d'], None)
        service = self.project.get_service('simple')
        self.command.dispatch(['kill', '-s', 'SIGINT'], None)
        self.assertTrue(service.containers()[0].is_running)

        self.command.dispatch(['kill', '-s', 'SIGKILL'], None)

        self.assertEqual(len(service.containers(stopped=True)), 1)
        self.assertFalse(service.containers(stopped=True)[0].is_running)

    def test_restart(self):
        service = self.project.get_service('simple')
        container = service.create_container()
        service.start_container(container)
        started_at = container.dictionary['State']['StartedAt']
        self.command.dispatch(['restart', '-t', '1'], None)
        container.inspect()
        self.assertNotEqual(
            container.dictionary['State']['FinishedAt'],
            '0001-01-01T00:00:00Z',
        )
        self.assertNotEqual(
            container.dictionary['State']['StartedAt'],
            started_at,
        )

    def test_scale(self):
        project = self.project

        self.command.scale(project, {'SERVICE=NUM': ['simple=1']})
        self.assertEqual(len(project.get_service('simple').containers()), 1)

        self.command.scale(project, {'SERVICE=NUM': ['simple=3', 'another=2']})
        self.assertEqual(len(project.get_service('simple').containers()), 3)
        self.assertEqual(len(project.get_service('another').containers()), 2)

        self.command.scale(project, {'SERVICE=NUM': ['simple=1', 'another=1']})
        self.assertEqual(len(project.get_service('simple').containers()), 1)
        self.assertEqual(len(project.get_service('another').containers()), 1)

        self.command.scale(project, {'SERVICE=NUM': ['simple=1', 'another=1']})
        self.assertEqual(len(project.get_service('simple').containers()), 1)
        self.assertEqual(len(project.get_service('another').containers()), 1)

        self.command.scale(project, {'SERVICE=NUM': ['simple=0', 'another=0']})
        self.assertEqual(len(project.get_service('simple').containers()), 0)
        self.assertEqual(len(project.get_service('another').containers()), 0)

    def test_port(self):

        self.command.base_dir = 'tests/fixtures/ports-composefile'
        self.command.dispatch(['up', '-d'], None)
        container = self.project.get_service('simple').get_container()

        @patch('sys.stdout', new_callable=StringIO)
        def get_port(number, mock_stdout):
            self.command.dispatch(['port', 'simple', str(number)], None)
            return mock_stdout.getvalue().rstrip()

        self.assertEqual(get_port(3000), container.get_local_port(3000))
        self.assertEqual(get_port(3001), "0.0.0.0:49152")
        self.assertEqual(get_port(3002), "")

    def test_env_file_relative_to_compose_file(self):
        config_path = os.path.abspath('tests/fixtures/env-file/docker-compose.yml')
        self.command.dispatch(['-f', config_path, 'up', '-d'], None)
        self._project = self.command.get_project(config_path)

        containers = self.project.containers(stopped=True)
        self.assertEqual(len(containers), 1)
        self.assertIn("FOO=1", containers[0].get('Config.Env'))

    def test_up_with_extends(self):
        self.command.base_dir = 'tests/fixtures/extends'
        self.command.dispatch(['up', '-d'], None)

        self.assertEqual(
            set([s.name for s in self.project.services]),
            set(['mydb', 'myweb']),
        )

        # Sort by name so we get [db, web]
        containers = sorted(
            self.project.containers(stopped=True),
            key=lambda c: c.name,
        )

        self.assertEqual(len(containers), 2)
        web = containers[1]

        self.assertEqual(set(web.links()), set(['db', 'mydb_1', 'extends_mydb_1']))

        expected_env = set([
            "FOO=1",
            "BAR=2",
            "BAZ=2",
        ])
        self.assertTrue(expected_env <= set(web.get('Config.Env')))
