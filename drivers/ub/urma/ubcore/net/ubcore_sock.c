// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * Description: ubcore sock implementation
 * Author: Wang Hang
 * Create: 2025-02-18
 * Note:
 * History: 2025-02-18: create file
 */

#include <net/sock.h>
#include <linux/processor.h>
#include <linux/uaccess.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/tcp.h>
#include <linux/version.h>

#include "ubcore_log.h"
#include "ubcore_priv.h"
#include "ubcore_sock.h"

#define IPV4_MAP_IPV6_PREFIX 0x0000ffff
#define SOCK_PORT 1226
#define SOCK_MSG_MAX_LEN 4096
#define SK_EVENT_TIME_LIMIT 300

enum sk_type {
	SOCK_LISTEN,
	SOCK_CONNECT,
	SOCK_ACCEPT,
};

struct sk_entry {
	struct list_head all_list_entry;
	struct list_head ready_list_entry;
	struct kref ref;
	spinlock_t sk_lock;
	struct socket *sock;
	enum sk_type sk_type;
	union ubcore_net_addr_union ub_addr;
	atomic_t inactive;
	// for epoll
	wait_queue_head_t *whead;
	wait_queue_entry_t wait;
	poll_table pt;
};

struct sk_msg_descriptor {
	ubcore_net_msg_handler handler;
	uint16_t msg_len;
};

struct sk_service {
	struct task_struct *daemon;
	struct list_head all_list;
	struct list_head ready_list;
	uint16_t port;
	struct sk_msg_descriptor descriptor[UBCORE_NET_MSG_MAX];
	spinlock_t lock;
	wait_queue_head_t wq;
};

static void k_parse_addr(union ubcore_net_addr_union *ub_addr, uint16_t port,
			 struct sockaddr *sk_addr, int *sk_addr_size)
{
	if (ub_addr->in4.reserved1 == 0 &&
	    ub_addr->in4.reserved2 == htonl(IPV4_MAP_IPV6_PREFIX)) {
		struct sockaddr_in *sk_addr_in = (struct sockaddr_in *)sk_addr;

		sk_addr_in->sin_family = AF_INET;
		sk_addr_in->sin_port = htons(port);
		sk_addr_in->sin_addr.s_addr = ub_addr->in4.addr;

		*sk_addr_size = (int)sizeof(*sk_addr_in);
	} else {
		struct sockaddr_in6 *sk_addr_in6 =
			(struct sockaddr_in6 *)sk_addr;

		sk_addr_in6->sin6_family = AF_INET6;
		sk_addr_in6->sin6_port = htons(port);
		memcpy(&sk_addr_in6->sin6_addr, &ub_addr->in6,
		       sizeof(ub_addr->in6));

		*sk_addr_size = (int)sizeof(*sk_addr_in6);
	}
}

static int k_setsockopt_keepalive(struct socket *sock)
{
	int keepidle = 5;
	int keepcnt = 3;

	sock_set_keepalive(sock->sk);
	tcp_sock_set_keepcnt(sock->sk, keepcnt);
	tcp_sock_set_keepidle(sock->sk, keepidle);
	tcp_sock_set_keepintvl(sock->sk, keepidle);

	return 0;
}

static inline void k_setsockopt_reuse(struct socket *sock)
{
	sock->sk->sk_reuse = true;
}

static inline void k_close(struct socket *sock)
{
	sock_release(sock);
}

static struct socket *k_listen(union ubcore_net_addr_union *ub_addr,
			       uint16_t port)
{
	struct socket *sock = NULL;
	struct sockaddr_in6 sk_addr_inner = { 0 };
	struct sockaddr *sk_addr = (struct sockaddr *)&sk_addr_inner;
	int sk_addr_size, backlog = 128;
	int ret;

	k_parse_addr(ub_addr, port, sk_addr, &sk_addr_size);

	ret = sock_create(sk_addr->sa_family, SOCK_STREAM, 0, &sock);
	if (ret < 0) {
		ubcore_log_err("Failed to create socket");
		return NULL;
	}

	k_setsockopt_reuse(sock);

	ret = kernel_bind(sock, sk_addr, sk_addr_size);
	if (ret < 0) {
		ubcore_log_err("Failed to call kernel_bind, ret: %d.\n", ret);
		goto destroy_sock;
	}

	ret = kernel_listen(sock, backlog);
	if (ret < 0) {
		ubcore_log_err("Failed to call kernel_listen");
		goto destroy_sock;
	}

	return sock;

destroy_sock:
	k_close(sock);
	return NULL;
}

static struct socket *k_accept(struct socket *sock,
			       union ubcore_net_addr_union *ub_addr)
{
	struct socket *newsock = NULL;
	struct sockaddr_in6 sk_addr_inner = { 0 };
	struct sockaddr *sk_addr = (struct sockaddr *)&sk_addr_inner;
	int ret;

	ret = kernel_accept(sock, &newsock, 0);
	if (ret < 0) {
		ubcore_log_err("Failed to call kernel_accept");
		return NULL;
	}

	ret = kernel_getpeername(newsock, sk_addr);
	if (ret == sizeof(struct sockaddr_in6) &&
	    sk_addr->sa_family == AF_INET6) {
		struct sockaddr_in6 *sk_addr_in6 =
			(struct sockaddr_in6 *)sk_addr;

		memcpy(&ub_addr->in6, &sk_addr_in6->sin6_addr,
		       sizeof(sk_addr_in6->sin6_addr));
	} else if (ret == sizeof(struct sockaddr_in) &&
		   sk_addr->sa_family == AF_INET) {
		struct sockaddr_in *sk_addr_in = (struct sockaddr_in *)sk_addr;

		ub_addr->in4.addr = sk_addr_in->sin_addr.s_addr;
		ub_addr->in4.reserved2 = IPV4_MAP_IPV6_PREFIX;
	} else {
		ubcore_log_err("Failed to call kernel_getpeername");
		goto destroy_sock;
	}

	ret = k_setsockopt_keepalive(newsock);
	if (ret < 0) {
		ubcore_log_err("Failed to set socket keepalive");
		goto destroy_sock;
	}

	return newsock;

destroy_sock:
	k_close(newsock);
	return NULL;
}

static struct socket *k_connect(union ubcore_net_addr_union *ub_addr,
				uint16_t port)
{
	struct socket *sock = NULL;
	struct sockaddr_in6 sk_addr_inner = { 0 };
	struct sockaddr *sk_addr = (struct sockaddr *)&sk_addr_inner;
	int sk_addr_size;
	int ret;

	k_parse_addr(ub_addr, port, sk_addr, &sk_addr_size);

	ret = sock_create(sk_addr->sa_family, SOCK_STREAM, 0, &sock);
	if (ret < 0) {
		ubcore_log_err("Failed to create socket");
		return NULL;
	}

	ret = kernel_connect(sock, sk_addr, sk_addr_size, 0);
	if (ret < 0) {
		ubcore_log_err("Failed to connect socket");
		goto destroy_sock;
	}

	ret = k_setsockopt_keepalive(sock);
	if (ret != 0) {
		ubcore_log_err("Failed to set socket keepalive");
		goto destroy_sock;
	}

	return sock;

destroy_sock:
	k_close(sock);
	return NULL;
}

static int k_recvmsg(struct socket *sock, void *buf, size_t buf_size, int flags)
{
	struct msghdr msg = { 0 };
	struct kvec vec;

	vec.iov_base = buf;
	vec.iov_len = buf_size;

	return kernel_recvmsg(sock, &msg, &vec, 1, buf_size, flags);
}

static struct sk_service ss = { 0 };

static void sk_entry_release(struct kref *ref)
{
	struct sk_entry *entry = container_of(ref, struct sk_entry, ref);

	k_close(entry->sock);
	kfree(entry);
}

static inline void sk_entry_ref_acquire(struct sk_entry *entry)
{
	kref_get(&entry->ref);
}

static inline void sk_entry_ref_release(struct sk_entry *entry)
{
	kref_put(&entry->ref, sk_entry_release);
}

static inline void sk_entry_add_to_all_list(struct sk_entry *entry)
{
	list_add(&entry->all_list_entry, &ss.all_list);
}

static inline void sk_entry_remove_from_all_list(struct sk_entry *entry)
{
	list_del(&entry->all_list_entry);
}

static inline void sk_entry_add_to_ready_list(struct sk_entry *entry)
{
	// Check if the current entry is in the list
	if (list_empty(&entry->ready_list_entry))
		list_add(&entry->ready_list_entry, &ss.ready_list);
}

static inline void sk_entry_remove_from_ready_list(struct sk_entry *entry)
{
	// Check if the current entry is in the list
	// The entry is Initialized after removing from the ready list,
	// prepared for future insertion.
	if (!list_empty(&entry->ready_list_entry))
		list_del_init(&entry->ready_list_entry);
}

static int sk_wait_callback(wait_queue_entry_t *wait, unsigned int mode,
			    int sync, void *key)
{
	struct sk_entry *entry = container_of(wait, struct sk_entry, wait);
	unsigned long flags;

	if (atomic_read(&entry->inactive))
		return 0;

	spin_lock_irqsave(&ss.lock, flags);
	sk_entry_add_to_ready_list(entry);
	smp_mb();
	// memory barrier required
	if (waitqueue_active(&ss.wq))
		wake_up_locked(&ss.wq);
	spin_unlock_irqrestore(&ss.lock, flags);
	return 0;
}

static void sk_add_wait_queue(struct file *file, wait_queue_head_t *whead,
			      poll_table *pt)
{
	struct sk_entry *entry = container_of(pt, struct sk_entry, pt);

	init_waitqueue_func_entry(&entry->wait, sk_wait_callback);
	add_wait_queue(whead, &entry->wait);
	entry->whead = whead;
}

static struct sk_entry *sk_entry_create(struct socket *sock,
					enum sk_type sk_type,
					union ubcore_net_addr_union ub_addr)
{
	struct sk_entry *entry;
	__poll_t events;
	unsigned long flags;

	entry = kmalloc(sizeof(struct sk_entry), GFP_KERNEL);
	if (IS_ERR_OR_NULL(entry)) {
		ubcore_log_err("Failed to alloc sock entry");
		return NULL;
	}
	kref_init(&entry->ref);
	spin_lock_init(&entry->sk_lock);
	entry->sock = sock;
	entry->sk_type = sk_type;
	entry->ub_addr = ub_addr;
	atomic_set(&entry->inactive, 0);
	entry->whead = NULL;
	INIT_LIST_HEAD(&entry->ready_list_entry);

	spin_lock_irqsave(&ss.lock, flags);
	sk_entry_add_to_all_list(entry);
	entry->pt._qproc = sk_add_wait_queue;
	entry->pt._key = POLLIN | POLLERR | POLLHUP | POLLRDHUP;
	events = (*entry->sock->ops->poll)(NULL, entry->sock, &entry->pt);
	entry->pt._qproc = NULL;

	if (events & EPOLLIN) {
		sk_entry_add_to_ready_list(entry);
		smp_mb();
		// memory barrier required
		if (waitqueue_active(&ss.wq))
			wake_up_locked(&ss.wq);
	} else if (unlikely(events & EPOLLRDHUP)) {
		ubcore_log_err(
			"EPOLLRDHUP event detected: peer socket closed or suspended");
	}
	spin_unlock_irqrestore(&ss.lock, flags);

	ubcore_log_info("Create sock entry, type:%d, addr:" EID_FMT, sk_type,
			EID_ARGS(ub_addr));
	return entry;
}

static struct sk_entry *sk_entry_create_listen(union ubcore_net_addr_union addr)
{
	struct sk_entry *entry;
	struct socket *sock;

	sock = k_listen(&addr, ss.port);
	if (!sock) {
		ubcore_log_err("Failed to create listen socket");
		return NULL;
	}

	entry = sk_entry_create(sock, SOCK_LISTEN, addr);
	if (!entry) {
		ubcore_log_err("Failed to create sock entry");
		k_close(sock);
	}
	return entry;
}

static struct sk_entry *sk_entry_create_accept(struct socket *sock)
{
	struct sk_entry *entry;
	struct socket *newsock;
	union ubcore_net_addr_union addr = { 0 };

	newsock = k_accept(sock, &addr);
	if (!newsock) {
		ubcore_log_err("Failed to create accept socket");
		return NULL;
	}

	entry = sk_entry_create(newsock, SOCK_ACCEPT, addr);
	if (!entry) {
		ubcore_log_err("Failed to register accept sock entry");
		k_close(newsock);
	}
	return entry;
}

static struct sk_entry *
sk_entry_create_connect(union ubcore_net_addr_union addr)
{
	struct sk_entry *entry;
	struct socket *sock;

	sock = k_connect(&addr, ss.port);
	if (!sock) {
		ubcore_log_err("Failed to create connect socket");
		return NULL;
	}

	entry = sk_entry_create(sock, SOCK_CONNECT, addr);
	if (!entry) {
		ubcore_log_err("Failed to create sock entry");
		k_close(sock);
	}
	return entry;
}

static struct sk_entry *sk_entry_find_by_addr(union ubcore_net_addr_union addr)
{
	struct sk_entry *cur, *tmp, *entry = NULL;
	unsigned long flags;

	spin_lock_irqsave(&ss.lock, flags);
	list_for_each_entry_safe(cur, tmp, &ss.all_list, all_list_entry) {
		if (cur->sk_type != SOCK_LISTEN &&
		    memcmp(&cur->ub_addr, &addr, sizeof(addr)) == 0) {
			entry = cur;
			sk_entry_ref_acquire(entry);
			break;
		}
	}
	spin_unlock_irqrestore(&ss.lock, flags);

	if (entry)
		return entry;

	entry = sk_entry_create_connect(addr);
	if (!entry) {
		ubcore_log_err("Failed to create connect sock entry\n");
		return NULL;
	}
	sk_entry_ref_acquire(entry);
	return entry;
}

static int sk_handle_msg(struct sk_entry *entry)
{
	struct ubcore_net_msg msg = { 0 };
	struct ubcore_device *dev = NULL;
	size_t ret;

	ret = k_recvmsg(entry->sock, &msg, MSG_HDR_SIZE, 0);
	if (ret != MSG_HDR_SIZE || msg.len > SOCK_MSG_MAX_LEN) {
		ubcore_log_err("Failed to recv sock hdr, recv: %zu, " MSG_FMT,
			       ret, MSG_ARG(&msg));
		return -EINVAL;
	}

	msg.data = kcalloc(1, msg.len, GFP_KERNEL);
	if (IS_ERR_OR_NULL(msg.data)) {
		ubcore_log_err("Failed to alloc sock msg data, " MSG_FMT,
			       MSG_ARG(&msg));
		return -EINVAL;
	}

	ret = k_recvmsg(entry->sock, msg.data, msg.len, 0);
	if (ret != msg.len) {
		ubcore_log_err("Failed to recv sock data, recv: %zu, " MSG_FMT,
			       ret, MSG_ARG(&msg));
		kfree(msg.data);
		return -EINVAL;
	}

	dev = ubcore_find_device((union ubcore_eid *)&entry->ub_addr,
				 (enum ubcore_transport_type)(msg.cap));
	if (!dev) {
		ubcore_log_err(
			"Failed to find device when handle sock msg, " MSG_FMT,
			MSG_ARG(&msg));
		kfree(msg.data);
		return 0;
	}

	ubcore_log_info("Handle sock message, " MSG_FMT, MSG_ARG(&msg));
	ubcore_net_handle_msg(dev, &msg, (void *)entry);

	ubcore_put_device(dev);

	kfree(msg.data);
	return 0;
}

static int sk_event_loop(void *data)
{
	wait_queue_entry_t wait;

	init_waitqueue_entry(&wait, current);

	while (!kthread_should_stop()) {
		ktime_t expires;
		ktime_t time_limit = ktime_set(SK_EVENT_TIME_LIMIT, 0);
		unsigned long flags;

		spin_lock_irqsave(&ss.lock, flags);
		while (!list_empty(&ss.ready_list)) {
			int is_sock_inactive = 0;
			struct sk_entry *cur;
			__poll_t events;

			cur = list_first_entry(&ss.ready_list, struct sk_entry,
					       ready_list_entry);
			events = (*cur->sock->ops->poll)(NULL, cur->sock,
							 &cur->pt);

			ubcore_log_info("Events occur, %x, addr:" EID_FMT,
					events, EID_ARGS(cur->ub_addr));
			if (events & (POLLERR | POLLHUP | POLLRDHUP))
				is_sock_inactive = -1;
			else if (events & POLLIN) {
				spin_unlock_irqrestore(&ss.lock, flags);
				if (cur->sk_type == SOCK_LISTEN)
					(void)sk_entry_create_accept(cur->sock);
				else
					is_sock_inactive = sk_handle_msg(cur);
				spin_lock_irqsave(&ss.lock, flags);
			} else {
				// No remaining data to process.
				sk_entry_remove_from_ready_list(cur);
			}
			// Prevent the socket from being re-added to the ready list
			if (is_sock_inactive) {
				atomic_set(&cur->inactive, 1);
				sk_entry_remove_from_ready_list(cur);
				sk_entry_remove_from_all_list(cur);

				// Release sock will invoke sk_wait_callback
				spin_unlock_irqrestore(&ss.lock, flags);
				sk_entry_ref_release(cur);
				spin_lock_irqsave(&ss.lock, flags);
			}
		}
		spin_unlock_irqrestore(&ss.lock, flags);

		if (signal_pending(current)) {
			ubcore_log_err("pending signal error");
			break;
		}

		set_current_state(TASK_INTERRUPTIBLE);
		__add_wait_queue_exclusive(&ss.wq, &wait);
		expires = ktime_add(ktime_get(), time_limit);
		schedule_hrtimeout_range(&expires, 0, HRTIMER_MODE_ABS);
		set_current_state(TASK_RUNNING);
		__remove_wait_queue(&ss.wq, &wait);
	}

	while (!list_empty(&ss.all_list)) {
		struct sk_entry *cur;

		cur = list_first_entry(&ss.all_list, struct sk_entry,
				       all_list_entry);
		if (cur->whead)
			__remove_wait_queue(cur->whead, &cur->wait);
		sk_entry_remove_from_ready_list(cur);
		sk_entry_remove_from_all_list(cur);
		sk_entry_ref_release(cur);
	}
	return 0;
}

static int ubcore_sock_send_inner(struct ubcore_device *dev,
				  struct ubcore_net_msg *msg,
				  struct sk_entry *entry)
{
	struct msghdr msghdr = { 0 };
	struct kvec vec[2] = { 0 };
	const int vec_num = 2;
	int ret;

	ubcore_log_info("Send sock message, " MSG_FMT, MSG_ARG(msg));

	msg->cap = dev->transport_type;
	msghdr.msg_flags = 0;
	vec[0].iov_base = msg;
	vec[0].iov_len = MSG_HDR_SIZE;
	vec[1].iov_base = msg->data;
	vec[1].iov_len = msg->len;

	spin_lock(&entry->sk_lock);
	ret = kernel_sendmsg(entry->sock, &msghdr, vec, vec_num,
			     MSG_HDR_SIZE + msg->len);
	spin_unlock(&entry->sk_lock);

	return ret;
}

int ubcore_sock_send(struct ubcore_device *dev, void *conn,
		struct ubcore_net_msg *msg)
{
	struct sk_entry *entry = conn;

	return ubcore_sock_send_inner(dev, msg, entry);
}

int ubcore_sock_send_to(struct ubcore_device *dev, struct ubcore_net_msg *msg,
			union ubcore_eid addr)
{
	struct sk_entry *entry;
	int ret;

	entry = sk_entry_find_by_addr(
		*((union ubcore_net_addr_union *)(&addr)));
	if (!entry)
		return -EINVAL;
	ret = ubcore_sock_send_inner(dev, msg, entry);
	sk_entry_ref_release(entry);
	return ret;
}

int ubcore_sock_init(void)
{
	union ubcore_net_addr_union any_addr6 = { 0 };

	ubcore_log_info("sock service init\n");

	INIT_LIST_HEAD(&ss.all_list);
	INIT_LIST_HEAD(&ss.ready_list);
	spin_lock_init(&ss.lock);
	ss.port = SOCK_PORT;
	init_waitqueue_head(&ss.wq);

	if (sk_entry_create_listen(any_addr6) == NULL) {
		ubcore_log_err("Failed to register ipv6 listen sock entry");
		return -EINVAL;
	}

	ss.daemon = kthread_run(sk_event_loop, NULL, "sk_event_loop");
	if (!ss.daemon) {
		ubcore_log_err("sock thread launch failed");
		return -EINVAL;
	}
	return 0;
}

void ubcore_sock_uninit(void)
{
	ubcore_log_info("sock service uninit\n");

	if (ss.daemon) {
		smp_mb();
		// memory barrier required
		if (waitqueue_active(&ss.wq))
			wake_up(&ss.wq);
		kthread_stop(ss.daemon);
		ss.daemon = NULL;
	}
}
