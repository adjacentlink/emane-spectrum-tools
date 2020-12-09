#
# Copyright (c) 2019-2020 - Adjacent Link LLC, Bridgewater, New Jersey
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# * Redistributions of source code must retain the above copyright
#   notice, this list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright
#   notice, this list of conditions and the following disclaimer in
#   the documentation and/or other materials provided with the
#   distribution.
# * Neither the name of Adjacent Link LLC nor the names of its
#   contributors may be used to endorse or promote products derived
#   from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
# COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
# ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#

from __future__ import absolute_import, division, print_function

import zmq
import threading
import traceback
import copy
from collections import namedtuple
from collections import defaultdict

import emane_spectrum_tools.interface.spectrummonitor_pb2 as spectrummonitor_pb2

class SpectrumEnergyStreamer(object):
    class SubIdInfo(namedtuple('SubIdInfo',['bandwidth_hz'])):
        pass

    def __init__(self,endpoint):
        self._endpoint = endpoint
        self._cancel_event = threading.Event()

        self._receiver_sensitivity_dBm = 0
        self._subid_info = defaultdict(lambda : SpectrumEnergyStreamer.SubIdInfo(0))
        self._lock = threading.Lock()
        self._first = True
        self._store = defaultdict(lambda : defaultdict(lambda : 0))

    def run(self):
        thread = threading.Thread(target=self._run)

        thread.setDaemon(True)

        thread.start()

    def _run(self):
        context = zmq.Context()

        subscriber = context.socket(zmq.SUB)

        subscriber.connect("tcp://"+self._endpoint)

        subscriber.setsockopt(zmq.SUBSCRIBE, b'')

        poller = zmq.Poller()

        poller.register(subscriber, zmq.POLLIN)

        try:
            while not self._cancel_event.is_set():
                timeout=100

                socks = dict(poller.poll(timeout))

                if (subscriber in socks and socks[subscriber] == zmq.POLLIN):

                    msgs = subscriber.recv_multipart()

                    self.update(msgs[1])

        except KeyboardInterrupt:
            print(traceback.format_exc())

    def stop(self):
        self._cancel_event.set()

    def update(self,serialized_measurement):

        measurement = spectrummonitor_pb2.SpectrumEnergy()

        measurement.ParseFromString(serialized_measurement)

        subid_bandwidth_map = {}

        self._lock.acquire()

        if self._first:
            self._first = False

        for entry in measurement.entries:
            subid_bandwidth_map[entry.subid] = entry.bandwidth_hz

            for energy in entry.energies:

                energy_mW = max(energy.energy_mW)

                self._store[energy.frequency_hz][entry.subid] = max(self._store[energy.frequency_hz][entry.subid],
                                                                    energy_mW)

        for subid,bandwidth_hz in list(subid_bandwidth_map.items()):
            if self._subid_info[subid].bandwidth_hz != bandwidth_hz:
                self._subid_info[subid] = self._subid_info[subid]._replace(bandwidth_hz=bandwidth_hz)

        self._lock.release()

    def data(self):
        self._lock.acquire()

        ret = (copy.copy(self._subid_info),
               copy.copy(self._store))

        self._store.clear()

        self._lock.release()

        return ret
