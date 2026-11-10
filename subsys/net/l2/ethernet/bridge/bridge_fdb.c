/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_eth_bridge_fdb, CONFIG_NET_ETHERNET_BRIDGE_LOG_LEVEL);

#include <zephyr/net/net_log.h>
#include <zephyr/net/ethernet_bridge.h>
#include <zephyr/net/ethernet_bridge_fdb.h>

#include "bridge_internal.h"

/* FDB table */
static sys_slist_t fdb_entries = SYS_SLIST_STATIC_INIT(&fdb_entries);
static K_MUTEX_DEFINE(fdb_lock);
static size_t fdb_count;

/* Memory pool for FDB entries */
K_MEM_SLAB_DEFINE_STATIC_TYPE(fdb_slab, struct eth_bridge_fdb_entry,
			      CONFIG_NET_ETHERNET_BRIDGE_FDB_MAX_ENTRIES);

/* Give an entry with a port map to the hooks of its bridge */
static int fdb_hw_config(const struct eth_bridge_fdb_entry *entry, bool set)
{
	struct eth_bridge_hw_fdb_entry hw_entry = {
		.portmap = entry->portmap,
		.host_rx = entry->host_rx,
	};

	memcpy(&hw_entry.mac, &entry->mac, sizeof(hw_entry.mac));

	return eth_bridge_hw_fdb_config(entry->bridge, set, &hw_entry);
}

static void fdb_remove(sys_snode_t *prev, struct eth_bridge_fdb_entry *entry)
{
	NET_DBG("FDB entry removed: %02x:%02x:%02x:%02x:%02x:%02x -> ports 0x%x", entry->mac.addr[0],
		entry->mac.addr[1], entry->mac.addr[2], entry->mac.addr[3], entry->mac.addr[4],
		entry->mac.addr[5], entry->portmap);

	if ((entry->flags & ETHERNET_BRIDGE_FDB_FLAG_HW) != 0U) {
		/* The device is told, whatever it answers: the entry is gone here */
		(void)fdb_hw_config(entry, false);
	}

	sys_slist_remove(&fdb_entries, prev, &entry->node);
	fdb_count--;

	k_mem_slab_free(&fdb_slab, (void *)entry);
}

/* Remove the learned entry seen longest ago, the first learned of those seen
 * at the same time. Called with fdb_lock held.
 */
static bool fdb_remove_oldest_learned(void)
{
	struct eth_bridge_fdb_entry *entry, *oldest = NULL;
	sys_snode_t *prev = NULL, *oldest_prev = NULL;

	SYS_SLIST_FOR_EACH_CONTAINER(&fdb_entries, entry, node) {
		if ((entry->flags & ETHERNET_BRIDGE_FDB_FLAG_DYNAMIC) != 0U &&
		    (oldest == NULL || entry->last_seen <= oldest->last_seen)) {
			oldest = entry;
			oldest_prev = prev;
		}

		prev = &entry->node;
	}

	if (oldest == NULL) {
		return false;
	}

	fdb_remove(oldest_prev, oldest);

	return true;
}

static int fdb_add_hw(struct net_if *bridge, const struct net_eth_addr *mac, uint32_t portmap,
		      bool host_rx)
{
	struct eth_bridge_fdb_entry *entry, *found = NULL;
	struct eth_bridge_fdb_entry new_entry;
	int ret;

	ret = eth_bridge_hw_fdb_check(bridge, portmap);
	if (ret != 0) {
		return ret;
	}

	k_mutex_lock(&fdb_lock, K_FOREVER);

	SYS_SLIST_FOR_EACH_CONTAINER(&fdb_entries, entry, node) {
		if ((entry->flags & ETHERNET_BRIDGE_FDB_FLAG_HW) != 0U &&
		    entry->bridge == bridge &&
		    memcmp(&entry->mac, mac, sizeof(struct net_eth_addr)) == 0) {
			found = entry;
			break;
		}
	}

	memset(&new_entry, 0, sizeof(new_entry));
	memcpy(&new_entry.mac, mac, sizeof(struct net_eth_addr));
	new_entry.bridge = bridge;
	new_entry.portmap = portmap;
	new_entry.host_rx = host_rx;
	new_entry.flags = ETHERNET_BRIDGE_FDB_FLAG_STATIC | ETHERNET_BRIDGE_FDB_FLAG_HW;

	if (found == NULL) {
		/* As for the other entries, learned ones give way to it */
		if (fdb_count >= CONFIG_NET_ETHERNET_BRIDGE_FDB_MAX_ENTRIES &&
		    !fdb_remove_oldest_learned()) {
			NET_ERR("FDB table full");
			ret = -ENOMEM;
			goto out;
		}

		if (k_mem_slab_alloc(&fdb_slab, (void **)&found, K_NO_WAIT) != 0) {
			NET_ERR("Failed to allocate FDB entry");
			ret = -ENOMEM;
			goto out;
		}

		ret = fdb_hw_config(&new_entry, true);
		if (ret < 0) {
			k_mem_slab_free(&fdb_slab, (void *)found);
			goto out;
		}

		memcpy(found, &new_entry, sizeof(*found));
		sys_slist_prepend(&fdb_entries, &found->node);
		fdb_count++;
	} else {
		ret = fdb_hw_config(&new_entry, true);
		if (ret < 0) {
			goto out;
		}

		found->portmap = portmap;
		found->host_rx = host_rx;
	}

	ret = 0;

	NET_DBG("FDB entry set: %02x:%02x:%02x:%02x:%02x:%02x -> ports 0x%x host %d", mac->addr[0],
		mac->addr[1], mac->addr[2], mac->addr[3], mac->addr[4], mac->addr[5], portmap,
		host_rx);
out:
	k_mutex_unlock(&fdb_lock);
	return ret;
}

int eth_bridge_fdb_add(struct net_if *iface, const struct eth_bridge_fdb_params *params)
{
	struct eth_bridge_fdb_entry *entry;
	struct net_if *bridge;
	struct net_eth_addr *mac;
	uint32_t portmap;
	bool host_rx;
	int ret = 0;

	/* Check params, mac and iface is valid or not */
	if (params == NULL || params->mac == NULL || iface == NULL) {
		return -EINVAL;
	}

	mac = params->mac;
	portmap = params->portmap;
	host_rx = params->host_rx;

	if (!net_eth_iface_is_bridged(net_if_l2_data(iface))) {
		return -EINVAL;
	}

	bridge = net_eth_get_bridge(net_if_l2_data(iface));

	if (portmap == 0U) {
		portmap = eth_bridge_port_bit(iface);
	}

	/* The devices forward on their own, with the port map */
	if (eth_bridge_port_hw_forwarding(iface)) {
		return fdb_add_hw(bridge, mac, portmap, host_rx);
	}

	if (portmap != eth_bridge_port_bit(iface) || host_rx) {
		return -ENOTSUP;
	}

	k_mutex_lock(&fdb_lock, K_FOREVER);

	/* Check if the mac already exists in the FDB table of this bridge */
	SYS_SLIST_FOR_EACH_CONTAINER(&fdb_entries, entry, node) {
		if ((entry->flags & ETHERNET_BRIDGE_FDB_FLAG_HW) == 0U &&
		    memcmp(&entry->mac, mac, sizeof(struct net_eth_addr)) == 0 &&
		    entry->bridge == bridge) {

			if (entry->portmap == portmap) {
				/* A learned entry becomes static */
				entry->flags = ETHERNET_BRIDGE_FDB_FLAG_STATIC;
				NET_DBG("FDB entry exists");
				goto out;
			}

			/* For unicast address, just update it */
			if (net_eth_is_addr_valid(mac)) {
				entry->portmap = portmap;
				entry->flags = ETHERNET_BRIDGE_FDB_FLAG_STATIC;
				NET_DBG("FDB entry updated: %02x:%02x:%02x:%02x:%02x:%02x -> ports "
					"0x%x",
					mac->addr[0], mac->addr[1], mac->addr[2], mac->addr[3],
					mac->addr[4], mac->addr[5], portmap);
				goto out;
			}
		}
	}

	/* Learned entries share the table: a full one gives up its oldest
	 * learned entry, so that received frames cannot fill it against
	 * configuration.
	 */
	if (fdb_count >= CONFIG_NET_ETHERNET_BRIDGE_FDB_MAX_ENTRIES &&
	    !fdb_remove_oldest_learned()) {
		NET_ERR("FDB table full");
		ret = -ENOMEM;
		goto out;
	}

	/* Allocate new entry */
	if (k_mem_slab_alloc(&fdb_slab, (void **)&entry, K_NO_WAIT) != 0) {
		NET_ERR("Failed to allocate FDB entry");
		ret = -ENOMEM;
		goto out;
	}

	/* Initialize entry */
	memcpy(&entry->mac, mac, sizeof(struct net_eth_addr));
	entry->bridge = bridge;
	entry->portmap = portmap;
	entry->host_rx = false;
	entry->flags = ETHERNET_BRIDGE_FDB_FLAG_STATIC;

	/* Add to list */
	sys_slist_prepend(&fdb_entries, &entry->node);
	fdb_count++;

	NET_DBG("FDB entry added: %02x:%02x:%02x:%02x:%02x:%02x -> ports 0x%x", mac->addr[0],
		mac->addr[1], mac->addr[2], mac->addr[3], mac->addr[4], mac->addr[5], portmap);
out:
	k_mutex_unlock(&fdb_lock);
	return ret;
}

int eth_bridge_fdb_del(struct net_if *iface, const struct eth_bridge_fdb_params *params)
{
	struct eth_bridge_fdb_entry *entry;
	sys_snode_t *node, *next, *prev = NULL;
	struct net_eth_addr *mac;
	struct net_if *bridge = NULL;
	uint32_t bit = 0U;
	int ret = -ENOENT;

	if (params == NULL || params->mac == NULL) {
		return -EINVAL;
	}

	mac = params->mac;

	/* A NULL iface deletes the address from every interface */
	if (iface != NULL) {
		bridge = net_eth_get_bridge(net_if_l2_data(iface));
		if (bridge == NULL) {
			return -EINVAL;
		}

		bit = eth_bridge_port_bit(iface);
	}

	k_mutex_lock(&fdb_lock, K_FOREVER);

	SYS_SLIST_FOR_EACH_NODE_SAFE(&fdb_entries, node, next) {
		entry = CONTAINER_OF(node, struct eth_bridge_fdb_entry, node);

		/* An entry with a port map belongs to the bridge, not to a port */
		if (memcmp(&entry->mac, mac, sizeof(struct net_eth_addr)) != 0 ||
		    (iface != NULL &&
		     (entry->bridge != bridge ||
		      ((entry->flags & ETHERNET_BRIDGE_FDB_FLAG_HW) == 0U &&
		       entry->portmap != bit)))) {
			prev = node;
			continue;
		}

		/* Remove from list, prev stays the last kept node */
		fdb_remove(prev, entry);

		ret = 0;

		/* eth_bridge_fdb_add() keeps at most one entry per address and interface */
		if (iface != NULL) {
			break;
		}
	}

	k_mutex_unlock(&fdb_lock);
	return ret;
}

int eth_bridge_fdb_del_iface(struct net_if *iface)
{
	struct eth_bridge_fdb_entry *entry;
	sys_snode_t *node, *next, *prev = NULL;
	struct net_if *bridge = NULL;
	uint32_t bit = 0U;

	/* A NULL iface deletes the entries of every interface */
	if (iface != NULL) {
		bridge = net_eth_get_bridge(net_if_l2_data(iface));
		if (bridge == NULL) {
			return -EINVAL;
		}

		bit = eth_bridge_port_bit(iface);
	}

	k_mutex_lock(&fdb_lock, K_FOREVER);

	SYS_SLIST_FOR_EACH_NODE_SAFE(&fdb_entries, node, next) {
		entry = CONTAINER_OF(node, struct eth_bridge_fdb_entry, node);

		if ((entry->flags & ETHERNET_BRIDGE_FDB_FLAG_HW) != 0U && iface != NULL) {
			/* The port leaves the port map of the entry */
			if (entry->bridge == bridge && (entry->portmap & bit) != 0U) {
				entry->portmap &= ~bit;

				if (entry->portmap == 0U && !entry->host_rx) {
					fdb_remove(prev, entry);
					continue;
				}

				(void)fdb_hw_config(entry, true);
			}

			prev = node;
			continue;
		}

		if (iface != NULL && (entry->bridge != bridge || entry->portmap != bit)) {
			prev = node;
			continue;
		}

		/* Remove from list, prev stays the last kept node */
		fdb_remove(prev, entry);
	}

	k_mutex_unlock(&fdb_lock);
	return 0;
}

void eth_bridge_fdb_foreach(eth_bridge_fdb_entry_cb_t cb, void *user_data)
{
	struct eth_bridge_fdb_entry *entry, *tmp;

	k_mutex_lock(&fdb_lock, K_FOREVER);

	SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&fdb_entries, entry, tmp, node) {
		cb(entry, user_data);
	}

	k_mutex_unlock(&fdb_lock);
}

#if defined(CONFIG_NET_ETHERNET_BRIDGE_FDB_LEARNING)
static void fdb_ageing_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(fdb_ageing_work, fdb_ageing_handler);

/* Remove the learned entries not refreshed within the ageing time, once a
 * second while there are learned entries.
 */
static void fdb_ageing_handler(struct k_work *work)
{
	struct eth_bridge_fdb_entry *entry;
	sys_snode_t *node, *next, *prev = NULL;
	uint32_t now = k_uptime_seconds();
	bool dynamic_left = false;

	ARG_UNUSED(work);

	k_mutex_lock(&fdb_lock, K_FOREVER);

	SYS_SLIST_FOR_EACH_NODE_SAFE(&fdb_entries, node, next) {
		entry = CONTAINER_OF(node, struct eth_bridge_fdb_entry, node);

		if ((entry->flags & ETHERNET_BRIDGE_FDB_FLAG_DYNAMIC) == 0U) {
			prev = node;
			continue;
		}

		if (now - entry->last_seen >= CONFIG_NET_ETHERNET_BRIDGE_FDB_AGEING_TIME) {
			/* prev stays the last kept node */
			fdb_remove(prev, entry);
			continue;
		}

		dynamic_left = true;
		prev = node;
	}

	k_mutex_unlock(&fdb_lock);

	if (dynamic_left) {
		(void)k_work_schedule(&fdb_ageing_work, K_SECONDS(1));
	}
}

void eth_bridge_fdb_learn(struct net_if *bridge, struct net_eth_addr *mac, struct net_if *iface)
{
	struct eth_bridge_fdb_entry *entry;
	uint32_t bit = eth_bridge_port_bit(iface);

	/* Only a unicast source identifies a station, and a frame with the
	 * bridge's own address as source is not from a station behind a port.
	 * Learning that would send the frames for the bridge to that port.
	 */
	if (!net_eth_is_addr_valid(mac) ||
	    memcmp(mac, net_if_get_link_addr(bridge)->addr, sizeof(*mac)) == 0) {
		return;
	}

	k_mutex_lock(&fdb_lock, K_FOREVER);

	SYS_SLIST_FOR_EACH_CONTAINER(&fdb_entries, entry, node) {
		if (memcmp(&entry->mac, mac, sizeof(struct net_eth_addr)) != 0 ||
		    entry->bridge != bridge) {
			continue;
		}

		/* A static entry is never changed by what is received */
		if ((entry->flags & ETHERNET_BRIDGE_FDB_FLAG_DYNAMIC) != 0U) {
			if (entry->portmap != bit) {
				NET_DBG("FDB move %02x:%02x:%02x:%02x:%02x:%02x -> iface %d",
					mac->addr[0], mac->addr[1], mac->addr[2], mac->addr[3],
					mac->addr[4], mac->addr[5], net_if_get_by_iface(iface));
				entry->portmap = bit;
			}

			entry->last_seen = k_uptime_seconds();
		}

		goto out;
	}

	/* A full table learns nothing; such destinations are flooded */
	if (fdb_count >= CONFIG_NET_ETHERNET_BRIDGE_FDB_MAX_ENTRIES ||
	    k_mem_slab_alloc(&fdb_slab, (void **)&entry, K_NO_WAIT) != 0) {
		goto out;
	}

	memcpy(&entry->mac, mac, sizeof(struct net_eth_addr));
	entry->bridge = bridge;
	entry->portmap = bit;
	entry->host_rx = false;
	entry->flags = ETHERNET_BRIDGE_FDB_FLAG_DYNAMIC;
	entry->last_seen = k_uptime_seconds();

	sys_slist_prepend(&fdb_entries, &entry->node);
	fdb_count++;

	/* Does nothing when the ageing work is already scheduled */
	(void)k_work_schedule(&fdb_ageing_work, K_SECONDS(1));

	NET_DBG("FDB entry learned: %02x:%02x:%02x:%02x:%02x:%02x -> iface %d", mac->addr[0],
		mac->addr[1], mac->addr[2], mac->addr[3], mac->addr[4], mac->addr[5],
		net_if_get_by_iface(iface));
out:
	k_mutex_unlock(&fdb_lock);
}
#endif /* CONFIG_NET_ETHERNET_BRIDGE_FDB_LEARNING */

size_t eth_bridge_fdb_lookup(struct net_if *bridge, const struct net_eth_addr *mac,
			     struct net_if *ports[], size_t max_ports)
{
	struct eth_bridge_fdb_entry *entry;
	struct net_if *port;
	size_t count = 0;

	k_mutex_lock(&fdb_lock, K_FOREVER);

	SYS_SLIST_FOR_EACH_CONTAINER(&fdb_entries, entry, node) {
		if ((entry->flags & ETHERNET_BRIDGE_FDB_FLAG_HW) != 0U ||
		    memcmp(&entry->mac, mac, sizeof(struct net_eth_addr)) != 0 ||
		    entry->bridge != bridge) {
			continue;
		}

		/* An entry without a port map has exactly one port */
		port = eth_bridge_get_port_iface(bridge, find_lsb_set(entry->portmap) - 1);
		if (port == NULL) {
			continue;
		}

		if (count < max_ports) {
			ports[count] = port;
		}

		count++;
	}

	k_mutex_unlock(&fdb_lock);

	return count;
}

void eth_bridge_fdb_flush(struct net_if *bridge, struct net_if *iface, bool dynamic_only)
{
	struct eth_bridge_fdb_entry *entry;
	sys_snode_t *node, *next, *prev = NULL;
	uint32_t bit = (iface != NULL) ? eth_bridge_port_bit(iface) : 0U;

	k_mutex_lock(&fdb_lock, K_FOREVER);

	SYS_SLIST_FOR_EACH_NODE_SAFE(&fdb_entries, node, next) {
		entry = CONTAINER_OF(node, struct eth_bridge_fdb_entry, node);

		if (entry->bridge != bridge ||
		    (iface != NULL && ((entry->flags & ETHERNET_BRIDGE_FDB_FLAG_HW) != 0U ||
				       entry->portmap != bit)) ||
		    (dynamic_only && (entry->flags & ETHERNET_BRIDGE_FDB_FLAG_DYNAMIC) == 0U)) {
			prev = node;
			continue;
		}

		/* prev stays the last kept node */
		fdb_remove(prev, entry);
	}

	k_mutex_unlock(&fdb_lock);
}

bool eth_bridge_fdb_hw_lookup(struct net_if *bridge, const struct net_eth_addr *mac,
			      uint32_t *portmap, bool *host_rx)
{
	struct eth_bridge_fdb_entry *entry;
	bool found = false;

	k_mutex_lock(&fdb_lock, K_FOREVER);

	SYS_SLIST_FOR_EACH_CONTAINER(&fdb_entries, entry, node) {
		if ((entry->flags & ETHERNET_BRIDGE_FDB_FLAG_HW) != 0U &&
		    entry->bridge == bridge &&
		    memcmp(&entry->mac, mac, sizeof(struct net_eth_addr)) == 0) {
			*portmap = entry->portmap;
			*host_rx = entry->host_rx;
			found = true;
			break;
		}
	}

	k_mutex_unlock(&fdb_lock);

	return found;
}
