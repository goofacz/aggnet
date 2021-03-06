/*
 * This file is part of aggnet.
 *
 * Aggnet is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Aggnet is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Aggnet.  If not, see <https://www.gnu.org/licenses/>.
 */ 
#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/netdevice.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/poll.h>

MODULE_LICENSE("GPL");

#define MAX_PACKET_QUEUE_SIZE 10

typedef struct {
    struct list_head list;
    struct sk_buff *skb;
} packet_t;

typedef struct {
    wait_queue_head_t wait_queue;
    struct mutex mutex;
    struct list_head packets;
    unsigned int cnt;
} packet_queue_t;

typedef struct {
    packet_queue_t queue;
    size_t copied_size;
} cdev_read_queue_t;

typedef struct {
    struct sk_buff *skb;
    size_t copied_size;
    u32 packet_size;
} cdev_write_buffer_t;

typedef struct {
    /* char device */
    int major;
    int minor;
    struct cdev char_dev;

    cdev_read_queue_t read_queue;
    cdev_write_buffer_t write_buffer;

    /* Network device */
    struct net_device* net_dev;
    struct net_device_stats net_dev_stats;
} instance_t;

static instance_t instance;
static char* mode = "server";

static packet_t* packet_alloc_from_skb(struct sk_buff *skb)
{
    packet_t* pkt;

    pkt = kmalloc (sizeof(*skb), GFP_KERNEL);
    if (!pkt) {
        dev_kfree_skb(skb);
        return NULL;
    }

    pkt->skb = skb;
    return pkt;
}

static void packet_free(packet_t* pkt)
{
    dev_kfree_skb(pkt->skb);
    kfree(pkt);
}

static void packet_queue_init(packet_queue_t* queue)
{
    init_waitqueue_head(&queue->wait_queue);
    mutex_init(&queue->mutex);
    INIT_LIST_HEAD(&queue->packets);
    queue->cnt = 0;
}

static void packet_queue_fini(packet_queue_t* queue)
{
    packet_t* curr_pkt;
    packet_t* next_pkt;

    list_for_each_entry_safe(curr_pkt, next_pkt, &queue->packets, list) {
        packet_free(curr_pkt);
    }
}

static int packet_queue_push(packet_queue_t* queue, packet_t* pkt)
{
    if (mutex_lock_interruptible(&queue->mutex)) {
        packet_free(pkt);
        return -ERESTARTSYS;
    }

    if (queue->cnt > MAX_PACKET_QUEUE_SIZE) {
        goto error_nobufs;
    }

    list_add_tail(&pkt->list, &queue->packets);
    queue->cnt++;

    mutex_unlock(&queue->mutex);
    wake_up_interruptible(&queue->wait_queue);

    return 0;

error_nobufs:
    mutex_unlock(&queue->mutex);
    packet_free(pkt);

    printk(KERN_WARNING "aggnet: TX queue is full");
    return -ENOBUFS;
}

static int packet_queue_peak(packet_queue_t* queue, packet_t** pkt)
{
    if (mutex_lock_interruptible(&queue->mutex)) {
        return -ERESTARTSYS;
    }

    while (list_empty(&queue->packets)) {
        mutex_unlock(&queue->mutex);

        if (wait_event_interruptible(queue->wait_queue, !list_empty(&queue->packets))) {
            return -ERESTARTSYS;
        }

        if (mutex_lock_interruptible(&queue->mutex)) {
            return -ERESTARTSYS;
        }
    }

    *pkt = list_entry(queue->packets.next, packet_t, list);

    mutex_unlock(&queue->mutex);

    return 0;
}

static int packet_queue_try_peak(packet_queue_t* queue, packet_t** pkt)
{
    if (mutex_lock_interruptible(&queue->mutex)) {
        return -ERESTARTSYS;
    }

    if (list_empty(&queue->packets)) {
        *pkt = NULL;
    }
    else {
        *pkt = list_entry(queue->packets.next, packet_t, list);
    }

    mutex_unlock(&queue->mutex);

    return 0;
}

static int packet_queue_pop(packet_queue_t* queue)
{
    if (mutex_lock_interruptible(&queue->mutex)) {
        return -ERESTARTSYS;
    }

    if (!list_empty(&queue->packets)) {
        packet_t* pkt = list_entry(queue->packets.next, packet_t, list);
        list_del(&pkt->list);
        packet_free(pkt);
        queue->cnt--;
    }

    mutex_unlock(&queue->mutex);

    return 0;
}

static bool packet_queue_poll(packet_queue_t* queue, struct file *filp, poll_table *wait)
{
    bool has_pkts;

    mutex_lock(&queue->mutex);
    poll_wait(filp, &queue->wait_queue, wait);
    has_pkts = !list_empty(&queue->packets);
    mutex_unlock(&queue->mutex);

    return has_pkts;
}

static void cdev_read_queue_init(cdev_read_queue_t* queue)
{
    packet_queue_init(&queue->queue);
    queue->copied_size = 0;
}

static void cdev_read_queue_fini(cdev_read_queue_t* queue)
{
    packet_queue_fini(&queue->queue);
}

static ssize_t cdev_read_queue_copy_to_user(cdev_read_queue_t* queue, char __user *buf, size_t count)
{
    size_t total_copied_size = 0;

    while (total_copied_size < count) {
        int res;
        packet_t* pkt;
        u32 skb_len;
        unsigned long not_copied_size;

        if (total_copied_size == 0) {
            res = packet_queue_peak(&instance.read_queue.queue, &pkt);
        }
        else {
            res = packet_queue_try_peak(&instance.read_queue.queue, &pkt);
        }

        if (res < 0) {
            return res;
        }

        if (!pkt) {
            break;
        }

        if (queue->copied_size < sizeof(skb_len)) {
            skb_len = pkt->skb->len;
            not_copied_size = copy_to_user(buf + total_copied_size, &skb_len + queue->copied_size, sizeof(skb_len) - queue->copied_size);
            total_copied_size += (sizeof(skb_len) - queue->copied_size) - not_copied_size;
            queue->copied_size += (sizeof(skb_len) - queue->copied_size) - not_copied_size;
        }
        else {
            not_copied_size = copy_to_user(buf + total_copied_size, pkt->skb->data + queue->copied_size - sizeof(skb_len), pkt->skb->len - queue->copied_size + sizeof(skb_len));
            total_copied_size += (pkt->skb->len - queue->copied_size + sizeof(skb_len)) - not_copied_size;
            queue->copied_size += (pkt->skb->len - queue->copied_size + sizeof(skb_len)) - not_copied_size;
        }

        if (queue->copied_size == sizeof(skb_len) + pkt->skb->len) {
            queue->copied_size = 0;
            res = packet_queue_pop(&instance.read_queue.queue);
            if (res < 0) {
                return res;
            }
        }

        if (not_copied_size > 0) {
            break;
        }
    }

    return total_copied_size;
}

static void cdev_write_buffer_init(cdev_write_buffer_t* queue)
{
    queue->skb = NULL;
    queue->copied_size = 0;
    queue->packet_size = 0;
}

static void cdev_write_buffer_fini(cdev_write_buffer_t* queue)
{
    dev_kfree_skb(queue->skb);
}

static ssize_t cdev_write_queue_copy_from_user(cdev_write_buffer_t* queue, const char __user *buf, size_t count)
{
    size_t total_copied_size = 0;

    return total_copied_size;
}

static int char_dev_open(struct inode *inode, struct file *filp)
{
    printk(KERN_DEBUG "%s", __func__);
    return 0;
}

static int char_dev_release(struct inode *inode, struct file *filp)
{
    printk(KERN_DEBUG "%s", __func__);
    return 0;
}

static ssize_t char_dev_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t copied_bytes;

    printk(KERN_DEBUG "%s", __func__);
    copied_bytes = cdev_read_queue_copy_to_user(&instance.read_queue, buf, count);

    if (netif_queue_stopped(instance.net_dev)) {
        netif_wake_queue(instance.net_dev);
    }

    return copied_bytes;
}

static ssize_t char_dev_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    printk(KERN_DEBUG "%s", __func__);
    return 0;
}

static loff_t char_dev_llseek(struct file *filp, loff_t off, int foo)
{
    if (off != 0) {
        return -EINVAL;
    }

    return 0;
}

static unsigned int char_dev_poll(struct file *filp, poll_table *wait)
{
    return (packet_queue_poll(&instance.read_queue.queue, filp, wait) ? POLLIN : 0) | POLLOUT;
}

static int net_dev_open(struct net_device *dev)
{
    const u8 addr[] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x23};

    printk(KERN_DEBUG "%s", __func__);
    memcpy(dev->dev_addr, addr, ETH_ALEN);
    netif_start_queue(dev);
    return 0;
}

static int net_dev_stop(struct net_device *dev)
{
    printk(KERN_DEBUG "%s", __func__);
    netif_stop_queue(dev);
    return 0;
}

static int net_dev_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
    int res;
    packet_t* pkt;

    printk(KERN_DEBUG "%s", __func__);

    pkt = packet_alloc_from_skb(skb);
    if (!pkt) {
        return -ENOMEM;
    }

    res = packet_queue_push(&instance.read_queue.queue, pkt);
    if (res < 0) {
        if (res == -ENOBUFS) {
            netif_stop_queue(instance.net_dev);
        }

        return res;
    }

    instance.net_dev_stats.tx_packets++;
    instance.net_dev_stats.tx_bytes += skb->len;

    return 0;
}

static int net_dev_hard_header(struct sk_buff *skb, struct net_device *dev, unsigned short type, const void *daddr, const void *saddr, unsigned len)
{
	struct ethhdr *eth = (struct ethhdr*) skb_push(skb, ETH_HLEN);

    printk(KERN_DEBUG "%s", __func__);
	eth->h_proto = htons(type);
	memcpy(eth->h_source, saddr ? saddr : dev->dev_addr, dev->addr_len);
	memcpy(eth->h_dest, daddr ? daddr : dev->dev_addr, dev->addr_len);
	
	if (!daddr) {
        eth->h_dest[ETH_ALEN-1] = 0xFF;
    }

	return dev->hard_header_len;
}

static void net_dev_tx_timeout(struct net_device *dev)
{
    printk(KERN_DEBUG "%s", __func__);
    instance.net_dev_stats.tx_errors++;
    return;
}

static struct net_device_stats* net_dev_get_stats(struct net_device *dev)
{
    printk(KERN_DEBUG "%s", __func__);
    return &instance.net_dev_stats;
}

static int net_dev_set_config(struct net_device *dev, struct ifmap *map)
{
    printk(KERN_DEBUG "%s", __func__);
    return 0;
}

static void net_dev_priv_destructor(struct net_device *dev)
{
    printk(KERN_DEBUG "%s", __func__);
}

static const struct file_operations char_dev_fops = {
    .owner = THIS_MODULE,
    .read = char_dev_read,
    .write = char_dev_write,
    .open = char_dev_open,
    .release = char_dev_release,
    .poll = char_dev_poll,
    .llseek = char_dev_llseek,
};

static const struct header_ops net_dev_header_ops = {
    .create = net_dev_hard_header,
};

static const struct net_device_ops net_dev_ops = {
    .ndo_open = net_dev_open,
    .ndo_stop = net_dev_stop,
    .ndo_start_xmit = net_dev_start_xmit,
    .ndo_tx_timeout = net_dev_tx_timeout,
    .ndo_get_stats = net_dev_get_stats,
    .ndo_set_config = net_dev_set_config,
};

static void net_dev_setup(struct net_device* dev)
{
    ether_setup(dev);
    dev->watchdog_timeo = 5 * HZ;
	dev->netdev_ops = &net_dev_ops;
    dev->header_ops = &net_dev_header_ops;
	dev->flags = (dev->flags & ~(IFF_MULTICAST | IFF_BROADCAST)) | IFF_NOARP;
	dev->features |= NETIF_F_HW_CSUM;
	dev->needs_free_netdev	= false;
	dev->priv_destructor = net_dev_priv_destructor;
}

static int aggnet_init(void)
{
    dev_t devno = 0;
    int result = 0;

    printk(KERN_DEBUG "%s", __func__);
    memset(&instance, 0, sizeof(instance));

    cdev_read_queue_init(&instance.read_queue);
    cdev_write_buffer_init(&instance.write_buffer);

    result = alloc_chrdev_region(&devno, instance.minor, 1, "aggnet");
    if (result < 0) {
        goto error;
    }

    instance.major = MAJOR(devno);

    cdev_init(&instance.char_dev, &char_dev_fops);
    instance.char_dev.owner = THIS_MODULE;

    result = cdev_add(&instance.char_dev, devno, 1);
    if (result < 0)  {
        goto error;
    }

    instance.net_dev = alloc_netdev(0, "aggnet0", NET_NAME_ENUM, net_dev_setup);
    if (!instance.net_dev)  {
        goto error;
    }

    result = register_netdev(instance.net_dev);
    if (result < 0)  {
        goto error;
    }

    return 0;

error:
    unregister_chrdev_region(devno, 1);
    cdev_del(&instance.char_dev);

    unregister_netdev(instance.net_dev);
    free_netdev(instance.net_dev);

    return result;
}

static void aggnet_exit(void)
{
    dev_t devno = MKDEV(instance.major, instance.minor);

    printk(KERN_DEBUG "%s", __func__);
    cdev_read_queue_fini(&instance.read_queue);
    cdev_write_buffer_fini(&instance.write_buffer);

    unregister_netdev(instance.net_dev);
    free_netdev(instance.net_dev);

    unregister_chrdev_region(devno, 1);
    cdev_del(&instance.char_dev);
}

module_param(mode, charp, S_IRUGO);

module_init(aggnet_init);
module_exit(aggnet_exit);
