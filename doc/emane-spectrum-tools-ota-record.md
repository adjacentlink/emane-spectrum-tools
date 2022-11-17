Copyright (c) 2022 - Adjacent Link LLC, Bridgewater, New Jersey  
This work is licensed under [Creative Commons Attribution 4.0 International License][1].

[1]: https://creativecommons.org/licenses/by/4.0/

# Overview 

This document describes the output and formats for
`emane-spectrum-ota-recorder` data.

# emane-spectrum-ota-recorder

The `emane-spectrum-ota-recorder` subscribes to both the EMANE
over-the-air and event multicast groups and records the emulator
physical layer common header and common radio model (MAC) header for
each over-the-air frame and all published events.

\bigskip
\footnotesize
```bash
$ emane-spectrun-ota-recorder \
    --realtime \
    --daemonize \
    ---loglevel 3 \
    --logfile /tmp/emane-spectrum-ota-recorder.log \
    --eventservicedevice backchan0 \
    --eventservicegroup 224.1.2.8:45703 \
    --otamanagerdevice ota0 \
    --otamanagergroup 224.1.2.8:45702 \
    /tmp/emane-ota-recorder.data
```
\vspace{-.2cm}
Sample `emane-spectrum-ota-recorder` command line.
\normalsize

`emane-spectrum-ota-recorder` data are stored as length prefix framed
serialized `Record` messages defined using a Google Protocol Buffer
specification, where each serialized message is preceded by its length
as an unsigned 32-bit integer value (4 bytes) in network byte order.

\bigskip
\footnotesize
```proto
syntax = "proto2";

package EMANESpectrumTools;

option optimize_for = SPEED;

message Record
{
  message OTA
  {
    required uint32 source = 1;
    required uint32 destination = 2;
    required uint64 sequence = 3;
    required bytes common_phy_header = 4;
    required bytes common_mac_header = 5;
  };

  message Event
  {
    required bytes event_data = 1;
  };

  enum Type
  {
    TYPE_OTA = 1;
    TYPE_EVENT = 2;
  };

  required Type type = 1;
  required uint64 timestamp = 2;
  optional OTA ota = 3;
  optional Event event = 4;
}
```
\vspace{-.2cm}
`emane-spectrum-tools/src/emane-spectrum-ota-recorder/record.proto`
\normalsize

1. `type`: Specifies either `TYPE_OTA` or `TYPE_EVENT`
   corresponding to the presence of `ota` or `event`,
   for an over-the-air record or an event record, respectively.

2. `timestamp`: Specifies the time of the record in microseconds since
   the epoch (1970-01-01 UTC).

3. `ota`: Over-the-air frame data when `type` is `TYPE_OTA`.

    1. `source`: NEM id of the transmitting node.
    
    2. `destination`: NEM id of the destination node or 65535 for
       broadcast.
   
    3. `sequence`: Unique per source over-the-air sequence number for
       the transmission.
    
    4. `common_phy_header`: Serialized emulator framework header.  
       `emane/src/libemane/commonphyheader.proto`.
    
    5. `common_mac_header`: Serialized common radio model (MAC)
       header.  
       `emane/src/libemane/commonmacheader.proto`.


4. `event`: Event data when `type` is `TYPE_EVENT`.

    1. `event_data`: Serialized event.  
       `emane/src/libemane/event.proto`  
       
       With `serializations` being one of:
       
        1. `antennaprofileevent.proto`
        2. `commeffectevent.proto`
        3. `fadingselectionevent.proto`
        4. `locationevent.proto`
        5. `pathlossevent.proto`
        6. `tdmascheduleevent.proto`

       based on `eventId`: `emane/include/emane/events/eventids.h`.
       
# emane-spectrum-ota-record-tool

The `emane-spectrum-ota-record-tool` converts an
`emane-spectrum-ota-recorder` data file into a long
format tidy data set as either a CSV or [SQLite][2] database
file. Both output formats contain the same information. Each
over-the-air frame is converted into multiple row entries (long
format), where each row contains the start-of-transmission (SoT) and
end-of-transmission (EoT) time for an individual frequency segment
uniquely identified by `ota_source`, `ota_sequence`,
`antenna_index`, `frequency_hz`, and `sot`.

1. `ota_source`: (`INT`) NEM id of the transmitting node.

2. `ota_destination`: (`INT`) NEM id of the destination node or
   65535 for broadcast.
   
3. `ota_sequence`: (`INT`) Unique per source over-the-air
   sequence number for the transmission.
   
4. `phy_sub_id`: (`INT`) Used to differentiate between emulator
   physical layer instances for different waveforms.

5. `phy_sequence`: (`INT`) Unique per source physical layer
   sequence number for the transmission.

6. `mac_registration_id`: (`INT`) Waveform radio model unique
   identifier.

7. `mac_sequence`: (`INT`) Unique per source radio model
   sequence number for the transmission.

8. `transmitter`: (`INT`) NEM id of the transmitting node.

9. `antenna_index`: (`INT`) Transmit antenna index of the
   transmitting NEM.

10. `bandwidth_hz`: (`INT`) Transmit bandwidth or 0 when using spectral masks.

11. `fixed_gain_dbi`: (`DOUBLE`) Fixed transmit antenna gain in
    dBi (ideal omni). Optional and present only when profile defined
    antenna information: `antenna_profile_id`, `antenna_azimuth`, and
    `antenna_elevation` are not.

12. `spectral_mask_index`: (`INT`) Spectral mask index in
    use. Optional and only present when `bandwidth_hz` is 0.

13. `antenna_profile_id`: (`INT`) Transmit antenna profile
    id. Optional and only present when `fixed_gain_dbi` is not.

14. `antenna_azimuth`: (`DOUBLE`) Profile defined antenna
    pointing information. Optional and only present when
    `fixed_gain_dbi` is not.

15. `antenna_elevation`: (`DOUBLE`) Profile defined antenna
    pointing information. Optional and only present when
    `fixed_gain_dbi` is not.
    
16. `frequency_hz`: (`INT`): Center frequency of frequency
    segment in Hz.

17. `sot`: (`INT`) Start-of-transmission in microseconds since
    the epoch (1970-01-01 UTC).

18. `eot`: (`INT`) End-of-transmission in microseconds since
    the epoch (1970-01-01 UTC).

19. `power_dbm`: (`DOUBLE`) Transmit power in dBm.

20. `latitude_degrees`: (`DOUBLE`) Transmitting NEM's latitude
    in degrees. Optional and only present when location events are
    published.

21. `longitude_degrees`: (`DOUBLE`) Transmitting NEM's
    longitude in degrees. Optional and only present when location
    events are published.

22. `altitude_meters`: (`DOUBLE`) Transmitting NEM's altitude
    in meters. Optional and only present when location events are
    published.

23. `azimuth_degrees`: (`DOUBLE`) Transmitting NEM's velocity
    azimuth in degrees. Optional and only present when location events
    containing velocity are published.

24. `elevation_degree`: (`DOUBLE`) Transmitting NEM's velocity
    elevation in degrees. Optional and only present when location
    events containing velocity are published.

25. `magnitude_meters_per_second`: (`DOUBLE`) Transmitting
    NEM's velocity magnitude in meters per second. Optional and only
    present when location events containing velocity are published.

26. `roll_degrees`: (`DOUBLE`) Transmitting NEM's orientation
    roll in degrees. Optional and only present when location events
    containing orientation are published.

27. `pitch_degrees`: (`DOUBLE`) Transmitting NEM's orientation
    pitch in degrees. Optional and only present when location events
    containing oricentation are published.

28. `yaw_degrees`: (`DOUBLE`) Transmitting NEM's orientation
    yaw in degrees. Optional and only present when location events
    containing oricentation are published.

More information on [platform orientation][3] and [profile defined
antennas][4] can be found on the EMANE Wiki.


[2]: https://sqlite.org/index.html
[3]: https://github.com/adjacentlink/emane/wiki/Platform-Orientation
[4]: https://github.com/adjacentlink/emane/wiki/Antenna-Profile

\bigskip
\footnotesize
```sql
CREATE TABLE ota (ota_source INT, 
                  ota_destination INT, 
                  ota_sequence INT, 
                  phy_sub_id INT, 
                  phy_sequence INT, 
                  mac_registration_id INT, 
                  mac_sequence INT, 
                  transmitter INT, 
                  antenna_index INT, 
                  bandwidth_hz INT, 
                  fixed_gain_dbi DOUBLE, 
                  spectral_mask_index INT, 
                  antenna_profile_id INT,
                  antenna_azimuth DOUBLE,
                  antenna_elevation DOUBLE,
                  frequency_hz INT,
                  sot INT,
                  eot INT,
                  power_dbm DOUBLE,
                  latitude_degrees DOUBLE,
                  longitude_degrees DOUBLE,
                  altitude_meters DOUBLE,
                  azimuth_degrees DOUBLE,
                  elevation_degrees DOUBLE,
                  magnitude_meters_per_second DOUBLE,
                  roll_degrees DOUBLE,
                  pitch_degrees DOUBLE,
                  yaw_degrees DOUBLE,
                  PRIMARY KEY (ota_source,ota_sequence,antenna_index,frequency_hz,sot));
```
\vspace{-.2cm}
SQLite schema used by `emane-spectrum-ota-record-tool`.
\normalsize

