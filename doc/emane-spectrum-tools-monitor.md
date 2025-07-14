Copyright (c) 2022 - Adjacent Link LLC, Bridgewater, New Jersey  
This work is licensed under [Creative Commons Attribution 4.0 International License][1].

[1]: https://creativecommons.org/licenses/by/4.0/

# Overview 

This document describes the output and formats for
`emane-spectrum-monitor` data.

# emane-spectrum-monitor

The `emane-spectrum-monitor` is a monitoring (receive only) EMANE NEM
instance that publishes and optionally records over-the-air energy as
seen at the receiver.

\bigskip
\footnotesize
```bash
$ emane-spectrum-monitor
    --realtime \
    --daemonize \
    --config emane-spectrum-monitor.xml \
    --loglevel 3 \
    --logfile/tmp/emane-spectrum-monitor.log \
    --spectrumquery.recorderfile /tmp/emane-spectrum-monitor.data
```
\vspace{-.2cm}
Sample `emane-spectrum-monitor` command line.
\normalsize

`emane-spectrum-monitor` data are stored as length prefix framed
serialized `SpectrumEnergy` messages defined using a Google Protocol
Buffer specification, where each serialized message is preceded by its
length as an unsigned 32-bit integer value (4 bytes) in network byte
order.

\bigskip
\footnotesize
```proto
syntax = "proto2";

package EMANESpectrumMonitor;

option optimize_for = SPEED;

message SpectrumEnergy
{
  message POV
  {
    message Position
    {
      required double latitude_degrees = 1;
      required double longitude_degrees = 2;
      required double altitude_meters = 3;
    }

    message Orientation
    {
      required double roll_degrees = 1;
      required double pitch_degrees = 2;
      required double yaw_degrees = 3;
    }

    message Velocity
    {
      required double azimuth_degrees = 1;
      required double elevation_degrees = 2;
      required double magnitude_meters_per_second = 3;
    }

    required Position position = 1;
    optional Orientation orientation = 2;
    optional Velocity velocity = 3;
  }

  message Antenna
  {
    message Pointing
    {
      required uint32 profile_id = 1;
      required double azimuth_degrees = 2;
      required double elevation_degrees = 3;
    }

    optional double fixed_gain_dbi = 1;
    optional Pointing pointing = 2;
  }

  message Entry
  {
    message Energy
    {
      required uint64 frequency_hz = 1;
      repeated double energy_mW = 2;
    }

    required uint32 subid = 1;
    required uint64 bandwidth_hz = 2;
    repeated Energy energies = 3;
  }

  required uint64 start_time = 1;
  required uint64 duration = 2;
  required uint64 sequence = 3;
  repeated Entry entries = 4;
  required Antenna antenna = 5;
  optional POV pov = 6;
}
```
\vspace{-.2cm}
`emane-spectrum-tools/src/libemane-spectrum-monitor/spectrummonitor.proto`
\normalsize

1. `start_time`: Specifies the start time of the energy measurement in
   microseconds since the epoch (1970-01-01 UTC).

2. `duration`: Specifies the duration of the energy measurement in
   microseconds.

3. `sequence`: Unique monitoring NEM sequence number for the energy
   measurement.

4. `entries`: One or more energy measurement groups, where each
   measurement group is uniquely identified by `subid` and contains
   one or more `energies` measurements identified by `frequency_hz`
   and containing one or more time binned receive energy measurements,
   `energy_mW`, in mW.
   
   Each time bin is `bandwidth_hz` wide with time boundaries that
   equally subdivide the time interval `start_time` to `start_time` +
   `duration`. The number of bins is defined via
   `emane-spectrum-monitor` configuration and defaults to 10 bins
   every 100msec (10ms bin duration).
   
5. `antenna`: Receive antenna information.

    1. `fixed_gain_dbi`: Fixed receive antenna gain in
       dBi (ideal omni). Optional and present only when `pointing` is not.

    2. `pointing`: Profile defined antenna pointing
       information. Optional and only present when `fixed_gain_dbi` is
       not.

        1. `antenna_profile_id`: Receive antenna profile id.

        2. `antenna_azimuth`: Profile defined antenna pointing
           information.

        3. `antenna_elevation`: Profile defined antenna pointing
           information.

6. `pov`: Monitoring NEM position, orientation, and velocity. Optional
   and only present when location events are published.

    1. `position`: Monitoring NEM's position.
    
        1. `latitude_degrees`: Monitoring NEM's latitude in degrees.

        2. `longitude_degrees`: Monitoring NEM's longitude in degrees.

        3. `altitude_meters`: Monitoring NEM's altitude in meters.


    2. `velocity`: Monitoring NEM's velocity. Optional and only
       present when location events containing velocity are published.
    
        1. `azimuth_degrees`: Monitoring NEM's velocity azimuth in
           degrees.

        2. `elevation_degree`: Monitoring NEM's velocity elevation in
           degrees.

        3. `magnitude_meters_per_second`: Monitoring NEM's velocity
           magnitude in meters per second.

    3. `orientation`: Monitoring NEM's orientation. Optional and only
       present when location events containing orientation are
       published.
       
        1. `roll_degrees`: Monitoring NEM's orientation roll in
           degrees.

        2. `pitch_degrees`: Monitoring NEM's orientation pitch in degrees.

        3. `yaw_degrees`: Monitoring NEM's orientation yaw in degrees.


# emane-spectrum-energy-recording-tool

The `emane-spectrum-energy-recording-tool` converts an
`emane-spectrum-monitor` data file into a long format tidy data set as
either a CSV or [SQLite][2] database file. Both output formats contain
the same information. Each energy measurement is converted into
multiple row entries (long format), where each row contains the center
frequency and time binned energy measurements uniquely identified by
`start_time`, `sequence`, `phy_sub_id`, and `frequency_hz`.

1. `start_time`: (`INT`) Specifies the start time of the energy
   measurement in microseconds since the epoch (1970-01-01 UTC).

2. `duration`: (`INT`) Specifies the duration of the energy
   measurement in microseconds.

3. `sequence`: (`INT`) Unique monitoring NEM sequence number
   for the energy measurement.

4. `phy_sub_id`: (`INT`) Used to differentiate between emulator
   physical layer instances for different waveforms.

5. `bandwidth_hz`: (`INT`) Transmitter bandwidth in Hz.

6. `frequency_hz`: (`INT`) Transmitter center frequency in Hz.

7. `fixed_gain_dbi`: (`DOUBLE`) Fixed transmit antenna gain in
    dBi (ideal omni). Optional and present only when profile defined
    antenna information: `antenna_profile_id`, `antenna_azimuth`, and
    `antenna_elevation` are not.

8. `antenna_profile_id`: (`INT`) Receive antenna profile
    id. Optional and only present when `fixed_gain_dbi` is not.

9. `antenna_azimuth`: (`DOUBLE`) Profile defined antenna
    pointing information. Optional and only present when
    `fixed_gain_dbi` is not.

10. `antenna_elevation`: (`DOUBLE`) Profile defined antenna
    pointing information. Optional and only present when
    `fixed_gain_dbi` is not.

11. `latitude_degrees`: (`DOUBLE`) Monitoring NEM's latitude in
    degrees. Optional and only present when location events are
    published.

12. `longitude_degrees`: (`DOUBLE`) Monitoring NEM's longitude
    in degrees. Optional and only present when location events are
    published.

13. `altitude_meters`: (`DOUBLE`) Monitoring NEM's altitude in
    meters. Optional and only present when location events are
    published.

14. `azimuth_degrees`: (`DOUBLE`) Monitoring NEM's velocity
    azimuth in degrees. Optional and only present when location events
    containing velocity are published.

15. `elevation_degree`: (`DOUBLE`) Monitoring NEM's velocity
    elevation in degrees. Optional and only present when location
    events containing velocity are published.

16. `magnitude_meters_per_second`: (`DOUBLE`) Monitoring NEM's
    velocity magnitude in meters per second. Optional and only present
    when location events containing velocity are published.

17. `roll_degrees`: (`DOUBLE`) Monitoring NEM's orientation
    roll in degrees. Optional and only present when location events
    containing orientation are published.

18. `pitch_degrees`: (`DOUBLE`) Monitoring NEM's orientation
    pitch in degrees. Optional and only present when location events
    containing orientation are published.

19. `yaw_degrees`: (`DOUBLE`) Monitoring NEM's orientation yaw
    in degrees. Optional and only present when location events
    containing orientation are published.

20. `bin_0` - `bin_N`: (`DOUBLE`) Time binned receive energy in
    mW. Each bin is `bandwidth_hz` wide with time boundaries that
    equally subdivide the time interval `start_time` to `start_time` +
    `duration`. The number of bins is defined via
    `emane-spectrum-monitor` configuration and defaults to 10 bins
    every 100msec (10ms bin duration).

More information on [platform orientation][3] and [profile defined
antennas][4] can be found on the EMANE Wiki.


[2]: https://sqlite.org/index.html
[3]: https://github.com/adjacentlink/emane/wiki/Platform-Orientation
[4]: https://github.com/adjacentlink/emane/wiki/Antenna-Profile

\bigskip
\footnotesize
```sql
CREATE TABLE energy (start_time INT,
                     duration INT,
                     sequence INT,
                     phy_sub_id INT,
                     bandwidth_hz INT,
                     frequency_hz INT,
                     fixed_gain_dbi DOUBLE,
                     antenna_profile_id INT,
                     antenna_azimuth DOUBLE,
                     antenna_elevation DOUBLE,
                     latitude_degrees DOUBLE,
                     longitude_degrees DOUBLE,
                     altitude_meters DOUBLE,
                     azimuth_degrees DOUBLE,
                     elevation_degrees DOUBLE,
                     magnitude_meters_per_second DOUBLE,
                     roll_degrees DOUBLE,
                     pitch_degrees DOUBLE,
                     yaw_degrees DOUBLE,
                     bin_0 DOUBLE,
                     bin_1 DOUBLE,
                     bin_2 DOUBLE,
                     bin_3 DOUBLE,
                     bin_4 DOUBLE,
                     bin_5 DOUBLE,
                     bin_6 DOUBLE,
                     bin_7 DOUBLE,
                     bin_8 DOUBLE,
                     bin_9 DOUBLE,
                     PRIMARY KEY (start_time,sequence,phy_sub_id,frequency_hz));
```
\vspace{-.2cm}
SQLite schema used by `emane-spectrum-monitor`.
\normalsize

# Limitations

The following are known limitations of the `emane-spectrum-monitor`:

1. All emulator physical instances using the same subid must have the
   same transmit bandwidth in order for reported
   `emane-spectrum-monitor` data to convey the correct
   bandwidth. It is valid to use any combination of physical instances
   with the same subid using different spectral masks, including the
   default square mask, provided that the width of the primary signal
   is the same for all.
   
2. The `emane-spectrum-monitor` auto discovers and reports
   energy based on the center frequency of the transmitter's primary
   signal. Any spurs defined via spectral masks are not reported,
   however any spur energy that contributes within a primary signal's
   band is represented in the measurement.
