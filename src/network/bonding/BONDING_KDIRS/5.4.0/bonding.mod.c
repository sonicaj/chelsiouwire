#include <linux/build-salt.h>
#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(.gnu.linkonce.this_module) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

MODULE_INFO(intree, "Y");

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used __section(__versions) = {
	{ 0xaee5dd69, "module_layout" },
	{ 0xc0c9638f, "register_netdevice" },
	{ 0xaf0d0b9d, "dev_mc_sync_multiple" },
	{ 0x93c20051, "kobject_put" },
	{ 0x5951a741, "netdev_info" },
	{ 0x4a24d174, "kmalloc_caches" },
	{ 0xeb233a45, "__kmalloc" },
	{ 0x860f01e1, "dev_mc_unsync" },
	{ 0x349cba85, "strchr" },
	{ 0xf35d38ec, "proc_create_seq_private" },
	{ 0x8d7f9e17, "param_ops_int" },
	{ 0x754d539c, "strlen" },
	{ 0x4817129, "dev_disable_lro" },
	{ 0x19f462ab, "kfree_call_rcu" },
	{ 0xde9b9408, "vlan_dev_vlan_id" },
	{ 0xd315d3a8, "__skb_flow_dissect" },
	{ 0x79aa04a2, "get_random_bytes" },
	{ 0xa89d8928, "seq_puts" },
	{ 0xe197e832, "netdev_rx_handler_register" },
	{ 0xc7a4fbed, "rtnl_lock" },
	{ 0xd5a1f5d7, "vlan_uses_dev" },
	{ 0xfa690589, "netdev_cmd_to_name" },
	{ 0x368afda6, "netif_carrier_on" },
	{ 0x58443e98, "dst_release" },
	{ 0xb3635b01, "_raw_spin_lock_bh" },
	{ 0x62d3d9f4, "skb_clone" },
	{ 0xffeedf6a, "delayed_work_timer_fn" },
	{ 0xbc3bdc7f, "flow_get_u32_src" },
	{ 0xc601fa37, "seq_printf" },
	{ 0xd2da1048, "register_netdevice_notifier" },
	{ 0x9af37447, "netif_carrier_off" },
	{ 0x56470118, "__warn_printk" },
	{ 0xc2ee38c6, "netdev_master_upper_dev_get" },
	{ 0xa1089f32, "remove_proc_entry" },
	{ 0x837b7b09, "__dynamic_pr_debug" },
	{ 0x47a40d24, "dev_set_allmulti" },
	{ 0xf7f1b81c, "vlan_vid_del" },
	{ 0x5172eb9f, "netpoll_poll_dev" },
	{ 0x8ba1525, "call_netdevice_notifiers" },
	{ 0xbc54b88c, "__dev_kfree_skb_any" },
	{ 0xc6f46339, "init_timer_key" },
	{ 0x9fa7184a, "cancel_delayed_work_sync" },
	{ 0xf3c1ecc5, "vlan_vid_add" },
	{ 0x3520721e, "__netpoll_setup" },
	{ 0x5bb98c5e, "vlan_vids_del_by_dev" },
	{ 0x3c3ff9fd, "sprintf" },
	{ 0x8452c13b, "pv_ops" },
	{ 0xb809fb16, "netdev_walk_all_upper_dev_rcu" },
	{ 0x15ba50a6, "jiffies" },
	{ 0xbdd77179, "__dynamic_netdev_dbg" },
	{ 0x9d0d6206, "unregister_netdevice_notifier" },
	{ 0xbc0c91cb, "skb_trim" },
	{ 0xe2d5255a, "strcmp" },
	{ 0x85162720, "vlan_vids_add_by_dev" },
	{ 0x22ef4a46, "netdev_master_upper_dev_link" },
	{ 0xf6342c8b, "dev_mc_add" },
	{ 0xf9a011c1, "__netdev_alloc_skb" },
	{ 0x9132c624, "netdev_lower_get_next_private_rcu" },
	{ 0x4d7fc13f, "netdev_lower_state_changed" },
	{ 0x760f2e30, "__pskb_pull_tail" },
	{ 0x530b00f2, "netdev_change_features" },
	{ 0x86fcad4, "netpoll_send_skb_on_dev" },
	{ 0x6b10bee1, "_copy_to_user" },
	{ 0x8bdbb23f, "PDE_DATA" },
	{ 0x1f6ccde8, "netdev_has_upper_dev" },
	{ 0xf1db1704, "nla_memcpy" },
	{ 0x619877de, "param_ops_charp" },
	{ 0xd382c48, "dev_set_mac_address" },
	{ 0x448821dd, "unregister_pernet_subsys" },
	{ 0x8ce64cfd, "proc_mkdir" },
	{ 0x9fdecc31, "unregister_netdevice_many" },
	{ 0x11089ac7, "_ctype" },
	{ 0xc025016c, "flow_keys_dissector" },
	{ 0x19a11659, "current_task" },
	{ 0xf0c26165, "__ethtool_get_link_ksettings" },
	{ 0x45ba026d, "arp_create" },
	{ 0xc5850110, "printk" },
	{ 0xfb66f6b4, "ethtool_op_get_link" },
	{ 0xbcab6ee6, "sscanf" },
	{ 0xe1537255, "__list_del_entry_valid" },
	{ 0xa965ca81, "reciprocal_value" },
	{ 0xe0e3cea6, "ns_capable" },
	{ 0x9cf230cf, "kobject_init_and_add" },
	{ 0x62849ac7, "dev_valid_name" },
	{ 0x198a797e, "netdev_class_remove_file_ns" },
	{ 0xcfc1a03c, "free_netdev" },
	{ 0xe7b00dfb, "__x86_indirect_thunk_r13" },
	{ 0x9166fada, "strncpy" },
	{ 0x460ce1ff, "dev_mc_del" },
	{ 0x20b2a6ce, "nla_put" },
	{ 0x98cc67c1, "netdev_upper_dev_unlink" },
	{ 0x5a921311, "strncmp" },
	{ 0x5792f848, "strlcpy" },
	{ 0xbc3362a1, "skb_push" },
	{ 0x652032cb, "mac_pton" },
	{ 0x8c03d20c, "destroy_workqueue" },
	{ 0x8da0fc7c, "dev_close" },
	{ 0xf4f14de6, "rtnl_trylock" },
	{ 0xcd65b05b, "netdev_bonding_info_change" },
	{ 0xdfd55ecb, "dev_mc_flush" },
	{ 0xfda9581f, "prandom_u32" },
	{ 0x6091797f, "synchronize_rcu" },
	{ 0x361bca87, "inet_confirm_addr" },
	{ 0x9ab681a5, "init_net" },
	{ 0x7f3e62f3, "rtnl_link_unregister" },
	{ 0x7b2b1090, "__dev_get_by_index" },
	{ 0x68f31cbd, "__list_add_valid" },
	{ 0x6cde2296, "netdev_lower_dev_get_private" },
	{ 0x9eacf8a5, "kstrndup" },
	{ 0x59d242ba, "dev_open" },
	{ 0xa9dbdd62, "dev_uc_flush" },
	{ 0xc6cbbc89, "capable" },
	{ 0xb601be4c, "__x86_indirect_thunk_rdx" },
	{ 0xc9ed1a3d, "netdev_upper_get_next_dev_rcu" },
	{ 0x680ac323, "sysfs_remove_file_ns" },
	{ 0x49c41a57, "_raw_spin_unlock_bh" },
	{ 0xb2fcb56d, "queue_delayed_work_on" },
	{ 0xdecd0b29, "__stack_chk_fail" },
	{ 0x79e50ab4, "vlan_dev_vlan_proto" },
	{ 0xbfecf4dc, "netdev_rx_handler_unregister" },
	{ 0x1d24c881, "___ratelimit" },
	{ 0xb8b9f817, "kmalloc_order_trace" },
	{ 0x7187d2d4, "kfree_skb" },
	{ 0xac5fcec0, "in4_pton" },
	{ 0x4aea20cf, "passthru_features_check" },
	{ 0xd5a9684d, "alloc_netdev_mqs" },
	{ 0x2ea2c95c, "__x86_indirect_thunk_rax" },
	{ 0x6bd0f787, "arp_xmit" },
	{ 0x9a58125, "netdev_lower_get_next_private" },
	{ 0x290060af, "register_pernet_subsys" },
	{ 0x3674f82a, "pskb_expand_head" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0x705704fd, "netdev_err" },
	{ 0x112fd9cb, "ether_setup" },
	{ 0xfa0fb039, "dev_uc_unsync" },
	{ 0xc6a4f3e6, "__dev_get_by_name" },
	{ 0x7bd7a1df, "kmem_cache_alloc_trace" },
	{ 0xdbf17652, "_raw_spin_lock" },
	{ 0xc5042aa1, "unregister_netdevice_queue" },
	{ 0xdf174df0, "ip_route_output_flow" },
	{ 0xf6ebc03b, "net_ratelimit" },
	{ 0x7e2827e9, "netdev_warn" },
	{ 0xd8cf57a9, "__skb_flow_get_ports" },
	{ 0x65e41b95, "dev_set_promiscuity" },
	{ 0x8516c14a, "flow_get_u32_dst" },
	{ 0x37a0cba, "kfree" },
	{ 0xc75e994b, "dev_uc_sync_multiple" },
	{ 0x69acdf38, "memcpy" },
	{ 0x1c6b048f, "param_array_ops" },
	{ 0x3a8bd2b, "dev_trans_start" },
	{ 0x5dbe726b, "__dev_set_mtu" },
	{ 0x2bb7fcb8, "rtnl_link_register" },
	{ 0xdc919464, "dev_uc_sync" },
	{ 0x23bcdd0f, "netdev_lower_get_first_private_rcu" },
	{ 0xa0dad88e, "netdev_adjacent_get_private" },
	{ 0xca81d5b7, "nla_put_64bit" },
	{ 0x24c137ee, "__netpoll_free" },
	{ 0x656e4a6e, "snprintf" },
	{ 0xb0e602eb, "memmove" },
	{ 0x6dde072b, "consume_skb" },
	{ 0x85670f1d, "rtnl_is_locked" },
	{ 0x7f02188f, "__msecs_to_jiffies" },
	{ 0x406c0f94, "sysfs_create_file_ns" },
	{ 0x3b28e685, "dev_queue_xmit" },
	{ 0x6d26b36b, "netdev_is_rx_handler_busy" },
	{ 0xc64e196c, "skb_put" },
	{ 0x13c49cc2, "_copy_from_user" },
	{ 0x9d93c9d0, "param_ops_uint" },
	{ 0x43b4a8b, "skb_copy_bits" },
	{ 0x54f3954a, "dev_mc_sync" },
	{ 0xdf9208c0, "alloc_workqueue" },
	{ 0x25bf910d, "dev_pre_changeaddr_notify" },
	{ 0x6e720ff2, "rtnl_unlock" },
	{ 0x69668826, "netdev_increment_features" },
	{ 0xe75af167, "dev_get_stats" },
	{ 0xd578121c, "netdev_class_create_file_ns" },
	{ 0xa0d84bb4, "dev_set_mtu" },
	{ 0xe914e41e, "strcpy" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "2066880D7BD17AA9529FC97");