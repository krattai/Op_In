#!/bin/bash
# installs base virtualized p2p system
#
# Copyright (C) 2017 Uvea I. S., Kevin Rattai
#
# The install script is borrowed from noo-ebs so needs to be changed
# but in many ways, expect noo-ebs would be part of the core
#
# Obviously, in this first instance of noo-ebs for test and
# production, this script installs MQTT
#
# The goal is to eventually move to, or at least add as a
# supplemental, a peer to peer message queue system
#
# The following example is for installing openstack as a MAAS, from ubuntu:
# http://ronaldbradford.com/blog/installing-ubuntu-openstack-2015-06-01/
#
# Run as sudo
#
# apt-add-repository may not be installed, so need to

apt-get update
apt-get install -y software-properties-common

apt-add-repository -y ppa:cloud-installer/stable
apt-get update
apt-get install -y openstack
openstack-install --version
openstack-install

# sudo apt-get -y install mosquitto python-mosquitto mosquitto-clients dnsutils
