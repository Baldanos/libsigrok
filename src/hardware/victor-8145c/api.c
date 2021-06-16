/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 Baldanos <balda@balda.ch>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include "protocol.h"

#define SERIALCOMM "9600/8n1/dtr=1/rts=0/flow=1"
#define BUF_MAX 50

static const uint32_t scanopts[] = {
        SR_CONF_CONN,
        SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
        SR_CONF_MULTIMETER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
        SR_CONF_LIMIT_SAMPLES | SR_CONF_SET,
};

static struct sr_dev_driver victor_8145c_driver_info;

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_dev_inst *sdi;
	struct sr_config *src;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	const char *conn, *serialcomm;
	GSList *l, *devices;

	(void)options;

	devices = NULL;
	conn = serialcomm = NULL;

	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		case SR_CONF_SERIALCOMM:
			serialcomm = g_variant_get_string(src->data, NULL);
			break;
		}
	}
	if (!conn)
		return NULL;
	if (!serialcomm){
		serialcomm = SERIALCOMM;
	}

	serial = sr_serial_dev_inst_new(conn, serialcomm);

	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return NULL;
	sr_info("Probing serial port %s.", conn);

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = "Victor";
	sdi->model = "3145C";
	sdi->version = "1";
	sdi->conn = serial;
	devc = g_malloc0(sizeof(struct dev_context));
	sr_sw_limits_init(&devc->limits);
	sdi->priv = devc;
	sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "A1");
	devices = g_slist_append(devices, sdi);

	serial_close(serial);
	if (!devices)
		sr_serial_dev_inst_free(serial);

	/* TODO: scan for devices, either based on a SR_CONF_CONN option
	 * or on a USB scan. */

	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_serial_dev_inst *serial;
	char init[] = "#*ONL\r\n";
	int len;
	char * buf;

	buf = g_malloc(BUF_MAX);

	serial = sdi->conn;

	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return SR_ERR_IO;

	 if (serial_write_blocking(serial, init, strlen(init),
				   serial_timeout(serial, strlen(init))) < 0) {
		 sr_err("Unable to send identification request.");
		 g_free(buf);
		 return SR_ERR_IO;
	 }

	 len = BUF_MAX;
	 serial_readline(serial, &buf, &len, 100);
	 buf[len] = '\0';

	 /*TODO: Identify device*/
	 g_free(buf);
	 return SR_OK;


}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_serial_dev_inst *serial;
	char req[] = "#*RST\r\n";

	serial = sdi->conn;

	if (serial_write_blocking(serial, req, strlen(req),
				  serial_timeout(serial, strlen(req))) < 0) {
		return SR_ERR_IO;
	}
	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;
	devc = sdi->priv;

	return sr_sw_limits_config_get(&devc->limits, key, data);
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;
	devc = sdi->priv;

	return sr_sw_limits_config_set(&devc->limits, key, data);
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	(void)sdi;
	(void)data;
	(void)cg;

	return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	/* TODO: configure hardware, reset acquisition state, set up
	 * callbacks and send header packet. */

	(void)sdi;
	struct sr_serial_dev_inst *serial;
	char req[] = "#*RD?\r\n";

	serial = sdi->conn;

	if (serial_write_blocking(serial, req, strlen(req),
		serial_timeout(serial, strlen(req))) < 0) {
		sr_err("Unable to send capture request.");
		return SR_ERR_IO;
	}

	serial_source_add(sdi->session, serial, G_IO_IN, 100,
			  victor_8145c_receive_data, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	/* TODO: stop acquisition. */

	(void)sdi;
	struct sr_serial_dev_inst *serial;

	serial = sdi->conn;

	serial_source_remove(sdi->session, serial);

	return SR_OK;
}

static struct sr_dev_driver victor_8145c_driver_info = {
	.name = "victor-8145c",
	.longname = "Victor 8145C",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(victor_8145c_driver_info);
