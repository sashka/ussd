/*
 * Written by Vadim Guchenko <yhw@rambler-co.ru>
 *
 * 	$Id: stat_hdd.c 112401 2012-01-12 12:57:01Z dark $
 */

#ifdef __linux__
#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>

#include "vg_lib/vg_macros.h"

#include "linux_smart.h"
#include "stat_common.h"
#include "linux_hdd_list.h"

/* This flag shows that HDD command is given */
int f_stat_hdd_command_hdd = 0;

/* This flag shows that HDD_LIST command is given */
int f_stat_hdd_command_hdd_list = 0;

/* This flag shows that SMART command is given */
int f_stat_hdd_command_smart = 0;

/* This flag shows that at least one SMART attribute is requested */
int f_stat_hdd_smart_attrs_requested = 0;

/* This array of flags shows which SMART attributes should be returned */
char f_stat_hdd_smart_attrs[256];

/* Descriptor of ATA control device. Value -1 means that device is not
   opened */

#else

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/ata.h>
#include <sys/time.h>
#include <sys/dkstat.h>

#include <fcntl.h>
#include <unistd.h>
#include <devstat.h>

#include "stat_common.h"

#include <camlib.h>
#include <cam/scsi/scsi_pass.h>
#include <cam/scsi/scsi_message.h>

/* Maximum length of device name without "/dev/" prefix not including null */
#define DEVICE_NAME_MAXLEN		31

/* Maximum length of device model name not including null */
#define DEVICE_MODEL_MAXLEN		40

/* Maximum length of device serial number not including null */
#define DEVICE_SERIAL_MAXLEN		20

/* Maximum length of device firmware revision not including null */
#define DEVICE_REVISION_MAXLEN		8


/* Set of ATA commands */
typedef enum {
	IDENTIFY_DEVICE,
	SMART_READ_VALUES,
	SMART_READ_THRESHOLDS,
	SMART_ENABLE_OPERATIONS
} ata_command_set;

/* String representation of ATA commands */
static char *ata_command_str[] __attribute__ ((unused)) = {
	"IDENTIFY_DEVICE",
	"SMART_READ_VALUES",
	"SMART_READ_THRESHOLDS",
	"SMART_ENABLE_OPERATIONS"
};

/* ATA Command register values */
#define ATA_CMD_IDENTIFY_DEVICE		0xec
#define ATA_CMD_SMART			0xb0

/* ATA Feature register values for SMART command */
#define ATA_SMART_READ_VALUES		0xd0
#define ATA_SMART_READ_THRESHOLDS	0xd1
#define ATA_SMART_ENABLE_OPERATIONS	0xd8

/* Maximum number of SMART attributes */
#define ATA_SMART_ATTRIBUTES_MAXN	30

/* SMART attribute value structure */
#pragma pack(1)
struct ata_smart_value {
	uint8_t id;
	uint16_t flags;
	uint8_t value;
	uint8_t worst_value;
	uint8_t raw_value[6];
	uint8_t reserved;
};
#pragma pack()
VG_CASSERT_DECL(sizeof(struct ata_smart_value) == 12);

/* Structure for getting values of all SMART attributes */
#pragma pack(1)
struct ata_smart_values {
	uint16_t revno;
	struct ata_smart_value attr[ATA_SMART_ATTRIBUTES_MAXN];
	uint8_t reserved[149];
	uint8_t chksum;
};
#pragma pack()
VG_CASSERT_DECL(sizeof(struct ata_smart_values) == 512);

/* SMART attribute threshold structure */
#pragma pack(1)
struct ata_smart_threshold {
	uint8_t id;
	uint8_t threshold;
	uint8_t reserved[10];
};
#pragma pack()
VG_CASSERT_DECL(sizeof(struct ata_smart_threshold) == 12);

/* Structure for getting thresholds of all SMART attributes */
#pragma pack(1)
struct ata_smart_thresholds {
	uint16_t revno;
	struct ata_smart_threshold attr[ATA_SMART_ATTRIBUTES_MAXN];
	uint8_t reserved[149];
	uint8_t chksum;
};
#pragma pack()
VG_CASSERT_DECL(sizeof(struct ata_smart_thresholds) == 512);

/* Structure for ATA device */
struct ata_device {
	/* ATA channel on which device is present */
	int channel;
	/* Device number within channel. Must be 0 for master and 1 for
	   slave */
	int device;
	/* Device name without "/dev/" prefix */
	char name[DEVICE_NAME_MAXLEN + 1];
	/* Device parameters */
	struct ata_params params;
	/* Device model name */
	char model[DEVICE_MODEL_MAXLEN + 1];
	/* Device serial number */
	char serial[DEVICE_SERIAL_MAXLEN + 1];
	/* Device firmware revision */
	char revision[DEVICE_REVISION_MAXLEN + 1];
	/* Device SMART values */
	struct ata_smart_values smart_values;
	/* Device SMART thresholds */
	struct ata_smart_thresholds smart_thresholds;
};
VG_CASSERT_DECL(sizeof(struct ata_params) == 512);

/* This flag shows that HDD command is given */
int f_stat_hdd_command_hdd = 0;

/* This flag shows that HDD_LIST command is given */
int f_stat_hdd_command_hdd_list = 0;

/* This flag shows that SMART command is given */
int f_stat_hdd_command_smart = 0;

/* This flag shows that at least one SMART attribute is requested */
int f_stat_hdd_smart_attrs_requested = 0;

/* This array of flags shows which SMART attributes should be returned */
char f_stat_hdd_smart_attrs[256];

/* Descriptor of ATA control device. Value -1 means that device is not
   opened */
static int ata_control_device_fd = -1;

/* Name of currently opened ATA device without "/dev/" prefix. Empty name
   means that there is no opened ATA device */
static char ata_device_name[DEVICE_NAME_MAXLEN + 1] = "";

/* Descriptor of currently opened ATA device. Value -1 means that device
   is not opened */
static int ata_device_fd = -1;

/* Statistics of all devices returned by devstat library */
static struct devinfo dinfo;

/* Time when %dinfo% statistics has been requested from devstat library */
static time_t dinfo_tm;


static int stat_hdd_get_devstat(void);
static const struct devstat *stat_hdd_find_devstat(const char *);
static void stat_hdd_print_devstat(const char *);
static void stat_hdd_fake_devstat(const char *);
static int stat_hdd_is_ata_request_supported(void);
static int stat_hdd_open_device(const char *, int, int, int *);
static int stat_hdd_close_device(const char *, int);
static int stat_hdd_get_ata_control_device_fd(int *);
static int stat_hdd_get_ata_device_fd(const char *, int *) __attribute__ ((unused));
static int stat_hdd_get_ata_channels_n(int *);
static int stat_hdd_get_ata_channel_devices(int, struct ata_device [2]);
static int stat_hdd_is_device_ata(const struct ata_device *);
static int stat_hdd_refresh_ata_device_params(struct ata_device *);
static int stat_hdd_refresh_cam_device_params(struct ata_device *);
static u_llong stat_hdd_get_device_size(const struct ata_device *);
static int stat_hdd_is_smart_supported(const struct ata_device *);
static int stat_hdd_is_smart_enabled(const struct ata_device *);
static int stat_hdd_ata_command_interface(const struct ata_device *, ata_command_set, char *);
static void stat_hdd_process_smart_attributes(const struct ata_device *);
static void stat_hdd_finish(void);


/*****************************************************************************
 * Processes HDD and SMART commands.
 *****************************************************************************/
void stat_hdd() {
	time_t tm;
	int channels_n, channel, device, i;
	int f_ata_request_supported, f_smart_supported, f_smart_enabled;
	struct ata_device dev[2];
	u_llong sectors;
	struct dev_match_result matches[100];
	union ccb ccb;

	msg_debug(1, "Processing of HDD/HDD_LIST/SMART command started");

	/* check byte order */
#if BYTE_ORDER != LITTLE_ENDIAN
	msg_err(0, "%s: ussd supports only LITTLE_ENDIAN byte order", __FUNCTION__);
	stat_hdd_finish();
	return;
#endif

	/* get statistics of all devices from devstat library */
	if ((f_stat_hdd_command_hdd || f_stat_hdd_command_hdd_list) && !stat_hdd_get_devstat()) {
		stat_hdd_finish();
		return;
	}

	/* check if [IOC]ATAREQUEST ioctl request supported by system */
	f_ata_request_supported = stat_hdd_is_ata_request_supported();
	if (f_stat_hdd_command_hdd || f_stat_hdd_command_smart) {
		tm = get_remote_tm();
		printf("%lu ata_request_supported %d\n", (u_long)tm, f_ata_request_supported);
	}

	/* determine number of ATA channels */
	if (stat_hdd_get_ata_channels_n(&channels_n)) {

		/* process all channels */
			for (channel = 0; channels_n < 0 || channel < channels_n; channel++) {
			msg_debug(2, "%s: Processing ATA channel %d", __FUNCTION__, channel);

			/* get channel devices */
			bzero(dev, sizeof(dev));
			if (!stat_hdd_get_ata_channel_devices(channel, dev)) {
				if (channels_n < 0)
					break;
				else
					continue;
			}
	
			/* check if channel has at least one device */
			if (!*dev[0].name && !*dev[1].name) {
				msg_debug(2, "%s: No devices found", __FUNCTION__);
				continue;
			}

			/* Process all devices within channel */
			for (device = 0; device < 2; device++) {
				/* skip if there is no device */
				if (!*dev[device].name)
					continue;
				msg_debug(2, "%s: Found device %s", __FUNCTION__, dev[device].name);
	
				if (!stat_hdd_is_device_ata(&dev[device])) {
					msg_debug(2, "%s: Device is not ATA device", __FUNCTION__);
					continue;
				}

				/* process HDD_LIST command */
				if (f_stat_hdd_command_hdd_list) {
					tm = get_remote_tm();
					printf("%lu hdd_exists:%s 1\n", (u_long)tm, dev[device].name);
				}
	
				/* go to the next device if no HDD or SMART command given */
				if (!f_stat_hdd_command_hdd && !f_stat_hdd_command_smart) {
					stat_hdd_fake_devstat(dev[device].name);
					continue;
				}
	
				/* refresh device parameters because they can be
				   cached by system */
				stat_hdd_refresh_ata_device_params(&dev[device]);
	
				/* process HDD command */
				if (f_stat_hdd_command_hdd) {
					/* get device size */
					sectors = stat_hdd_get_device_size(&dev[device]);
	
					tm = get_remote_tm();
					printf("%lu hdd_model:%s %s\n",
					    (u_long)tm, dev[device].name, dev[device].model);
					printf("%lu hdd_serno:%s %s\n",
					    (u_long)tm, dev[device].name, dev[device].serial);
					printf("%lu hdd_revision:%s %s\n",
					    (u_long)tm, dev[device].name, dev[device].revision);
					printf("%lu hdd_size_sectors:%s %llu\n",
					    (u_long)tm, dev[device].name, sectors);
					printf("%lu hdd_size_mbytes:%s %llu\n",
					    (u_long)tm, dev[device].name, sectors >> 11);
	
					/* print devstat statistics of device */
					stat_hdd_print_devstat(dev[device].name);
				}
				stat_hdd_fake_devstat(dev[device].name);
	
				/* process SMART command in remaining code */
				if (!f_stat_hdd_command_smart)
					continue;
	
				/* check if SMART supported by device */
				f_smart_supported = stat_hdd_is_smart_supported(&dev[device]);
	
				/* check if SMART enabled on device */
				f_smart_enabled = stat_hdd_is_smart_enabled(&dev[device]);
	
				/* enable SMART if requested */
				if (conf.f_enable_smart && f_ata_request_supported &&
				    f_smart_supported && !f_smart_enabled &&
				    stat_hdd_ata_command_interface(&dev[device],
				    SMART_ENABLE_OPERATIONS, NULL)) {
					f_smart_enabled = 1;
					msg_notice("SMART has been successfully enabled "
					    "on device %s", dev[device].name);
				}
	
				tm = get_remote_tm();
				printf("%lu smart_supported:%s %d\n",
				    (u_long)tm, dev[device].name, f_smart_supported);
				printf("%lu smart_enabled:%s %d\n",
				    (u_long)tm, dev[device].name, f_smart_enabled);
	
				/* don't try to get SMART if no attributes requested */
				if (!f_stat_hdd_smart_attrs_requested) {
					msg_debug(2, "%s: No SMART attributes requested",
					    __FUNCTION__);
					continue;
				}
	
				/* don't try to get SMART if system doesn't support
				   [IOC]ATAREQUEST ioctl request */
				if (!f_ata_request_supported) {
					msg_debug(2, "%s: Can't get SMART because system doesn't "
					    "support [IOC]ATAREQUEST ioctl request", __FUNCTION__);
					continue;
				}
	
				/* don't try to get SMART if it is not supported by device */
				if (!f_smart_supported) {
					msg_debug(2, "%s: SMART is not supported by device",
					    __FUNCTION__);
					continue;
				}
	
				/* don't try to get SMART if it is not enabled on device */
				if (!f_smart_enabled) {
					msg_debug(2, "%s: SMART is not enabled on device",
					    __FUNCTION__);
					continue;
				}
	
				/* get SMART values */
				if (!stat_hdd_ata_command_interface(&dev[device],
				    SMART_READ_VALUES, (char *)&dev[device].smart_values))
					continue;
				msg_debug(2, "%s: Got SMART values", __FUNCTION__);
	
				/* get SMART thresholds */
				if (!stat_hdd_ata_command_interface(&dev[device],
				    SMART_READ_THRESHOLDS, (char *)&dev[device].smart_thresholds))
					continue;
				msg_debug(2, "%s: Got SMART thresholds", __FUNCTION__);
	
				/* process SMART attributes */
				stat_hdd_process_smart_attributes(&dev[device]);
			}
		}
	
		msg_debug(2, "%s: All ATA channels processed", __FUNCTION__);
	}

	/* processing SCSI bus
	 * now we're not support SMART on SCSI, sad but true */

	msg_debug(2, "%s: Processing SCSI bus", __FUNCTION__);
	if ((channel = open(XPT_DEVICE, O_RDWR)) == -1) {
		msg_debug(1, "%s: open(%s) error %d (%s)", __FUNCTION__, XPT_DEVICE, errno, strerror(errno));
		stat_hdd_finish();
		return;
	}
	bzero(&ccb, sizeof(ccb));
	ccb.ccb_h.path_id = CAM_XPT_PATH_ID;
	ccb.ccb_h.target_id = CAM_TARGET_WILDCARD;
	ccb.ccb_h.target_lun = CAM_LUN_WILDCARD;
	ccb.ccb_h.func_code = XPT_DEV_MATCH;
	ccb.cdm.match_buf_len = sizeof(matches);
	ccb.cdm.matches = &matches[0];
	if (ioctl(channel, CAMIOCOMMAND, &ccb) == -1) {
		msg_debug(1, "%s: ioctl(CAMIOCOMMAND) error %d (%s)", __FUNCTION__, errno, strerror(errno));
		stat_hdd_finish();
		return;
	}
	if ((ccb.ccb_h.status != CAM_REQ_CMP)
		|| ((ccb.cdm.status != CAM_DEV_MATCH_LAST)
		&& (ccb.cdm.status != CAM_DEV_MATCH_MORE))) {
		msg_debug(1, "%s: CAM return error %#x, CDM error %d\n",
			__FUNCTION__, ccb.ccb_h.status, ccb.cdm.status);
		stat_hdd_finish();
		return;
	}

	/* walking CAM hierarchy */
	for(device = 0; (unsigned) device < ccb.cdm.num_matches; device++) {
		if (ccb.cdm.matches[device].type == DEV_MATCH_DEVICE) {
			if (ccb.cdm.matches[device].result.device_result.flags
				& DEV_RESULT_UNCONFIGURED)
				continue;
#if __FreeBSD_version > 800100
			/* new atacam devices - see ada(4) */
			if (ccb.cdm.matches[device].result.device_result.protocol == PROTO_ATA) {
				cam_strvis((void *) dev[0].model,
					ccb.cdm.matches[device].result.device_result.ident_data.model,
					sizeof(ccb.cdm.matches[device].result.device_result.ident_data.model),
					sizeof(dev[0].model));
				cam_strvis((void *) dev[0].serial,
					ccb.cdm.matches[device].result.device_result.ident_data.serial,
					sizeof(ccb.cdm.matches[device].result.device_result.ident_data.serial),
					sizeof(dev[0].serial));
				cam_strvis((void *) dev[0].revision,
					ccb.cdm.matches[device].result.device_result.ident_data.revision,
					sizeof(ccb.cdm.matches[device].result.device_result.ident_data.revision),
					sizeof(dev[0].revision));
				memcpy(&dev[0].params,
					&ccb.cdm.matches[device].result.device_result.ident_data,
					sizeof(dev[0].params));
				/* fake 3'rd device on IDE channel is atacam */
				dev[0].device = 2;
			}
			else if (ccb.cdm.matches[device].result.device_result.protocol == PROTO_SCSI) {
#endif
				cam_strvis((u_int8_t *) dev[0].model,
					(u_int8_t *) ccb.cdm.matches[device].result.device_result.inq_data.vendor,
					sizeof(ccb.cdm.matches[device].result.device_result.inq_data.vendor),
					sizeof(dev[0].model));
				if (strlen(dev[0].model) < sizeof(dev[0].model)+2)
					strlcat(dev[0].model, " ", sizeof(dev[0].model));
				cam_strvis((u_int8_t *) dev[0].model+strlen(dev[0].model),
					(u_int8_t *) ccb.cdm.matches[device].result.device_result.inq_data.product,
					sizeof(ccb.cdm.matches[device].result.device_result.inq_data.product),
					sizeof(dev[0].model)-strlen(dev[0].model));
				cam_strvis((u_int8_t *) dev[0].revision,
					(u_int8_t *) ccb.cdm.matches[device].result.device_result.inq_data.revision,
					sizeof(ccb.cdm.matches[device].result.device_result.inq_data.revision),
					sizeof(dev[0].revision));
				/* fake 4'th device on IDE channel is SCSI disk */
				dev[0].device = 3;
				msg_debug(2, "%s: found SCSI device, model is %s",
					__FUNCTION__, dev[0].model);
#if __FreeBSD_version > 800100
			}
#endif
		}
		if (ccb.cdm.matches[device].type == DEV_MATCH_PERIPH) {
			for (i=0; i < dinfo.numdevs; i++) {
				if (((dinfo.devices[i].device_type & DEVSTAT_TYPE_MASK) != DEVSTAT_TYPE_DIRECT)
					|| (dinfo.devices[i].device_type & DEVSTAT_TYPE_PASS))
					continue;
				if (!strcmp(dinfo.devices[i].device_name,
					ccb.cdm.matches[device].result.periph_result.periph_name)
					&& (unsigned) dinfo.devices[i].unit_number == ccb.cdm.matches[device].result.periph_result.unit_number) {
					snprintf(dev[0].name, sizeof(dev[0].name),
						"%s%d",
						ccb.cdm.matches[device].result.periph_result.periph_name,
						ccb.cdm.matches[device].result.periph_result.unit_number);
					msg_debug(2, "%s: periph %s have stat", __FUNCTION__, dev[0].name);
					if (dev[0].device == 2)
						stat_hdd_refresh_ata_device_params(&dev[0]);
					if (dev[0].device == 3)
						stat_hdd_refresh_cam_device_params(&dev[0]);

					if (f_stat_hdd_command_hdd_list) {
						tm = get_remote_tm();
						printf("%lu hdd_exists:%s 1\n",
							(u_long)tm, dev[0].name);
					}
					if (f_stat_hdd_command_hdd) {
						sectors = stat_hdd_get_device_size(&dev[0]);
						tm = get_remote_tm();
						printf("%lu hdd_model:%s %s\n",
							(u_long)tm, dev[0].name, dev[0].model);
						printf("%lu hdd_serno:%s %s\n",
							(u_long)tm, dev[0].name, dev[0].serial);
						printf("%lu hdd_revision:%s %s\n",
							(u_long)tm, dev[0].name, dev[0].revision);
						printf("%lu hdd_size_sectors:%s %llu\n",
							(u_long)tm, dev[0].name, sectors);
						printf("%lu hdd_size_mbytes:%s %llu\n",
							(u_long)tm, dev[0].name, sectors >> 11);
						stat_hdd_print_devstat(dev[0].name);
					}
					stat_hdd_fake_devstat(dev[0].name);
					if (!f_stat_hdd_command_smart)
						break;
					/* Now i don't know how to read SMART from SCSI */
					if (dev[0].device == 3) {
						break;
					}
			
					f_smart_supported = stat_hdd_is_smart_supported(&dev[0]);
					f_smart_enabled = stat_hdd_is_smart_enabled(&dev[0]);
					if (conf.f_enable_smart
						&& f_ata_request_supported
						&& f_smart_supported
						&& !f_smart_enabled
						&& stat_hdd_ata_command_interface(&dev[0], SMART_ENABLE_OPERATIONS, NULL)) {
						f_smart_enabled = 1;
						msg_notice("SMART has been successfully enabled on device %s", dev[0].name);
					}
					tm = get_remote_tm();
					printf("%lu smart_supported:%s %d\n", (u_long)tm, dev[0].name, f_smart_supported);
					printf("%lu smart_enabled:%s %d\n", (u_long)tm, dev[0].name, f_smart_enabled);
					if (!f_stat_hdd_smart_attrs_requested) {
						msg_debug(2, "%s: No SMART attributes requested", __FUNCTION__);
						break;
					}
					if (!f_smart_supported) {
						msg_debug(2, "%s: SMART is not supported by device", __FUNCTION__);
						break;
					}
					if (!f_smart_enabled) {
						msg_debug(2, "%s: SMART is not enabled on device", __FUNCTION__);
						break;
					}
					if (!stat_hdd_ata_command_interface(&dev[0], SMART_READ_VALUES, (char *)&dev[0].smart_values))
						break;
					msg_debug(2, "%s: Got SMART values", __FUNCTION__);
					if (!stat_hdd_ata_command_interface(&dev[0], SMART_READ_THRESHOLDS, (char *)&dev[0].smart_thresholds))
						break;
					msg_debug(2, "%s: Got SMART thresholds", __FUNCTION__);
					stat_hdd_process_smart_attributes(&dev[0]);

					break;
				}
			}
		}
	}
	close(channel);

	for (i=0; i < dinfo.numdevs; i++) {
		if (((dinfo.devices[i].device_type & DEVSTAT_TYPE_MASK) != DEVSTAT_TYPE_DIRECT) || (dinfo.devices[i].device_type & DEVSTAT_TYPE_PASS))
			continue;
		snprintf(dev[0].name, sizeof(dev[0].name), "%s%d", dinfo.devices[i].device_name, dinfo.devices[i].unit_number);
		if (f_stat_hdd_command_hdd_list) {
			tm = get_remote_tm();
			printf("%lu hdd_exists:%s 1\n", (u_long)tm, dev[0].name);
		}
		if (f_stat_hdd_command_hdd) {
			stat_hdd_print_devstat(dev[0].name);
		}
/*
		printf("device %s, unit %d, type %x\n",
			dinfo.devices[i].device_name, dinfo.devices[i].unit_number,
			dinfo.devices[i].device_type & DEVSTAT_TYPE_MASK);
*/
	}
	
	stat_hdd_finish();
}

/*****************************************************************************
 * Gets statistics of all devices from devstat library. If successful,
 * returns non-zero. If not successful, returns zero.
 *****************************************************************************/
static int stat_hdd_get_devstat() {
	struct statinfo stats;

	/* make sure that the userland devstat version matches the kernel
	   devstat version */
#if __FreeBSD_version < 500000
	if (checkversion() < 0) {
		msg_err(0, "%s: checkversion: %s", __FUNCTION__, devstat_errbuf);
#else
	if (devstat_checkversion(NULL) < 0) {
		msg_err(0, "%s: devstat_checkversion: %s", __FUNCTION__, devstat_errbuf);
#endif
		return(0);
	}

	/* get devstat statistics */
	bzero(&dinfo, sizeof(dinfo));
	stats.dinfo = &dinfo;
#if __FreeBSD_version < 500000
	if (getdevs(&stats) < 0) {
		msg_err(0, "%s: getdevs: %s", __FUNCTION__, devstat_errbuf);
#else
	if (devstat_getdevs(NULL, &stats) < 0) {
		msg_err(0, "%s: devstat_getdevs: %s", __FUNCTION__, devstat_errbuf);
#endif
		return(0);
	}
	dinfo_tm = get_remote_tm();

	msg_debug(2, "%s: Got devstat statistics of all devices", __FUNCTION__);
	return(1);
}

/*****************************************************************************
 * Searches for devstat statistics of device with name %name%. If successful,
 * returns pointer to found statistics. If not successful, returns NULL.
 *****************************************************************************/
static const struct devstat *stat_hdd_find_devstat(const char *name) {
	int i, unit_number;
	char *p;

	for (i = 0; i < dinfo.numdevs; i++) {
		if (parse_get_str(name, &p, dinfo.devices[i].device_name) &&
		    parse_get_int(p, &p, &unit_number) && !*p &&
		    unit_number == dinfo.devices[i].unit_number) {
			msg_debug(2, "%s: Found devstat statistics for device %s (index=%d)",
			    __FUNCTION__, name, i);
			return(&dinfo.devices[i]);
		}
	}

	msg_err(0, "%s: devstat statistics for device %s not found", __FUNCTION__, name);
	return(NULL);
}

/*****************************************************************************
 * Prints devstat statistics of device with name %name%.
 *****************************************************************************/
static void stat_hdd_print_devstat(const char *name) {
	time_t tm;
	const struct devstat *ds;
	u_long busy_sec;
	uint64_t bytes_read, bytes_written, bytes_deleted;
#if __FreeBSD_version < 500000
	struct timeval busy_time;
#else
	u_llong qlen;
#endif

	/* find devstat statistics for device */
	if (!(ds = stat_hdd_find_devstat(name)))
		return;

	/* calculate busy time */
#if __FreeBSD_version < 500000
	timerclear(&busy_time);
	if (ds->busy_count && timercmp(&ds->last_comp_time, &ds->start_time, >))
		timersub(&ds->last_comp_time, &ds->start_time, &busy_time);
	timeradd(&busy_time, &ds->busy_time, &busy_time);
	busy_sec = busy_time.tv_sec;
	msg_debug(2, "%s: device %s: busy_count=%ld", __FUNCTION__, name,
	    (long)ds->busy_count);
#else
	busy_sec = ds->busy_time.sec;
	msg_debug(2, "%s: device %s: busy_count=%ld", __FUNCTION__, name,
	    (long)(ds->start_count - ds->end_count));
	if (ds->end_count <= ds->start_count)
		qlen = ds->start_count - ds->end_count;
	else
		qlen = 0x100000000ll - ds->start_count + ds->end_count;
#endif

	/* get bytes counters */
#if __FreeBSD_version < 500000
	bytes_read = ds->bytes_read;
	bytes_written = ds->bytes_written;
	bytes_deleted = ds->bytes_freed;
#else
	bytes_read = ds->bytes[DEVSTAT_READ];
	bytes_written = ds->bytes[DEVSTAT_WRITE];
	bytes_deleted = ds->bytes[DEVSTAT_FREE];
#endif

	/* print statistics */
	tm = dinfo_tm;
	printf("%lu hdd_busy_time:%s %lu\n", (u_long)tm, name, busy_sec);
	printf("%lu hdd_bytes_read:%s %llu\n", (u_long)tm, name, (u_llong)bytes_read);
	printf("%lu hdd_bytes_written:%s %llu\n", (u_long)tm, name, (u_llong)bytes_written);
	printf("%lu hdd_bytes_deleted:%s %llu\n", (u_long)tm, name, (u_llong)bytes_deleted);
#if __FreeBSD_version >= 500000
	printf("%lu hdd_operations_read:%s %llu\n", (u_long)tm, name, (unsigned long long) (0ll+ds->operations[DEVSTAT_READ]));
	printf("%lu hdd_operations_written:%s %llu\n", (u_long)tm, name, (unsigned long long) (0ll+ds->operations[DEVSTAT_WRITE]));
	printf("%lu hdd_operations_deleted:%s %llu\n", (u_long)tm, name, (unsigned long long) (0ll+ds->operations[DEVSTAT_FREE]));
	printf("%lu hdd_duration_read:%s %lu\n", (u_long)tm, name, (0l+ds->duration[DEVSTAT_READ].sec));
	printf("%lu hdd_duration_written:%s %lu\n", (u_long)tm, name, (0l+ds->duration[DEVSTAT_WRITE].sec));
	printf("%lu hdd_duration_deleted:%s %lu\n", (u_long)tm, name, (0l+ds->duration[DEVSTAT_FREE].sec));

	printf("%lu hdd_operations_queue_length:%s %llu\n", (u_long)tm, name, qlen);
#endif
}

/*****************************************************************************
 * Fake devstat statistics of device with name %name%.
 *****************************************************************************/
static void stat_hdd_fake_devstat(const char *name) {
	int i, unit_number;
	char *p;

	for (i = 0; i < dinfo.numdevs; i++) {
		if (parse_get_str(name, &p, dinfo.devices[i].device_name) &&
		    parse_get_int(p, &p, &unit_number) && !*p &&
		    unit_number == dinfo.devices[i].unit_number) {
			/* faking device */
			dinfo.devices[i].device_type = -1;
			return;
		}
	}
}

/*****************************************************************************
 * Returns 1 if IOCATAREQUEST or ATAREQUEST ioctl request supported by system.
 * Otherwise returns 0.
 *****************************************************************************/
static int stat_hdd_is_ata_request_supported() {
#if defined(IOCATAREQUEST) || defined(ATAREQUEST)
	return(1);
#else
	return(0);
#endif
}

/*****************************************************************************
 * Opens device with name %name%. Before opening adds "/dev/" prefix to the
 * begin of device name. %flags% argument is the same as in open(2) function.
 * If successful, returns non-zero. In this case descriptor of opened device
 * returned in %fd%. If not successful, returns zero. In this case value of
 * %fd% is undefined. If flag %f_silent_if_not_exist% is non-zero, error will
 * not be reported in case device doesn't exist.
 *****************************************************************************/
static int stat_hdd_open_device(const char *name, int flags, int f_silent_if_not_exist,
    int *fd) {
	char devname[strlen("/dev/") + DEVICE_NAME_MAXLEN + 1];

	if (snprintf(devname, sizeof(devname), "/dev/%s", name) >= (int)sizeof(devname)) {
		msg_err(0, "%s: Device name too long: %s", __FUNCTION__, name);
		return(0);
	}
	if ((*fd = open(devname, flags)) < 0) {
		if (f_silent_if_not_exist && (errno == ENOENT || errno == ENXIO))
			msg_debug(2, "%s: Device %s not found", __FUNCTION__, devname);
		else
			msg_syserr(0, "%s: open(%s)", __FUNCTION__, devname);
		return(0);
	}

	msg_debug(2, "%s: Device %s opened", __FUNCTION__, devname);
	return(1);
}

/*****************************************************************************
 * Closes device with descriptor %fd%. %name% is device name without "/dev/"
 * prefix used for messages. If successful, returns non-zero. If not
 * successful, returns zero.
 *****************************************************************************/
static int stat_hdd_close_device(const char *name, int fd) {
	if (close(fd) < 0) {
		msg_syserr(0, "%s: close(/dev/%s)", __FUNCTION__, name);
		return(0);
	}

	msg_debug(2, "%s: Device /dev/%s closed", __FUNCTION__, name);
	return(1);
}

/*****************************************************************************
 * Returns descriptor of ATA control device /dev/ata. Opens device if needed.
 * If successful, returns non-zero. In this case descriptor of ATA control
 * device returned in %fd%. If not successful, returns zero. In this case
 * value of %fd% is undefined.
 *****************************************************************************/
static int stat_hdd_get_ata_control_device_fd(int *fd) {
	/* check if device is already opened */
	if (ata_control_device_fd < 0) {
		/* open device */
		if (!stat_hdd_open_device("ata", O_RDONLY, 1, &ata_control_device_fd)) {
			ata_control_device_fd = -1;
			return(0);
		}
	}

	*fd = ata_control_device_fd;
	return(1);
}

/*****************************************************************************
 * Returns descriptor of ATA device with name %name%. %name% must not contain
 * "/dev/" prefix. Opens device if needed. Closes earlier opened device if
 * needed. If successful, returns non-zero. In this case descriptor of ATA
 * device returned in %fd%. If not successful, returns zero. In this case
 * value of %fd% is undefined.
 *****************************************************************************/
static int stat_hdd_get_ata_device_fd(const char *name, int *fd) {
	/* check input parameters */
	if (!*name) {
		msg_err(0, "%s: Device name empty", __FUNCTION__);
		return(0);
	}
	if (strlen(name) >= sizeof(ata_device_name)) {
		msg_err(0, "%s: Device name too long: %s", __FUNCTION__, name);
		return(0);
	}

	/* check if device is already opened */
	if (strcmp(name, ata_device_name)) {
		/* close earlier opened device */
		if (ata_device_fd >= 0) {
			stat_hdd_close_device(ata_device_name, ata_device_fd);
			ata_device_name[0] = 0;
			ata_device_fd = -1;
		}

		/* open device */
		if (!stat_hdd_open_device(name, O_RDONLY, 0, &ata_device_fd)) {
			ata_device_fd = -1;
			return(0);
		}
		strcpy(ata_device_name, name);
	}

	*fd = ata_device_fd;
	return(1);
}

/*****************************************************************************
 * Returns number of ATA channels in the system. If successful, returns
 * non-zero. In this case number of ATA channels returned in %channels_n%.
 * Value -1 of %channels_n% means that old channels detection algorithm should
 * be used. If not successful, returns zero. In this case value of
 * %channels_n% is undefined.
 *****************************************************************************/
static int stat_hdd_get_ata_channels_n(int *channels_n) {
#if defined(IOCATAGMAXCHANNEL) || defined(ATAGMAXCHANNEL)
	int fd;
#endif
#if defined(ATAGMAXCHANNEL)
	struct ata_cmd iocmd;
#endif

#if defined(IOCATAGMAXCHANNEL) || defined(ATAGMAXCHANNEL)
	/* get descriptor of ATA control device */
	if (!stat_hdd_get_ata_control_device_fd(&fd))
		return(0);
#endif

#if defined(IOCATAGMAXCHANNEL)
	if (ioctl(fd, IOCATAGMAXCHANNEL, channels_n) < 0) {
		msg_syserr(0, "%s: ioctl(IOCATAGMAXCHANNEL)", __FUNCTION__);
		return(0);
	}
	msg_debug(2, "%s: Found %d ATA channel(s)", __FUNCTION__, *channels_n);
#elif defined(ATAGMAXCHANNEL)
	bzero(&iocmd, sizeof(iocmd));
	iocmd.cmd = ATAGMAXCHANNEL;
	if (ioctl(fd, IOCATA, &iocmd) < 0) {
		msg_syserr(0, "%s: ioctl(ATAGMAXCHANNEL)", __FUNCTION__);
		return(0);
	}
	*channels_n = iocmd.u.maxchan;
	msg_debug(2, "%s: Found %d ATA channel(s)", __FUNCTION__, *channels_n);
#else
	*channels_n = -1;
	msg_debug(2, "%s: Can't determine number of ATA channels; "
	    "old channels detection algorithm will be used", __FUNCTION__);
#endif
	return(1);
}

/*****************************************************************************
 * Returns parameters of two devices (master and slave) on specified ATA
 * channel %channel%. Before call, array %dev% must be filled with zeros.
 * If successful, returns non-zero. In this case array %dev% filled with
 * parameters of devices. If not successful, returns zero. In this case value
 * of %dev% is undefined.
 *****************************************************************************/
static int stat_hdd_get_ata_channel_devices(int channel, struct ata_device dev[2]) {
	int fd, i;
#if defined(IOCATADEVICES)
	struct ata_ioc_devices devices;
#else
	struct ata_cmd iocmd;
#endif

	/* get descriptor of ATA control device */
	if (!stat_hdd_get_ata_control_device_fd(&fd))
		return(0);

#if defined(IOCATADEVICES)
	bzero(&devices, sizeof(devices));
	devices.channel = channel;
	if (ioctl(fd, IOCATADEVICES, &devices) < 0) {
#else
	bzero(&iocmd, sizeof(iocmd));
	iocmd.channel = channel;
	iocmd.device = -1;
	iocmd.cmd = ATAGPARM;
	if (ioctl(fd, IOCATA, &iocmd) < 0) {
#endif
		if (errno == ENXIO)
			msg_debug(2, "%s: Channel doesn't exist", __FUNCTION__);
		else
#if defined(IOCATADEVICES)
			msg_syserr(0, "%s: ioctl(IOCATADEVICES,channel=%d)",
			    __FUNCTION__, channel);
#else
			msg_syserr(0, "%s: ioctl(ATAGPARM,channel=%d)",
			    __FUNCTION__, channel);
#endif
		return(0);
	}

#if !defined(IOCATADEVICES)
#define devices iocmd.u.param
#endif

	/* fill array with parameters of devices */
	for (i = 0; i < 2; i++) {
		dev[i].channel = channel;
		dev[i].device = i;

		/* device name */
		if (strlen(devices.name[i]) >= sizeof(dev[i].name)) {
			msg_err(0, "%s: Device name too long: %s", __FUNCTION__,
			    devices.name[i]);
			return(0);
		}
		strcpy(dev[i].name, devices.name[i]);

		/* device parameters */
		memcpy(&dev[i].params, &devices.params[i], sizeof(dev[i].params));

		/* device model name */
		memcpy(dev[i].model, dev[i].params.model, sizeof(dev[i].model) - 1);

		/* device serial number */
		memcpy(dev[i].serial, dev[i].params.serial, sizeof(dev[i].serial) - 1);

		/* device firmware revision */
		memcpy(dev[i].revision, dev[i].params.revision, sizeof(dev[i].revision) - 1);
	}

#undef devices
	return(1);
}

/*****************************************************************************
 * Returns 1, if device %dev% is ATA device. Returns 0, if device %dev% is
 * ATAPI or other device.
 *****************************************************************************/
static int stat_hdd_is_device_ata(const struct ata_device *dev) {
	uint16_t *params = (uint16_t *)&dev->params;

	if (params[0] & 0x8000)
		return(0);
	else
		return(1);
}

/*****************************************************************************
 * Refreshes parameters of ATA device %dev% by reading them directly from
 * device. If successful, returns non-zero. In this case %dev->params%
 * structure filled with refreshed parameters. If not successful, returns
 * zero. In this case %dev% is unchanged.
 *****************************************************************************/
static int stat_hdd_refresh_ata_device_params(struct ata_device *dev) {
#if defined(IOCATAREQUEST) || defined(ATAREQUEST)
	if (!stat_hdd_ata_command_interface(dev, IDENTIFY_DEVICE, (char *)&dev->params))
		return(0);

	msg_debug(2, "%s: Device parameters refreshed", __FUNCTION__);
	return(1);
#else
	/* suppress compiler warnings */
	dev = NULL;

	msg_debug(2, "%s: Can't refresh device parameters because system "
	    "doesn't support [IOC]ATAREQUEST ioctl request; "
	    "cached device parameters will be used", __FUNCTION__);
	return(0);
#endif
}

static int stat_hdd_refresh_cam_device_params(struct ata_device *dev) {
	union ccb * ccb;
	struct scsi_inquiry_data ibuf;
	struct scsi_vpd_unit_serial_number sbuf;
	struct cam_device * device;

	if ((device = cam_open_device(dev->name, O_RDWR)) == NULL) {
		msg_syserr(0, "%s: cam_open_device(%s) error %d",
		__FUNCTION__, dev->name, errno);
		return(0);
	}
	if ((ccb = cam_getccb(device)) == NULL) {
		msg_syserr(0, "%s: cam_getccb(%s) error %d",
		__FUNCTION__, dev->name, errno);
		return(0);
	}
	bzero(&(&ccb->ccb_h)[1],
		sizeof(struct ccb_scsiio) - sizeof(struct ccb_hdr));
	scsi_inquiry(&ccb->csio, 0, NULL, MSG_SIMPLE_Q_TAG, (u_int8_t *) &ibuf,
		SHORT_INQUIRY_LENGTH, 0, 0, SSD_FULL_SIZE, 1000);
	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;
	if ((cam_send_ccb(device, ccb) < 0) || ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)) {
		cam_freeccb(ccb);
		cam_close_device(device);
		msg_syserr(0, "%s: cam_send_ccb(%s, INQUIRY) return error %d",
			__FUNCTION__, dev->name, errno);
		return(0);
	}

	/* Getting serial number */
	bzero(&(&ccb->ccb_h)[1],
		sizeof(struct ccb_scsiio) - sizeof(struct ccb_hdr));
	scsi_inquiry(&ccb->csio, 0, NULL, MSG_SIMPLE_Q_TAG, (u_int8_t *) &sbuf,
		sizeof(sbuf), 1, SVPD_UNIT_SERIAL_NUMBER, SSD_FULL_SIZE, 1000);
	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;
	if ((cam_send_ccb(device, ccb) < 0) || ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)) {
		cam_freeccb(ccb);
		cam_close_device(device);
		msg_syserr(0, "%s: cam_send_ccb(%s, SERIAL_NUMBER) return error %d",
			__FUNCTION__, dev->name, errno);
		return(0);
	}
	cam_strvis((void *) dev->serial, sbuf.serial_num,
		sizeof(sbuf.serial_num) < sbuf.length ? sizeof(sbuf.serial_num) : sbuf.length,
		sizeof(dev->serial));
	cam_freeccb(ccb);
	cam_close_device(device);
	return(1);
}
		
/*****************************************************************************
 * Returns size of device %dev% in sectors.
 *****************************************************************************/
static u_llong stat_hdd_get_device_size(const struct ata_device *dev) {
	const struct ata_params *params = &dev->params;
	u_llong sectors;
	struct cam_device * device;
	struct scsi_read_capacity_data caps;
#ifdef scsi_read_capacity_data_long
	struct scsi_read_capacity_data_long longcaps;
#endif
	union ccb *ccb;

	if (dev->device == 3) {
		if ((device = cam_open_device(dev->name, O_RDWR)) == NULL) {
			msg_syserr(0, "%s: cam_open_device(%s) error %d",
			__FUNCTION__, dev->name, errno);
			return(0);
		}
		if ((ccb = cam_getccb(device)) == NULL) {
			msg_syserr(0, "%s: cam_getccb(%s) error %d",
			__FUNCTION__, dev->name, errno);
			return(0);
		}

		/* Test Unit Ready, to skip dead disks */
		bzero(&(&ccb->ccb_h)[1],
			sizeof(struct ccb_scsiio) - sizeof(struct ccb_hdr));
		scsi_test_unit_ready(&ccb->csio, 0, NULL, MSG_SIMPLE_Q_TAG,
			SSD_FULL_SIZE, 1000);
		ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;
		if (cam_send_ccb(device, ccb) < 0) {
			cam_freeccb(ccb);
			cam_close_device(device);
			msg_syserr(0, "%s: cam_send_ccb(%s, TUR) return error %d",
			__FUNCTION__, dev->name, errno);
			return(0);
		}
		if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
			cam_freeccb(ccb);
			cam_close_device(device);
			return(0);
		}

		/* Read Capacity command */
		bzero(&(&ccb->ccb_h)[1],
			sizeof(struct ccb_scsiio) - sizeof(struct ccb_hdr));
		scsi_read_capacity(&ccb->csio, 0, NULL, MSG_SIMPLE_Q_TAG, &caps,
			SSD_FULL_SIZE, 1000);
		ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;
		if ((cam_send_ccb(device, ccb) < 0)
		|| ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)) {
			cam_freeccb(ccb);
			cam_close_device(device);
			msg_syserr(0, "%s: cam_send_ccb(%s, READCAP) return error %d",
				__FUNCTION__, dev->name, errno);
			return(0);
		}
		sectors = scsi_4btoul(caps.addr);
#ifdef scsi_read_capacity_data_long
		if (sectors == 0xffffffff) {
			scsi_read_capacity_16(&ccb->csio, 0, NULL, MSG_SIMPLE_Q_TAG,
				0, 0, 0, &longcaps, SSD_FULL_SIZE, 1000);
			ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;
			if ((cam_send_ccb(device, ccb) < 0)
			|| ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)) {
				cam_freeccb(ccb);
				cam_close_device(device);
				msg_syserr(0, "%s: cam_send_ccb(%s, READCAP) return error %d",
					__FUNCTION__, dev->name, errno);
				return(0);
			}
			sectors = scsi_8btou64(longcaps.addr);
		}
#endif
		cam_freeccb(ccb);
		cam_close_device(device);
		return(sectors);
	}
	
	/* check if 48-bit addressing is supported */
#if __FreeBSD_version < 500000
	if (params->support.support_one && !params->support.support_zero &&
	    params->support.address48)
		sectors = (u_llong)params->lba_size48;
#else
	if (params->support.command2 >> 14 == 1 &&
	    params->support.command2 & ATA_SUPPORT_ADDRESS48)
		sectors = (u_llong)params->lba_size48_1 |
		    (u_llong)params->lba_size48_2 << 16 |
		    (u_llong)params->lba_size48_3 << 32 |
		    (u_llong)params->lba_size48_4 << 48;
#endif
	else
	/* check if LBA is supported */
#if __FreeBSD_version < 500000
	if (params->support_lba)
		sectors = (u_llong)params->lba_size;
#else
	if (params->capabilities1 & ATA_SUPPORT_LBA)
		sectors = (u_llong)params->lba_size_1 |
		    (u_llong)params->lba_size_2 << 16;
#endif
	else
	/* use CHS geometry */
		sectors = (u_llong)params->cylinders *
		    (u_llong)params->heads *
		    (u_llong)params->sectors;

	return(sectors);
}

/*****************************************************************************
 * Returns 1, if SMART is supported by device %dev%. Returns 0, if SMART is
 * not supported by device or if can't determine.
 *****************************************************************************/
static int stat_hdd_is_smart_supported(const struct ata_device *dev) {
	const struct ata_params *params = &dev->params;

#if __FreeBSD_version < 500000
	if (params->support.support_one && !params->support.support_zero &&
	    params->support.smart)
#else
	if (params->support.command2 >> 14 == 1 &&
	    params->support.command1 & ATA_SUPPORT_SMART)
#endif
		return(1);

	return(0);
}

/*****************************************************************************
 * Returns 1, if SMART is enabled on device %dev%. Returns 0, if SMART is
 * not enabled on device or if can't determine.
 *****************************************************************************/
static int stat_hdd_is_smart_enabled(const struct ata_device *dev) {
	const struct ata_params *params = &dev->params;

#if __FreeBSD_version < 500000
	if (params->enabled.extended_one && !params->enabled.extended_zero &&
	    params->enabled.smart)
#else
	if (params->enabled.extension >> 14 == 1 &&
	    params->enabled.command1 & ATA_SUPPORT_SMART)
#endif
		return(1);

	return(0);
}

/*****************************************************************************
 * Sends command %command% to ATA device %dev%. If command must read some
 * data from device, this data stored in %data% buffer. Before call, %data%
 * buffer must be allocated and must be of sufficient size. If successful,
 * returns non-zero. In this case read data, if any, returned in %data%
 * buffer. If not successful, returns zero. In this case %data% is unchanged.
 *****************************************************************************/
static int stat_hdd_ata_command_interface(const struct ata_device *dev,
    ata_command_set command, char *data) {
#if defined(IOCATAREQUEST) || defined(ATAREQUEST)
	int fd;
	size_t bytes_to_copy;
	char buf[512];
#if defined(IOCATAREQUEST)
	struct ata_ioc_request request;
#if __FreeBSD_version > 800100
	struct cam_device * m_camdev;
	union ccb ccb;
	int camflags;
#endif

#else
	struct ata_cmd iocmd;
#endif

	bzero(buf, sizeof(buf));
#if defined(IOCATAREQUEST)
	/* get descriptor of ATA device */
#if __FreeBSD_version > 800100
	if (dev->device < 2) {
		m_camdev = NULL;
#endif
		if (!stat_hdd_get_ata_device_fd(dev->name, &fd))
			return(0);
#if __FreeBSD_version > 800100
	} else {
		fd = -1;
		if ((m_camdev = cam_open_device(dev->name, O_RDWR)) == NULL) {
			msg_syserr(0, "%s: cam_open_device(%s) error %d",
		    	__FUNCTION__, dev->name, errno);
			return(0);
		}
	}
#endif

	bzero(&request, sizeof(request));
#else
	/* get descriptor of ATA control device */
	if (!stat_hdd_get_ata_control_device_fd(&fd))
		return(0);

	bzero(&iocmd, sizeof(iocmd));
	iocmd.cmd = ATAREQUEST;
	iocmd.channel = dev->channel;
	iocmd.device = dev->device;
#define request iocmd.u.request
#endif

	request.timeout = 600;
	bytes_to_copy = 0;

	switch(command) {
	case IDENTIFY_DEVICE:
		request.u.ata.command = ATA_CMD_IDENTIFY_DEVICE;
		request.flags = ATA_CMD_READ;
		request.data = buf;
		request.count = 512;
		bytes_to_copy = 512;
		break;
	case SMART_READ_VALUES:
		request.u.ata.command = ATA_CMD_SMART;
		request.u.ata.feature = ATA_SMART_READ_VALUES;
		request.u.ata.lba = 0xc24f00;
		request.flags = ATA_CMD_READ;
		request.data = buf;
		request.count = 512;
		bytes_to_copy = 512;
		break;
	case SMART_READ_THRESHOLDS:
		request.u.ata.command = ATA_CMD_SMART;
		request.u.ata.feature = ATA_SMART_READ_THRESHOLDS;
		request.u.ata.count = 1;
		request.u.ata.lba = 0xc24f01;
		request.flags = ATA_CMD_READ;
		request.data = buf;
		request.count = 512;
		bytes_to_copy = 512;
		break;
	case SMART_ENABLE_OPERATIONS:
		request.u.ata.command = ATA_CMD_SMART;
		request.u.ata.feature = ATA_SMART_ENABLE_OPERATIONS;
		request.u.ata.lba = 0xc24f00;
		request.flags = ATA_CMD_CONTROL;
		break;
	default:
		msg_err(0, "%s: unrecognized command %d", __FUNCTION__, command);
#if __FreeBSD_version > 800100
		if (dev->device == 2)
			cam_close_device(m_camdev);
#endif
		return(0);
	}

#if defined(IOCATAREQUEST)
#if __FreeBSD_version > 800100
	if (dev->device < 2) {
#endif
		if (ioctl(fd, IOCATAREQUEST, &request) < 0 || request.error) {
			msg_syserr(0, "%s: ioctl(IOCATAREQUEST,command=%s,device=%s)",
		    	__FUNCTION__, ata_command_str[command], dev->name);
			return(0);
		}
#if __FreeBSD_version > 800100
	} else {
		bzero(&ccb, sizeof(ccb));
		if(!request.count)
			camflags = CAM_DIR_NONE;
		else if (request.flags == ATA_CMD_READ)
			camflags = CAM_DIR_IN;
		else
			camflags = CAM_DIR_OUT;
		cam_fill_ataio(&ccb.ataio, 0, NULL, camflags, MSG_SIMPLE_Q_TAG,
			(u_int8_t*)request.data, request.count,
			request.timeout * 1000);
		ccb.ataio.cmd.flags = 0;
		ccb.ataio.cmd.command = request.u.ata.command;
		ccb.ataio.cmd.features = request.u.ata.feature;
		ccb.ataio.cmd.lba_low = request.u.ata.lba;
		ccb.ataio.cmd.lba_mid = request.u.ata.lba >> 8;
		ccb.ataio.cmd.lba_high = request.u.ata.lba >> 16;
		ccb.ataio.cmd.device = 0x40 | ((request.u.ata.lba >> 24) & 0x0f);
		ccb.ataio.cmd.sector_count = request.u.ata.count;
		ccb.ccb_h.flags |= CAM_DEV_QFRZDIS;
		if (cam_send_ccb(m_camdev, &ccb) < 0 || request.error) {
			msg_syserr(0, "%s: cam_send_ccb(command=%s,device=%s)",
		    	__FUNCTION__, ata_command_str[command], dev->name);
			cam_close_device(m_camdev);
			return(0);
		}
		cam_close_device(m_camdev);
		if ((ccb.ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)
			return(0);
		//cam_error_print(m_camdev, &ccb, CAM_ESF_ALL, CAM_EPF_ALL, stdout);
	}
#endif
#else
	if (ioctl(fd, IOCATA, &iocmd) < 0 || request.error) {
		msg_syserr(0, "%s: ioctl(ATAREQUEST,command=%s,device=%s)",
		    __FUNCTION__, ata_command_str[command], dev->name);
		return(0);
	}
#endif
#undef request

	if (bytes_to_copy)
		memcpy(data, buf, bytes_to_copy);
	return(1);
#else
	/* suppress compiler warnings */
	dev = NULL;
	command = 0;
	data = NULL;

	return(0);
#endif
}

/*****************************************************************************
 * Processes and outputs SMART attributes of device %dev%.
 *****************************************************************************/

/*****************************************************************************
 * Closes opened descriptors.
 *****************************************************************************/
static void stat_hdd_finish() {
	/* close currently opened ATA device */
	if (ata_device_fd >= 0) {
		stat_hdd_close_device(ata_device_name, ata_device_fd);
		ata_device_name[0] = 0;
		ata_device_fd = -1;
	}

	/* close ATA control device */
	if (ata_control_device_fd >= 0) {
		stat_hdd_close_device("ata", ata_control_device_fd);
		ata_control_device_fd = -1;
	}

	msg_debug(1, "Processing of HDD/HDD_LIST/SMART command finished");
}
#endif

#ifdef __linux__
void stat_hdd_process_smart_attributes(const char * dev) {
#else
void stat_hdd_process_smart_attributes(const struct ata_device *dev) {
#endif
	time_t tm;
	int i;
	uint8_t id;
	u_int flags, value, worst, thresh, f_failed, f_failed_max;
	const uint8_t *raw;
	uint16_t raw_word[3];
	uint64_t raw_value;
	int cur, min, max, avg;
#ifdef __linux__
    struct disk_smart_data smart_data;
    init_disk_smart_data(&smart_data);
    #define GET_ATTR(i) smart_data.datas[i]    
    #define GET_DEV_NAME get_dev_name_from_path(dev)
#else    
    #define GET_ATTR(i) dev->smart_values.attr[i]
    #define GET_DEV_NAME dev->name
#endif

	f_failed_max = 0;
	tm = get_remote_tm();
#ifdef __linux__    
    if(!parse_disk(&smart_data, dev, 1)) {
        printf("%lu smart_supported:%s %d\n", tm, GET_DEV_NAME, smart_data.smart_supported);
        printf("%lu smart_enabled:%s %d\n", tm, GET_DEV_NAME, smart_data.smart_enabled);
    } else 
        msg_err(0, "Can't parse disk %s", dev);
	for (i = 1; i < MAX_SMART_ATTR_ID; i++) {
#else    
	for (i = 0; i < ATA_SMART_ATTRIBUTES_MAXN; i++) {
#endif    
		id = GET_ATTR(i).id;

		/* skip unused attributes */
		if (!id)
			continue;
#ifndef __linux__
		/* skip attributes without thresholds */
		if (dev->smart_thresholds.attr[i].id != id) {
			msg_err(0, "%s: cannot find threshold for attribute %u of device %s",
			    __FUNCTION__, (u_int)id, dev->name);
			continue;
		}
#endif        

		flags = GET_ATTR(i).flags;
#ifdef __linux
		value = GET_ATTR(i).current_value;
#else        
		value = GET_ATTR(i).value;
#endif
		worst = GET_ATTR(i).worst_value;
#ifdef __linux
		raw = GET_ATTR(i).raw;
		thresh = GET_ATTR(i).threshold;
#else
		raw = GET_ATTR(i).raw_value;
		thresh = dev->smart_thresholds.attr[i].threshold;
#endif        

		/* check whether SMART failed */
		f_failed = thresh > 0 && thresh < 254 ? (value <= thresh ? 2 :
		    (worst <= thresh ? 1 : 0)) : 0;
		f_failed_max = VG_MAX(f_failed, f_failed_max);

		/* skip not requested attributes */
		if (!f_stat_hdd_smart_attrs[id]) {
            msg_debug(2, "Skipping %d SMART attribute as it was not requested", id);
			continue;
        }

		/* translate raw value */
		raw_word[0] = (uint16_t)raw[0] | (uint16_t)raw[1] << 8;
		raw_word[1] = (uint16_t)raw[2] | (uint16_t)raw[3] << 8;
		raw_word[2] = (uint16_t)raw[4] | (uint16_t)raw[5] << 8;
		raw_value = (uint64_t)raw_word[0] |
		    (uint64_t)raw_word[1] << 16 |
		    (uint64_t)raw_word[2] << 32;
		printf("%lu smart_%u_flags:%s %u\n",	(u_long)tm, (u_int)id, GET_DEV_NAME,
		    flags);
		printf("%lu smart_%u_value:%s %u\n",	(u_long)tm, (u_int)id, GET_DEV_NAME,
		    value);
		printf("%lu smart_%u_worst:%s %u\n",	(u_long)tm, (u_int)id, GET_DEV_NAME,
		    worst);
		printf("%lu smart_%u_thresh:%s %u\n",	(u_long)tm, (u_int)id, GET_DEV_NAME,
		    thresh);
		printf("%lu smart_%u_raw:%s %llu\n",	(u_long)tm, (u_int)id, GET_DEV_NAME,
#ifndef __linux__        
		    (u_llong)raw_value);
#else            
		    (unsigned long long) raw_value);
#endif            

		/* process some attributes */
		switch (id) {
		/* spin-up time */
		case 3:
			cur = raw_word[0];
			avg = raw_word[1];
			if (cur)
				printf("%lu smart_spinup_time:%s %d\n", (u_long)tm,
				    GET_DEV_NAME, cur);
			if (avg)
				printf("%lu smart_spinup_time_avg:%s %d\n", (u_long)tm,
				    GET_DEV_NAME, avg);
			break;
		/* temperature */
		case 194:
			cur = raw_word[0];
			min = VG_MIN(raw_word[1], raw_word[2]);
			max = VG_MAX(raw_word[1], raw_word[2]);
			printf("%lu smart_temp:%s %d\n", (u_long)tm, GET_DEV_NAME, cur);
			if (min && max && cur >= min && cur <= max) {
				printf("%lu smart_temp_min:%s %d\n", (u_long)tm,
				    GET_DEV_NAME, min);
				printf("%lu smart_temp_max:%s %d\n", (u_long)tm,
				    GET_DEV_NAME, max);
			}
			break;
		}
	}

	printf("%lu smart_failed:%s %u\n", (u_long)tm, GET_DEV_NAME, f_failed_max);
}
