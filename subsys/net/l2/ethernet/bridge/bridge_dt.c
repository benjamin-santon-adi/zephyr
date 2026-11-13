/*
 * SPDX-License-Identifier: Apache-2.0
 */

/* Bridges created from the devicetree, see zephyr,ethernet-bridge */

#define DT_DRV_COMPAT zephyr_ethernet_bridge

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(net_eth_bridge, CONFIG_NET_ETHERNET_BRIDGE_LOG_LEVEL);

#include <zephyr/net/ethernet_bridge.h>
#include <zephyr/net/net_if.h>

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) <= CONFIG_NET_ETHERNET_BRIDGE_COUNT,
	     "More bridges in the devicetree than CONFIG_NET_ETHERNET_BRIDGE_COUNT");

struct bridge_dt_config {
	/* Devices whose interface is a port */
	const struct device *const *ports;
	size_t num_ports;
	/* Address of the bridge, the one of the first port when not set */
	uint8_t mac[NET_ETH_ADDR_LEN];
	bool has_mac;
};

static void bridge_dt_find_free(struct eth_bridge_iface_context *br, void *user_data)
{
	struct net_if **free_br = user_data;

	if (*free_br == NULL && br->count == 0U) {
		*free_br = br->iface;
	}
}

static int bridge_dt_setup(const struct bridge_dt_config *config)
{
	struct net_if *br = NULL;
	int ret;

	net_eth_bridge_foreach(bridge_dt_find_free, &br);
	if (br == NULL) {
		LOG_ERR("No free bridge interface, see NET_ETHERNET_BRIDGE_COUNT");
		return -ENOENT;
	}

	for (size_t i = 0U; i < config->num_ports; i++) {
		if (!device_is_ready(config->ports[i]) ||
		    net_if_lookup_by_dev(config->ports[i]) == NULL) {
			LOG_ERR("Port %s has no network interface", config->ports[i]->name);
			return -ENODEV;
		}
	}

	if (config->has_mac) {
		ret = net_if_set_link_addr(br, (uint8_t *)config->mac, sizeof(config->mac),
					   NET_LINK_ETHERNET);
	} else {
		struct net_linkaddr *port_addr =
			net_if_get_link_addr(net_if_lookup_by_dev(config->ports[0]));

		ret = net_if_set_link_addr(br, port_addr->addr, port_addr->len, port_addr->type);
	}

	if (ret < 0) {
		LOG_ERR("Failed to set the address of bridge %d, %d", net_if_get_by_iface(br), ret);
		return ret;
	}

	for (size_t i = 0U; i < config->num_ports; i++) {
		ret = eth_bridge_iface_add(br, net_if_lookup_by_dev(config->ports[i]));
		if (ret < 0) {
			LOG_ERR("Failed to add port %s to bridge %d, %d", config->ports[i]->name,
				net_if_get_by_iface(br), ret);
			return ret;
		}
	}

	/* The bridge is configured and up, so it can be used like any interface that starts by
	 * itself. NET_CONFIG only uses the first of them.
	 */
	net_if_flag_clear(br, NET_IF_NO_AUTO_START);

	return net_if_up(br);
}

#define BRIDGE_DT_PORT(node_id, prop, idx) DEVICE_DT_GET(DT_PHANDLE_BY_IDX(node_id, prop, idx))

#define BRIDGE_DT_INIT(inst)									\
	static const struct device *const bridge_dt_ports_##inst[] = {				\
		DT_INST_FOREACH_PROP_ELEM_SEP(inst, ports, BRIDGE_DT_PORT, (,))			\
	};											\
	BUILD_ASSERT(ARRAY_SIZE(bridge_dt_ports_##inst) >= 2,					\
		     "A bridge needs at least two ports");					\
	static const struct bridge_dt_config bridge_dt_config_##inst = {			\
		.ports = bridge_dt_ports_##inst,						\
		.num_ports = ARRAY_SIZE(bridge_dt_ports_##inst),				\
		.mac = DT_INST_PROP_OR(inst, local_mac_address, {0}),				\
		.has_mac = DT_INST_NODE_HAS_PROP(inst, local_mac_address),			\
	};

DT_INST_FOREACH_STATUS_OKAY(BRIDGE_DT_INIT)

#define BRIDGE_DT_SETUP(inst) (void)bridge_dt_setup(&bridge_dt_config_##inst);

/* Called by the network stack once the interfaces are initialized, the ports and the bridges,
 * and before they are brought up. The errors are logged.
 */
void eth_bridge_dt_init(void)
{
	DT_INST_FOREACH_STATUS_OKAY(BRIDGE_DT_SETUP)
}
