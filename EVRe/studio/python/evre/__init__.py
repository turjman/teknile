# SPDX-License-Identifier: Apache-2.0
"""evre: EVRe devices from Python, by register name, with the device's map.

    import evre
    with evre.connect_tcp('127.0.0.1', 1210, 'maps/example_device.json', token='example-token') as dev:
        print(dev['SUPPLY_V'], dev.decoded('STATE'))
        dev['LED_MODE'] = 'blink'

Several devices on one link (a bus file, as EVRe Studio writes it):

    with evre.connect_bus_tcp('127.0.0.1', 1231, 'maps/example_bus.json', token='example-token') as bus:
        print(bus['D1_SUPPLY_V'], bus['D2']['SUPPLY_V'])
        bus.broadcast('FAN_SPEED', 0)   # every device at once, then each read back

Through EVRe Studio's JSON API (port 1220): registers by name, and a fast stream's channels as min, max and mean:

    with evre.connect_studio() as studio:
        print(studio.get('SUPPLY_V', 'ADC.I_LOAD'))
        for t, summary in studio.fast_stream(['ADC.I_LOAD'], period_ms=100):
            print(t, summary['ADC.I_LOAD'].mean)

A fast stream's recording (Fast EVRe, a .evrs file of EVRe Studio or `evre record`):

    rec = evre.read_recording('run.ADC.evrs')
    print(len(rec.times), rec.lost, rec.values['I_LOAD'][:10])

Standard library only; a serial port needs pyserial. The protocol is
docs/PROTOCOL.md of EVRe, the map format docs/MAP_FORMAT.md of EVRe Studio.
"""
from .bus import Bus, connect_bus_serial, connect_bus_tcp
from .device import Device, connect_serial, connect_tcp, load_map
from .device_map import DeviceMap, Field, Register
from .fast import Recording, read_recording
from .frame import Parser, build, crc16
from .link import EvreError, Master, SerialLink, TcpLink
from .studio import Line, Studio, Summary, connect_studio

__version__ = '1.0.0'
__all__ = ['Bus', 'Device', 'DeviceMap', 'EvreError', 'Field', 'Line', 'Master', 'Parser', 'Recording', 'Register',
           'SerialLink', 'Studio', 'Summary', 'TcpLink', 'build', 'connect_bus_serial', 'connect_bus_tcp', 'connect_serial',
           'connect_studio', 'connect_tcp', 'crc16', 'load_map', 'read_recording']
