# dynoWT1x

## Purpose

dynoWT1x is the interface program to a BK Precision 8500 and variable frequency drive (vfd).
This allows the program to control the RPM of the dynomometer and the load the BK 8500 is
applying.

## Build

```
sudo apt-get install libjson-c-dev libmosquitto-dev mosquitto mosquitto-clients libmodbus-dev

make
```

The make will create two directories, `bin/` and `doc/` if necessary and then attempt to
compile `dynoWT1x.c` into `bin/dynoWT1x`.

## dynoWT1x_sweep_configurations.h

contains an array test configuration structures.  The structure contains:

```
typedef struct {
	char *label;
	int DYNO_RPM_START; /* RPM */
	int DYNO_RPM_END; /* RPM to end at */
	int DYNO_RPM_STEP; /* RPM step size */
	int DYNO_RPM_WAIT; /* seconds to wait for dyno to reach RPM */
	int VOLTAGE_START; /* milliVolts  */
	int VOLTAGE_END; /* milliVolts to end at */
	int VOLTAGE_STEP; /* milliVolts step size */
	int VOLTAGE_WAIT; /* milliseconds to wait for load to reach milliVolts */
	int SAMPLING_DURATION; /* seconds */
	int SAMPLING_HERTZ; /* samples per second */
	char *MQTT_HOST;
	char *MQTT_TOPIC;
} TEST_CONFIG;
```

There are just two items that can be overriden from the command line, MQTT_HOST, and
MQTT_TOPIC.   That way the operator cannot snafu your test but may send the moquitto output
to a different server.

I have three sample configs and an empty config in the file.  Adjust the parameters to be
suitable for your use.

Make sure that `DYNO_RPM_START` is smaller than `DYNO_RPM_END` unless you want to start at higher speeds
and get slower.   To do that make `DYNO_RPM_STEP` a negative number.

All `VOLTAGE_`stuff is in milliVolts and milliseconds.

`SAMPLING_HERTZ` must be 1,2,4,5,8, or 10.   These are all small factors of 1 million.   If you use
`SAMPLING_HERTZ` of 5 and `SAMPLING_DURATION` of 2 then you should get 10 samples at a particular voltage-rpm combination.

Do not forget to run `make`

## Command line switches

switch|Required/Optional|argument|description
---|---|---|---
--help|OPTIONAL|(none)|displays help and exits
--verbose|OPTIONAL|(none)|sets verbose mode
--verbose-level|OPTIONAL|number|sets verbose level above 1
--override|OPTIONAL|(none)|must be used before any switches below
--daq-device|OPTIONAL|device name|Only use when the software does not autmatically conncect.
--mqtt-topic|OPTIONAL|topic|mqtt topic
--mqtt-host|OPTIONAL|qualified host|mqtt host operating mqtt server
--mqtt-port|OPTIONAL|number|default is 1883
--mqtt-user-name|OPTIONAL|user login|mqtt server may require this
--mqtt-password|OPTIONAL|password|mqtt server may require this
--modbus-host|OPTIONAL|qualified host|modbus host operating modbus server
--modbus-port|OPTIONAL|number|default is 502
--modbus-slave|OPTIONAL|number|default is 1

## Command line parameters

The first arguement is an identifier of the wind turbine.   You may optionally put a path before
the id, ie. `data/WT1234`.   This will create a csv file in the `data/` folder whose name starts with `WT1234_` and ends
with `_stats.csv`.   The middle characters are the number seconds since the epoch.

The second arguement is the label of the test to be run.   The label maybe as simple as `24` or `24extended` or some other
string.   To see the labels of all the tests hardcoded in the program  run `dynoWT1x --help`.

## Safety First

The software connects to real hardware that can cause serious injury to people working in or near the hardware.  
If running `dynoWT1x` and there is an emergency, you can `Control-c`.   This will cause the load to be removed and the
the dynomometer to stop and return to local operator control.  The hardware load 
`BK 8500` and the dynomometer should be powered down when changing out the wind turbine.   The computer does not have to
be turned off.  Do *NOT* turn off the computer unless there is an emergency.  Try to power down the dynomometer first.

## Load Cell Interface

The load cell to be interfaces is a *LOADSTAR*.   It is assumed to have the entry `/dev/ttyLoadStar0`.   If the
entry does *NOT* exist or is unusable then the output will not contain load cell info.

## BK 8500

The `BK 8500` is interfaced at `/dev/ttyLoad0`.   
If the entry does *NOT* exist or is unusable then the program will exit with an error message.  If you have a
`BK 8500` connected and know the entry name you can used the `--daq-device=/dev/ttyUSB?` option where you specify
the name.   Remember that `--override` must be on the command line ahead of `--daq_device`

## MQTT Options

It is easiest to build `MQTT_HOST` and `MQTT_TOPIC` into each test specification.

Overriding `MQTT_HOST`  requires `--override` on the command line before `--mqtt-host 10.0.0.1` or
`--mqtt-host=10.0.0.1` or `--mqtt-host=mosquitto/server`.   If you leave it empty in the test spec then it will default 
to `localhost`.

Overriding `MQTT_TOPIC` requires `--override` on the command line before `--mqtt-topic someTopic`. 
There is *NO* default mqtt-topic.   If the topic is missing then mqtt will not be used.

Overriding mqtt port requires `--override` on the command line before `--mqtt-port 9966`. The default mqtt-port is 1883.

Setting `--mqtt-user-name` requires `--override` on the command line before `--mqtt-user-name JoeSmallMqttUser`. There is
no default.   Using this option is required when the mqtt server requires it.

Setting `--mqtt-password` requires `--override` on the command line before `--mqtt-password MyFirstDogFido`. There is
no default.   Using this option is required when the mqtt server requires it.

## SAMPLING_DURATION

The default sampling duration is 5 seconds.   In each test specification if `SAMPLING_DURATION` is zero then the 5 seconds
will be used.

## SAMPLING_HERTZ

The default sampling rate is 1 Hertz.   In each test specification if `SAMPLING_HERTZ` is zero then the 1 Hertz
will be used.  The allowed hertz are 1, 2, 4, 5, 8, and 10.  The *only* prime factors of 1,000,000 are 2 and 5.
Therefore the only allowed Hertz must 1 and/or some number where 2 and/or 5 are its factors.  The next candidate is
16.

## Editting dynoWT1x_sweep_configurations.h


`/* DO NOT CHANGE ANYTHING ABOVE THIS LINE */` literally means what is says.

```
/* SAMPLE t_config entry that can be copied used to start a new entry */
{
0, /*label*/
0, /*DYNO_RPM_START;  RPM */
0, /*DYNO_RPM_END;  RPM to end at */
0, /*DYNO_RPM_STEP;  RPM step size */
0, /*DYNO_RPM_WAIT;  seconds to wait for dyno to reach RPM */
0, /*VOLTAGE_START;  milliVolts  */
0, /*VOLTAGE_END;  milliVolts to end at */
0, /*VOLTAGE_STEP;  milliVolts step size */
0, /*VOLTAGE_WAIT;  milliseconds to wait for load to reach milliVolts */
0, /* SAMPLING_DURATION; seconds */
0, /* SAMPLING_HERTZ; samples per second */
0, /*MQTT_HOST*/
0, /*MQTT_TOPIC*/
},
/* END SAMPLE */
```

You can copy these lines and paste them to where you need them and create a new entry
from scratch.

### label

Replace the `0` with text enclosed in `"`.   Examples are `"24"`, `"24extreme"` and `"SomeLabel"`.
The label should be unique but this is not enforced.   The program will only find the first
non-unique label.

### DYNO_RPM_START

Replace the `0` with a value desired.

### DYNO_RPM_END

Replace the `0` with a value desired. `DYNO_RPM_END` >= `DYNO_RPM_START` must be true unless you want to start
with greater RPM and then go slower.

### DYNO_RPM_STEP

Replace the `0` with a value desired. if `DYNO_RPM_END` > `DYNO_RPM_START` then `DYNO_RPM_STEP` must be a negative
number.

### DYNO_RPM_WAIT

Replace the `0` with a value desired. This is the number of seconds to wait for the speed to completely adjust.

### VOLTAGE_START

Replace the `0` with a value desired. This is milliVolts so 26 Volts is 26000 mV.  

### VOLTAGE_END

Replace the `0` with a value desired. This is milliVolts so 26 Volts is 26000 mV.  Make sure `VOLTAGE_END` >= `VOLTAGE_START`.

### VOLTAGE_STEP

Replace the `0` with a value desired. This is milliVolts so 1 Volt is 1000 mV.  If `VOLTAGE_START` not equal `VOLTAGE_END` then
`VOLTAGE_STEP` must be *NONZERO*.

### VOLTAGE_WAIT

Replace the `0` with a value desired. This is milliseconds so 500 = 1/2 second.
