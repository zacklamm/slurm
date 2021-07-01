/*****************************************************************************\
 * src/common/io_hdr.c - IO connection header functions
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark A. Grondona <mgrondona@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "src/common/fd.h"
#include "src/common/io_hdr.h"
#include "src/common/slurm_protocol_defs.h"

#define IO_PROTOCOL_VERSION 0xb001

/* If this changes, io_hdr_pack|unpack must change. */
int g_io_hdr_size = sizeof(uint32_t) + 3*sizeof(uint16_t);

/* If this changes, io_init_msg_pack|unpack must change. */
static int g_io_init_msg_packed_size =
	sizeof(uint16_t)    /* version */
	+ sizeof(uint32_t)  /* nodeid */
	+ (SLURM_IO_KEY_SIZE + sizeof(uint32_t)) /* signature */
	+ sizeof(uint32_t)  /* stdout_objs */
	+ sizeof(uint32_t); /* stderr_objs */

#define io_init_msg_packed_size() g_io_init_msg_packed_size

void io_hdr_pack(io_hdr_t *hdr, buf_t *buffer)
{
	/* If this function changes, io_hdr_packed_size must change. */
	pack16(hdr->type, buffer);
	pack16(hdr->gtaskid, buffer);
	pack16(hdr->ltaskid, buffer);
	pack32(hdr->length, buffer);
}

int io_hdr_unpack(io_hdr_t *hdr, buf_t *buffer)
{
	/* If this function changes, io_hdr_packed_size must change. */
	safe_unpack16(&hdr->type, buffer);
	safe_unpack16(&hdr->gtaskid, buffer);
	safe_unpack16(&hdr->ltaskid, buffer);
	safe_unpack32(&hdr->length, buffer);
	return SLURM_SUCCESS;

    unpack_error:
	error("%s: error: %m", __func__);
	return SLURM_ERROR;
}

/*
 * Only return when the all of the bytes have been read, or an unignorable
 * error has occurred.
 */
static int _full_read(int fd, void *buf, size_t count)
{
	int n;
	int left;
	void *ptr;

	left = count;
	ptr = buf;
	while (left > 0) {
	again:
		if ((n = read(fd, (void *) ptr, left)) < 0) {
			if (errno == EINTR
			    || errno == EAGAIN
			    || errno == EWOULDBLOCK)
				goto again;
			debug3("Leaving  _full_read on error!");
			return -1;
		} else if (n == 0) { /* got eof */
			debug3("  _full_read (_client_read) got eof");
			return 0;
		}
		left -= n;
		ptr += n;
	}

	return count;
}

/*
 * Read and unpack an io_hdr_t from a file descriptor (socket).
 */
int io_hdr_read_fd(int fd, io_hdr_t *hdr)
{
	int n = 0;
	buf_t *buffer = init_buf(io_hdr_packed_size());

	debug3("Entering %s", __func__);
	n = _full_read(fd, buffer->head, io_hdr_packed_size());
	if (n <= 0)
		goto fail;
	if (io_hdr_unpack(hdr, buffer) == SLURM_ERROR) {
		n = -1;
		goto fail;
	}

fail:
	debug3("Leaving %s", __func__);
	free_buf(buffer);
	return n;
}




int
io_init_msg_validate(struct slurm_io_init_msg *msg, const char *sig)
{
	debug2("Entering io_init_msg_validate");

	debug3("  msg->version = %x", msg->version);
	debug3("  msg->nodeid = %u", msg->nodeid);

	if (msg->version != IO_PROTOCOL_VERSION) {
		error("Invalid IO init header version");
		return SLURM_ERROR;
	}

	if (memcmp((void *) sig, (void *) msg->io_key, SLURM_IO_KEY_SIZE)) {
		error("Invalid IO init header signature");
		return SLURM_ERROR;
	}

	debug2("Leaving %s", __func__);
	return SLURM_SUCCESS;
}


static int io_init_msg_pack(struct slurm_io_init_msg *hdr, buf_t *buffer)
{
	/* If this function changes, io_init_msg_packed_size must change. */
	if (hdr->version == SLURM_PROTOCOL_VERSION) {
		pack16(hdr->version, buffer);
		pack32(hdr->nodeid, buffer);
		pack32(hdr->stdout_objs, buffer);
		pack32(hdr->stderr_objs, buffer);
		packmem((char *) hdr->io_key,
			(uint32_t) SLURM_IO_KEY_SIZE, buffer);
	} else if (hdr->version == IO_PROTOCOL_VERSION) {
		pack16(hdr->version, buffer);
		pack32(hdr->nodeid, buffer);
		pack32(hdr->stdout_objs, buffer);
		pack32(hdr->stderr_objs, buffer);
		packmem((char *) hdr->io_key,
			(uint32_t) SLURM_IO_KEY_SIZE, buffer);
	} else {
		error("Invalid IO init header version");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}


static int io_init_msg_unpack(struct slurm_io_init_msg *hdr, buf_t *buffer)
{
	/* If this function changes, io_init_msg_packed_size must change. */
	uint32_t val;
	char *tmp_ptr = NULL;

	safe_unpack16(&hdr->version, buffer);
	if (hdr->version == IO_PROTOCOL_VERSION ||
	    hdr->version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&hdr->nodeid, buffer);
		safe_unpack32(&hdr->stdout_objs, buffer);
		safe_unpack32(&hdr->stderr_objs, buffer);
		safe_unpackmem_ptr(&tmp_ptr, &val, buffer);
		if (val != SLURM_IO_KEY_SIZE)
			goto unpack_error;
		memcpy(hdr->io_key, tmp_ptr, SLURM_IO_KEY_SIZE);
	} else
		goto unpack_error;

	return SLURM_SUCCESS;

    unpack_error:
	error("%s: unpack error", __func__);
	return SLURM_ERROR;
}


int
io_init_msg_write_to_fd(int fd, struct slurm_io_init_msg *msg)
{
	int rc = SLURM_ERROR;
	buf_t *buf = init_buf(io_init_msg_packed_size());

	xassert(msg);

	debug2("%s: entering", __func__);
	msg->version = IO_PROTOCOL_VERSION;
	debug2("%s: msg->nodeid = %d", __func__, msg->nodeid);
	if (io_init_msg_pack(msg, buf) != SLURM_SUCCESS)
		goto rwfail;

	safe_write(fd, buf->head, get_buf_offset(buf));
	rc = SLURM_SUCCESS;

rwfail:
	free_buf(buf);
	debug2("%s: leaving", __func__);
	return rc;
}

int
io_init_msg_read_from_fd(int fd, struct slurm_io_init_msg *msg)
{
	buf_t *buf;

	xassert(msg);

	debug2("Entering %s", __func__);
	if (wait_fd_readable(fd, 300)) {
		error("io_init_msg_read timed out");
		return SLURM_ERROR;
	}

	buf = init_buf(io_init_msg_packed_size());
	safe_read(fd, buf->head, io_init_msg_packed_size());

	io_init_msg_unpack(msg, buf);

	free_buf(buf);
	debug2("Leaving %s", __func__);
	return SLURM_SUCCESS;

rwfail:
	free_buf(buf);
	error("%s: reading slurm_io_init_msg failed: %m",__func__);
	return SLURM_ERROR;
}
