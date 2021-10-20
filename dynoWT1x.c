#if 0
rpm sweep from low to high DONE
voltage to be swept at each rpm DONE
file should contain Watts, Volts, Amps. HEADING DONE OUTPUT DONE
Fix spelling in file header.   DONE
have config built in for each model.   Hard coded with more models to come. DONE
markdown document DONE
averages DONE
LoadStart   partially
#endif

#define _GNU_SOURCE

#include <stdio.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <libgen.h>
#include <getopt.h>

#include <termios.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <modbus.h>
#include "test_config.h"
#include "MillionFractions.h"
#include <time.h>
#include <json.h>
#include <mosquitto.h>

#define modbus_host "192.168.10.228"
#define modbus_port 502
#define modbus_slave 1
int outputDebug;
static int _overrideFlag;
char cmd_line[1024];
#define  run_duration  5

char *daq_device= "/dev/ttyLoad0";	/* /dev/ttyLoad0 is the default */
int _daq_baud = B38400;
int _serial_fd = -1;

char *loadCell_device= "/dev/ttyLoadStar0";	/* /dev/ttyLoadStar0 is the default */

int _loadCell_baud = B230400;
int _loadCell_serial_fd = -1;
char loadStarUNIT[32];
char _loadCell_weight[16]; 
char *_loadCell_set_units = "units LB \r\n";

static u_int8_t  _Remote0buff[32];	/* turn Remote off */
static u_int8_t  _Remote1buff[32];	/* turn Remote on */
static u_int8_t	_Mode1buff[32];	/* CV mode */
static u_int8_t  _Load0buff[32];	/* turn Load off */
static u_int8_t  _Load1buff[32];	/* turn Load on */
static u_int8_t	_Querybuff[32];		/* _query */

enum arguments {
	A_help = 512,
	A_verbose,
	A_verbose_level,
	A_override,
	X_override_cutoff,
	A_daq_device,
	A_mqtt_host,
	A_mqtt_topic,
	A_mqtt_meta_topic,
	A_mqtt_port,
	A_mqtt_user_name,
	A_mqtt_password,
};

int mqtt_port = 1883;
static char *mqtt_topic;
static char *mqtt_host;
static char *mqtt_user_name;
static char *mqtt_passwd;
static int disable_mqtt_output = 1;
static int retainedFlag;
static char *_Argv1;	/* label for this test run */
static char *_Argv2;	/* type of test */
#define default_mqtt_host "localhost"

/* VFD modbus globals */
modbus_t *mb;
int commandedRPM=-1;
int commandedVOLTAGE;
static int _usec_divider = OneMillion << 1;	/* double the tic rate */

/* output file globals */
FILE *fp_stats;

#define CHANNEL_MODE_ANALOG                0
#define CHANNEL_MODE_FREQUENCY_FROM_ANALOG 1

#define VOLTAGE_LOGIC_HIGH 3.0
#define VOLTAGE_LOGIC_LOW  1.0

/* Dyno and testing configuration */
/* physical dyno configuration */
// #define DYNO_CONFIG_HZ_PER_RPM	(1.0/8.8)	/* Hz on drive motor per RPM on output shaft */
#define DYNO_CONFIG_HZ_PER_RPM	(33.4/1000.0)	/* Hz on drive motor per RPM on output shaft */  /* jim and clare 10-5-2021 */


/* dyno sweep specifications */
/* see test_config.h */
int generate_timestamp(char *d, int len ) {

	struct tm *now;
        struct timeval time;
        char timestamp[32];
        gettimeofday(&time, NULL);
        now = localtime(&time.tv_sec);
        if ( 0 == now ) {
                fprintf(stderr,"# error calling localtime() %s",strerror(errno));
		return	1;
        }

        snprintf(timestamp,sizeof(timestamp),"%04d-%02d-%02d %02d:%02d:%02d.%03ld",
                1900 + now->tm_year,1 + now->tm_mon, now->tm_mday,now->tm_hour,now->tm_min,
		now->tm_sec,time.tv_usec/1000);

	if ( strlen(timestamp) > len ) {
		fprintf(stderr,"# generate_timestamp result length %d longer than destination length %d.\n",
				(int) (strlen(timestamp) +1) , len);
		memset(d,'\0',len);
		strncpy(d,"UNKNOWN",len-1);
		return	2;
	}
	strcpy(d,timestamp);
	return	0;
}


char *strsave(char *s ) {
	int len = strlen(s);
	char *ret_val = calloc(1 + len, sizeof(char));
	if ( 0 != ret_val) {
		strcpy(ret_val,s);
	}
	return	ret_val;
}
static struct mosquitto *mosq;
static void _mosquitto_shutdown(void);


void connect_callback(struct mosquitto *mosq, void *obj, int result) {
        if ( 5 == result ) {
                fprintf(stderr,"# --mqtt-user-name and --mqtt-passwd required at this site.\n");
        }
        fprintf(stderr,"# connect_callback, rc=%d\n", result);
}
static struct mosquitto * _mosquitto_startup(void) {
        char clientid[24] = {};
        int rc = 0;


        fprintf(stderr,"# initializing mosquitto MQTT library\n");
        mosquitto_lib_init();

        snprintf(clientid, sizeof(clientid), "mqtt-send-example_%d", getpid());
        mosq = mosquitto_new(clientid, true, 0);

        if (mosq) {
                if ( 0 != mqtt_user_name && 0 != mqtt_passwd ) {
                        mosquitto_username_pw_set(mosq,mqtt_user_name,mqtt_passwd);
                }
                mosquitto_connect_callback_set(mosq, connect_callback);

                fprintf(stderr,"# connecting to MQTT server %s:%d\n",mqtt_host,mqtt_port);
                rc = mosquitto_connect(mosq, mqtt_host, mqtt_port, 60);

                /* start mosquitto network handling loop */
                mosquitto_loop_start(mosq);
                }

	return  mosq;
}


static void _mosquitto_shutdown(void) {

        if ( mosq ) {

                /* disconnect mosquitto so we can be done */
                mosquitto_disconnect(mosq);
                /* stop mosquitto network handling loop */
                mosquitto_loop_stop(mosq,0);


                mosquitto_destroy(mosq);
                }

        fprintf(stderr,"# mosquitto_lib_cleanup()\n");
        mosquitto_lib_cleanup();
}
static int _dynoWT_pub(const char *message, const char *topic  ) {
        int rc = 0;
        if ( 0 < outputDebug ) {
                fputs(message,stdout);
                fflush(stdout);
        }

        if ( 0 == disable_mqtt_output ) {

                static int messageID;
                /* instance, message ID pointer, topic, data length, data, qos, retain */
                rc = mosquitto_publish(mosq, &messageID, topic, strlen(message), message, 2, retainedFlag );
                retainedFlag = 0; /* default is off */


                if (0 != outputDebug) { 
			fprintf(stderr,"# mosquitto_publish provided messageID=%d and ret urn code=%d\n",messageID,rc);
		}

                /* check return status of mosquitto_publish */
                /* this really just checks if mosquitto library accepted the message. Not that it was a ctually send on the network */
                if ( MOSQ_ERR_SUCCESS == rc ) {
                        /* successful send */
		} else if ( MOSQ_ERR_INVAL == rc ) {
                        fprintf(stderr,"# mosquitto error invalid parameters\n");
                } else if ( MOSQ_ERR_NOMEM == rc ) {
                        fprintf(stderr,"# mosquitto error out of memory\n");
                } else if ( MOSQ_ERR_NO_CONN == rc ) {
                        fprintf(stderr,"# mosquitto error no connection\n");
                } else if ( MOSQ_ERR_PROTOCOL == rc ) {
                        fprintf(stderr,"# mosquitto error protocol\n");
                } else if ( MOSQ_ERR_PAYLOAD_SIZE == rc ) {
                        fprintf(stderr,"# mosquitto error payload too large\n");
#if 0
                } else if ( MOSQ_ERR_MALFORMED_UTF8 == rc ) {
                        fprintf(stderr,"# mosquitto error topic is not valid UTF-8\n");
#endif
                } else {
                        fprintf(stderr,"# mosquitto unknown error = %d\n",rc);
                }
        }


        return  rc;
}
int  _mosquitto_publish_cmd_line(void) {
	char	topic[256] = {};
	char timestamp[32];
	char buffer[256] = {};

	(void) generate_timestamp(timestamp,sizeof(timestamp));

	snprintf(topic,sizeof(topic),"%s/cmd_line",mqtt_topic);
	retainedFlag = 1;
	strcpy(buffer,timestamp);
	strcat(buffer," $ ");
	strcat(buffer,cmd_line);
	return	_dynoWT_pub(buffer,topic);

}
int  _mosquitto_publish_exited(const char *message) {
	char	topic[256] = {};
	char buffer[512] = {};	
	int ret_val = 0;

	snprintf(topic,sizeof(topic),"%s/exited",mqtt_topic);
	(void) generate_timestamp(buffer,sizeof(buffer));

	strcat(buffer," exited ");
	strcat(buffer,message);
	retainedFlag = 1;
	ret_val = _dynoWT_pub(buffer,topic);
	usleep(OneMillion);

	return	ret_val;
}
int _mosquitto_publish_data(json_object *jobj ) {
	char	topic[256] = {};
	snprintf(topic,sizeof(topic),"%s/data",mqtt_topic);
	retainedFlag = 0;
	 int rc = _dynoWT_pub(json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_PRETTY ),topic);

	return	rc;
}

int _mosquitto_publish_run_config(void ) {
	if ( 0 == run_config ) {
		(void) _mosquitto_publish_exited("run_config NOT set");
		return	1;
	}
	char	topic[256] = {};
	snprintf(topic,sizeof(topic),"%s/run_config",mqtt_topic);

	char timestamp[32];

	(void) generate_timestamp(timestamp,sizeof(timestamp));

	json_object *jobj = json_object_new_object();

	json_object_object_add(jobj,"timestamp",json_object_new_string(timestamp));
	json_object_object_add(jobj,"label",json_object_new_string(run_config->label));
	json_object_object_add(jobj,"DYNO_RPM_START",json_object_new_int(run_config->DYNO_RPM_START));
	json_object_object_add(jobj,"DYNO_RPM_END",json_object_new_int(run_config->DYNO_RPM_END));
	json_object_object_add(jobj,"DYNO_RPM_STEP",json_object_new_int(run_config->DYNO_RPM_STEP));
	json_object_object_add(jobj,"DYNO_RPM_WAIT",json_object_new_int(run_config->DYNO_RPM_WAIT));

	json_object_object_add(jobj,"VOLTAGE_START",json_object_new_int(run_config->VOLTAGE_START));
	json_object_object_add(jobj,"VOLTAGE_END",json_object_new_int(run_config->VOLTAGE_END));
	json_object_object_add(jobj,"VOLTAGE_STEP",json_object_new_int(run_config->VOLTAGE_STEP));
	json_object_object_add(jobj,"VOLTAGE_WAIT",json_object_new_int(run_config->VOLTAGE_WAIT));

	json_object_object_add(jobj,"SAMPLING_DURATION",json_object_new_int(run_config->SAMPLING_DURATION));
	json_object_object_add(jobj,"SAMPLING_HERTZ",json_object_new_int(run_config->SAMPLING_HERTZ));

	json_object_object_add(jobj,"MQTT_HOST",json_object_new_string(run_config->MQTT_HOST));
	json_object_object_add(jobj,"MQTT_TOPIC",json_object_new_string(run_config->MQTT_TOPIC));

	retainedFlag = 1;
	 int rc = _dynoWT_pub(json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_PRETTY ),topic);

	json_object_put(jobj);
	return	rc;
}



int vfd_connect() {
	/* connect to modbus gateway */
	mb = modbus_new_tcp(modbus_host, modbus_port);

//	modbus_set_debug(mb,TRUE);
	modbus_set_error_recovery(mb,MODBUS_ERROR_RECOVERY_LINK);

	if ( NULL == mb ) {
		fprintf(stderr,"# error creating modbus instance: %s\n", modbus_strerror(errno));
		return -1;
	}

	if ( -1 == modbus_connect(mb) ) {
		fprintf(stderr,"# modbus connection failed: %s\n", modbus_strerror(errno));
		modbus_free(mb);
		return -2;
	}

	/* set slave address of device we want to talk to */
	if ( 0 != modbus_set_slave(mb,modbus_slave) ) {
		fprintf(stderr,"# modbus_set_slave() failed: %s\n", modbus_strerror(errno));
		modbus_free(mb);
		return -3;
	}


	return 0;
}


/* set parameters on VFD so we can control it programattically */
int vfd_gs3_set_automated_parameters() {
//	uint16_t value = 0;

	/* TODO: implement or remove stub */
	/* Set P3.00 "source of operation" to 0x03 "RS-485 with keypad STOP enabled"*/
	/* Set P4.00 "source of frequency command" to 0x05 "RS-485" */
	/* what else is needed? */

	return 0;
}

/* set parameters on VFD back to manual control through its front keypad interface */
int vfd_gs3_clear_automated_parameters() {
//	uint16_t value = 0;

	/* TODO: implement or remove stub */
	return 0;
}

/* set VFD output frequency corresponding to desired RPM */
int vfd_gs3_set_rpm(int rpm) {
	uint16_t value = 0;
	double d;

	commandedRPM=rpm;

	/* output shaft has reduction, so we use a constant to get from motor Hz to output RPM */
	d = rpm * DYNO_CONFIG_HZ_PER_RPM;
	/* VFD expects frequency in deciHz */
	d *= 10.0;
	d += 0.5; /* so we round */
	value=(uint16_t) d;


	fprintf(stderr,"# vfd_gs3_set_rpm() rpm=%d d=%f value to write=%d\n",rpm,d,value);

	/* Set P3.16 "desired frequency" to RPM scaled to Hz */
	/* forward direction */
	if ( -1 == modbus_write_register(mb,2330,value) ) {
		fprintf(stderr,"# modbus_write_register() failed in vfd_gs3_set_rpm() with:\n%s\n", modbus_strerror(errno));
		return -1;
	}

	return 0;

}

/* set VFD rotation direction to FWD and mode to run */
int vfd_gs3_command_run() {
	uint16_t value = 0;

	/* forward direction */
	value=0;
	if ( -1 == modbus_write_register(mb,2332,value) ) {
		fprintf(stderr,"# modbus_write_register() failed in vfd_gs3_command_run() setting forward direction with with:\n%s\n", modbus_strerror(errno));
		return -1;
	}

	/* run mode */
	value=1;
	if ( -1 == modbus_write_register(mb,2331,value) ) {
		fprintf(stderr,"# modbus_write_register() failed in vfd_gs3_command_run() setting mode run with: %s\n", modbus_strerror(errno));
		return -1;
	}

	return 0;
}

/* set VFD mode to stop */
int vfd_gs3_command_stop() {
	uint16_t value = 0;

	/* stop mode */
	value=0;
	if ( -1 == modbus_write_register(mb,2331,value) ) {
		fprintf(stderr,"# modbus_write_register() failed in vfd_gs3_command_run() setting mode stop with: %s\n", modbus_strerror(errno));
		return -1;
	}

	return 0;
}

long  getMicroTime(void) {
	struct timeval tv;
	gettimeofday(&tv,NULL);
	return	1000000*tv.tv_sec + tv.tv_usec;
}
static void _hexdump(char *label, u_int8_t *s, int count ) {
	int i = 0;

	if ( 1 < outputDebug ) {
		fprintf(stderr,"# %s\n# ",label);
		for ( ; count > i; i++, s++ ) {
			fprintf(stderr,"%02x ",s[0]);
		}
		fputc('\n',stderr);
		fflush(stderr);
	}
}

int _check_sum( u_int8_t *buff ) {
	int i,j;

	for ( i = j = 0; 25 > i; i++ ) {
		j += buff[i];
		if ( outputDebug  > 2 ) {
			fprintf(stderr,"# 0x%02x 0x%02x 0x%02x 0x%02x\n",
			i,buff[i],j,j % 256);
		}
	}
	if ( outputDebug > 1 ) {
		fprintf(stderr,"# _check_sum = 0x%02x\n",j % 256);
	}

	return (buff[25] != (j %256));
}
void clear_serial_input(int fd) {
	u_int8_t buff[32] = {};
	int rd;

	while ( rd = read(fd,buff , 26 ) ) {
		if ( outputDebug > 2 ) {
			fprintf(stderr,"# clear_serial_input %3d bytes read.\n",rd);
		}
		if ( 0 > rd ) {
			break;
		}
		usleep(1000);	/* wait one millisec */
	}

}
static void _query(void ) {
	if ( outputDebug ) {
		_hexdump("_query",_Querybuff,26);
		_check_sum(_Querybuff);
	}
	write(_serial_fd,_Querybuff,26);
}
int _loadCell_serial_process(int(*func)(u_int8_t *)) {
	u_int8_t buff[128] = {};
	/* etx = \r\n */
	int bytes_received = 0;
	int rd;
#define INCOMPLETE 0xff1
	int ret_val = INCOMPLETE;
	int retries = 0;

	for ( ;  INCOMPLETE  == ret_val && 13 > retries; ) {
		rd = read(_loadCell_serial_fd,buff + bytes_received, sizeof(buff) - bytes_received);
		if ( 1 <outputDebug  && 0 != rd ) {
			fprintf(stderr,"#  _loadCell_serial_process %3d bytes read.\n",rd);
		}
		if ( 0 > rd ) {
			ret_val = -1;
			break;
		}

		bytes_received += rd;
		if ( 2 > bytes_received ) {
			usleep(5000);
			retries++;
			continue;
		}
		char	*b = buff + bytes_received -2;
		if ( '\r' != b[0] && '\n' != b[1] ) {
			usleep(5000);
			retries++;
		}
		ret_val = 0;
	}
	if ( 0 == ret_val ) {
		ret_val = func(buff);
	}

	ret_val = ( INCOMPLETE == ret_val ) ? 0 : ret_val;

	return	ret_val;
#undef INCOMPLETE
}
int _serial_process(int serialfd,int(*func)(u_int8_t *)) {
	u_int8_t buff[32] = {};
	int bytes_received = 0;
	int rd;
	int ret_val = 0;
	int retries = 0;

	for ( ; 26 > bytes_received && 13 > retries; ) {
		rd = read(serialfd,buff + bytes_received, 26 - bytes_received);
		if ( 1 <outputDebug  && 0 != rd ) {
			fprintf(stderr,"#  _serial_process %3d bytes read.\n",rd);
		}
		if ( 0 > rd ) {
			break;
		}
		bytes_received += rd;
		if ( 26 != bytes_received ) {
			usleep(5000);
			retries++;
		}
	}

	if ( outputDebug ) {
		fflush(stderr);
		_hexdump("_serial_process",buff,26);
		fflush(stderr);
	}
	if ( 26 != bytes_received ) {
		ret_val |= 1;
	}
	if ( 0xaa != buff[0] ) {
		ret_val |= 2;
	}
	if ( _check_sum(buff) ) {
		ret_val |= 4;
	}
	if ( 0 != func && 0 == ret_val ) {
		ret_val |= ((0 == func(buff)) ? 0 : 8);
	}

	if ( outputDebug && 0 != ret_val ) {
		fflush(stderr);
		fprintf(stderr,"# _serial_process returns 0x%02x\n",ret_val);
		fflush(stderr);
	}

	return	(1 == ret_val);
}
int process_set_func(u_int8_t *buff ) {
	int ret_val = 0;
	if ( 0xaa != buff[0] ) {
		ret_val |= 1;
	}
	if ( 0x12 != buff[2] ) {
		ret_val |= 2;
	}
	if ( 0x80 != buff[3] ) {
		ret_val |= 4;
	}
	if ( 0 != outputDebug ) {
		fprintf(stderr,"# set %s 0x%02x\n",(0 == ret_val) ? "succeeded" : "FAILED",
			ret_val);
	}
	return	ret_val;

}
struct {
	int n;
	long long AverageMilliWatts;
	long long AverageMilliVolts;
	long long AverageMilliAmps;
	int loadCell_n;
	double	AverageWeight;
} averages;
void clear_averages(void) {
	memset(&averages,'\0',sizeof(averages));
}
#if 0
New average = old average * (n-1)/n + new value /n
#endif
void dummy(void) {
}
void update_average_weight( double d ) {
	if ( averages.n >= averages.loadCell_n ) {
		snprintf(_loadCell_weight,sizeof(_loadCell_weight),"%0.3lf",d);
		averages.loadCell_n++;
		averages.AverageWeight *= ( averages.loadCell_n -1);
		averages.AverageWeight /=  averages.loadCell_n;
		averages.AverageWeight += d / averages.loadCell_n;
	}
}
void update_averages( u_int32_t *n,u_int32_t Milliwatts, u_int32_t Millivolts, u_int32_t MilliAmps,
		u_int32_t *AverageMilliWatts, u_int32_t *AverageMilliVolts, int32_t *AverageMilliAmps ) {
	long trueMW;
	averages.n++;
#if 0
	if ( 11 == averages.n ) {
		dummy();
	}
#endif

	averages.AverageMilliWatts *= ( averages.n -1);
	averages.AverageMilliWatts /=  averages.n;
	averages.AverageMilliWatts +=  (Milliwatts << 10) / averages.n;

	averages.AverageMilliVolts *= ( averages.n -1);
	averages.AverageMilliVolts /=  averages.n;
	averages.AverageMilliVolts +=  (Millivolts << 10) / averages.n;

	averages.AverageMilliAmps *= ( averages.n -1);
	averages.AverageMilliAmps /=  averages.n;
	averages.AverageMilliAmps +=  (MilliAmps << 10) / averages.n;

	if ( 0 != AverageMilliWatts ) {
		*AverageMilliWatts = ( u_int32_t) (averages.AverageMilliWatts >> 10 );
	}
	if ( 0 != AverageMilliVolts ) {
		*AverageMilliVolts = ( u_int32_t) (averages.AverageMilliVolts >> 10 );
	}
	if ( 0 != AverageMilliAmps ) {
		*AverageMilliAmps = ( u_int32_t) (averages.AverageMilliAmps >> 10 );
	}
	if ( 0 != n ) {
		*n = averages.n;
	}
}
int process_query(u_int8_t *buff ) {
	int ret_val = 0;
	u_int32_t result, milliVolts, milliAmps, milliWatts;
	u_int32_t n,  AverageMilliWatts, AverageMilliVolts, AverageMilliAmps;

	milliVolts = buff[3] + (buff[4]<<8) + (buff[5]<<16) + (buff[6]<<24);

	milliAmps = buff[7] + (buff[8]<<8) + (buff[9]<<16) + (buff[10]<<24);

	milliWatts = buff[11] + (buff[12]<<8) + (buff[13]<<16) + (buff[14]<<24);
	result = buff[16] + (buff[17]<<8);

	update_averages(&n,milliWatts,milliVolts,milliAmps,
			&AverageMilliWatts,&AverageMilliVolts,&AverageMilliAmps);

	char _commandedVOLTAGE[16] = {};
	char _commandedRPM[16] = {};
	char _Watts[16] = {};
	char _Volts[16] = {};
	char _Amps[16] = {};
	char _StatusFlag0[16] = {};
	char _StatusFlag1[16] = {};
	char _averages_n[16] = {};
	char _averages_meanWatts[16] = {};
	char _averages_meanVolts[16] = {};
	char _averages_meanAmps[16] = {};
	char _averages_meanWeight[16] = {};
	char _loadCell_n[16] = {};

	snprintf(_commandedRPM,sizeof(_commandedRPM),"%d",commandedRPM);
	snprintf(_commandedVOLTAGE,sizeof(_commandedVOLTAGE),"%d.%03d",
		commandedVOLTAGE/1000, commandedVOLTAGE % 1000);
	snprintf(_Watts,sizeof(_Watts),"%d.%03d",milliWatts/1000, milliWatts % 1000);
	snprintf(_Volts,sizeof(_Volts),"%d.%03d",milliVolts/1000, milliVolts % 1000);
	snprintf(_Amps,sizeof(_Amps),"%d.%04d",milliAmps/10000, milliAmps % 10000);
	snprintf(_StatusFlag0,sizeof(_StatusFlag0),"0x%02x",buff[15]);
	snprintf(_StatusFlag1,sizeof(_StatusFlag1),"0x%02x",result);
	snprintf(_averages_n,sizeof(_averages_n),"%d",n);
	snprintf(_averages_meanWatts,sizeof(_averages_meanWatts),"%d.%03d",AverageMilliWatts/1000, AverageMilliWatts % 1000);
	snprintf(_averages_meanVolts,sizeof(_averages_meanVolts),"%d.%03d",AverageMilliVolts/1000, AverageMilliVolts % 1000);
	snprintf(_averages_meanAmps,sizeof(_averages_meanAmps),"%d.%03d",AverageMilliAmps/10000, AverageMilliAmps % 10000);
	snprintf(_averages_meanWeight,sizeof(_averages_meanWeight),"%0.6lf",averages.AverageWeight);
	snprintf(_loadCell_n,sizeof(_loadCell_n),"%d",averages.loadCell_n);
	
	if ( 0 != outputDebug ) {
		fprintf(stderr,"#terminal %s Watts\n",_Watts);
		fprintf(stderr,"#terminal %s Volts\n",_Volts);
		fprintf(stderr,"#terminal %s Amps\n",_Amps);
		if ( 1 & buff[15] ) {
			fprintf(stderr,"# Calculate the new demarcation coefficient\n");
		}
		if ( 2 & buff[15] ) {
			fprintf(stderr,"# Waiting for a trigger signal\n");
		}
		if ( 4 & buff[15] ) {
			fprintf(stderr,"# Remote control state (enabled)\n");
		}
		if ( 8 & buff[15] ) {
			fprintf(stderr,"# Output state (ON)\n");
		}
		if ( 0x10 & buff[15] ) {
			fprintf(stderr,"# Local key state (enabled)\n");
		}
		if ( 0x20 & buff[15] ) {
			fprintf(stderr,"# Remote sensing mode (enabled)\n");
		}
		if ( 0x40 & buff[15] ) {
			fprintf(stderr,"# LOAD ON timer is enabled\n");
		}
		if ( 1 & result ) {
			fprintf(stderr,"# Over voltage\n");
		}
		if ( 2 & result ) {
			fprintf(stderr,"# Over current\n");
		}
		if ( 4 & result ) {
			fprintf(stderr,"# Over power\n");
		}
		if ( 8 & result ) {
			fprintf(stderr,"# Over temperature\n");
		}
		if ( 0x10 & result ) {
			fprintf(stderr,"# Not connect remote terminal\n");
		}
		if ( 0x20 & result ) {
			fprintf(stderr,"# Constant current\n");
		}
		if ( 0x40 & result ) {
			fprintf(stderr,"# Constant voltage\n");
		}
		if ( 0x80 & result ) {
			fprintf(stderr,"# Constant power\n");
		}
		if ( 0x100 & result ) {
			fprintf(stderr,"# Constant resistance\n");
		}
	}
	struct tm *now;
        struct timeval time;
        char timestamp[32];
        gettimeofday(&time, NULL);
        now = localtime(&time.tv_sec);
        if ( 0 == now ) {
                fprintf(stderr,"# error calling localtime() %s",strerror(errno));
                exit(1);
        }

        snprintf(timestamp,sizeof(timestamp),"%04d-%02d-%02d %02d:%02d:%02d.%03ld",
                1900 + now->tm_year,1 + now->tm_mon, now->tm_mday,now->tm_hour,now->tm_min,
		now->tm_sec,time.tv_usec/1000);

	const char *fmt = "\"%s\",\"%s\",%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,\n";
	fprintf(fp_stats,fmt,
			timestamp,_Argv1,_commandedRPM,
			_commandedVOLTAGE,_Watts,_Volts,_Amps,
			_StatusFlag0,_StatusFlag1,
			_averages_n, _averages_meanWatts,_averages_meanVolts,_averages_meanAmps,
			_loadCell_weight,_averages_meanWeight,_loadCell_n);
	fflush(fp_stats);
	fprintf(stderr,fmt,
			timestamp,_Argv1,_commandedRPM,
			_commandedVOLTAGE,_Watts,_Volts,_Amps,
			_StatusFlag0,_StatusFlag1,
			_averages_n, _averages_meanWatts,_averages_meanVolts,_averages_meanAmps,
			_loadCell_weight,_averages_meanWeight,_loadCell_n);
	fflush(stderr);
	if ( 0 == disable_mqtt_output ) {
		json_object *jobj = json_object_new_object();

		json_object_object_add(jobj,"timestamp",json_object_new_string(timestamp));
		json_object_object_add(jobj,"argv1",json_object_new_string(_Argv1));
		json_object_object_add(jobj,"argv2",json_object_new_string(_Argv2));
		json_object_object_add(jobj,"commandedRPM",json_object_new_string(_commandedRPM));
		json_object_object_add(jobj,"commandedVOLTAGE",json_object_new_string(_commandedVOLTAGE));
		json_object_object_add(jobj,"Watts",json_object_new_string(_Watts));
		json_object_object_add(jobj,"Volts",json_object_new_string(_Volts));
		json_object_object_add(jobj,"Amps",json_object_new_string(_Amps));
		json_object_object_add(jobj,"StatusFlag0",json_object_new_string(_StatusFlag0));
		json_object_object_add(jobj,"StatusFlag1",json_object_new_string(_StatusFlag1));
		json_object_object_add(jobj,"n",json_object_new_string(_averages_n));
		json_object_object_add(jobj,"meanWatts",json_object_new_string(_averages_meanWatts));
		json_object_object_add(jobj,"meanVolts",json_object_new_string(_averages_meanVolts));
		json_object_object_add(jobj,"meanAmps",json_object_new_string(_averages_meanAmps));
		json_object_object_add(jobj,"loadCellWeight",json_object_new_string(_loadCell_weight));
		json_object_object_add(jobj,"meanWeight",json_object_new_string(_averages_meanWeight));
		json_object_object_add(jobj,"loadCell-n",json_object_new_string(_loadCell_n));
		ret_val = _mosquitto_publish_data(jobj);
		json_object_put(jobj);
		}
	return	ret_val;

}
void _loadCell_query(void) {
	if ( -1 < _loadCell_serial_fd ) {
		if ( 2 < outputDebug ) {
			char timestamp[32] = {};
			generate_timestamp(timestamp,sizeof(timestamp));
			fprintf(stderr,"# _loadCell_query() %s\n",timestamp); fflush(stderr);
		}
		write(_loadCell_serial_fd,"W\r\n",3);
	}

}
static double _atof(const u_int8_t *s ) {
	if ( '\0' == s[0] ) {
		return	NAN;
	}
	return	atof(s);
}
int _process_loadCell_query(u_int8_t *buff ) {
	if ( 2 < outputDebug ) {
		fprintf(stderr,"# process_loadCell_query()\n"); fflush(stderr);
	}
	double d = _atof(buff);
	if ( NAN != d ) {
		update_average_weight(d);
	}
	
	return	0;
}
void daq_acquire(void) {
        int i;
	int status;
        int rc = 0;
	int index,query_index,loadCell_index;
	struct timeval tv = {0,0,};
	struct timeval select_tv = {0,60,};

	clear_averages();

        /* server socket */
        fd_set active_fd_set, read_fd_set;

        FD_ZERO( &active_fd_set);


        /* add serial fd to fd_set for select() */
        FD_SET(_serial_fd, &active_fd_set);
	if ( -1 < _loadCell_serial_fd ) {
		FD_SET(_loadCell_serial_fd, &active_fd_set);
	}

        alarm(0);
	fprintf(stderr,"# SAMPLING_DURATION= %d  SAMPLING_HERTZ= %d\n",
		run_config->SAMPLING_DURATION, run_config->SAMPLING_HERTZ);
	int duration_countdown = 0 + run_config->SAMPLING_DURATION;
	gettimeofday(&tv,NULL);
	int j =  NineTenthsMillion - tv.tv_usec;
	j += (0 > j ) ? OneMillion : 0;
	usleep(j);
	gettimeofday(&tv,NULL);
	time_t tv_sec = tv.tv_sec;
	loadCell_index = query_index = tv.tv_usec / _usec_divider;	/* compute which hertz  in this second */
	clear_serial_input(_serial_fd);
	clear_serial_input(_loadCell_serial_fd);
	_loadCell_query();
        for ( ; (0 == rc) ; ) {
		gettimeofday(&tv,NULL);
		index = tv.tv_usec / _usec_divider;	/* compute which hertz  in this second */

		if ( tv_sec != tv.tv_sec ) {
			tv_sec = tv.tv_sec;
			/* start of new second */
			if ( 0 < duration_countdown ) {
				query_index = -2;	/* force a _query().  j can never be negative */
			}
			duration_countdown--;
			/* go 1 more second of receiving */
			if ( 0 > duration_countdown ) {
				break;
			}
		}
		if (  0 == ( index & 1 ) && query_index != index ) {
			query_index = index;
			_query();
		}
		if ( 0 != ( index & 1) && loadCell_index != index ) {
			loadCell_index = index;
			_loadCell_query();
		}



                read_fd_set = active_fd_set;
                /* Block until input arrives on one or more active sockets. */
                i=select(FD_SETSIZE, &read_fd_set, NULL, NULL, &select_tv);

                if ( EBADF == i ) {
                        fprintf(stderr,"# select() EBADF error. Aborting.\n");
                        exit(1);
                } else if ( ENOMEM == i ) {
                        fprintf(stderr,"# select() ENOMEM error. Aborting.\n");
                        exit(1);
                }


                if ( FD_ISSET(_serial_fd, &read_fd_set) ) {
                        rc = _serial_process(_serial_fd,process_query);
		}
		/* -1 == _loadCell_serial_fd FD_ISSET is never True. */
                if ( FD_ISSET(_loadCell_serial_fd, &read_fd_set) ) {
			/* fprintf(stderr,"# _loadCell_serial_fd trigger FD_ISSET\n"); fflush(stderr);  */
                        rc = _loadCell_serial_process(_process_loadCell_query);
		}
        }

	clear_averages();

}


void dyno_step(int rpm) {
	fflush(fp_stats);
	/* start new measurement */
	fprintf(stderr,"##################################################################################\n");

	/* command dyno RPM */
	fprintf(stderr,"# setting dyno RPM to %d\n",rpm);
	fprintf(stderr,"# load set to %d.%03d volts\n",
		commandedVOLTAGE/1000, commandedVOLTAGE % 1000);
	vfd_gs3_set_rpm(rpm);

	/* wait for dyno RPM to stabilize */
	fprintf(stderr,"# delay to allow RPM to be reached\n");
	sleep(run_config->DYNO_RPM_WAIT);

	/* acquire data */
	fprintf(stderr,"# acquiring data\n");
	fprintf(fp_stats,"#\n");
	if ( 0 == disable_mqtt_output ) {
		fprintf(stderr,"# mqtt-topic: %s/data\n",mqtt_topic);
	}

	daq_acquire();

	/* send / save data */
	fprintf(stderr,"# flushing logged data\n");
	fflush(fp_stats);
}
static void _report_reason_for_serialfd_failure(char *s ) {
	struct stat     buf;

	fprintf(stderr,"# %s - %s\n",s,strerror(errno));
	if ( 0 != stat(s,&buf)) {
		fprintf(stderr,"# %s does not exisit!\n",s);
	} else {
		fprintf(stderr,"# %s does exisit!\n",s);
		fprintf(stderr,"# %s permissions are %03o\n",s,buf.st_mode % 2048);
		fprintf(stderr,"# %s owner=%d group=%d\n",s,buf.st_uid,buf.st_gid);
        }
}
static int set_interface_attribs (int fd, int speed, int parity) {
        struct termios tty;

        memset (&tty, 0, sizeof tty);
        if (tcgetattr (fd, &tty) != 0) {
                return -1;
        }

        cfsetospeed (&tty, speed);
        cfsetispeed (&tty, speed);

        tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
        // disable IGNBRK for mismatched speed tests; otherwise receive break
        // as \000 chars
        tty.c_iflag &= ~IGNBRK; // disable break processing
        tty.c_lflag = 0;        // no signaling chars, no echo,
                                // no canonical processing
        tty.c_oflag = 0;        // no remapping, no delays
        tty.c_cc[VMIN]  = 0;    // read doesn't block
        tty.c_cc[VTIME] = 5;    // 0.5 seconds read timeout

        tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl

        tty.c_cflag |= (CLOCAL | CREAD);// ignore modem controls,
                                        // enable reading
        tty.c_cflag &= ~(PARENB | PARODD);      // shut off parity
        tty.c_cflag |= parity;
        tty.c_cflag &= ~CSTOPB;
        tty.c_cflag &= ~CRTSCTS;

        if (tcsetattr (fd, TCSANOW, &tty) != 0) {
                return -1;
        }
        return 0;
}
void set_blocking (int fd, int vmin, int vtime) {
        struct termios tty;
        memset (&tty, 0, sizeof tty);
        if (tcgetattr (fd, &tty) != 0) {
                fprintf(stderr,"# set_blocking unable to load tty attributes\n");
                return;
        }

        tty.c_cc[VMIN]  = vmin; // minimum  number of characters for noncanonical read
        tty.c_cc[VTIME] = vtime; // timeout in deciseconds for noncanonical read

        if ( outputDebug ) {
                fprintf(stderr,"# set_blocking tty.c_cc[VMIN]=%d\n",tty.c_cc[VMIN]);
                fprintf(stderr,"# set_blocking tty.c_cc[VTIME]=%d\n",tty.c_cc[VTIME]);
        }

        if (tcsetattr (fd, TCSANOW, &tty) != 0) {
                printf("error %d setting term attributes", errno);
        }

}
void daq_register_serialfd(int serialfd) {
	_serial_fd = serialfd;
}
void loadCell_register_serialfd(int serialfd) {
	_loadCell_serial_fd = serialfd;
}
#if 0
static void _setRemote(int serialhd , int state) {


	unsigned char buff[32] = {};

	buff[0]=0xAA; // STX
	buff[1]=(0xff & 0x00 ); // address
	buff[2]=0x20; // function
	buff[3] = ( 0 != state ) ? 1 : 0;

	int i,j;

	for ( i = j = 0; 25 > i; i++ ) {
		j += buff[i];
	}

	buff[25]= j %256;

	if ( outputDebug ) {
		_hexdump("_setRemote",buff,26);
		_check_sum(buff);
	}
	clear_serial_input();
	write(serialhd,buff,26);

}
#else 
static void _setRemote( int state) {
	u_int8_t *buff = (0 == state ) ? _Remote0buff : _Remote1buff;
	write(_serial_fd,buff,26);
}
#endif
#if 0
static void _setLoad(int serialhd , int state) {


	unsigned char buff[32] = {};

	buff[0]=0xAA; // STX
	buff[1]=(0xff & 0x00 ); // address
	buff[2]=0x21; // function
	buff[3] = ( 0 != state ) ? 1 : 0;

	int i,j;

	for ( i = j = 0; 25 > i; i++ ) {
		j += buff[i];
	}

	buff[25]= j %256;

	if ( outputDebug ) {
		_hexdump("_setLoad",buff,26);
		_check_sum(buff);
	}
	write(serialhd,buff,26);

}
#else

static void _setLoad( int state) {
	u_int8_t *buff = (0 == state ) ? _Load0buff : _Load1buff;
	write(_serial_fd,buff,26);
}
#endif
static void _getMode(int serialhd , int state) {


	unsigned char buff[32] = {};

	buff[0]=0xAA; // STX
	buff[1]=(0xff & 0x00 ); // address
	buff[2]=0x29; // function

	int i,j;

	for ( i = j = 0; 25 > i; i++ ) {
		j += buff[i];
	}

	buff[25]= j %256;

	if ( 1 < outputDebug ) {
		_hexdump("_getMode",buff,26);
		_check_sum(buff);
	}
	clear_serial_input(serialhd);
	write(serialhd,buff,26);

}
#if 0
static void _setMode(int serialhd , int state) {



	unsigned char buff[32] = {};

	buff[0]=0xAA; // STX
	buff[1]=(0xff & 0x00 ); // address
	buff[2]=0x28; // function
	buff[3] = ( 0 != state ) ? 1 : 0;	/* 1 == CV */

	int i,j;

	for ( i = j = 0; 25 > i; i++ ) {
		j += buff[i];
	}

	buff[25]= j %256;

	if ( outputDebug ) {
		_hexdump("_setMode",buff,26);
		_check_sum(buff);
	}
	clear_serial_input();
	write(serialhd,buff,26);

}
#else
static void _setMode(void) {
	write(_serial_fd,_Mode1buff,26);
}
#endif

static void _setVoltage( int volts) {


	commandedVOLTAGE = volts;
	unsigned char buff[32] = {};

	buff[0]=0xAA; // STX
	buff[1]=(0xff & 0x00 ); // address
	buff[2]=0x2c; // function
	buff[3] = (volts & 0xff);
	buff[4] = ((volts>>8) & 0xff);
	buff[5] = ((volts>>16) & 0xff);
	buff[6] = ((volts>>24) & 0xff);

	int i,j;

	for ( i = j = 0; 25 > i; i++ ) {
		j += buff[i];
	}

	buff[25]= j %256;

	if ( outputDebug ) {
		_hexdump("_setVoltage",buff,26);
		_check_sum(buff);
	}
	clear_serial_input(_serial_fd);
	write(_serial_fd,buff,26);

}
void vfd_gs3_shutdown(void) {
	/* stop the motor */
	fprintf(stderr,"# commanding VFD to stop\n");
	vfd_gs3_command_stop();


	/* clean up modbus */
	fprintf(stderr,"# restoring VFD to manual operation\n");
	vfd_gs3_clear_automated_parameters();

	fprintf(stderr,"# MODBUS shutdown / cleanup\n");
	modbus_close(mb);
	modbus_free(mb);
}
static void signal_handler(int signum) {


        if ( SIGALRM == signum ) {
                fprintf(stderr,"\n# Timeout while waiting for NMEA data.\n");
                fprintf(stderr,"# Terminating.\n");
		fflush(stderr);
		(void) _mosquitto_publish_exited("SIGALRM");
		_mosquitto_shutdown();
		vfd_gs3_shutdown();
                exit(100);
        } else if ( SIGPIPE == signum ) {
                fprintf(stderr,"\n# Broken pipe.\n");
                fprintf(stderr,"# Terminating.\n");
		_setRemote(0);
		_setLoad(0);
		fflush(stderr);
		(void) _mosquitto_publish_exited("SIGPIPE");
		_mosquitto_shutdown();
		vfd_gs3_shutdown();
                exit(101);
        } else if ( SIGUSR1 == signum ) {
                /* clear signal */
                signal(SIGUSR1, SIG_IGN);

                fprintf(stderr,"# SIGUSR1 triggered data_block dump:\n");

                /* re-install alarm handler */
                signal(SIGUSR1, signal_handler);
        } else if ( SIGTERM == signum || SIGINT == signum ) {
                fprintf(stderr,"# Terminating.\n");
		_setRemote(0);
		_setLoad(0);
		fflush(stderr);
		(void) _mosquitto_publish_exited("SIGTERM");
		_mosquitto_shutdown();
		vfd_gs3_shutdown();
                exit(0);
        } else {
                fprintf(stderr,"\n# Caught unexpected signal %d.\n",signum);
                fprintf(stderr,"# Terminating.\n");
		_setRemote(0);
		_setLoad(0);
		fflush(stderr);
		(void) _mosquitto_publish_exited("UNKNOWN SIGNAL");
		_mosquitto_shutdown();
		vfd_gs3_shutdown();
                exit(102);
        }

}
// int _loadCell_serial_fd = -1;
void loadCell_stop_continous(void) {
	char buffer[256] = {};
	int rd;
	
	write(_loadCell_serial_fd,"\r\n\r\n\r\n",6);	/* send 3 crlf to get it to stop continous */
	usleep(100000);

	while ( 0 != (rd = read(_loadCell_serial_fd,buffer,sizeof(buffer)))) {	/* let the input stream drain */
		usleep(100000);
	}
}
static int  _loadCell_get_units( void ) {
	char	buffer[256] = {};
	int rd;
	int	rc = 0;

#if 0
	switch ( _loadCell_set_units[0] ) {
		case 'L':
			write(_loadCell_serial_fd,"UNIT L\r\n",8);	/* unit of measure */
			break;
		case 'K':
			write(_loadCell_serial_fd,"UNIT K\r\n",8);	/* unit of measure */
			break;
		case 'N':
			write(_loadCell_serial_fd,"UNIT N\r\n",8);	/* unit of measure */
			break;
		default:
			fprintf(stderr,"# Bad _loadCell_set_units %s\n",_loadCell_set_units);
	}
#else
	write(_loadCell_serial_fd,_loadCell_set_units,strlen(_loadCell_set_units));
#endif
	usleep(100000);


	alarm(0);
	write(_loadCell_serial_fd,"UNIT\r\n",6);	/* unit of measure */
	usleep(100000);
	alarm(0);
	if ( 0 == (rd = read(_loadCell_serial_fd,buffer,sizeof(buffer)))) {	/* wait for input */
		rc = 1;
		fprintf(stderr,"# no reponse from UNIT\n");
	} else {
		char *p;
		alarm(0);
		p = buffer;
		for ( ; 0 != p[0] && ' ' > p[0] ;	p++ ) {
			}
		p = strpbrk("LKN",p);
		switch ( p[0] ) {
			case	'L':
				strcpy(loadStarUNIT,"LBS");	
				break;
			case	'K':
				strcpy(loadStarUNIT,"Kilograms");
				break;
			case	'N':
				strcpy(loadStarUNIT,"Newtons");
				break;
			default:
				rc  = 1;
				strcpy(loadStarUNIT,"Unknown");
		}
	}
	fprintf(stderr,"# %s units\n",loadStarUNIT);
	return	rc;
}
int start_up_loadCell_device(void) {
	int ret_val  = 0;
        int serialfd = open (loadCell_device, O_RDWR | O_NOCTTY | O_SYNC);
        if (serialfd < 0) {
                _report_reason_for_serialfd_failure(loadCell_device);
		fprintf(stderr,"# %s errors will NOT be fatal!\n",loadCell_device);
		goto GoodBye;
	}
	loadCell_register_serialfd(serialfd);
        set_interface_attribs (serialfd, _loadCell_baud, 0);  
        set_blocking (serialfd, 0, 0);  

	/*   set up the LOADSTAR for polling and set the units */
	loadCell_stop_continous();
	if ( ret_val = _loadCell_get_units() ) {
		goto GoodBye;
	}



GoodBye:
	return	0;
}
int start_up_daq_device(void) {
        int serialfd = open (daq_device, O_RDWR | O_NOCTTY | O_SYNC);

        if (serialfd < 0) {
                _report_reason_for_serialfd_failure(daq_device);
                exit(1);
        }

	daq_register_serialfd(serialfd);
        set_interface_attribs (serialfd, _daq_baud, 0);  
        set_blocking (serialfd, 0, 0);  

	

        signal(SIGALRM, signal_handler); /* timeout */
        signal(SIGUSR1, signal_handler); /* user signal to do data block debug dump */
        signal(SIGTERM, signal_handler); /* user signal to terminate */
        signal(SIGPIPE, signal_handler); /* broken TCP connection */
        signal(SIGINT, signal_handler); /* user signal to interupt  ^c */

	_setRemote(1);
	usleep(1000000);
	if (  _serial_process(serialfd,process_set_func)) {
		return 1;
	}
	// _setMode(serialfd,1);
	_setMode();
	usleep(1000000);
	_getMode(serialfd,1);
	usleep(1000000);
	if (  _serial_process(serialfd,process_set_func)) {
		return 1;
	}
	_setLoad(1);
	usleep(1000000);
	if (  _serial_process(serialfd,process_set_func)) {
		return 1;
	}

	return 0;
}

void shut_down_daq_device(void) {
	_setVoltage(0);
	(void) _serial_process(_serial_fd,NULL);
	_setLoad(0);
	(void) _serial_process(_serial_fd,NULL);
	_setRemote(0);
	(void) _serial_process(_serial_fd,NULL);
}
void capture_cmd_line(int argc, char **argv) {
	int i = 0;
	for ( ; i < argc; i++, argv++ ) {
		strcat(cmd_line,argv[0]);
		strcat(cmd_line," ");
	}
}
void frun_config(void ) {
	fprintf(fp_stats,"#\n");
	fprintf(fp_stats,"# config: \"%s\" char *label;\n",run_config->label);
	fprintf(fp_stats,"# config: %7d int DYNO_RPM_START;\n",run_config->DYNO_RPM_START);
	fprintf(fp_stats,"# config: %7d int DYNO_RPM_END;\n",run_config->DYNO_RPM_END);
	fprintf(fp_stats,"# config: %7d int DYNO_RPM_STEP;\n",run_config->DYNO_RPM_STEP);
	fprintf(fp_stats,"# config: %7d int DYNO_RPM_WAIT;\n",run_config->DYNO_RPM_WAIT);
	fprintf(fp_stats,"#\n");
	fprintf(fp_stats,"# config: %7d int VOLTAGE_START;\n",run_config->VOLTAGE_START);
	fprintf(fp_stats,"# config: %7d int VOLTAGE_END;\n",run_config->VOLTAGE_END);
	fprintf(fp_stats,"# config: %7d int VOLTAGE_STEP;\n",run_config->VOLTAGE_STEP);
	fprintf(fp_stats,"# config: %7d int VOLTAGE_WAIT;\n",run_config->VOLTAGE_WAIT);
	fprintf(fp_stats,"#\n");
	fprintf(fp_stats,"# config: %7d int SAMPLING_DURATION;\n",run_config->SAMPLING_DURATION);
	fprintf(fp_stats,"# config: %7d int SAMPLING_HERTZ;\n",run_config->SAMPLING_HERTZ);
	fprintf(fp_stats,"#\n");
	fprintf(fp_stats,"# config: \"%s\" char *MQTT_HOST;\n",run_config->MQTT_HOST);
	fprintf(fp_stats,"# config: \"%s\" char *MQTT_TOPIC;\n",run_config->MQTT_TOPIC);
	fprintf(fp_stats,"#\n");
	fflush(fp_stats);
}
int validate_mqtt_parameters(void) {
	int flag = 0;
	disable_mqtt_output = 1;
	flag |= ( 0 != mqtt_topic) ? 0x1 : 0;
	flag |= ( 0 != mqtt_host) ? 0x2 : 0;
	flag |= ( 0 != mqtt_user_name) ? 0x4 : 0;
	flag |= ( 0 != mqtt_passwd) ? 0x8 : 0;
	flag |= ( 0 != run_config->MQTT_HOST) ? 0x10 : 0;
	flag |= ( 0 != run_config->MQTT_TOPIC) ? 0x20 : 0;

	if ( 0 != flag ) {
		( 0 == (flag & 0x1)) && fprintf(stderr,"# mqtt_topic is missing (required)\n");
		( 0 == (flag & 0x2)) && fprintf(stderr,"# mqtt_host is missing (optional)\n");
		( 0 == (flag & 0x4)) && fprintf(stderr,"# mqtt_user_name is missing (optional)\n");
		( 0 == (flag & 0x8)) && fprintf(stderr,"# mqtt_passwd is missing (optional)\n");
		/* order of priority is cmd_line, test_configuration, default */
		mqtt_host = ( 0 == mqtt_host ) ? run_config->MQTT_HOST : mqtt_host;
		mqtt_host = ( 0 == mqtt_host ) ? strsave(default_mqtt_host) : mqtt_host;
		if ( 0 != mqtt_host ) {
			fprintf(stderr,"# mqtt_host is \"%s\"\n", mqtt_host);
		}
		mqtt_topic = ( 0 == mqtt_topic ) ? run_config->MQTT_TOPIC : mqtt_topic;
		if ( 0 != mqtt_topic ) {
			fprintf(stderr,"# mqtt_topic is \"%s\"\n", mqtt_topic);
		}
	}
	if ( 0 != mqtt_host &&  0 != mqtt_topic ) {
		disable_mqtt_output = 0;
		return	0;
	}
	return	0;
}
#if 0
static u_int8_t  _setRemote1buff[32];	/* turn remove on */
#endif
void generate_Remote0buff(void) {

	_Remote0buff[0]=0xAA; // STX
	_Remote0buff[1]=(0xff & 0x00 ); // address
	_Remote0buff[2]=0x20; // function
	_Remote0buff[3] =  0;

	int i,j;

	for ( i = j = 0; 25 > i; i++ ) {
		j += _Remote0buff[i];
	}

	_Remote0buff[25]= j %256;
	_hexdump("_Remote0buff",_Remote0buff,26);
}
void generate_Remote1buff(void) {

	_Remote1buff[0]=0xAA; // STX
	_Remote1buff[1]=(0xff & 0x00 ); // address
	_Remote1buff[2]=0x20; // function
	_Remote1buff[3] =  1;

	int i,j;

	for ( i = j = 0; 25 > i; i++ ) {
		j += _Remote1buff[i];
	}

	_Remote1buff[25]= j %256;
	_hexdump("_Remote1buff",_Remote1buff,26);
}
void generate_Mode1buff(void) {

	_Mode1buff[0]=0xAA; // STX
	_Mode1buff[1]=(0xff & 0x00 ); // address
	_Mode1buff[2]=0x28; // function
	_Mode1buff[3] =  1;

	int i,j;

	for ( i = j = 0; 25 > i; i++ ) {
		j += _Mode1buff[i];
	}

	_Mode1buff[25]= j %256;
	_hexdump("_Mode1buff",_Mode1buff,26);
}
void generate_Load0buff(void) {

	_Load0buff[0]=0xAA; // STX
	_Load0buff[1]=(0xff & 0x00 ); // address
	_Load0buff[2]=0x21; // function
	_Load0buff[3] =  0;

	int i,j;

	for ( i = j = 0; 25 > i; i++ ) {
		j += _Load0buff[i];
	}

	_Load0buff[25]= j %256;
	_hexdump("_Load0buff",_Load0buff,26);
}
void generate_Load1buff(void) {

	_Load1buff[0]=0xAA; // STX
	_Load1buff[1]=(0xff & 0x00 ); // address
	_Load1buff[2]=0x21; // function
	_Load1buff[3] =  1;

	int i,j;

	for ( i = j = 0; 25 > i; i++ ) {
		j += _Load1buff[i];
	}

	_Load1buff[25]= j %256;
	_hexdump("_Load1buff",_Load1buff,26);
}
void generate_Querybuff(void) {

	_Querybuff[0]=0xAA; // STX
	_Querybuff[1]=(0xff & 0x00 ); // address
	_Querybuff[2]=0x5f; // function

	int i,j;

	for ( i = j = 0; 25 > i; i++ ) {
		j += _Querybuff[i];
	}

	_Querybuff[25]= j %256;
	_hexdump("_Querybuff",_Querybuff,26);
}
void generate_buffs(void) {
	generate_Remote0buff();
	generate_Remote1buff();
	generate_Mode1buff();
	generate_Load0buff();
	generate_Load1buff();
	generate_Querybuff();
}
int check_run_config(void ) {
	int ret_val = 0;
	run_config->SAMPLING_DURATION = ( 0 == run_config->SAMPLING_DURATION ) ? 
		run_duration : run_config->SAMPLING_DURATION ;
	run_config->SAMPLING_HERTZ = ( 0 == run_config->SAMPLING_HERTZ ) ? 1 : run_config->SAMPLING_HERTZ ;

	switch ( run_config->SAMPLING_HERTZ ) {
		case 0:
		case 1:
			_usec_divider = OneMillion;	break;
		case 2:
			_usec_divider = OneHalfMillion;	break;
		case 4:
			_usec_divider = OneQuarterMillion;	break;
		case 5:
			_usec_divider = OneFifthMillion;	break;
		case 8:
			_usec_divider = OneEighthMillion;	break;
		case 10:
			_usec_divider = OneTenthMillion;	break;
		default:
			fprintf(stderr,"# SAMPLING_HERTZ must be 1,2,4,5,8, or 10.\n");
			ret_val++;
	}
	_usec_divider >>= 1;	/* double the tic rate */
	fprintf(stderr,"# tic rate %d\n",OneMillion / _usec_divider);	fflush(stderr);
	return	ret_val;

}
TEST_CONFIG *t_configsLookup(char * key ) {
	TEST_CONFIG *start, *end;

	start = end = t_configs;
	end += t_configsCount;
	for ( ; start < end ; start++ ) {
		if ( 0 == start->label ) {
			continue;
		}
		if ( 0 == strcmp(key,start->label)) {
			break;
		}
	}
	return ( start < end ) ? start : 0;

}
char  *get_t_configs_labels(char *d, int len ) {
	char *buffer = calloc(len+1024,1);
	memset(d,'\0',len);
	TEST_CONFIG *start, *end;

	start = end = t_configs;
	end += t_configsCount;
	for ( ; start < end ; start++ ) {
		if ( 0 == start->label ) {
			continue;
		}
		strcat(buffer,"\"");
		strcat(buffer,start->label);
		strcat(buffer,"\", ");
		if ( strlen(buffer) >= len ) {
			break;
		}
	}
	strncpy(d,buffer,len-1);
	return	d;

}
int _check_override(int n ) {
	if ( n > X_override_cutoff ) {
		switch( n ) {
			case A_daq_device:
				fprintf(stderr,"--daq-device requites override\n");	break;
			case A_mqtt_topic:	
				fprintf(stderr,"--mqtt-topic requites override\n");	break;
			case A_mqtt_host:	
				fprintf(stderr,"--mqtt-host requites override\n");	break;
			case A_mqtt_port:
				fprintf(stderr,"--mqtt-port requites override\n");	break;
			case A_mqtt_user_name:
				fprintf(stderr,"--mqtt-user-name requites override\n");	break;
			case A_mqtt_password:
				fprintf(stderr,"--mqtt-password requites override\n");	break;
		}

		n = A_help;
	}
	return	n;
}
void print_labels(FILE *out ) {
	char buffer[256];
	fprintf(out,"# tests available: %s\n#\n",get_t_configs_labels(buffer,sizeof(buffer)));
}
char *_do_pretty( char *s ) {
	static char buffer[80];
	memset(buffer,'#',sizeof(buffer)-1);
	int len = strlen(s);
	int offset = strlen(buffer) - len -2;
	offset >>= 1;	/* go to middle */
	char *p = buffer + offset;
	*p++ = ' ';
	for ( ; '\0' != p[0] && '\0' != s[0] ; p++,s++ ) {
		p[0] = s[0];
	}
	*p++ = ' ';
	return	buffer;
}

int main (int argc, char **argv) {
	int i,n;
	int ret;
	char filename_stats[1024];
	struct timeval tv;
	capture_cmd_line(argc,argv);

	fprintf(stderr,"# dynoWT being run as '%s'\n",basename(argv[0]));


	while (1) {
		int option_index = 0;
		static struct option long_options[] = {
			/* normal program */
		        {"daq-device",                       1,                 0, A_daq_device, },
		        {"mqtt-host",                        1,                 0, A_mqtt_host },
		        {"mqtt-topic",                       1,                 0, A_mqtt_topic },
		        {"mqtt-meta-topic",                  1,                 0, A_mqtt_meta_topic },
		        {"mqtt-port",                        1,                 0, A_mqtt_port },
		        {"mqtt-user-name",                   1,                 0, A_mqtt_user_name },
		        {"mqtt-passwd",                      1,                 0, A_mqtt_password },
		        {"verbose-level",                    1,                 0, A_verbose_level, },
			{"override",                         no_argument,       0, A_override, },
			{"verbose",                          no_argument,       0, A_verbose, },
		        {"help",                             no_argument,       0, A_help, },
			{},
	};

	n = getopt_long(argc, argv, "", long_options, &option_index);

	if (n == -1) {
		break;
	}
	
	switch (_check_override(n)) {
		case A_override:	_overrideFlag++;
			break;
		case A_daq_device:
			daq_device = strsave(optarg);
			break;
		case A_verbose:
			outputDebug=1;
			fprintf(stderr,"# verbose (debugging) output to stderr enabled\n");
			break;
		case A_verbose_level:
			outputDebug=atoi(optarg);
			fprintf(stderr,"# verbose-level = %d\n",outputDebug);
			break;
		case A_mqtt_topic:	
			mqtt_topic = strsave(optarg);
			break;
		case A_mqtt_host:	
			mqtt_host = strsave(optarg);
			break;
		case A_mqtt_port:
			mqtt_port = atoi(optarg);
			break;
		case A_mqtt_user_name:
			mqtt_user_name = strsave(optarg);
			break;
		case A_mqtt_password:
			mqtt_passwd = strsave(optarg);
			break;
		case	'?':
		case A_help:
			fprintf(stdout,"%s\n",_do_pretty("help"));
			fprintf(stdout,"#\n");
			fprintf(stdout,"# --help\t\tThis help message then exit\n");
			fprintf(stdout,"#\n");
			fprintf(stdout,"# --verbose\t\tOutput verbose / debugging to stderr\n"); 
			fprintf(stdout,"# --verbose-level\tSee more debugging stuff to stderr\n");
			fprintf(stdout,"# --override\t\tmust be used with options below.\n");
			fprintf(stdout,"#\n");
			fprintf(stdout,"%s\n",_do_pretty("requires --override"));
			fprintf(stdout,"# --daq-device\t\tio daq-device default=\"%s\"\n",daq_device); 
			fprintf(stdout,"# --mqtt-host\t\tThe network name or ip address of another mosquitto server.\n");
			fprintf(stdout,"# --mqtt-topic\t\tThe mosquitto topic to be used for this run.\n");
			fprintf(stdout,"# --mqtt-port\t\tThe port where the mosquitto server listens.\n");
			fprintf(stdout,"# --mqtt-user-name\tSome mosquitto servers require a user name.\n");
			fprintf(stdout,"# --mqtt-password\tSome mosquitto servers require a password.\n");
			fprintf(stdout,"%s\n",_do_pretty("end --override"));
			print_labels(stdout);
			fprintf(stdout,"%s\n",_do_pretty("end help"));
			return(0);
		}
	}

	if ( 2 > (argc - optind) ) {
		fprintf(stderr,"dynoWT <outputFilenamePrefix> <test_config> [options || --help]\n");
		exit(1);
	}
	_Argv1 = strsave(basename(argv[optind]));
	_Argv2 = strsave(argv[optind+1]);
	
	run_config = t_configsLookup(_Argv2);

	if ( 0 == run_config ) {
		fprintf(stderr,"# \"%s\" not in table\n",_Argv2);
		return	1;
	}

	/* generate outfile files with prefix_unixtime_raw.csv and prefix_unixtime_stats.csv */
	gettimeofday(&tv, NULL);
	sprintf(filename_stats,"%s_%ld_stats.csv",argv[optind],tv.tv_sec);
	(void) _do_pretty(filename_stats);

	if ( check_run_config() ) {
		fprintf(stderr,"# pproblem run_config.   exiting....\n");
		return	1;
	}

	generate_buffs();

	if ( 0 == daq_device || '\0' == daq_device[0] ) {
		fprintf(stderr,"# --daq-device cannot be empty.\n");
		return	1;
	}

	if ( validate_mqtt_parameters() ) {
		return	1;
	}

	if ( 0 == disable_mqtt_output && 0 == _mosquitto_startup() ) {
                return  1;
        }
	 if ( 0 == disable_mqtt_output && _mosquitto_publish_cmd_line()) {
		_mosquitto_shutdown();
		return	1;
	 }
	 if ( 0 == disable_mqtt_output && _mosquitto_publish_run_config()) {
		_mosquitto_shutdown();
		return	1;
	 }

	if ( start_up_daq_device()) {
		return	1;
	}

	fprintf(stderr,"# Load0 start successful!\n");



	fprintf(stderr,"# %s is optional!\n",loadCell_device);

	if ( start_up_loadCell_device()) {
		return	1;
	}

	fprintf(stderr,"# connecting to GS3 VFD via Modbus (%s:%d address %d)\n",modbus_host,modbus_port,modbus_slave);
	if ( 0 != vfd_connect() ) {
		fprintf(stderr,"# unable to connect to VFD. Terminating.\n");
		exit(2);
	}
	fprintf(stderr,"# connected to GS3 VFD");
	vfd_gs3_set_automated_parameters();
	sleep(1);

	fprintf(stderr,"# commanding VFD to run\n");
	vfd_gs3_set_rpm(180);
	vfd_gs3_command_run();
	sleep(20); 	/* ramp up to initial speed */
	vfd_gs3_set_rpm(run_config->DYNO_RPM_END);
	sleep(15); 	/* ramp up to initial speed */


	fprintf(stderr,"# creating output files:\n");
	fprintf(stderr,"# %s stats data log filename\n",filename_stats);
	fp_stats=fopen(filename_stats,"w");

	fprintf(fp_stats,"# %s\n",cmd_line);
	frun_config();	
	fprintf(fp_stats,"# %s units\n#\n",loadStarUNIT);
	fprintf(fp_stats,
			"# timestamp,argv[1],RPM,loadVolts,Watts,Volts,Amps,statusFlag0,statusFlag1,"
			"n,meanWatts,meanVolts,meanAmps,weight,meanWeight,n,\n");
	int rpm;
	int voltage;

	/* step up. */
	for ( voltage=run_config->VOLTAGE_START ; voltage<=run_config->VOLTAGE_END ; 
		voltage+=run_config->VOLTAGE_STEP ) {
		for ( rpm=run_config->DYNO_RPM_START ; rpm<=run_config->DYNO_RPM_END ; 
			rpm+=run_config->DYNO_RPM_STEP ) {
			_setVoltage(voltage);
			usleep(1000*run_config->VOLTAGE_WAIT);
			dyno_step(rpm);
		}
	}

	/* if program invoked as dynoWTe ('e' at the end) we will do an extended run */
	if ( 0 == strcmp("dynoWTe",basename(argv[0])) ) {
		fprintf(stderr,"# program run as dynoWTe so we will do extended run for 1000 steps\n");

		/* do initial RPM set and allow time for ramp */
		fprintf(stderr,"# setting dyno RPM to %d\n",150);
		vfd_gs3_set_rpm(rpm);
		fprintf(stderr,"# sleeping 15 seconds to allow ramp\n");
		sleep(15);

		/* do extended run at 150 RPM */
		for ( i=0 ; i<1000 ; i++ ) {
			fprintf(stderr,"########## Extended Run - step=%d #########################################################\n",i);

			dyno_step(150);
		}
	}

	shut_down_daq_device();
	fprintf(stderr,"# Load0 shutdown successful!\n");

	/* close output file */
	fprintf(stderr,"# closing output files\n");
	fclose(fp_stats);
	fprintf(stderr,"%s\n",_do_pretty(filename_stats));

	vfd_gs3_shutdown();

	if ( 0 == disable_mqtt_output ) {
		 _mosquitto_publish_exited("NORMAL");
		_mosquitto_shutdown();
	}

	return 0;
}
