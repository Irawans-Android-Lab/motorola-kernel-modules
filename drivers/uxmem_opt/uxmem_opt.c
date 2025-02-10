// SPDX-License-Identifier: GPL-2.0-only

#define pr_fmt(fmt) "uxmem_opt: " fmt

#include <linux/module.h>
#include <trace/hooks/vmscan.h>
#include <trace/hooks/mm.h>
#include <linux/swap.h>
#include <linux/proc_fs.h>
#include <linux/gfp.h>
#include <linux/types.h>
#include <linux/printk.h>
#include <linux/cgroup.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/vmstat.h>
#include <linux/oom.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <uapi/linux/sched/types.h>
#include <linux/cpufreq.h>
#include <linux/mm.h>
#include <linux/mm_inline.h>
#include <linux/pagemap.h>
#include <linux/page-flags.h>
#include <linux/pageblock-flags.h>
#include <linux/debugfs.h>
#include <linux/memcontrol.h>
#include <linux/mm_types.h>
#include <linux/kasan.h>
#include <linux/page_owner.h>
#include <linux/page_ref.h>
#include <linux/mmzone.h>
#include <linux/kprobes.h>
#include <linux/delay.h>
#include "../../../mm/internal.h"

#include "msched_common.h"

#include <linux/cred.h>
#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/version.h>

#define UX_TYPE_MASK ( UX_TYPE_INHERIT_BINDER | UX_TYPE_TOPAPP|UX_TYPE_LAUNCHER|UX_TYPE_TOPUI |UX_TYPE_INHERIT_LOCK | UX_TYPE_SYSTEM_LOCK \
	 | UX_TYPE_PERF_DAEMON | UX_TYPE_AUDIO |UX_TYPE_AUDIOSERVICE |UX_TYPE_LOW_LATENCY_BINDER | UX_TYPE_NATIVESERVICE | UX_TYPE_KSWAPD )

#include "uxmem_opt.h"
#define UX_PAGE_POOL_NAME "ux_page_pool_fillthread"
#define MAX_UXMEM_POOL_ALLOC_RETRIES (5)

static int ux_page_debug = 0;

//static const unsigned int orders[] = {0, 1};
static const unsigned int orders[] = {0};
/* 64M for order 0, 16M  for order 1 by default */
//static const unsigned int page_pool_nr_pages[] = {(SZ_64M >> PAGE_SHIFT), (SZ_16M >> PAGE_SHIFT)};
static const unsigned int page_pool_nr_pages[] = {((SZ_64M) >> PAGE_SHIFT), (SZ_1M >> PAGE_SHIFT)};
#define NUM_ORDERS ARRAY_SIZE(orders)
static struct page_pool *pools[NUM_ORDERS];
static struct task_struct *ux_page_pool_tsk = NULL;
static wait_queue_head_t kworkthread_waitq;
static unsigned int kworkthread_wait_flag;
static bool ux_page_pool_enabled = false;
static bool fillthread_enabled = false;

static struct per_cpu_pageset *dma32_pageset_base = NULL;

static unsigned long ux_pool_alloc_fail = 0;
/* true by default, false when uxmem.enable=N in cmdline */
bool enable = true;
module_param(enable, bool, S_IRUGO | S_IWUSR);

typedef void (*post_alloc_hook_t)(struct page *page, unsigned int order, gfp_t gfp_flags);
static post_alloc_hook_t post_alloc_hook_dup = NULL;

static atomic_long_t g_alloc_pages_fast[NUM_ORDERS][POOL_MIGRATETYPE_TYPES_SIZE] = {{ATOMIC_LONG_INIT(0)}};
static atomic_long_t g_alloc_pages_fast_retry[NUM_ORDERS][POOL_MIGRATETYPE_TYPES_SIZE][MAX_UXMEM_POOL_ALLOC_RETRIES] = {{{ATOMIC_LONG_INIT(0)}}};
static atomic_long_t g_alloc_pages_slow[NUM_ORDERS][POOL_MIGRATETYPE_TYPES_SIZE] = {{ATOMIC_LONG_INIT(0)}};
static atomic_long_t fillthread_runtime_times[NUM_ORDERS][POOL_MIGRATETYPE_TYPES_SIZE] = {{ATOMIC_LONG_INIT(0)}};

#define PARA_BUF_LEN 2560

static int page_pool_fill(struct page_pool *pool, int migratetype);

unsigned long uxmem_pool_inuse_pages(void)
{
	struct page_pool *pool;
	unsigned long inuse_pages = 0;
	unsigned long flags;
	int i,j;

	for (i = 0; i < NUM_ORDERS; i++) {
		pool = pools[i];
		spin_lock_irqsave(&pool->lock, flags);
		for (j = 0; j < POOL_MIGRATETYPE_TYPES_SIZE; j++) {
			inuse_pages += (pool->count[j] * (i + 1));
		}
		spin_unlock_irqrestore(&pool->lock, flags);
	}

        return inuse_pages;
}

unsigned long uxmem_pool_total_pages(void)
{
	struct page_pool *pool;
	unsigned long total_pages = 0;
	int i,j;

	for (i = 0; i < NUM_ORDERS; i++) {
		pool = pools[i];
		for (j = 0; j < POOL_MIGRATETYPE_TYPES_SIZE; j++)
			total_pages += (pool->high[j] * (i + 1));
	}

        return total_pages;
}

static int order_to_index(unsigned int order)
{
	int i;

	for (i = 0; i < NUM_ORDERS; i++) {
		if (order == orders[i])
			return i;
	}
	return -1;
}

static void page_pool_wakeup_process(struct page_pool *pool)
{
	if (unlikely(!ux_page_pool_enabled))
		return;

	if (pool == NULL) {
		pr_err("%s: boost_pool is NULL!\n", __func__);
		return;
	}

	if (fillthread_enabled) {
		kworkthread_wait_flag = 1;
		wake_up_interruptible(&kworkthread_waitq);
	}
}

void __maybe_unused set_ux_page_pool_fillthread_cpus(void)
{
	struct cpumask mask;
	struct cpumask *cpumask = &mask;
	pg_data_t *pgdat = NODE_DATA(0);
	unsigned int cpu = 0, cpufreq_max_tmp = 0;
	struct cpufreq_policy *policy_max = NULL;

	for_each_possible_cpu(cpu) {
		struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);

		if (policy == NULL)
			continue;

		if (policy->cpuinfo.max_freq >= cpufreq_max_tmp) {
			cpufreq_max_tmp = policy->cpuinfo.max_freq;
			policy_max = policy;
		}
	}

	cpumask_copy(cpumask, cpumask_of_node(pgdat->node_id));
	if (policy_max)
		cpumask_andnot(cpumask, cpumask, policy_max->related_cpus);

	if (!cpumask_empty(cpumask))
		set_cpus_allowed_ptr(current, cpumask);
}

static int ux_page_pool_fillthread(void *p)
{
	struct page_pool *pool;
	int i, j;
	int ret;
	unsigned long begin;
	int record_i, record_j;

	if (unlikely(!ux_page_pool_enabled))
		return -1;

	set_ux_page_pool_fillthread_cpus();

	while (!kthread_should_stop()) {
		ret = wait_event_interruptible(kworkthread_waitq,
						       (kworkthread_wait_flag == 1));
		if (ret < 0)
			continue;

		kworkthread_wait_flag = 0;

		for (i = 0; i < NUM_ORDERS; i++) {
			pool = pools[i];
			for (j = 0; j < POOL_MIGRATETYPE_TYPES_SIZE; j++) {
				if (ux_page_debug > 0) {
					if (pool->count[j] < pool->low[j]) {
						record_i = i;
						record_j = j;
					}

					begin = jiffies;
					pr_info("fill start >>>>>order:%d migratetype:%d low: %d high: %d count:%d gfp_mask %#x.\n",
						pool->order, j, pool->low[j], pool->high[j], pool->count[j],
						pool->gfp_mask);
				}

				while (pool->count[j] < pool->high[j])
					if (page_pool_fill(pool, j) < 0) {
						/* sleep for 20ms if alloc fail */
						msleep(20);
					}
				if (ux_page_debug > 0)
					pr_info("fill end   <<<<<order:%d migratetype:%d low: %d high: %d count:%d use %dms\n",
						pool->order, j, pool->low[j], pool->high[j], pool->count[j],
						jiffies_to_msecs(jiffies - begin));
			}
		}

		if (ux_page_debug > 0)
			atomic_long_inc(&fillthread_runtime_times[record_i][record_j]);

	}
	return 0;
}

struct page_pool *ux_page_pool_create(gfp_t gfp_mask, unsigned int order, unsigned int nr_pages)
{
	struct page_pool *pool;
	int i;

	pool = kmalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return ERR_PTR(-ENOMEM);

	pool->gfp_mask = gfp_mask;
	pool->order = order;
	for (i = 0; i < POOL_MIGRATETYPE_TYPES_SIZE; i++) {
		pool->count[i] = 0;
		/* MIGRATETYPE: UNMOVABLE & MOVABLE */
		/* wakeup kthread on count < low*/
		if (i == 0)
			pool->high[i]  = nr_pages / 8;
		else
			pool->high[i]  = nr_pages * 7 / 8;

		pool->low[i]  = pool->high[i] / 2;

		INIT_LIST_HEAD(&pool->items[i]);

		pr_info("%s order:%d migratetype:%d low: %d high: %d count:%d.\n",
			__func__, pool->order, i, pool->low[i], pool->high[i], pool->count[i]);
	}

	spin_lock_init(&pool->lock);
	return pool;
}

static void page_pool_add(struct page_pool *pool, struct page *page, int migratetype)
{
	unsigned long flags;

	spin_lock_irqsave(&pool->lock, flags);
	list_add_tail(&page->lru, &pool->items[migratetype]);
	pool->count[migratetype]++;
	spin_unlock_irqrestore(&pool->lock, flags);
}

static struct page *page_pool_remove(struct page_pool *pool, int migratetype)
{
	struct page *page;
	unsigned long flags;

	spin_lock_irqsave(&pool->lock, flags);
	page = list_first_entry_or_null(&pool->items[migratetype], struct page, lru);
	if (page) {
		pool->count[migratetype]--;
		list_del(&page->lru);
	}
	spin_unlock_irqrestore(&pool->lock, flags);

	/* wakeup kthread on count < low*/
	if (pool->count[migratetype] < pool->low[migratetype])
		page_pool_wakeup_process(pool);

	return page;
}

static int page_pool_fill(struct page_pool *pool, int migratetype)
{
	struct page *page;
	gfp_t gfp_refill = pool->gfp_mask;
	/* unsigned long pfn; */

	if (pool == NULL) {
		pr_err("%s: pool is NULL!\n", __func__);
		return -1;
	}

	if (migratetype == 0) {
		gfp_refill &= ~(__GFP_CMA | __GFP_MOVABLE);
	}

	page = alloc_pages(gfp_refill, pool->order);
	if (page == NULL)
		return -1;

	/*
	 * if (put_page_testzero(page)) {
	 *	pfn = page_to_pfn(page);
	 *	if (!free_unref_page_prepare_temp(page, pool->order, pfn)) {
	 *		pr_info("KEN_pages free_unref_page_prepare fail\n");
	 *		return -1;
	 *	}
	 * }
	 */

	page_pool_add(pool, page, migratetype);
	return 1;
}

/* fast path */
struct page *ux_page_pool_alloc_pages(unsigned int order, int migratetype)
{
	struct page *page = NULL;
	int retries = 0;
	struct page_pool *pool = NULL;
	int order_ind = order_to_index(order);

	if (unlikely(!ux_page_pool_enabled) || (order_ind == -1))
		return NULL;

	if (migratetype > MIGRATE_MOVABLE)
		return NULL;

	pool = pools[order_ind];
	if (pool == NULL)
		return NULL;

retry:
	/* Fast-path: Get a page from cache */
	page = page_pool_remove(pool, migratetype);
	if (!page && retries < MAX_UXMEM_POOL_ALLOC_RETRIES) {
		retries++;
		goto retry;
	}

	if (ux_page_debug > 0) {
		if (page)
			atomic_long_inc(&g_alloc_pages_fast[order][migratetype]);
		else
			atomic_long_inc(&g_alloc_pages_slow[order][migratetype]);

		if (retries != 0)
			atomic_long_inc(&g_alloc_pages_fast_retry[order][migratetype][retries - 1]);
	}

	if (!page)
		ux_pool_alloc_fail += 1;

	return page;
}

int ux_page_pool_refill(struct page *page, unsigned int order, int migratetype)
{
	struct page_pool *pool;
	int order_ind = order_to_index(order);

	if (unlikely(!ux_page_pool_enabled) || (order_ind == -1))
		return false;

	pool = pools[order_ind];
	if (pool == NULL)
		return false;

	if (pool->count[migratetype] >= pool->high[migratetype])
		return false;

	/* set_page_count(page, 1); */
	page_pool_add(pool, page, migratetype);
	return true;
}

static ssize_t ux_page_pool_write(struct file *file,
		const char __user *buff, size_t len, loff_t *ppos)
{
	char kbuf[PARA_BUF_LEN] = {'\0'};
	char *str;
	int high_0, high_1;
	int i, ret;
	struct page_pool *pool;
	unsigned long flags;

	if (len > PARA_BUF_LEN - 1) {
		pr_err("len %ld is too long\n", len);
		return -EINVAL;
	}

	if (copy_from_user(&kbuf, buff, len))
		return -EFAULT;
	kbuf[len] = '\0';

	str = strstrip(kbuf);
	if (!str) {
		pr_err("buff %s is invalid\n", kbuf);
		return -EINVAL;
	}

	ret = sscanf(str, "%d %d", &high_0, &high_1);

	if (ret == 2) {
		for (i = 0; i < POOL_MIGRATETYPE_TYPES_SIZE; i++) {
			pool = pools[0];
			spin_lock_irqsave(&pool->lock, flags);
			/* MIGRATETYPE: UNMOVABLE & MOVABLE */
			if (i == 0)
				pool->high[i]  = high_0 / 8;
			else
				pool->high[i] = high_0 * 7 / 8;

			pool->low[i]  = pool->high[i] / 2;

			spin_unlock_irqrestore(&pool->lock, flags);
			pr_info("%s order:%d migratetype:%d low: %d high: %d count:%d.\n",
				__func__, pool->order, i,
				pool->low[i], pool->high[i], pool->count[i]);

			pool = pools[1];
			spin_lock_irqsave(&pool->lock, flags);
			/* MIGRATETYPE: UNMOVABLE & MOVABLE */
			pool->high[i] = high_1/POOL_MIGRATETYPE_TYPES_SIZE;
			pool->low[i]  = pool->high[i]/2;
			spin_unlock_irqrestore(&pool->lock, flags);
			pr_info("%s order:%d migratetype:%d low: %d high: %d count:%d.\n",
				__func__, pool->order, i,
				pool->low[i], pool->high[i], pool->count[i]);
		}
		return len;
	}

	if (strstr(str, "fillthread_pause")) {
		fillthread_enabled = false;
		ux_page_pool_enabled = false;
		return len;
	}

	if (strstr(str, "fillthread_resume")) {
		fillthread_enabled = true;
		ux_page_pool_enabled = true;
		return len;
	}

	return -EINVAL;
}

static ssize_t ux_page_pool_read(struct file *file,
		char __user *buffer, size_t count, loff_t *off)
{
	char kbuf[PARA_BUF_LEN] = {'\0'};
	int len = 0;
	int i, j, k;
	struct page_pool *pool;

	for (i = 0; i < NUM_ORDERS; i++) {
		pool = pools[i];
		for (j = 0; j < POOL_MIGRATETYPE_TYPES_SIZE; j++) {
			len += snprintf(kbuf + len, PARA_BUF_LEN - len,
					"order:%d migratetype:%d low: %d high: %d count:%d.\n",
					pool->order, j,
					pool->low[j], pool->high[j], pool->count[j]);
		}
	}

	//pool
	if (ux_page_debug > 0) {
		for (i = 0; i < NUM_ORDERS; i++) {
			for (j = 0; j < POOL_MIGRATETYPE_TYPES_SIZE; j++) {
				len += snprintf(kbuf + len, PARA_BUF_LEN - len,
					"g_alloc_pages_fast[%d][%d]:%ld g_alloc_pages_slow[%d][%d]:\
					%ld fillthread_runtime_times:%ld\n",
					i, j, atomic_long_read(&g_alloc_pages_fast[i][j]), i, j, \
					atomic_long_read(&g_alloc_pages_slow[i][j]), \
					atomic_long_read(&fillthread_runtime_times[i][j]));

				for (k = 0; k < MAX_UXMEM_POOL_ALLOC_RETRIES; k++)
					len += snprintf(kbuf + len, PARA_BUF_LEN - len,
							"g_alloc_pages_fast_retry[%d][%d][%d]:%ld\n",
							i, j, k, atomic_long_read(&g_alloc_pages_fast_retry[i][j][k]));
			}
		}
	}

	len += snprintf(kbuf + len, PARA_BUF_LEN - len,
			"page_pool alloc fail count:%ld\n", ux_pool_alloc_fail);
	len += snprintf(kbuf + len, PARA_BUF_LEN - len,
			"page_pool fillthread status:%s\n",
			fillthread_enabled ? "running" : "not running");

	if (len == PARA_BUF_LEN)
		kbuf[len - 1] = '\0';

	if (len > *off)
		len -= *off;
	else
		len = 0;

	if (copy_to_user(buffer, kbuf + *off, (len < count ? len : count)))
		return -EFAULT;

	*off += (len < count ? len : count);
	return (len < count ? len : count);
}

static const struct proc_ops ux_page_pool_fops = {
	.proc_write = ux_page_pool_write,
	.proc_read = ux_page_pool_read,
};

static ssize_t ux_page_debug_enabled_write(struct file *file, const char __user *buf,
                size_t count, loff_t *ppos)
{
        char buffer[13];
        int err, val;

        memset(buffer, 0, sizeof(buffer));

        if (count > sizeof(buffer) - 1)
                count = sizeof(buffer) - 1;

        if (copy_from_user(buffer, buf, count))
                return -EFAULT;

        buffer[count] = '\0';
        err = kstrtoint(strstrip(buffer), 16, &val);
        if (err)
                return err;

        ux_page_debug = val;

        return count;
}

static ssize_t ux_page_debug_enabled_read(struct file *file, char __user *buf,
                size_t count, loff_t *ppos)
{
        char buffer[13];
        size_t len = 0;

        len = snprintf(buffer, sizeof(buffer), "%d\n", ux_page_debug);

        return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static const struct proc_ops ux_page_debug_enabled_fops = {
	.proc_write = ux_page_debug_enabled_write,
	.proc_read  = ux_page_debug_enabled_read,
};

struct pglist_data *first_online_pgdat(void)
{
	return NODE_DATA(first_online_node);
}

struct pglist_data *next_online_pgdat(struct pglist_data *pgdat)
{
	int nid = next_online_node(pgdat->node_id);

	if (nid == MAX_NUMNODES)
		return NULL;
	return NODE_DATA(nid);
}

struct zone *next_zone(struct zone *zone)
{
	pg_data_t *pgdat = zone->zone_pgdat;

	if (zone < pgdat->node_zones + MAX_NR_ZONES - 1)
		zone++;
	else {
		pgdat = next_online_pgdat(pgdat);
		if (pgdat)
			zone = pgdat->node_zones;
		else
			zone = NULL;
	}
	return zone;
}

static struct proc_dir_entry *ux_page_pool_entry;
static struct proc_dir_entry *ux_page_debug_entry;
static int ux_page_pool_init(void)
{
	int ret = 0;
	int i;
	struct proc_dir_entry *root_dir_entry = proc_mkdir("ux_mem", NULL);
	struct zone *zone;
	struct kprobe post_alloc_hook_kp = {
		.symbol_name = "post_alloc_hook"
	};

	for (i = 0; i < NUM_ORDERS; i++) {
		pools[i] = ux_page_pool_create((GFP_HIGHUSER_MOVABLE | __GFP_CMA | __GFP_ZERO | __GFP_NOWARN |
			   __GFP_NORETRY) & ~__GFP_RECLAIM, orders[i], page_pool_nr_pages[i]);
	}
	ux_page_pool_enabled = true;

	init_waitqueue_head(&kworkthread_waitq);
	ux_page_pool_tsk = kthread_run(ux_page_pool_fillthread, NULL, UX_PAGE_POOL_NAME);
	if (IS_ERR_OR_NULL(ux_page_pool_tsk))
		pr_err("%s:run ux_page_pool_fillthread failed!\n", __func__);
	fillthread_enabled = true;
	page_pool_wakeup_process(pools[0]);


	ux_page_pool_entry = proc_create((root_dir_entry ?
				"ux_page_pool" : "ux_mem/ux_page_pool"),
				0666, root_dir_entry, &ux_page_pool_fops);
	if (!ux_page_pool_entry) {
		pr_err("failed to create proc node ux_page_pool\n");
		goto err_creat_ux_page_pool;
	}

	ux_page_debug_entry = proc_create((root_dir_entry ?
				"ux_page_debug_enabled" : "ux_mem/ux_page_debug_enabled"),
				0666, root_dir_entry, &ux_page_debug_enabled_fops);
	if (!ux_page_debug_entry) {
		pr_err("failed to create proc node ux_page_debug_enabled\n");
		goto err_creat_ux_page_debug_enabled;
	}

	/* record dma32_zone pageset if exist */
	for_each_zone(zone) {
		if (!strcmp(zone->name, "DMA32") && populated_zone(zone)) {
			pr_info("zone DMA32 detect\n");
			dma32_pageset_base = zone->pageset;
		}
	}

	/* get post_alloc_hook addr */
	ret = register_kprobe(&post_alloc_hook_kp);
	if (ret < 0) {
		pr_err("get post_alloc_hook addr from kprobe failed!\n");
		return ret;
	}

	post_alloc_hook_dup = (post_alloc_hook_t)post_alloc_hook_kp.addr;
	pr_info("suceesfully get post_alloc_hook addr:0x%px\n", post_alloc_hook_dup);

	unregister_kprobe(&post_alloc_hook_kp);

	return 0;

err_creat_ux_page_debug_enabled:
err_creat_ux_page_pool:
	proc_remove(ux_page_pool_entry);

	return -ENOENT;
}

inline bool task_in_top_app_group(struct task_struct *p)
{
	int cgrp_id = get_task_cgroup_id(p);
	return (cgrp_id == CGROUP_TOP_APP) || (cgrp_id == CGROUP_FOREGROUND);
}

inline bool test_task_is_fair(struct task_struct *task)
{
        BUG_ON(!task);

        /* valid CFS priority is MAX_RT_PRIO..MAX_PRIO-1 */
        if ((task->prio >= MAX_RT_PRIO) && (task->prio <= MAX_PRIO-1))
                return true;
        return false;
}

inline int task_is_fg(struct task_struct *tsk)
{
	return task_in_top_app_group(tsk);
}

inline bool test_task_ux(struct task_struct *tsk)
{
	struct moto_task_struct *oem_data;

	oem_data  = get_moto_task_struct(tsk);

	return (oem_data && ((oem_data->ux_type & UX_TYPE_MASK) || (oem_data->ux_type & UX_TYPE_TOPAPP)));
}

inline bool task_is_systemserver_or_surfaceflinger(struct task_struct *tsk)
{
	if ((global_systemserver_tgid == tsk->tgid) || (global_sf_tgid == tsk->tgid))
		return true;
	return false;
}

static inline bool current_is_key_task(void)
{
	return test_task_ux(current) ||
			rt_task(current) ||
			task_is_fg(current) ||
			task_is_systemserver_or_surfaceflinger(current);
}

static void __nocfi get_page_from_uxmempool(void *data, gfp_t gfp_mask, int order, int alloc_flags,
		int migratetype, struct page **p_page)
{
	struct page *page = NULL;
	int i;

	if (current_is_key_task() && !(gfp_mask & __GFP_DMA32)) {
		page = ux_page_pool_alloc_pages(order, migratetype);
		/* a refilled from __free_pages */
		if (page && !page_count(page)) {
			/* prep_new_page(page, order, gfp_mask, alloc_flags);
			 * post_alloc_hook(page, order,  GFP_KERNEL);
			 */
			if (post_alloc_hook_dup)
				post_alloc_hook_dup(page, order, gfp_mask);
			else {
				set_page_private(page, 0);
				set_page_refcounted(page);

				for (i = 0; i < (1 << order); i++)
					clear_highpage(page + i);
			}
		}
	}
	*p_page = page;
}

static void uxmempool_refill(void *data, struct page *page, int order,
		int migratetype, bool *bypass)
{
	struct zone *zone = page_zone(page);
	unsigned long mark = zone->_watermark[WMARK_LOW];
	long free_pages = zone_page_state(zone, NR_FREE_PAGES);

	free_pages -= zone->nr_reserved_highatomic;

	//if ((migratetype <= MIGRATE_MOVABLE) && (zone_idx(zone) == ZONE_NORMAL)
	if ((migratetype <= MIGRATE_MOVABLE) && free_pages > mark) {
		if (ux_page_pool_refill(page, order, migratetype))
			*bypass = true;
	}
}

#define KMALLOC_MAX_PAGES 8
#define UXMEM_POOL_MAX_PAGES 1

static void kvmalloc_check_use_vmalloc(void *data, size_t size,
		gfp_t *kmalloc_flags, bool *use_vmalloc)
{
	if (test_task_ux(current) && (size > UXMEM_POOL_MAX_PAGES * PAGE_SIZE)) {
		*kmalloc_flags &= ~__GFP_DIRECT_RECLAIM;
		*kmalloc_flags |= __GFP_KSWAPD_RECLAIM;
		*use_vmalloc = false;
	} else if (!test_task_ux(current) && (size >= KMALLOC_MAX_PAGES * PAGE_SIZE)) {
		*use_vmalloc = true;
	}
}

static void should_alloc_pages_retry(void *data, gfp_t gfp_mask,
		int order, int *alloc_flags, int migratetype,
		struct zone *preferred_zone, struct page **p_page, bool *should_alloc_retry)
{
	if (unlikely(test_task_ux(current)) && !in_interrupt() &&
		(preferred_zone->nr_reserved_highatomic >= (SZ_8M >> PAGE_SHIFT)) &&
		!(*alloc_flags & (ALLOC_HARDER|ALLOC_OOM)) && !(gfp_mask & __GFP_NORETRY)) {
		*alloc_flags |= ALLOC_HARDER;
		*should_alloc_retry = true;
	} else {
		*should_alloc_retry = false;
	}
}

static void unreserve_highatomic_bypass(void *data, bool force,
		struct zone *zone, bool *skip_unreserve_highatomic)
{
	if (!force && (zone->nr_reserved_highatomic <= (SZ_16M >> PAGE_SHIFT)))
		*skip_unreserve_highatomic = true;
}

static bool in_dma32_zone(struct per_cpu_pages *pcp)
{
	if (dma32_pageset_base)
		return pcp == &(this_cpu_ptr(dma32_pageset_base)->pcp);
	return false;
}

static void fill_pcplist_from_uxmempool(void *data, unsigned int order,
		struct per_cpu_pages *pcp, int migratetype, struct list_head *list)
{
	struct page *page = NULL;

	if (current_is_key_task() && !in_dma32_zone(pcp)) {
		page = ux_page_pool_alloc_pages(order,
				migratetype == get_cma_migrate_type() ?
				MIGRATE_MOVABLE : migratetype);
	}

	if (page) {
		list_add_tail(&page->lru, list);
		pcp->count += 1 << order;
	}
}

void vh_meminfo_proc_show(void *data, struct seq_file *m)
{
	seq_printf(m, "UXmemUsed:       %lu kB\n",
		   uxmem_pool_inuse_pages() * PAGE_SIZE / 1024);
	seq_printf(m, "UXmemTotal:      %lu kB\n",
		   uxmem_pool_total_pages() * PAGE_SIZE / 1024);

}

static int register_uxmem_opt_vendor_hooks(void)
{
	int ret = 0;

	ret = register_trace_android_vh_alloc_pages_reclaim_bypass(get_page_from_uxmempool, NULL);
	if (ret != 0) {
		pr_err("register_trace_android_vh_alloc_pages_reclaim_bypass failed! ret=%d\n",
				ret);
		goto out;
	}

	ret = register_trace_android_vh_free_unref_page_bypass(uxmempool_refill, NULL);
	if (ret != 0) {
		pr_err("register_trace_android_vh_free_page failed! ret=%d\n", ret);
		goto out;
	}

	ret = register_trace_android_vh_kvmalloc_node_use_vmalloc(kvmalloc_check_use_vmalloc, NULL);
	if (ret != 0) {
		pr_err("register_trace_android_vh_kvmalloc_node_use_vmalloc failed! ret=%d\n",
				ret);
		goto out;
	}

	ret = register_trace_android_vh_should_alloc_pages_retry(should_alloc_pages_retry, NULL);
	if (ret != 0) {
		pr_err("register_trace_android_vh_should_alloc_pages_retry failed! ret=%d\n", ret);
		goto out;
	}

	ret = register_trace_android_vh_unreserve_highatomic_bypass(unreserve_highatomic_bypass,
			NULL);
	if (ret != 0) {
		pr_err("register_trace_android_vh_unreserve_highatomic_bypass failed! ret=%d\n",
				ret);
		goto out;
	}


	ret = register_trace_android_vh_rmqueue_bulk_bypass(fill_pcplist_from_uxmempool, NULL);
	if (ret != 0) {
		pr_err("register_trace_android_vh_rmqueue_bulk_bypass failed! ret=%d\n", ret);
		goto out;
	}

	ret = register_trace_android_vh_meminfo_proc_show(vh_meminfo_proc_show, NULL);
	if (ret != 0) {
		pr_err("register_trace_android_vh_meminfo_proc_show failed! ret = %d\n", ret);
		goto out;
	}
out:
	return ret;
}

static void unregister_uxmem_opt_vendor_hooks(void)
{
	unregister_trace_android_vh_rmqueue_bulk_bypass(fill_pcplist_from_uxmempool, NULL);

	unregister_trace_android_vh_unreserve_highatomic_bypass(unreserve_highatomic_bypass, NULL);

	unregister_trace_android_vh_should_alloc_pages_retry(should_alloc_pages_retry, NULL);

	unregister_trace_android_vh_kvmalloc_node_use_vmalloc(kvmalloc_check_use_vmalloc, NULL);

	unregister_trace_android_vh_free_unref_page_bypass(uxmempool_refill, NULL);

	unregister_trace_android_vh_alloc_pages_reclaim_bypass(get_page_from_uxmempool, NULL);

	unregister_trace_android_vh_meminfo_proc_show(vh_meminfo_proc_show, NULL);
}

static int __init uxmem_opt_init(void)
{
	int ret = 0;

	if (!enable) {
		pr_err("uxmem_opt is disabled in cmdline\n");
		return 0;
	}

	if (IS_ENABLED(CONFIG_PAGE_POISONING)) {
		pr_err("uxmem_opt conflict with CONFIG_PAGE_POISONING\n");
		return 0;
	}

	ret = ux_page_pool_init();
	if (ret != 0) {
		pr_err("uxmem_opt init failed!\n");
		return ret;
	}

	ret = register_uxmem_opt_vendor_hooks();
	if (ret != 0)
		return ret;

	pr_info("uxmem_opt_init succeed!\n");
	return 0;
}

static void __exit uxmem_opt_exit(void)
{
	unregister_uxmem_opt_vendor_hooks();
	proc_remove(ux_page_pool_entry);
	pr_info("uxmem_opt_exit succeed!\n");
}

module_init(uxmem_opt_init);
module_exit(uxmem_opt_exit);
MODULE_SOFTDEP("pre: moto_sched.ko");
MODULE_LICENSE("GPL v2");
