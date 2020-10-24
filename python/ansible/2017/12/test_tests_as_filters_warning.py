from ansible.template import Templar, display
from units.mock.loader import DictDataLoader


def test_tests_as_filters_warning(mocker):
    fake_loader = DictDataLoader({
        "/path/to/my_file.txt": "foo\n",
    })
    templar = Templar(loader=fake_loader, variables={})
    filters = templar._get_filters()

    mocker.patch.object(display, 'deprecated')

    # Call successful test, ensure the message is correct
    filters['successful']({})
    display.deprecated.assert_called_once_with(
        'Using tests as filters is deprecated. Instead of using `result|successful` instead use `result is successful`', version='2.9'
    )

    # Call success test, ensure the message is correct
    display.deprecated.reset_mock()
    filters['success']({})
    display.deprecated.assert_called_once_with(
        'Using tests as filters is deprecated. Instead of using `result|success` instead use `result is success`', version='2.9'
    )

    # Call bool filter, ensure no deprecation message was displayed
    display.deprecated.reset_mock()
    filters['bool'](True)
    assert display.deprecated.call_count == 0
