/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(net_shell);

#include <stdlib.h>
#include <string.h>

#include <zephyr/net/ethernet_switch.h>
#include <zephyr/sys/util.h>

#include "net_shell_private.h"

/* Optional words that follow the port-map of an entry or of a rule */
#define SWITCH_OPT_RECEIVE "receive"
#define SWITCH_OPT_EXT "ext"

static const struct device *switch_get_dev(const struct shell *sh, const char *name)
{
	const struct device *dev = device_get_binding(name);

	if (dev == NULL) {
		PR_WARNING("Switch device %s not found\n", name);
	}

	return dev;
}

static int switch_parse_mac(const struct shell *sh, const char *str, struct net_eth_addr *mac)
{
	if (net_bytes_from_str(mac->addr, sizeof(mac->addr), str) < 0) {
		PR_WARNING("Invalid MAC address %s\n", str);
		return -EINVAL;
	}

	return 0;
}

static int switch_parse_ports(const struct shell *sh, const char *str, uint32_t *ports)
{
	char *end;
	unsigned long val = strtoul(str, &end, 0);

	if (*str == '\0' || *end != '\0') {
		PR_WARNING("Invalid port bitmap %s\n", str);
		return -EINVAL;
	}

	*ports = (uint32_t)val;

	return 0;
}

/* Hex string without prefix, shorter than the buffer is padded with 0 */
static int switch_parse_hex(const struct shell *sh, const char *str, uint8_t *buf, size_t len)
{
	const size_t str_len = strlen(str);

	memset(buf, 0, len);

	if (str_len == 0U || (str_len % 2U) != 0U || (str_len / 2U) > len ||
	    hex2bin(str, str_len, buf, str_len / 2U) != (str_len / 2U)) {
		PR_WARNING("Invalid hex string %s, up to %zu bytes\n", str, len);
		return -EINVAL;
	}

	return 0;
}

/* Words after the port-map, in any order */
static int switch_parse_options(const struct shell *sh, size_t argc, char *argv[], bool *receive,
				bool *ext)
{
	for (size_t i = 0; i < argc; i++) {
		if (strcmp(argv[i], SWITCH_OPT_RECEIVE) == 0) {
			*receive = true;
		} else if (ext != NULL && strcmp(argv[i], SWITCH_OPT_EXT) == 0) {
			*ext = true;
		} else {
			PR_WARNING("Unknown option %s\n", argv[i]);
			return -EINVAL;
		}
	}

	return 0;
}

static int switch_result(const struct shell *sh, int ret)
{
	if (ret == -ENOTSUP) {
		PR_WARNING("Not supported by the switch\n");
	} else if (ret < 0) {
		PR_WARNING("Failed, %d\n", ret);
	}

	return ret;
}

/* net switch entry add <dev> <mac> <ports> [receive] [ext] */
static int cmd_switch_entry_add(const struct shell *sh, size_t argc, char *argv[])
{
	struct ethernet_switch_filtering_entry entry = {
		.vids = ETHERNET_SWITCH_VID_ANY,
	};
	const struct device *dev = switch_get_dev(sh, argv[1]);
	bool receive = false;
	bool ext = false;

	if (dev == NULL || switch_parse_mac(sh, argv[2], &entry.address) < 0 ||
	    switch_parse_ports(sh, argv[3], &entry.port_map.forward) < 0 ||
	    switch_parse_options(sh, argc - 4U, &argv[4], &receive, &ext) < 0) {
		return -EINVAL;
	}

	entry.port_map.receive = receive;
	entry.extended_lookup = ext;

	return switch_result(sh, ethernet_switch_filtering_entry_set(dev, &entry, true));
}

/* net switch entry del <dev> <mac> */
static int cmd_switch_entry_del(const struct shell *sh, size_t argc, char *argv[])
{
	struct ethernet_switch_filtering_entry entry = {
		.vids = ETHERNET_SWITCH_VID_ANY,
	};
	const struct device *dev = switch_get_dev(sh, argv[1]);

	if (dev == NULL || switch_parse_mac(sh, argv[2], &entry.address) < 0) {
		return -EINVAL;
	}

	return switch_result(sh, ethernet_switch_filtering_entry_set(dev, &entry, false));
}

static void switch_entry_print(const struct ethernet_switch_filtering_entry *entry,
			       void *user_data)
{
	const struct shell *sh = user_data;

	PR("%02x:%02x:%02x:%02x:%02x:%02x\tports 0x%x%s\n",
	   entry->address.addr[0], entry->address.addr[1], entry->address.addr[2],
	   entry->address.addr[3], entry->address.addr[4], entry->address.addr[5],
	   entry->port_map.forward, entry->port_map.receive ? " receive" : "");
}

/* net switch entry list <dev> */
static int cmd_switch_entry_list(const struct shell *sh, size_t argc, char *argv[])
{
	const struct device *dev = switch_get_dev(sh, argv[1]);

	if (dev == NULL) {
		return -EINVAL;
	}

	PR("Address\t\t\tPort map\n");

	return switch_result(sh, ethernet_switch_filtering_entry_foreach(dev, switch_entry_print,
									 (void *)sh));
}

static int switch_ext_parse(const struct shell *sh, char *argv[],
			    struct ethernet_switch_ext_lookup *ext)
{
	char *end;
	unsigned long etype = strtoul(argv[2], &end, 0);

	if (*argv[2] == '\0' || *end != '\0' || etype > UINT16_MAX) {
		PR_WARNING("Invalid ethertype %s\n", argv[2]);
		return -EINVAL;
	}

	ext->ethertype = (uint16_t)etype;

	if (switch_parse_hex(sh, argv[3], ext->data, sizeof(ext->data)) < 0 ||
	    switch_parse_hex(sh, argv[4], ext->mask, sizeof(ext->mask)) < 0) {
		return -EINVAL;
	}

	return 0;
}

/* net switch ext add <dev> <ethertype> <data> <mask> <ports> [receive] */
static int cmd_switch_ext_add(const struct shell *sh, size_t argc, char *argv[])
{
	struct ethernet_switch_ext_lookup ext = { 0 };
	const struct device *dev = switch_get_dev(sh, argv[1]);
	bool receive = false;

	if (dev == NULL || switch_ext_parse(sh, argv, &ext) < 0 ||
	    switch_parse_ports(sh, argv[5], &ext.port_map.forward) < 0 ||
	    switch_parse_options(sh, argc - 6U, &argv[6], &receive, NULL) < 0) {
		return -EINVAL;
	}

	ext.port_map.receive = receive;

	return switch_result(sh, ethernet_switch_ext_lookup_set(dev, &ext, true));
}

/* net switch ext del <dev> <ethertype> <data> <mask> */
static int cmd_switch_ext_del(const struct shell *sh, size_t argc, char *argv[])
{
	struct ethernet_switch_ext_lookup ext = { 0 };
	const struct device *dev = switch_get_dev(sh, argv[1]);

	if (dev == NULL || switch_ext_parse(sh, argv, &ext) < 0) {
		return -EINVAL;
	}

	return switch_result(sh, ethernet_switch_ext_lookup_set(dev, &ext, false));
}

/* net switch default <dev> <input ports> <ports> [receive] */
static int cmd_switch_default(const struct shell *sh, size_t argc, char *argv[])
{
	struct ethernet_switch_port_map port_map = { 0 };
	const struct device *dev = switch_get_dev(sh, argv[1]);
	uint32_t input_ports;
	bool receive = false;

	if (dev == NULL || switch_parse_ports(sh, argv[2], &input_ports) < 0 ||
	    switch_parse_ports(sh, argv[3], &port_map.forward) < 0 ||
	    switch_parse_options(sh, argc - 4U, &argv[4], &receive, NULL) < 0) {
		return -EINVAL;
	}

	port_map.receive = receive;

	return switch_result(sh, ethernet_switch_default_port_map_set(dev, input_ports, &port_map));
}

/* net switch port <dev> <index> */
static int cmd_switch_port(const struct shell *sh, size_t argc, char *argv[])
{
	const struct device *dev = switch_get_dev(sh, argv[1]);
	struct net_if *iface;
	char *end;
	unsigned long idx = strtoul(argv[2], &end, 0);

	if (dev == NULL) {
		return -EINVAL;
	}

	if (*argv[2] == '\0' || *end != '\0' || idx > UINT8_MAX) {
		PR_WARNING("Invalid port index %s\n", argv[2]);
		return -EINVAL;
	}

	iface = ethernet_switch_get_port_iface(dev, (uint8_t)idx);
	if (iface == NULL) {
		PR_WARNING("Port %lu has no interface\n", idx);
		return -ENOENT;
	}

	PR("Port %lu: interface %d (%p)\n", idx, net_if_get_by_iface(iface), iface);

	return 0;
}

static int cmd_switch(const struct shell *sh, size_t argc, char *argv[])
{
	PR("Usage:\n");
	PR("\tswitch entry add <dev> <mac> <ports> [receive] [ext]\n");
	PR("\tswitch entry del <dev> <mac>\n");
	PR("\tswitch entry list <dev>\n");
	PR("\tswitch ext add <dev> <ethertype> <data> <mask> <ports> [receive]\n");
	PR("\tswitch ext del <dev> <ethertype> <data> <mask>\n");
	PR("\tswitch default <dev> <input ports> <ports> [receive]\n");
	PR("\tswitch port <dev> <index>\n");
	PR("<ports> and <input ports> are bitmaps with a bit per port index.\n");

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(net_cmd_switch_entry,
	SHELL_CMD_ARG(add, NULL,
		      SHELL_HELP("Add or replace a filtering entry",
				 "<dev> <mac> <ports> [receive] [ext]"),
		      cmd_switch_entry_add, 4, 2),
	SHELL_CMD_ARG(del, NULL,
		      SHELL_HELP("Remove a filtering entry", "<dev> <mac>"),
		      cmd_switch_entry_del, 3, 0),
	SHELL_CMD_ARG(list, NULL,
		      SHELL_HELP("List the filtering entries", "<dev>"),
		      cmd_switch_entry_list, 2, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(net_cmd_switch_ext,
	SHELL_CMD_ARG(add, NULL,
		      SHELL_HELP("Add or replace an extended lookup rule",
				 "<dev> <ethertype> <data> <mask> <ports> [receive]"),
		      cmd_switch_ext_add, 6, 1),
	SHELL_CMD_ARG(del, NULL,
		      SHELL_HELP("Remove an extended lookup rule",
				 "<dev> <ethertype> <data> <mask>"),
		      cmd_switch_ext_del, 5, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(net_cmd_switch,
	SHELL_CMD(entry, &net_cmd_switch_entry, "Filtering entries", NULL),
	SHELL_CMD(ext, &net_cmd_switch_ext, "Extended lookup rules", NULL),
	SHELL_CMD_ARG(default, NULL,
		      SHELL_HELP("Set the port map of the frames no entry matches",
				 "<dev> <input ports> <ports> [receive]"),
		      cmd_switch_default, 4, 1),
	SHELL_CMD_ARG(port, NULL,
		      SHELL_HELP("Show the interface of a port", "<dev> <index>"),
		      cmd_switch_port, 3, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_SUBCMD_ADD((net), switch, &net_cmd_switch,
		 "Ethernet switch configuration.",
		 cmd_switch, 1, 0);
