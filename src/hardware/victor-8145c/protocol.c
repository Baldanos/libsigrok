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

static void victor_8145c_process_line(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	double value;    /* Measured value */
	double scale;    /* Scaling factor depending on range and function */
	int digits = 4;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	struct sr_datafeed_packet packet;

	devc = sdi->priv;
	sr_spew("Received line '%s'.", devc->buf);

	/* Start decoding. */
	value = 0.0;
	scale = 1.0;
	sr_analog_init(&analog, &encoding, &meaning, &spec, 2);

	analog.meaning->mq = SR_MQ_VOLTAGE;
	analog.meaning->unit = SR_UNIT_VOLT;
	analog.meaning->mqflags |= (SR_MQFLAG_DC | SR_MQFLAG_RMS);
	analog.meaning->channels = sdi->channels;

	value = g_ascii_strtod(devc->buf + 4, NULL);
	sr_dbg("Converted value: %f", value);

	/* Finish and send packet. */
	analog.num_samples = 1;
	analog.encoding->digits = digits;
	analog.data = &value;
	memset(&packet, 0, sizeof(struct sr_datafeed_packet));
	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(sdi, &packet);

	sr_sw_limits_update_samples_read(&devc->limits, 1);
	devc->buflen = 0;

}

static void victor_8145c_request_measure(const struct sr_dev_inst *sdi)
{
	struct sr_serial_dev_inst *serial;
	char req[] = "#*RD?\r\n";

	serial = sdi->conn;

	if (serial_write_blocking(serial, req, strlen(req),
				  serial_timeout(serial, strlen(req))) < 0) {
		sr_err("Unable to send capture request.");
	}

}


SR_PRIV int victor_8145c_receive_data(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	int len;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	serial = sdi->conn;

	if (revents == G_IO_IN) {
		/* TODO */
		len = serial_read_nonblocking(serial, devc->buf + devc->buflen, 1);
		if (len < 1)
			return FALSE;
		if( *(devc->buf + devc->buflen) == '\n') {
			*(devc->buf + devc->buflen-1) = '\0';
			devc->buflen -= 1;
			victor_8145c_process_line(sdi);
			victor_8145c_request_measure(sdi);
		} else {
			devc->buflen += len;
		}

	}
	if (sr_sw_limits_check(&devc->limits)) {
		sr_dev_acquisition_stop(sdi);
	}

	return TRUE;
}
