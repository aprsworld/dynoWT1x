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

TEST_CONFIG *run_config;

/* DO NOT CHANGE ANYTHING ABOVE THIS LINE */
TEST_CONFIG t_configs[] = {
	{
	"24", /*label*/
	500, /*DYNO_RPM_START;  RPM */
	1500, /*DYNO_RPM_END;  RPM to end at */
	100, /*DYNO_RPM_STEP;  RPM step size */
	2, /*DYNO_RPM_WAIT;  seconds to wait for dyno to reach RPM */
	23000, /*VOLTAGE_START;  milliVolts  */
	26000, /*VOLTAGE_END;  milliVolts to end at */
	500, /*VOLTAGE_STEP;  milliVolts step size */
	200, /*VOLTAGE_WAIT;  milliseconds to wait for load to reach milliVolts */
	0, /* SAMPLING_DURATION; seconds */
	0, /* SAMPLING_HERTZ; samples per second */
	},
	{
	"24Fast", /*label*/
	1000, /*DYNO_RPM_START;  RPM */
	1500, /*DYNO_RPM_END;  RPM to end at */
	100, /*DYNO_RPM_STEP;  RPM step size */
	2, /*DYNO_RPM_WAIT;  seconds to wait for dyno to reach RPM */
	24000,  /*VOLTAGE_START;  milliVolts  */
	26000, /*VOLTAGE_END;  milliVolts to end at */
	1000,  /*VOLTAGE_STEP;  milliVolts step size */
	200, /*VOLTAGE_WAIT;  milliseconds to wait for load to reach milliVolts */
	0, /* SAMPLING_DURATION; seconds */
	0, /* SAMPLING_HERTZ; samples per second */
	},
	{
	"24Faster", /*label*/
	1400, /*DYNO_RPM_START;  RPM */
	1500, /*DYNO_RPM_END;  RPM to end at */
	100, /*DYNO_RPM_STEP;  RPM step size */
	2, /*DYNO_RPM_WAIT;  seconds to wait for dyno to reach RPM */
	25000,  /*VOLTAGE_START;  milliVolts  */
	26000, /*VOLTAGE_END;  milliVolts to end at */
	1000,  /*VOLTAGE_STEP;  milliVolts step size */
	200, /*VOLTAGE_WAIT;  milliseconds to wait for load to reach milliVolts */
	3, /* SAMPLING_DURATION; seconds */
	10, /* SAMPLING_HERTZ; samples per second */
	"localhost", /*MQTT_HOST*/
	"dynoWT", /*MQTT_TOPIC*/
	},
	{
	"TestAverages", /*label*/
	1400, /*DYNO_RPM_START;  RPM */
	1500, /*DYNO_RPM_END;  RPM to end at */
	100, /*DYNO_RPM_STEP;  RPM step size */
	2, /*DYNO_RPM_WAIT;  seconds to wait for dyno to reach RPM */
	25000,  /*VOLTAGE_START;  milliVolts  */
	26000, /*VOLTAGE_END;  milliVolts to end at */
	1000,  /*VOLTAGE_STEP;  milliVolts step size */
	200, /*VOLTAGE_WAIT;  milliseconds to wait for load to reach milliVolts */
	2, /* SAMPLING_DURATION; seconds */
	10, /* SAMPLING_HERTZ; samples per second */
	"localhost", /*MQTT_HOST*/
	"dynoWT", /*MQTT_TOPIC*/
	},
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
	{ },


};


#define t_configsCount (sizeof(t_configs)/sizeof(TEST_CONFIG))

