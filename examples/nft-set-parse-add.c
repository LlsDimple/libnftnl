/*
 * (C) 2013 by Álvaro Neira Ayuso <alvaroneay@gmail.com>
 *
 * Based on nft-set-xml-add from:
 *
 * (C) 2013 by Pablo Neira Ayuso <pablo@netfilter.org>
 * (C) 2013 by Arturo Borrero Gonzalez <arturo@debian.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <linux/netfilter/nfnetlink.h>

#include <libmnl/libmnl.h>
#include <libnftnl/set.h>

static struct nftnl_set *set_parse_file(const char *file, uint16_t format)
{
	int fd;
	struct nftnl_set *s;
	struct nftnl_parse_err *err;
	char data[4096];

	s = nftnl_set_alloc();
	if (s == NULL) {
		perror("OOM");
		return NULL;
	}

	fd = open(file, O_RDONLY);
	if (fd < 0) {
		perror("open");
		return NULL;
	}

	if (read(fd, data, sizeof(data)) < 0) {
		perror("read");
		close(fd);
		return NULL;
	}
	close(fd);

	err = nftnl_parse_err_alloc();
	if (err == NULL) {
		perror("error");
		return NULL;
	}

	if (nftnl_set_parse(s, format, data, err) < 0) {
		nftnl_parse_perror("Unable to parse file", err);
		nftnl_parse_err_free(err);
		return NULL;
	}

	nftnl_parse_err_free(err);

	nftnl_set_set_u32(s, NFTNL_SET_ID, 1);
	return s;

}

int main(int argc, char *argv[])
{
	struct mnl_socket *nl;
	char buf[MNL_SOCKET_BUFFER_SIZE];
	struct nlmsghdr *nlh;
	uint32_t portid, seq, set_seq;
	struct nftnl_set *s;
	uint16_t family, format, outformat;
	struct mnl_nlmsg_batch *batch;
	int ret;

	if (argc < 2) {
		printf("Usage: %s {json} <file>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	if (strcmp(argv[1], "json") == 0) {
		format = NFTNL_PARSE_JSON;
		outformat = NFTNL_OUTPUT_JSON;
	} else {
		printf("Unknown format: only json is supported\n");
		exit(EXIT_FAILURE);
	}

	s = set_parse_file(argv[2], format);
	if (s == NULL)
		exit(EXIT_FAILURE);

	nftnl_set_fprintf(stdout, s, outformat, 0);
	fprintf(stdout, "\n");

	seq = time(NULL);

	batch = mnl_nlmsg_batch_start(buf, sizeof(buf));

	nftnl_batch_begin(mnl_nlmsg_batch_current(batch), seq++);
	mnl_nlmsg_batch_next(batch);

	family = nftnl_set_get_u32(s, NFTNL_SET_FAMILY);

	set_seq = seq;
	nlh = nftnl_set_nlmsg_build_hdr(mnl_nlmsg_batch_current(batch),
				      NFT_MSG_NEWSET, family,
				      NLM_F_CREATE|NLM_F_ACK, seq++);
	nftnl_set_nlmsg_build_payload(nlh, s);
	nftnl_set_free(s);
	mnl_nlmsg_batch_next(batch);

	nftnl_batch_end(mnl_nlmsg_batch_current(batch), seq++);
	mnl_nlmsg_batch_next(batch);

	nl = mnl_socket_open(NETLINK_NETFILTER);
	if (nl == NULL) {
		perror("mnl_socket_open");
		exit(EXIT_FAILURE);
	}

	if (mnl_socket_bind(nl, 0, MNL_SOCKET_AUTOPID) < 0) {
		perror("mnl_socket_bind");
		exit(EXIT_FAILURE);
	}
	portid = mnl_socket_get_portid(nl);

	if (mnl_socket_sendto(nl, mnl_nlmsg_batch_head(batch),
			      mnl_nlmsg_batch_size(batch)) < 0) {
		perror("mnl_socket_send");
		exit(EXIT_FAILURE);
	}

	mnl_nlmsg_batch_stop(batch);

	ret = mnl_socket_recvfrom(nl, buf, sizeof(buf));
	while (ret > 0) {
		ret = mnl_cb_run(buf, ret, set_seq, portid, NULL, NULL);
		if (ret <= 0)
			break;
		ret = mnl_socket_recvfrom(nl, buf, sizeof(buf));
	}
	if (ret == -1) {
		perror("error");
		exit(EXIT_FAILURE);
	}

	mnl_socket_close(nl);

	return EXIT_SUCCESS;
}
