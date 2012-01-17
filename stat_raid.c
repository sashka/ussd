#ifdef __linux__
/* This flag shows that RAID command is given */
int f_stat_raid_command_raid = 0;

/* This flag shows that RAID_LIST command is given */
int f_stat_raid_command_raid_list = 0;
#else
/*
 * Written by Vadim Guchenko <yhw@rambler-co.ru>
 *
 * 	$Id: stat_raid.c 112401 2012-01-12 12:57:01Z dark $
 */

#include "config.h"

#include <sys/queue.h>
#if defined(HAVE_LIBGEOM_H)
#include <libgeom.h>
#endif

#include "stat_common.h"


/* This flag shows that RAID command is given */
int f_stat_raid_command_raid = 0;

/* This flag shows that RAID_LIST command is given */
int f_stat_raid_command_raid_list = 0;


#if defined(HAVE_LIBGEOM_H)
static void stat_raid_process_geom_class(struct gclass *);
#endif

/*****************************************************************************
 * Processes RAID command.
 *****************************************************************************/
void stat_raid() {
#if defined(HAVE_LIBGEOM_H)
	struct gmesh mesh;
	struct gclass *classp;
#endif

	msg_debug(1, "Processing of RAID command started");

#if defined(HAVE_LIBGEOM_H)
	/* get GEOM tree */
	if (geom_gettree(&mesh)) {
		msg_syserr(0, "%s: geom_gettree", __FUNCTION__);
		msg_debug(1, "Processing of RAID command finished");
		return;
	}

	/* process RAID classes */
	LIST_FOREACH(classp, &mesh.lg_class, lg_class) {
		if (!strcmp(classp->lg_name, "MIRROR") ||
		    !strcmp(classp->lg_name, "STRIPE") ||
		    !strcmp(classp->lg_name, "CONCAT"))
			stat_raid_process_geom_class(classp);
	}

	/* free GEOM tree */
	geom_deletetree(&mesh);
#endif

	msg_debug(1, "Processing of RAID command finished");
}

/*****************************************************************************
 * Processes GEOM class %classp%.
 *****************************************************************************/
#if defined(HAVE_LIBGEOM_H)
static void stat_raid_process_geom_class(struct gclass *classp) {
	struct ggeom *geomp;
	struct gprovider *providerp;
	struct gconsumer *consumerp;
	struct gconfig *configp;
	const char *name, *state;
	int state_error;
	time_t tm;

	/* process all geoms */
	LIST_FOREACH(geomp, &classp->lg_geom, lg_geom) {
		/* skip geoms without providers */
		if (LIST_EMPTY(&geomp->lg_provider))
			continue;

		/* get RAID name */
		providerp = LIST_FIRST(&geomp->lg_provider);
		name = providerp->lg_name;

		/* process RAID_LIST command */
		if (f_stat_raid_command_raid_list) {
			tm = get_remote_tm();
			printf("%lu raid_exists:%s 1\n", (u_long)tm, name);
		}

		/* go to the next RAID if no RAID command given */
		if (!f_stat_raid_command_raid)
			continue;

		/* get RAID state */
		state = NULL;
		LIST_FOREACH(configp, &geomp->lg_config, lg_config) {
			if (!strcasecmp(configp->lg_name, "state")) {
				state = configp->lg_val;
				break;
			}
		}

		/* translate RAID state to numeric format */
		state_error = (state && ((!strcmp(classp->lg_name, "MIRROR") &&
		    !strcasecmp(state, "COMPLETE")) ||
		    ((!strcmp(classp->lg_name, "STRIPE") ||
		      !strcmp(classp->lg_name, "CONCAT")) &&
		    !strcasecmp(state, "UP")))) ? 0 : 2;

		/* check if RAID is synchronizing */
		if (state && state_error) {
			LIST_FOREACH(consumerp, &geomp->lg_consumer, lg_consumer) {
				LIST_FOREACH(configp, &consumerp->lg_config, lg_config) {
					if (!strcasecmp(configp->lg_name, "synchronized")) {
						state_error = 1;
						break;
					}
				}
				if (state_error == 1)
					break;
			}
		}

		tm = get_remote_tm();
		if (state)
			printf("%lu raid_state:%s %s\n", (u_long)tm, name, state);
		printf("%lu raid_state_error:%s %d\n", (u_long)tm, name, state_error);
	}
}
#endif


#endif //__linux__
