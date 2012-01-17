/*
 * Written by Vadim Guchenko <yhw@rambler-co.ru>
 *
 * 	$Id$
 */

#include <sys/types.h>
#include <sys/uio.h>

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#include "stat_common.h"


/* Name of the memory device */
#define MEMORY_DEVICE		"/dev/mem"

/* The SMBIOS entry point structure can be located by searching for the anchor-string
   on paragraph (16-byte) boundaries within the physical memory address range 0xf0000
   to 0xfffff */
#define EPS_RANGE_START		0xf0000
#define EPS_RANGE_LEN		0x10000
#define EPS_RANGE_STEP		0x10
#define EPS_ANCHOR		"_SM_"

/* Maximum number of strings in the unformed section of the SMBIOS structure */
#define STRINGS_MAXN		32

/* SMBIOS entry point structure */
#pragma pack(1)
struct smbios_eps {
	uint8_t anchor[4];
	uint8_t checksum;
	uint8_t length;
	uint8_t major_version;
	uint8_t minor_version;
	uint16_t max_size;
	uint8_t revision;
	uint8_t formatted_area[5];
	uint8_t ianchor[5];
	uint8_t ichecksum;
	uint16_t table_length;
	uint32_t table_address;
	uint16_t struct_count;
	uint8_t bcd_revision;
};
#pragma pack()
VG_CASSERT_DECL(sizeof(struct smbios_eps) == 31);

/* SMBIOS structure header */
#pragma pack(1)
struct smbios_h {
	uint8_t type;
	uint8_t length;
	uint16_t handle;
};
#pragma pack()
VG_CASSERT_DECL(sizeof(struct smbios_h) == 4);

/* SMBIOS structure */
#pragma pack(1)
struct smbios_s {
	struct smbios_h h;
	union {
		struct {
			uint8_t vendor;
			uint8_t version;
			uint8_t reserved[2];
			uint8_t release_date;
		} bios;
		struct {
			uint8_t manufacturer;
			uint8_t product_name;
			uint8_t version;
			uint8_t serial_number;
		} system;
		struct {
			uint8_t manufacturer;
			uint8_t product_name;
			uint8_t version;
			uint8_t serial_number;
			uint8_t asset_tag;
			uint8_t future_flags;
			uint8_t location;
			uint16_t chassis_handle;
			uint8_t type;
		} module;
		struct {
			uint8_t manufacturer;
			uint8_t type;
			uint8_t version;
			uint8_t serial_number;
			uint8_t asset_tag;
		} chassis;
		struct {
			uint8_t socket_designation;
			uint8_t type;
			uint8_t family;
			uint8_t manufacturer;
			uint8_t id[8];
			uint8_t version;
			uint8_t voltage;
			uint16_t external_clock;
			uint16_t max_speed;
			uint16_t current_speed;
			uint8_t status;
			uint8_t upgrade;
			uint16_t l1_cache_handle;
			uint16_t l2_cache_handle;
			uint16_t l3_cache_handle;
			uint8_t serial_number;
			uint8_t asset_tag;
			uint8_t part_number;
			uint8_t core_count;
			uint8_t core_enabled;
			uint8_t thread_count;
			uint16_t characteristics;
			uint16_t family2;
		} processor;
		struct {
			uint16_t physical_handle;
			uint16_t ecc_handle;
			uint16_t total_width;
			uint16_t data_width;
			uint16_t size;
			uint8_t form_factor;
			uint8_t device_set;
			uint8_t device_locator;
			uint8_t bank_locator;
			uint8_t type;
			uint16_t type_detail;
			uint16_t speed;
			uint8_t manufacturer;
			uint8_t serial_number;
			uint8_t asset_tag;
			uint8_t part_number;
		} memory;
	} u;
};
#pragma pack()

/* Decode state structure */
struct decode_state {
	u_int f_bios;
	u_int f_system;
	u_int modules;
	u_int chassis;
	u_int processors;
	u_int memory_devs;
};


static int stat_smbios_find_table(int, struct smbios_eps *);
static void stat_smbios_read_table(int, const struct smbios_eps *);
static void stat_smbios_decode_struct(struct smbios_s *, struct decode_state *, time_t);
static int stat_smbios_decode_strings(struct smbios_s *, char ***, u_int *);
static int stat_smbios_read_memory(int, off_t, size_t, uint8_t **);
static int stat_smbios_eval_checksum(const uint8_t *, size_t);
static void stat_smbios_finish(void);

/*****************************************************************************
 * Processes SMBIOS command.
 *****************************************************************************/
void stat_smbios() {
	int memfd;
	struct smbios_eps eps;

	msg_debug(1, "Processing of SMBIOS command started");

	/* open memory device */
	if ((memfd = open(MEMORY_DEVICE, O_RDONLY)) < 0) {
		msg_syserr(0, "%s: open(%s)", __FUNCTION__, MEMORY_DEVICE);
		stat_smbios_finish();
		return;
	}

	/* find SMBIOS table */
	if (!stat_smbios_find_table(memfd, &eps)) {
		close(memfd);
		stat_smbios_finish();
		return;
	}

	/* read and decode SMBIOS table */
	stat_smbios_read_table(memfd, &eps);

	close(memfd);
	stat_smbios_finish();
}

/*****************************************************************************
 * Searches for the SMBIOS table. If the table is found, stores the SMBIOS
 * entry point structure into %eps_buf% and returns non-zero. If the table
 * is not found or an error occurs, returns zero.
 *****************************************************************************/
int stat_smbios_find_table(int memfd, struct smbios_eps *eps_buf) {
	uint8_t *buf;
	size_t offset;
	int f_found;
	struct smbios_eps *eps;

	/* allocate buffer for memory block */
	if (!(buf = malloc(EPS_RANGE_LEN))) {
		msg_syserr(0, "%s: malloc(%lu)", __FUNCTION__, (u_long)EPS_RANGE_LEN);
		return(0);
	}

	/* read memory block */
	if (!stat_smbios_read_memory(memfd, EPS_RANGE_START, EPS_RANGE_LEN, &buf)) {
		free(buf);
		return(0);
	}

	/* search for SMBIOS entry point structure */
	f_found = 0;
	eps = NULL;
	for (offset = 0; offset < EPS_RANGE_LEN - sizeof(*eps); offset += EPS_RANGE_STEP) {
		eps = (struct smbios_eps *)(buf + offset);
		if (!memcmp(eps->anchor, EPS_ANCHOR, sizeof(eps->anchor))) {
			msg_debug(2, "%s: SMBIOS EPS found at 0x%08lx", __FUNCTION__,
			    (u_long)offset + EPS_RANGE_START);
			f_found = 1;
			break;
		}
	}
	if (!f_found) {
		msg_err(0, "%s: SMBIOS EPS not found", __FUNCTION__);
		free(buf);
		return(0);
	}

	/* verify EPS length */
	if (eps->length < sizeof(*eps)) {
		msg_err(0, "%s: SMBIOS EPS has too small length (%lu bytes)",
		    __FUNCTION__, (u_long)eps->length);
		free(buf);
		return(0);
	}

	/* evaluate EPS checksum */
	if (!stat_smbios_eval_checksum((uint8_t *)eps, eps->length)) {
		msg_err(0, "%s: SMBIOS EPS has wrong checksum", __FUNCTION__);
		free(buf);
		return(0);
	}

	/* verify SMBIOS table length */
	if (!eps->table_length) {
		msg_err(0, "%s: SMBIOS table has zero length", __FUNCTION__);
		free(buf);
		return(0);
	}

	msg_debug(2, "%s: SMBIOS version %u.%u present", __FUNCTION__,
	    (u_int)eps->major_version, (u_int)eps->minor_version);
	msg_debug(2, "%s: SMBIOS table located at 0x%08lx", __FUNCTION__,
	    (u_long)eps->table_address);
	msg_debug(2, "%s: %lu structures occupying %lu bytes", __FUNCTION__,
	    (u_long)eps->struct_count, (u_long)eps->table_length);

	/* store EPS */
	memcpy(eps_buf, eps, sizeof(*eps));

	free(buf);
	return(1);
}

/*****************************************************************************
 * Reads and decodes the SMBIOS table.
 *****************************************************************************/
void stat_smbios_read_table(int memfd, const struct smbios_eps *eps) {
	uint8_t *buf, *p, *pe;
	u_int i;
	struct smbios_s *smbios;
	struct decode_state decode_state;
	time_t tm;

	/* allocate buffer for SMBIOS table */
	if (!(buf = malloc(eps->table_length))) {
		msg_syserr(0, "%s: malloc(%lu)", __FUNCTION__, (u_long)eps->table_length);
		return;
	}

	/* read SMBIOS table */
	if (!stat_smbios_read_memory(memfd, eps->table_address, eps->table_length, &buf)) {
		free(buf);
		return;
	}
	tm = get_remote_tm();

	/* traverse SMBIOS table */
	p = buf;
	pe = p + eps->table_length;
	bzero(&decode_state, sizeof(decode_state));
	for (i = 1; i <= eps->struct_count; i++) {
		msg_debug(2, "%s: Processing structure %u of %u", __FUNCTION__,
		    i, (u_int)eps->struct_count);
		smbios = (struct smbios_s *)p;

		/* verify formatted section */
		if ((size_t)(pe - p) < sizeof(smbios->h) + 2 ||
		    (size_t)(pe - p) < (size_t)smbios->h.length + 2) {
			msg_err(0, "%s: Formatted section extends beyond the table",
			    __FUNCTION__);
			free(buf);
			return;
		}
		if (smbios->h.length < sizeof(smbios->h)) {
			msg_err(0, "%s: Formatted section too short (%lu bytes)",
			    __FUNCTION__, (u_long)smbios->h.length);
			free(buf);
			return;
		}

		/* skip unformed section */
		p += smbios->h.length + 2;
		while (*(p - 1) || *(p - 2)) {
			if (p == pe) {
				msg_err(0, "%s: Unformed section extends beyond the table",
				    __FUNCTION__);
				free(buf);
				return;
			}
			p++;
		}

		msg_debug(2, "%s: Type %u, Handle 0x%04x, %lu/%lu bytes",
		    __FUNCTION__, (u_int)smbios->h.type, (u_int)smbios->h.handle,
		    (u_long)smbios->h.length, (u_long)(p - (uint8_t *)smbios -
		    smbios->h.length));

		/* decode SMBIOS structure */
		stat_smbios_decode_struct(smbios, &decode_state, tm);
	}

	free(buf);
}

/*****************************************************************************
 * Decodes the SMBIOS structure %smbios%.
 *****************************************************************************/
void stat_smbios_decode_struct(struct smbios_s *smbios, struct decode_state *decode_state,
    time_t tm) {
	char *strings[STRINGS_MAXN], **str;
	u_int strings_n;
	u_long size;

	str = strings;
	if (!stat_smbios_decode_strings(smbios, &str, &strings_n))
		return;

#define is_field_exists(field)								\
	(VG_OFFSETOF(struct smbios_s, field) + sizeof(smbios->field) <=			\
	    smbios->h.length)
#define print_field_as_string(var, field)						\
	if (is_field_exists(field) && smbios->field && smbios->field <= strings_n)	\
		printf("%lu " var " %s\n", (u_long)tm, str[smbios->field - 1])
#define print_param_field_as_string(var, num, field)						\
	if (is_field_exists(field) && smbios->field && smbios->field <= strings_n)	\
		printf("%lu " var ":%u %s\n", (u_long)tm, num, str[smbios->field - 1])
#define print_param_field_as_num(var, num, field)	\
	if (is_field_exists(field) && smbios->field)	 \
		printf("%lu " var ":%u %d\n", (u_long)tm, num, smbios->field)

	switch (smbios->h.type) {
	case 0: /* BIOS Information */
		if (decode_state->f_bios++)
			break;
		print_field_as_string("bios_vendor", u.bios.vendor);
		print_field_as_string("bios_version", u.bios.version);
		print_field_as_string("bios_release_date", u.bios.release_date);
		break;
	case 1: /* System Information */
		if (decode_state->f_system++)
			break;
		print_field_as_string("system_manufacturer", u.system.manufacturer);
		print_field_as_string("system_product_name", u.system.product_name);
		print_field_as_string("system_version", u.system.version);
		print_field_as_string("system_serial_number", u.system.serial_number);
		break;
	case 2: /* Base Board Information */
		printf("%lu module_handle:%u %u\n", (u_long)tm, decode_state->modules, smbios->h.handle);
		print_param_field_as_num("module_type", decode_state->modules, u.module.type);
		if (is_field_exists(u.module.type) && smbios->u.module.type == 10) {
			print_field_as_string("base_board_manufacturer", u.module.manufacturer);
			print_field_as_string("base_board_product_name", u.module.product_name);
			print_field_as_string("base_board_version", u.module.version);
			print_field_as_string("base_board_serial_number", u.module.serial_number);
			print_field_as_string("base_board_asset_tag", u.module.asset_tag);
			print_field_as_string("base_board_location", u.module.location);
			if (is_field_exists(u.module.chassis_handle) && smbios->u.module.chassis_handle)
				printf("%lu base_board_chassis_handle %u\n", (u_long)tm, smbios->u.module.chassis_handle);
		}
		print_param_field_as_string("module_manufacturer", decode_state->modules, u.module.manufacturer);
		print_param_field_as_string("module_product_name", decode_state->modules, u.module.product_name);
		print_param_field_as_string("module_version", decode_state->modules, u.module.version);
		print_param_field_as_string("module_serial_number", decode_state->modules, u.module.serial_number);
		print_param_field_as_string("module_asset_tag", decode_state->modules, u.module.asset_tag);
		print_param_field_as_string("module_location", decode_state->modules, u.module.location);
		if (is_field_exists(u.module.chassis_handle) && smbios->u.module.chassis_handle)
			printf("%lu module_chassis_handle:%u %u\n", (u_long)tm, decode_state->modules, smbios->u.module.chassis_handle);
		break;
		decode_state->modules++;
	case 3: /* System Enclosure or Chassis */
		printf("%lu chassis_handle:%u %u\n", (u_long)tm, decode_state->modules, smbios->h.handle);
		print_param_field_as_num("chassis_type", decode_state->chassis, u.chassis.type);
		print_param_field_as_string("chassis_manufacturer", decode_state->chassis, u.chassis.manufacturer);
		print_param_field_as_string("chassis_version", decode_state->chassis, u.chassis.version);
		print_param_field_as_string("chassis_serial_number", decode_state->chassis, u.chassis.serial_number);
		print_param_field_as_string("chassis_asset_tag", decode_state->chassis, u.chassis.asset_tag);
		decode_state->chassis++;
		break;
	case 4: /* Processor Information  */
		print_param_field_as_string("processor_socket", decode_state->processors, u.processor.socket_designation);
		print_param_field_as_num("processor_type", decode_state->processors, u.processor.type);
		print_param_field_as_num("processor_family", decode_state->processors, u.processor.family);
		print_param_field_as_string("processor_manufacturer", decode_state->processors, u.processor.manufacturer);
		print_param_field_as_string("processor_version", decode_state->processors, u.processor.version);
		print_param_field_as_num("processor_voltage", decode_state->processors, u.processor.voltage);
		print_param_field_as_num("processor_external_clock", decode_state->processors, u.processor.external_clock);
		print_param_field_as_num("processor_max_speed", decode_state->processors, u.processor.max_speed);
		print_param_field_as_num("processor_current_speed", decode_state->processors, u.processor.current_speed);
		print_param_field_as_num("processor_status", decode_state->processors, u.processor.status);
		print_param_field_as_num("processor_upgrade", decode_state->processors, u.processor.upgrade);
		print_param_field_as_string("processor_serial_number", decode_state->processors, u.processor.serial_number);
		print_param_field_as_string("processor_asset_tag", decode_state->processors, u.processor.asset_tag);
		print_param_field_as_string("processor_part_number", decode_state->processors, u.processor.part_number);
		print_param_field_as_num("processor_core_count", decode_state->processors, u.processor.core_count);
		print_param_field_as_num("processor_core_enabled", decode_state->processors, u.processor.core_enabled);
		print_param_field_as_num("processor_thread_count", decode_state->processors, u.processor.thread_count);
		print_param_field_as_num("processor_characteristics", decode_state->processors, u.processor.characteristics);
		print_param_field_as_num("processor_family2", decode_state->processors, u.processor.family2);
		decode_state->processors++;
		break;
	case 17: /* Memory Device  */
		printf("%lu memory_handle:%u %u\n", (u_long)tm, decode_state->memory_devs, smbios->h.handle);
		if (is_field_exists(u.memory.physical_handle))
			printf("%lu physical_memory_handle:%hhu %hu\n", (u_long)tm, decode_state->memory_devs, smbios->u.memory.physical_handle);
		if (is_field_exists(u.memory.size)) {
			size = smbios->u.memory.size;
			if (size & 0x8000)
				size &= ~(0x8000);
			else
				size *= 1024;
			printf("%lu memory_size_kb:%hhu %lu\n", (u_long)tm, decode_state->memory_devs, size);
		}
		print_param_field_as_num("memory_form_factor", decode_state->memory_devs, u.memory.form_factor);
		print_param_field_as_string("memory_device_locator", decode_state->memory_devs, u.memory.device_locator);
		print_param_field_as_string("memory_bank_locator", decode_state->memory_devs, u.memory.bank_locator);
		print_param_field_as_num("memory_type", decode_state->memory_devs, u.memory.type);
		print_param_field_as_num("memory_type_detail", decode_state->memory_devs, u.memory.type_detail);
		print_param_field_as_num("memory_speed", decode_state->memory_devs, u.memory.speed);
		print_param_field_as_string("memory_manufacturer", decode_state->memory_devs, u.memory.manufacturer);
		print_param_field_as_string("memory_serial_number", decode_state->memory_devs, u.memory.serial_number);
		print_param_field_as_string("memory_asset_tag", decode_state->memory_devs, u.memory.asset_tag);
		print_param_field_as_string("memory_part_number", decode_state->memory_devs, u.memory.part_number);
		decode_state->memory_devs++;
		break;
	}
}

/*****************************************************************************
 * Decodes the unformed section of the SMBIOS structure %smbios%.
 * If successful, stores the array of strings into %strings%, the number of
 * strings into %strings_n% and returns non-zero. If not successful, returns
 * zero.
 *****************************************************************************/
int stat_smbios_decode_strings(struct smbios_s *smbios, char ***strings, u_int *strings_n) {
	uint8_t *p;
	u_int n;

	n = 0;
	p = (uint8_t *)smbios;
	p += smbios->h.length;
	while (*p) {
		if (n == STRINGS_MAXN) {
			msg_err(0, "%s: Too many strings in unformed section "
			    "(maximum %u allowed)", __FUNCTION__, (u_int)STRINGS_MAXN);
			return(0);
		}
		(*strings)[n++] = (char *)p;

		/* replace control characters */
		do {
			if (*p < 0x20)
				*p = ' ';
		} while (*(++p));
		p++;
	}

	*strings_n = n;
	return(1);
}

/*****************************************************************************
 * Reads %len% bytes from the memory device %memfd% into buffer %buf%
 * starting from offset %offset%. If successful, returns non-zero. If not
 * successful, returns zero.
 *****************************************************************************/
int stat_smbios_read_memory(int memfd, off_t offset, size_t len, uint8_t **buf) {
	ssize_t nbytes;

	if (lseek(memfd, offset, SEEK_SET) < 0) {
		msg_syserr(0, "%s: lseek", __FUNCTION__);
		return(0);
	}

	nbytes = read(memfd, *buf, len);
	if (nbytes < 0) {
		msg_syserr(0, "%s: read", __FUNCTION__);
		return(0);
	}
	if ((size_t)nbytes != len) {
		msg_err(0, "%s: read: %lu bytes requested but only %lu bytes actually read",
		    __FUNCTION__, (u_long)len, (u_long)nbytes);
		return(0);
	}

	return(1);
}

/*****************************************************************************
 * Returns non-zero if the checksum of the first %len% bytes of buffer %buf%
 * evaluates to 0. Otherwise returns zero.
 *****************************************************************************/
int stat_smbios_eval_checksum(const uint8_t *buf, size_t len) {
	const uint8_t *p, *pe;
	uint8_t sum;

	p = buf;
	pe = p + len;
	sum = 0;
	while (p < pe)
		sum += *p++;

	return(sum == 0);
}

/*****************************************************************************
 * Finishes processing of SMBIOS command.
 *****************************************************************************/
void stat_smbios_finish() {
	msg_debug(1, "Processing of SMBIOS command finished");
}
