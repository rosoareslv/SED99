# Copyright 2013 Amazon.com, Inc. or its affiliates. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License"). You
# may not use this file except in compliance with the License. A copy of
# the License is located at
#
#     http://aws.amazon.com/apache2.0/
#
# or in the "license" file accompanying this file. This file is
# distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF
# ANY KIND, either express or implied. See the License for the specific
# language governing permissions and limitations under the License.
from awscli.testutils import unittest

from botocore.exceptions import DataNotFoundError
import mock

from awscli.customizations import paginate


class TestPaginateBase(unittest.TestCase):

    def setUp(self):
        self.session = mock.Mock()
        self.paginator_model = mock.Mock()
        self.pagination_config = {
            'input_token': 'Foo',
            'limit_key': 'Bar',
        }
        self.paginator_model.get_paginator.return_value = \
            self.pagination_config
        self.session.get_paginator_model.return_value = self.paginator_model

        self.operation_model = mock.Mock()
        self.foo_param = mock.Mock()
        self.foo_param.name = 'Foo'
        self.foo_param.type_name = 'string'
        self.bar_param = mock.Mock()
        self.bar_param.type_name = 'string'
        self.bar_param.name = 'Bar'
        self.params = [self.foo_param, self.bar_param]
        self.operation_model.input_shape.members = {"Foo": self.foo_param,
                                                    "Bar": self.bar_param}


class TestArgumentTableModifications(TestPaginateBase):

    def test_customize_arg_table(self):
        argument_table = {
            'foo': mock.Mock(),
            'bar': mock.Mock(),
        }
        paginate.unify_paging_params(argument_table, self.operation_model,
                                     'building-argument-table.foo.bar',
                                     self.session)
        # We should mark the built in input_token as 'hidden'.
        self.assertTrue(argument_table['foo']._UNDOCUMENTED)
        # Also need to hide the limit key.
        self.assertTrue(argument_table['bar']._UNDOCUMENTED)
        # We also need to inject startin-token and max-items.
        self.assertIn('starting-token', argument_table)
        self.assertIn('max-items', argument_table)
        self.assertIn('page-size', argument_table)
        # And these should be PageArguments.
        self.assertIsInstance(argument_table['starting-token'],
                              paginate.PageArgument)
        self.assertIsInstance(argument_table['max-items'],
                              paginate.PageArgument)
        self.assertIsInstance(argument_table['page-size'],
                              paginate.PageArgument)

    def test_operation_with_no_paginate(self):
        # Operations that don't paginate are left alone.
        self.paginator_model.get_paginator.side_effect = ValueError()
        argument_table = {
            'foo': 'FakeArgObject',
            'bar': 'FakeArgObject',
        }
        starting_table = argument_table.copy()
        paginate.unify_paging_params(argument_table, self.operation_model,
                                     'building-argument-table.foo.bar',
                                     self.session)
        self.assertEqual(starting_table, argument_table)

    def test_service_with_no_paginate(self):
        # Operations that don't paginate are left alone.
        self.session.get_paginator_model.side_effect = \
            DataNotFoundError(data_path='foo.paginators.json')
        argument_table = {
            'foo': 'FakeArgObject',
            'bar': 'FakeArgObject',
        }
        starting_table = argument_table.copy()
        paginate.unify_paging_params(argument_table, self.operation_model,
                                     'building-argument-table.foo.bar',
                                     self.session)
        self.assertEqual(starting_table, argument_table)


class TestStringLimitKey(TestPaginateBase):

    def setUp(self):
        super(TestStringLimitKey, self).setUp()
        self.bar_param.type_name = 'string'

    def test_integer_limit_key(self):
        argument_table = {
            'foo': mock.Mock(),
            'bar': mock.Mock(),
        }
        paginate.unify_paging_params(argument_table, self.operation_model,
                                     'building-argument-table.foo.bar',
                                     self.session)
        # Max items should be the same type as bar, which may not be an int
        self.assertEqual('string', argument_table['max-items'].cli_type_name)


class TestIntegerLimitKey(TestPaginateBase):

    def setUp(self):
        super(TestIntegerLimitKey, self).setUp()
        self.bar_param.type_name = 'integer'

    def test_integer_limit_key(self):
        argument_table = {
            'foo': mock.Mock(),
            'bar': mock.Mock(),
        }
        paginate.unify_paging_params(argument_table, self.operation_model,
                                     'building-argument-table.foo.bar',
                                     self.session)
        # Max items should be the same type as bar, which may not be an int
        self.assertEqual('integer', argument_table['max-items'].cli_type_name)


class TestBadLimitKey(TestPaginateBase):

    def setUp(self):
        super(TestBadLimitKey, self).setUp()
        self.bar_param.type_name = 'bad'

    def test_integer_limit_key(self):
        argument_table = {
            'foo': mock.Mock(),
            'bar': mock.Mock(),
        }
        with self.assertRaises(TypeError):
            paginate.unify_paging_params(argument_table, self.operation_model,
                                         'building-argument-table.foo.bar',
                                         self.session)


class TestShouldEnablePagination(TestPaginateBase):
    def setUp(self):
        super(TestShouldEnablePagination, self).setUp()
        self.parsed_globals = mock.Mock()
        self.parsed_args = mock.Mock()

    def test_should_not_enable_pagination(self):
        # Here the user has specified a manual pagination argument,
        # so we should turn pagination off.
        # From setUp(), the limit_key is 'Bar'
        input_tokens = ['foo', 'bar']
        self.parsed_globals.paginate = True
        # Corresponds to --bar 10
        self.parsed_args.foo = None
        self.parsed_args.bar = 10
        paginate.check_should_enable_pagination(
            input_tokens, {}, {}, self.parsed_args, self.parsed_globals)
        # We should have turned paginate off because the
        # user specified --bar 10
        self.assertFalse(self.parsed_globals.paginate)

    def test_should_enable_pagination_with_no_args(self):
        input_tokens = ['foo', 'bar']
        self.parsed_globals.paginate = True
        # Corresponds to not specifying --foo nor --bar
        self.parsed_args.foo = None
        self.parsed_args.bar = None
        paginate.check_should_enable_pagination(
            input_tokens, {}, {}, self.parsed_args, self.parsed_globals)
        # We should have turned paginate off because the
        # user specified --bar 10
        self.assertTrue(self.parsed_globals.paginate)

    def test_default_to_pagination_on_when_ambiguous(self):
        input_tokens = ['foo', 'max-items']
        self.parsed_globals.paginate = True
        # Here the user specifies --max-items 10 This is ambiguous because the
        # input_token also contains 'max-items'.  Should we assume they want
        # pagination turned off or should we assume that this is the normalized
        # --max-items?
        # Will we default to assuming they meant the normalized
        # --max-items.
        self.parsed_args.foo = None
        self.parsed_args.max_items = 10
        paginate.check_should_enable_pagination(
            input_tokens, {}, {}, self.parsed_args, self.parsed_globals)
        self.assertTrue(self.parsed_globals.paginate,
                        "Pagination was not enabled.")

    def test_shadowed_args_are_replaced_when_pagination_off(self):
        input_tokens = ['foo', 'bar']
        self.parsed_globals.paginate = True
        # Corresponds to --bar 10
        self.parsed_args.foo = None
        self.parsed_args.bar = 10
        shadowed_args = {'foo': mock.sentinel.ORIGINAL_ARG}
        arg_table = {'foo': mock.sentinel.PAGINATION_ARG}
        paginate.check_should_enable_pagination(
            input_tokens, shadowed_args, arg_table,
            self.parsed_args, self.parsed_globals)
        # We should have turned paginate off because the
        # user specified --bar 10
        self.assertFalse(self.parsed_globals.paginate)
        self.assertEqual(arg_table['foo'], mock.sentinel.ORIGINAL_ARG)
