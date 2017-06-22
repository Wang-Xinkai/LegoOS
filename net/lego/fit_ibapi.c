/*
 * Copyright (c) 2016-2017 Wuklab, Purdue University. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <lego/sched.h>
#include <lego/net.h>
#include <rdma/ib_verbs.h>
#include <lego/fit_ibapi.h>
#include <lego/slab.h>
#include "fit.h"
#include "fit_internal.h"

#define HANDLER_LENGTH 0
#define HANDLER_INTERARRIVAL 0

//#define DEBUG_IBV
#ifdef TEST_PRINTK
#define test_printk(x...)	pr_crit(x)
#else
#define test_printk(x...)	do {} while (0)
#endif

#define SRCADDR INADDR_ANY
#define DSTADDR ((unsigned long int)0xc0a87b01) /* 192.168.123.1 */

int num_parallel_connection = NUM_PARALLEL_CONNECTION;
atomic_t global_reqid;
ppc *FIT_ctx;
int curr_node;
struct ib_device *ibapi_dev;
struct ib_pd *ctx_pd;

static void ibv_add_one(struct ib_device *device)
{
	FIT_ctx = (struct pingpong_context *)kmalloc(sizeof(struct pingpong_context), GFP_KERNEL);
	ibapi_dev = device;
	
	ctx_pd = ib_alloc_pd(device);
	if (!ctx_pd) {
		printk(KERN_ALERT "Couldn't allocate PD\n");
	}

	return;
}

static void ibv_remove_one(struct ib_device *device)
{
	return;
}

inline int ibapi_send_reply_imm(int target_node, void *addr, int size, void *ret_addr, int max_ret_size)
{
	ppc *ctx = FIT_ctx;
	int ret;
	ret = client_send_reply_with_rdma_write_with_imm(ctx, target_node, addr, size, ret_addr, max_ret_size, 0);
	return ret;
}

inline int ibapi_reply_message(void *addr, int size, uintptr_t descriptor)
{
	ppc *ctx = FIT_ctx;
	return client_reply_message(ctx, addr, size, descriptor, 0);
}
#if 0
uint64_t ibapi_dist_barrier(unsigned int check_num)
{
	int i;
	ppc *ctx = FIT_ctx;
	int source = ctx->node_id;
	int num_alive_nodes = atomic_read(&ctx->num_alive_nodes);
	uintptr_t tempaddr;
	int priority = LOW_PRIORITY;
	//int connection_id;
	atomic_inc(&ctx->dist_barrier_counter);
	for(i=1;i<=num_alive_nodes;i++)//skip CD
	{
		if(i==ctx->node_id)
			continue;
		tempaddr = client_ib_reg_mr_addr(ctx, &source, sizeof(int));
		client_send_message_sge_UD(ctx, i, MSG_DIST_BARRIER, (void *)tempaddr, sizeof(int), 0, 0, priority);
	}
	//while(atomic_read(&ctx->dist_barrier_counter)<num_alive_nodes)
	while(atomic_read(&ctx->dist_barrier_counter)<check_num)
	{
		schedule();
	}
	atomic_sub(check_num, &ctx->dist_barrier_counter);
	return 0;
}
#endif

void ibapi_free_recv_buf(void *input_buf)
{
	//printk(KERN_CRIT "IB freeing post_receive_cache vaddr %p\n", input_buf);
	//kmem_cache_free(post_receive_cache, input_buf);
	//client_free_recv_buf(input_buf);
	//kmem_cache_free(post_receive_cache, input_buf);
}

int ibapi_reg_send_handler(int (*input_funptr)(char *addr, uint32_t size, int sender_id))
{
	ppc *ctx = FIT_ctx;
	ctx->send_handler = input_funptr;
	return 0;
}

int ibapi_reg_send_reply_handler(int (*input_funptr)(char *input_addr, uint32_t input_size, char *output_addr, uint32_t *output_size, int sender_id))
{
	ppc *ctx = FIT_ctx;
	ctx->send_reply_handler = input_funptr;
	return 0;
}

int ibapi_reg_send_reply_opt_handler(int (*input_funptr)(char *input_addr, uint32_t input_size, void **output_addr, uint32_t *output_size, int sender_id))
{
	ppc *ctx = FIT_ctx;
	ctx->send_reply_opt_handler = input_funptr;
	return 0;
}

int ibapi_reg_send_reply_rdma_imm_handler(int (*input_funptr)(int sender_id, void *msg, uint32_t size, uint32_t inbox_addr, uint32_t inbox_rkey, uint32_t inbox_semaphore))
{
	ppc *ctx = FIT_ctx;
	ctx->send_reply_rdma_imm_handler = input_funptr;
	return 0;
}

int ibapi_num_connected_nodes(void)
{
	if(!FIT_ctx)
	{	
		printk(KERN_CRIT "%s: using FIT ctx directly since ctx is NULL\n", __func__);
		return atomic_read(&FIT_ctx->num_alive_nodes);
	}
	return atomic_read(&FIT_ctx->num_alive_nodes);
}

int ibapi_get_node_id(void)
{
	ppc *ctx;
	if(FIT_ctx)
	{
		ctx = FIT_ctx;
		return ctx->node_id;
	}
	return 0;
}

int ibapi_establish_conn(int ib_port, int mynodeid)
{
	ppc *ctx;
	
	printk(KERN_CRIT "Start calling rc_internal to create FIT based on %p\n", ibapi_dev);
	
	ctx = client_establish_conn(ibapi_dev, ib_port, mynodeid);
	
	if(!ctx)
	{
		printk(KERN_ALERT "%s: ctx %p fail to init_interface \n", __func__, (void *)ctx);
		return 0;	
	}

	FIT_ctx = ctx;

	printk(KERN_ALERT "%s: return before establish connection with NODE_ID: %d\n", __func__, ctx->node_id);
	printk(KERN_CRIT "Pass all possible test and return\n");

	return ctx->node_id;
}

static struct ib_client ibv_client = {
	.name   = "ibv_server",
	.add    = ibv_add_one,
	.remove = ibv_remove_one
};

int lego_ib_init(void)
{
	int ret;

	fit_internal_init();

	printk(KERN_CRIT "%s\n", __func__);
	while (mad_got_one < 10) {
		schedule();
	}

	ret = ib_register_client(&ibv_client);
	if (ret) {
		pr_err("couldn't register IB client\n");
		return ret;
	}

	atomic_set(&global_reqid, 0);
	
	ibapi_establish_conn(1, MY_NODE_ID);
	return 0;
}

int lego_ib_cleanup(void)
{
	printk(KERN_INFO "Ready to remove module\n");
	client_cleanup_module();
	ib_unregister_client(&ibv_client);
	fit_internal_cleanup();
}
