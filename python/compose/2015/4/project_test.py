from __future__ import unicode_literals
from compose import config
from compose.project import Project
from compose.container import Container
from .testcases import DockerClientTestCase


class ProjectTest(DockerClientTestCase):
    def test_volumes_from_service(self):
        service_dicts = config.from_dictionary({
            'data': {
                'image': 'busybox:latest',
                'volumes': ['/var/data'],
            },
            'db': {
                'image': 'busybox:latest',
                'volumes_from': ['data'],
            },
        }, working_dir='.')
        project = Project.from_dicts(
            name='composetest',
            service_dicts=service_dicts,
            client=self.client,
        )
        db = project.get_service('db')
        data = project.get_service('data')
        self.assertEqual(db.volumes_from, [data])

    def test_volumes_from_container(self):
        data_container = Container.create(
            self.client,
            image='busybox:latest',
            volumes=['/var/data'],
            name='composetest_data_container',
        )
        project = Project.from_dicts(
            name='composetest',
            service_dicts=config.from_dictionary({
                'db': {
                    'image': 'busybox:latest',
                    'volumes_from': ['composetest_data_container'],
                },
            }),
            client=self.client,
        )
        db = project.get_service('db')
        self.assertEqual(db.volumes_from, [data_container])

        project.kill()
        project.remove_stopped()

    def test_net_from_service(self):
        project = Project.from_dicts(
            name='composetest',
            service_dicts=config.from_dictionary({
                'net': {
                    'image': 'busybox:latest',
                    'command': ["/bin/sleep", "300"]
                },
                'web': {
                    'image': 'busybox:latest',
                    'net': 'container:net',
                    'command': ["/bin/sleep", "300"]
                },
            }),
            client=self.client,
        )

        project.up()

        web = project.get_service('web')
        net = project.get_service('net')
        self.assertEqual(web._get_net(), 'container:' + net.containers()[0].id)

        project.kill()
        project.remove_stopped()

    def test_net_from_container(self):
        net_container = Container.create(
            self.client,
            image='busybox:latest',
            name='composetest_net_container',
            command='/bin/sleep 300'
        )
        net_container.start()

        project = Project.from_dicts(
            name='composetest',
            service_dicts=config.from_dictionary({
                'web': {
                    'image': 'busybox:latest',
                    'net': 'container:composetest_net_container'
                },
            }),
            client=self.client,
        )

        project.up()

        web = project.get_service('web')
        self.assertEqual(web._get_net(), 'container:' + net_container.id)

        project.kill()
        project.remove_stopped()

    def test_start_stop_kill_remove(self):
        web = self.create_service('web')
        db = self.create_service('db')
        project = Project('composetest', [web, db], self.client)

        project.start()

        self.assertEqual(len(web.containers()), 0)
        self.assertEqual(len(db.containers()), 0)

        web_container_1 = web.create_container()
        web_container_2 = web.create_container()
        db_container = db.create_container()

        project.start(service_names=['web'])
        self.assertEqual(set(c.name for c in project.containers()), set([web_container_1.name, web_container_2.name]))

        project.start()
        self.assertEqual(set(c.name for c in project.containers()), set([web_container_1.name, web_container_2.name, db_container.name]))

        project.stop(service_names=['web'], timeout=1)
        self.assertEqual(set(c.name for c in project.containers()), set([db_container.name]))

        project.kill(service_names=['db'])
        self.assertEqual(len(project.containers()), 0)
        self.assertEqual(len(project.containers(stopped=True)), 3)

        project.remove_stopped(service_names=['web'])
        self.assertEqual(len(project.containers(stopped=True)), 1)

        project.remove_stopped()
        self.assertEqual(len(project.containers(stopped=True)), 0)

    def test_project_up(self):
        web = self.create_service('web')
        db = self.create_service('db', volumes=['/var/db'])
        project = Project('composetest', [web, db], self.client)
        project.start()
        self.assertEqual(len(project.containers()), 0)

        project.up(['db'])
        self.assertEqual(len(project.containers()), 1)
        self.assertEqual(len(db.containers()), 1)
        self.assertEqual(len(web.containers()), 0)

        project.kill()
        project.remove_stopped()

    def test_project_up_recreates_containers(self):
        web = self.create_service('web')
        db = self.create_service('db', volumes=['/etc'])
        project = Project('composetest', [web, db], self.client)
        project.start()
        self.assertEqual(len(project.containers()), 0)

        project.up(['db'])
        self.assertEqual(len(project.containers()), 1)
        old_db_id = project.containers()[0].id
        db_volume_path = project.containers()[0].get('Volumes./etc')

        project.up()
        self.assertEqual(len(project.containers()), 2)

        db_container = [c for c in project.containers() if 'db' in c.name][0]
        self.assertNotEqual(db_container.id, old_db_id)
        self.assertEqual(db_container.get('Volumes./etc'), db_volume_path)

        project.kill()
        project.remove_stopped()

    def test_project_up_with_no_recreate_running(self):
        web = self.create_service('web')
        db = self.create_service('db', volumes=['/var/db'])
        project = Project('composetest', [web, db], self.client)
        project.start()
        self.assertEqual(len(project.containers()), 0)

        project.up(['db'])
        self.assertEqual(len(project.containers()), 1)
        old_db_id = project.containers()[0].id
        db_volume_path = project.containers()[0].inspect()['Volumes']['/var/db']

        project.up(recreate=False)
        self.assertEqual(len(project.containers()), 2)

        db_container = [c for c in project.containers() if 'db' in c.name][0]
        self.assertEqual(db_container.id, old_db_id)
        self.assertEqual(db_container.inspect()['Volumes']['/var/db'],
                         db_volume_path)

        project.kill()
        project.remove_stopped()

    def test_project_up_with_no_recreate_stopped(self):
        web = self.create_service('web')
        db = self.create_service('db', volumes=['/var/db'])
        project = Project('composetest', [web, db], self.client)
        project.start()
        self.assertEqual(len(project.containers()), 0)

        project.up(['db'])
        project.stop()

        old_containers = project.containers(stopped=True)

        self.assertEqual(len(old_containers), 1)
        old_db_id = old_containers[0].id
        db_volume_path = old_containers[0].inspect()['Volumes']['/var/db']

        project.up(recreate=False)

        new_containers = project.containers(stopped=True)
        self.assertEqual(len(new_containers), 2)

        db_container = [c for c in new_containers if 'db' in c.name][0]
        self.assertEqual(db_container.id, old_db_id)
        self.assertEqual(db_container.inspect()['Volumes']['/var/db'],
                         db_volume_path)

        project.kill()
        project.remove_stopped()

    def test_project_up_without_all_services(self):
        console = self.create_service('console')
        db = self.create_service('db')
        project = Project('composetest', [console, db], self.client)
        project.start()
        self.assertEqual(len(project.containers()), 0)

        project.up()
        self.assertEqual(len(project.containers()), 2)
        self.assertEqual(len(db.containers()), 1)
        self.assertEqual(len(console.containers()), 1)

        project.kill()
        project.remove_stopped()

    def test_project_up_starts_links(self):
        console = self.create_service('console')
        db = self.create_service('db', volumes=['/var/db'])
        web = self.create_service('web', links=[(db, 'db')])

        project = Project('composetest', [web, db, console], self.client)
        project.start()
        self.assertEqual(len(project.containers()), 0)

        project.up(['web'])
        self.assertEqual(len(project.containers()), 2)
        self.assertEqual(len(web.containers()), 1)
        self.assertEqual(len(db.containers()), 1)
        self.assertEqual(len(console.containers()), 0)

        project.kill()
        project.remove_stopped()

    def test_project_up_starts_depends(self):
        project = Project.from_dicts(
            name='composetest',
            service_dicts=config.from_dictionary({
                'console': {
                    'image': 'busybox:latest',
                    'command': ["/bin/sleep", "300"],
                },
                'data': {
                    'image': 'busybox:latest',
                    'command': ["/bin/sleep", "300"]
                },
                'db': {
                    'image': 'busybox:latest',
                    'command': ["/bin/sleep", "300"],
                    'volumes_from': ['data'],
                },
                'web': {
                    'image': 'busybox:latest',
                    'command': ["/bin/sleep", "300"],
                    'links': ['db'],
                },
            }),
            client=self.client,
        )
        project.start()
        self.assertEqual(len(project.containers()), 0)

        project.up(['web'])
        self.assertEqual(len(project.containers()), 3)
        self.assertEqual(len(project.get_service('web').containers()), 1)
        self.assertEqual(len(project.get_service('db').containers()), 1)
        self.assertEqual(len(project.get_service('data').containers()), 1)
        self.assertEqual(len(project.get_service('console').containers()), 0)

        project.kill()
        project.remove_stopped()

    def test_project_up_with_no_deps(self):
        project = Project.from_dicts(
            name='composetest',
            service_dicts=config.from_dictionary({
                'console': {
                    'image': 'busybox:latest',
                    'command': ["/bin/sleep", "300"],
                },
                'data': {
                    'image': 'busybox:latest',
                    'command': ["/bin/sleep", "300"]
                },
                'db': {
                    'image': 'busybox:latest',
                    'command': ["/bin/sleep", "300"],
                    'volumes_from': ['data'],
                },
                'web': {
                    'image': 'busybox:latest',
                    'command': ["/bin/sleep", "300"],
                    'links': ['db'],
                },
            }),
            client=self.client,
        )
        project.start()
        self.assertEqual(len(project.containers()), 0)

        project.up(['db'], start_deps=False)
        self.assertEqual(len(project.containers(stopped=True)), 2)
        self.assertEqual(len(project.get_service('web').containers()), 0)
        self.assertEqual(len(project.get_service('db').containers()), 1)
        self.assertEqual(len(project.get_service('data').containers()), 0)
        self.assertEqual(len(project.get_service('data').containers(stopped=True)), 1)
        self.assertEqual(len(project.get_service('console').containers()), 0)

        project.kill()
        project.remove_stopped()

    def test_unscale_after_restart(self):
        web = self.create_service('web')
        project = Project('composetest', [web], self.client)

        project.start()

        service = project.get_service('web')
        service.scale(1)
        self.assertEqual(len(service.containers()), 1)
        service.scale(3)
        self.assertEqual(len(service.containers()), 3)
        project.up()
        service = project.get_service('web')
        self.assertEqual(len(service.containers()), 3)
        service.scale(1)
        self.assertEqual(len(service.containers()), 1)
        project.up()
        service = project.get_service('web')
        self.assertEqual(len(service.containers()), 1)
        # does scale=0 ,makes any sense? after recreating at least 1 container is running
        service.scale(0)
        project.up()
        service = project.get_service('web')
        self.assertEqual(len(service.containers()), 1)
        project.kill()
        project.remove_stopped()
