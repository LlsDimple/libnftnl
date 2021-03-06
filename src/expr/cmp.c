/*
 * (C) 2012 by Pablo Neira Ayuso <pablo@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This code has been sponsored by Sophos Astaro <http://www.sophos.com>
 */

#include "internal.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>
#include <errno.h>

#include <libmnl/libmnl.h>
#include <linux/netfilter/nf_tables.h>
#include <libnftnl/expr.h>
#include <libnftnl/rule.h>

struct nftnl_expr_cmp {
	union nftnl_data_reg	data;
	enum nft_registers	sreg;
	enum nft_cmp_ops	op;
};

static int
nftnl_expr_cmp_set(struct nftnl_expr *e, uint16_t type,
		      const void *data, uint32_t data_len)
{
	struct nftnl_expr_cmp *cmp = nftnl_expr_data(e);

	switch(type) {
	case NFTNL_EXPR_CMP_SREG:
		cmp->sreg = *((uint32_t *)data);
		break;
	case NFTNL_EXPR_CMP_OP:
		cmp->op = *((uint32_t *)data);
		break;
	case NFTNL_EXPR_CMP_DATA:
		memcpy(&cmp->data.val, data, data_len);
		cmp->data.len = data_len;
		break;
	default:
		return -1;
	}
	return 0;
}

static const void *
nftnl_expr_cmp_get(const struct nftnl_expr *e, uint16_t type,
		      uint32_t *data_len)
{
	struct nftnl_expr_cmp *cmp = nftnl_expr_data(e);

	switch(type) {
	case NFTNL_EXPR_CMP_SREG:
		*data_len = sizeof(cmp->sreg);
		return &cmp->sreg;
	case NFTNL_EXPR_CMP_OP:
		*data_len = sizeof(cmp->op);
		return &cmp->op;
	case NFTNL_EXPR_CMP_DATA:
		*data_len = cmp->data.len;
		return &cmp->data.val;
	}
	return NULL;
}

static int nftnl_expr_cmp_cb(const struct nlattr *attr, void *data)
{
	const struct nlattr **tb = data;
	int type = mnl_attr_get_type(attr);

	if (mnl_attr_type_valid(attr, NFTA_CMP_MAX) < 0)
		return MNL_CB_OK;

	switch(type) {
	case NFTA_CMP_SREG:
	case NFTA_CMP_OP:
		if (mnl_attr_validate(attr, MNL_TYPE_U32) < 0)
			abi_breakage();
		break;
	case NFTA_CMP_DATA:
		if (mnl_attr_validate(attr, MNL_TYPE_BINARY) < 0)
			abi_breakage();
		break;
	}

	tb[type] = attr;
	return MNL_CB_OK;
}

static void
nftnl_expr_cmp_build(struct nlmsghdr *nlh, const struct nftnl_expr *e)
{
	struct nftnl_expr_cmp *cmp = nftnl_expr_data(e);

	if (e->flags & (1 << NFTNL_EXPR_CMP_SREG))
		mnl_attr_put_u32(nlh, NFTA_CMP_SREG, htonl(cmp->sreg));
	if (e->flags & (1 << NFTNL_EXPR_CMP_OP))
		mnl_attr_put_u32(nlh, NFTA_CMP_OP, htonl(cmp->op));
	if (e->flags & (1 << NFTNL_EXPR_CMP_DATA)) {
		struct nlattr *nest;

		nest = mnl_attr_nest_start(nlh, NFTA_CMP_DATA);
		mnl_attr_put(nlh, NFTA_DATA_VALUE, cmp->data.len, cmp->data.val);
		mnl_attr_nest_end(nlh, nest);
	}
}

static int
nftnl_expr_cmp_parse(struct nftnl_expr *e, struct nlattr *attr)
{
	struct nftnl_expr_cmp *cmp = nftnl_expr_data(e);
	struct nlattr *tb[NFTA_CMP_MAX+1] = {};
	int ret = 0;

	if (mnl_attr_parse_nested(attr, nftnl_expr_cmp_cb, tb) < 0)
		return -1;

	if (tb[NFTA_CMP_SREG]) {
		cmp->sreg = ntohl(mnl_attr_get_u32(tb[NFTA_CMP_SREG]));
		e->flags |= (1 << NFTA_CMP_SREG);
	}
	if (tb[NFTA_CMP_OP]) {
		cmp->op = ntohl(mnl_attr_get_u32(tb[NFTA_CMP_OP]));
		e->flags |= (1 << NFTA_CMP_OP);
	}
	if (tb[NFTA_CMP_DATA]) {
		ret = nftnl_parse_data(&cmp->data, tb[NFTA_CMP_DATA], NULL);
		e->flags |= (1 << NFTA_CMP_DATA);
	}

	return ret;
}

static const char *expr_cmp_str[] = {
	[NFT_CMP_EQ]	= "eq",
	[NFT_CMP_NEQ]	= "neq",
	[NFT_CMP_LT]	= "lt",
	[NFT_CMP_LTE]	= "lte",
	[NFT_CMP_GT]	= "gt",
	[NFT_CMP_GTE]	= "gte",
};

static const char *cmp2str(uint32_t op)
{
	if (op > NFT_CMP_GTE)
		return "unknown";

	return expr_cmp_str[op];
}

static inline int nftnl_str2cmp(const char *op)
{
	if (strcmp(op, "eq") == 0)
		return NFT_CMP_EQ;
	else if (strcmp(op, "neq") == 0)
		return NFT_CMP_NEQ;
	else if (strcmp(op, "lt") == 0)
		return NFT_CMP_LT;
	else if (strcmp(op, "lte") == 0)
		return NFT_CMP_LTE;
	else if (strcmp(op, "gt") == 0)
		return NFT_CMP_GT;
	else if (strcmp(op, "gte") == 0)
		return NFT_CMP_GTE;
	else {
		errno = EINVAL;
		return -1;
	}
}

static int nftnl_expr_cmp_json_parse(struct nftnl_expr *e, json_t *root,
					struct nftnl_parse_err *err)
{
#ifdef JSON_PARSING
	struct nftnl_expr_cmp *cmp = nftnl_expr_data(e);
	const char *op;
	uint32_t uval32;
	int base;

	if (nftnl_jansson_parse_val(root, "sreg", NFTNL_TYPE_U32, &uval32,
				  err) == 0)
		nftnl_expr_set_u32(e, NFTNL_EXPR_CMP_SREG, uval32);

	op = nftnl_jansson_parse_str(root, "op", err);
	if (op != NULL) {
		base = nftnl_str2cmp(op);
		if (base < 0)
			return -1;

		nftnl_expr_set_u32(e, NFTNL_EXPR_CMP_OP, base);
	}

	if (nftnl_jansson_data_reg_parse(root, "data",
				       &cmp->data, err) == DATA_VALUE)
		e->flags |= (1 << NFTNL_EXPR_CMP_DATA);

	return 0;
#else
	errno = EOPNOTSUPP;
	return -1;
#endif
}

static int nftnl_expr_cmp_export(char *buf, size_t size,
				 const struct nftnl_expr *e, int type)
{
	struct nftnl_expr_cmp *cmp = nftnl_expr_data(e);
	NFTNL_BUF_INIT(b, buf, size);

	if (e->flags & (1 << NFTNL_EXPR_CMP_SREG))
		nftnl_buf_u32(&b, type, cmp->sreg, SREG);
	if (e->flags & (1 << NFTNL_EXPR_CMP_OP))
		nftnl_buf_str(&b, type, cmp2str(cmp->op), OP);
	if (e->flags & (1 << NFTNL_EXPR_CMP_DATA))
		nftnl_buf_reg(&b, type, &cmp->data, DATA_VALUE, DATA);

	return nftnl_buf_done(&b);
}

static int nftnl_expr_cmp_snprintf_default(char *buf, size_t size,
					   const struct nftnl_expr *e)
{
	struct nftnl_expr_cmp *cmp = nftnl_expr_data(e);
	int remain = size, offset = 0, ret;

	ret = snprintf(buf, remain, "%s reg %u ",
		       cmp2str(cmp->op), cmp->sreg);
	SNPRINTF_BUFFER_SIZE(ret, remain, offset);

	ret = nftnl_data_reg_snprintf(buf + offset, remain, &cmp->data,
				    NFTNL_OUTPUT_DEFAULT, 0, DATA_VALUE);
	SNPRINTF_BUFFER_SIZE(ret, remain, offset);

	return offset;
}

static int
nftnl_expr_cmp_snprintf(char *buf, size_t size, uint32_t type,
			uint32_t flags, const struct nftnl_expr *e)
{
	switch (type) {
	case NFTNL_OUTPUT_DEFAULT:
		return nftnl_expr_cmp_snprintf_default(buf, size, e);
	case NFTNL_OUTPUT_XML:
	case NFTNL_OUTPUT_JSON:
		return nftnl_expr_cmp_export(buf, size, e, type);
	default:
		break;
	}
	return -1;
}

static bool nftnl_expr_cmp_cmp(const struct nftnl_expr *e1,
			       const struct nftnl_expr *e2)
{
	struct nftnl_expr_cmp *c1 = nftnl_expr_data(e1);
	struct nftnl_expr_cmp *c2 = nftnl_expr_data(e2);
	bool eq = true;

	if (e1->flags & (1 << NFTNL_EXPR_CMP_DATA))
		eq &= nftnl_data_reg_cmp(&c1->data, &c2->data, DATA_VALUE);
	if (e1->flags & (1 << NFTNL_EXPR_CMP_SREG))
		eq &= (c1->sreg == c2->sreg);
	if (e1->flags & (1 << NFTNL_EXPR_CMP_OP))
		eq &= (c1->op == c2->op);

	return eq;
}

struct expr_ops expr_ops_cmp = {
	.name		= "cmp",
	.alloc_len	= sizeof(struct nftnl_expr_cmp),
	.max_attr	= NFTA_CMP_MAX,
	.cmp		= nftnl_expr_cmp_cmp,
	.set		= nftnl_expr_cmp_set,
	.get		= nftnl_expr_cmp_get,
	.parse		= nftnl_expr_cmp_parse,
	.build		= nftnl_expr_cmp_build,
	.snprintf	= nftnl_expr_cmp_snprintf,
	.json_parse	= nftnl_expr_cmp_json_parse,
};
