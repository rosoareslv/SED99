# -*- coding: utf-8 -*-

# Copyright 2017 Sloane Hertel <shertel@redhat.com>
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

import pytest
import datetime

# Just to test that we have the prerequisite for InventoryModule and instance_data_filter_to_boto_attr
boto3 = pytest.importorskip('boto3')
botocore = pytest.importorskip('botocore')

from ansible.errors import AnsibleError
from ansible.plugins.inventory.aws_ec2 import InventoryModule
from ansible.plugins.inventory.aws_ec2 import instance_data_filter_to_boto_attr

instances = {
    u'Instances': [
        {u'Monitoring': {u'State': 'disabled'},
         u'PublicDnsName': 'ec2-12-345-67-890.compute-1.amazonaws.com',
         u'State': {u'Code': 16, u'Name': 'running'},
         u'EbsOptimized': False,
         u'LaunchTime': datetime.datetime(2017, 10, 31, 12, 59, 25),
         u'PublicIpAddress': '12.345.67.890',
         u'PrivateIpAddress': '098.76.54.321',
         u'ProductCodes': [],
         u'VpcId': 'vpc-12345678',
         u'StateTransitionReason': '',
         u'InstanceId': 'i-00000000000000000',
         u'EnaSupport': True,
         u'ImageId': 'ami-12345678',
         u'PrivateDnsName': 'ip-098-76-54-321.ec2.internal',
         u'KeyName': 'testkey',
         u'SecurityGroups': [{u'GroupName': 'default', u'GroupId': 'sg-12345678'}],
         u'ClientToken': '',
         u'SubnetId': 'subnet-12345678',
         u'InstanceType': 't2.micro',
         u'NetworkInterfaces': [
            {u'Status': 'in-use',
             u'MacAddress': '12:a0:50:42:3d:a4',
             u'SourceDestCheck': True,
             u'VpcId': 'vpc-12345678',
             u'Description': '',
             u'NetworkInterfaceId': 'eni-12345678',
             u'PrivateIpAddresses': [
                 {u'PrivateDnsName': 'ip-098-76-54-321.ec2.internal',
                  u'PrivateIpAddress': '098.76.54.321',
                  u'Primary': True,
                  u'Association':
                      {u'PublicIp': '12.345.67.890',
                       u'PublicDnsName': 'ec2-12-345-67-890.compute-1.amazonaws.com',
                       u'IpOwnerId': 'amazon'}}],
             u'PrivateDnsName': 'ip-098-76-54-321.ec2.internal',
             u'Attachment':
                 {u'Status': 'attached',
                  u'DeviceIndex': 0,
                  u'DeleteOnTermination': True,
                  u'AttachmentId': 'eni-attach-12345678',
                  u'AttachTime': datetime.datetime(2017, 10, 31, 12, 59, 25)},
             u'Groups': [
                 {u'GroupName': 'default',
                  u'GroupId': 'sg-12345678'}],
             u'Ipv6Addresses': [],
             u'OwnerId': '123456789000',
             u'PrivateIpAddress': '098.76.54.321',
             u'SubnetId': 'subnet-12345678',
             u'Association':
                {u'PublicIp': '12.345.67.890',
                 u'PublicDnsName': 'ec2-12-345-67-890.compute-1.amazonaws.com',
                 u'IpOwnerId': 'amazon'}}],
         u'SourceDestCheck': True,
         u'Placement':
            {u'Tenancy': 'default',
             u'GroupName': '',
             u'AvailabilityZone': 'us-east-1c'},
         u'Hypervisor': 'xen',
         u'BlockDeviceMappings': [
            {u'DeviceName': '/dev/xvda',
             u'Ebs':
                {u'Status': 'attached',
                 u'DeleteOnTermination': True,
                 u'VolumeId': 'vol-01234567890000000',
                 u'AttachTime': datetime.datetime(2017, 10, 31, 12, 59, 26)}}],
         u'Architecture': 'x86_64',
         u'RootDeviceType': 'ebs',
         u'RootDeviceName': '/dev/xvda',
         u'VirtualizationType': 'hvm',
         u'Tags': [{u'Value': 'test', u'Key': 'ansible'}, {u'Value': 'aws_ec2', u'Key': 'name'}],
         u'AmiLaunchIndex': 0}],
    u'ReservationId': 'r-01234567890000000',
    u'Groups': [],
    u'OwnerId': '123456789000'
}


def test_compile_values():
    inv = InventoryModule()
    found_value = instances['Instances'][0]
    chain_of_keys = instance_data_filter_to_boto_attr['instance.group-id']
    for attr in chain_of_keys:
        found_value = inv._compile_values(found_value, attr)
    assert found_value == "sg-12345678"


def test_get_boto_attr_chain():
    inv = InventoryModule()
    instance = instances['Instances'][0]
    assert inv._get_boto_attr_chain('network-interface.addresses.private-ip-address', instance) == "098.76.54.321"


def test_boto3_conn():
    inv = InventoryModule()
    inv._options = {"boto_profile": "first_precedence",
                    "aws_access_key_id": "test_access_key",
                    "aws_secret_access_key": "test_secret_key",
                    "aws_security_token": "test_security_token"}
    inv._set_credentials()
    with pytest.raises(AnsibleError) as error_message:
        for connection, region in inv._boto3_conn(regions=['us-east-1']):
            assert error_message == "Insufficient credentials found."


def test_get_hostname_default():
    inv = InventoryModule()
    instance = instances['Instances'][0]
    assert inv._get_hostname(instance, hostnames=None) == "ec2-12-345-67-890.compute-1.amazonaws.com"


def test_get_hostname():
    hostnames = ['ip-address', 'dns-name']
    inv = InventoryModule()
    instance = instances['Instances'][0]
    assert inv._get_hostname(instance, hostnames) == "12.345.67.890"


def test_set_credentials(monkeypatch):
    inv = InventoryModule()
    inv._options = {'aws_access_key_id': 'test_access_key',
                    'aws_secret_access_key': 'test_secret_key',
                    'aws_security_token': 'test_security_token',
                    'boto_profile': 'test_profile'}
    inv._set_credentials()

    assert inv.boto_profile == "test_profile"
    assert inv.aws_access_key_id == "test_access_key"
    assert inv.aws_secret_access_key == "test_secret_key"
    assert inv.aws_security_token == "test_security_token"


def test_insufficient_credentials(monkeypatch):
    inv = InventoryModule()
    with pytest.raises(AnsibleError) as error_message:
        inv._set_credentials()
        assert "Insufficient boto credentials found" in error_message
