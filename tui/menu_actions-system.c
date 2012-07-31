/*
 * $Id$
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <cdk.h>
#include <cdk/cdk_objs.h>
#include <iniparser.h>
#include <cdk/radio.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <string.h>
#include <arpa/inet.h>
#include <net/if_arp.h>
#include <linux/sockios.h>
#include <linux/ethtool.h>
#include <cdk/swindow.h>

#include "menu_actions-system.h"
#include "menu_actions.h"
#include "main.h"

/*
 * Run the Networking dialog
 * Example code for getting available network interfaces taken from here:
 * http://www.adamrisi.com/?p=84
 */
void networkDialog(CDKSCREEN *main_cdk_screen) {
    CDKSCROLL *net_conf_list = 0;
    CDKSCREEN *net_screen = 0;
    CDKLABEL *net_label = 0, *short_label = 0;
    CDKENTRY *host_name = 0, *domain_name = 0, *default_gw = 0,
            *name_server_1 = 0, *name_server_2 = 0, *ip_addy = 0, *netmask = 0,
            *broadcast = 0;
    CDKRADIO *ip_config = 0;
    CDKBUTTON *ok_button = 0, *cancel_button = 0;
    WINDOW *net_window = 0;
    tButtonCallback ok_cb = &okButtonCB, cancel_cb = &cancelButtonCB;
    dictionary *ini_dict = NULL;
    FILE *ini_file = NULL;
    struct ifreq ifr = {0};
    struct if_nameindex* if_name = NULL;
    struct ethtool_cmd edata = {0};
    unsigned char* mac_addy = NULL;
    int sock = 0, i = 0, j = 0, net_conf_choice = 0, window_y = 0, window_x = 0,
            traverse_ret = 0;
    int net_window_lines = 16, net_window_cols = 70;
    char *net_scroll_msg[MAX_NET_IFACE] = {NULL},
            *net_if_name[MAX_NET_IFACE] = {NULL},
            *net_if_mac[MAX_NET_IFACE] = {NULL},
            *net_if_speed[MAX_NET_IFACE] = {NULL},
            *net_if_duplex[MAX_NET_IFACE] = {NULL}, *net_info_msg[10] = {NULL},
            *short_label_msg[1] = {NULL};
    char *conf_hostname = NULL, *conf_domainname = NULL, *conf_defaultgw = NULL,
            *conf_nameserver1 = NULL, *conf_nameserver2 = NULL, *conf_bootproto = NULL,
            *conf_ipaddr = NULL, *conf_netmask = NULL, *conf_broadcast = NULL, *error_msg = NULL;
    static char *ip_opts[] = {"Disabled", "Static", "DHCP"};
    char eth_duplex[20] = {0}, temp_str[100] = {0}, temp_ini_str[MAX_INI_VAL] = {0};
    __be16 eth_speed = 0;
    boolean question = FALSE;

    /* Get socket handle */
    sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        errorDialog(main_cdk_screen, "Couldn't get socket handle!", NULL);
        return;
    }

    /* Get all network interface names */
    if_name = if_nameindex();

    /* We set the counter ahead since the first row is for general settings */
    j = 1;
    asprintf(&net_scroll_msg[0], "<C>General Network Settings");
    
    while ((if_name[i].if_index != 0) && (if_name[i].if_name != NULL)) {
        /* Put the interface name into the ifreq struct */
        memcpy(&ifr.ifr_name, if_name[i].if_name, IFNAMSIZ);

        /* Get interface flags */
        if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) {
            errorDialog(main_cdk_screen, "ioctl: SIOCGIFFLAGS", NULL);
            return;
        }

        /* We don't want to include the loopback interface */
        if (!(ifr.ifr_flags & IFF_LOOPBACK)) {

            /* Get interface hardware address (MAC) */
            if (ioctl(sock, SIOCGIFHWADDR, &ifr) < 0) {
                errorDialog(main_cdk_screen, "ioctl: SIOCGIFHWADDR", NULL);
                return;
            }

            /* We only want Ethernet interfaces */
            if (ifr.ifr_hwaddr.sa_family == ARPHRD_ETHER) {
                mac_addy = (unsigned char*) &ifr.ifr_hwaddr.sa_data;
                asprintf(&net_if_name[j], "%s", if_name[i].if_name);
                asprintf(&net_if_mac[j], "%02X:%02X:%02X:%02X:%02X:%02X",
                        mac_addy[0],mac_addy[1],mac_addy[2],mac_addy[3],mac_addy[4],mac_addy[5]);

                /* Get additional Ethernet interface information */
                ifr.ifr_data = (caddr_t) &edata;
                edata.cmd = ETHTOOL_GSET;
                if (ioctl(sock, SIOCETHTOOL, &ifr) < 0) {
                    errorDialog(main_cdk_screen, "ioctl: SIOCETHTOOL", NULL);
                    return;
                }

                /* Get speed of Ethernet link; we use the returned speed value
                 * to determine the link status -- probably not the best
                 * solution long term, but its easy for now */
                eth_speed = ethtool_cmd_speed(&edata);
                if (eth_speed == 0 || eth_speed == (__be16)(-1) || eth_speed == (__be32)(-1)) {
                    asprintf(&net_scroll_msg[j], "<C>%s (%s - No Link)",
                            net_if_name[j], net_if_mac[j]);
                } else {
                    switch (edata.duplex) {
                        case DUPLEX_HALF:
                            snprintf(eth_duplex, 20, "Half Duplex");
                            break;
                        case DUPLEX_FULL:
                            snprintf(eth_duplex, 20, "Full Duplex");
                            break;
                        default:
                            snprintf(eth_duplex, 20, "Unknown Duplex");
                            break;
                    }
                    asprintf(&net_if_speed[j], "%u Mb/s", eth_speed);
                    asprintf(&net_if_duplex[j], "%s", eth_duplex);
                    asprintf(&net_scroll_msg[j], "<C>%s (%s - %s - %s)",
                            net_if_name[j], net_if_mac[j], net_if_speed[j], net_if_duplex[j]);
                }
                j++;
            }
        }
        i++;
    }

    /* Clean up */
    if_freenameindex(if_name);

    /* Scroll widget for network configuration choices */
    net_conf_list = newCDKScroll(main_cdk_screen, CENTER, CENTER, NONE, 15, 60,
            "<C></31/B>Choose a network configuration option:\n",
            net_scroll_msg, j, FALSE, COLOR_DIALOG_SELECT, TRUE, FALSE);
    if (!net_conf_list) {
        errorDialog(main_cdk_screen, "Couldn't create scroll widget!", NULL);
        goto cleanup;
    }
    setCDKScrollBoxAttribute(net_conf_list, COLOR_DIALOG_BOX);
    setCDKScrollBackgroundAttrib(net_conf_list, COLOR_DIALOG_TEXT);
    net_conf_choice = activateCDKScroll(net_conf_list, 0);

    /* Check exit from widget */
    if (net_conf_list->exitType == vESCAPE_HIT) {
        destroyCDKScroll(net_conf_list);
        refreshCDKScreen(main_cdk_screen);
        goto cleanup;

    } else if (net_conf_list->exitType == vNORMAL) {
        destroyCDKScroll(net_conf_list);
        refreshCDKScreen(main_cdk_screen);
    }

    /* Setup a new CDK screen for one of two network dialogs below */
    window_y = ((LINES / 2) - (net_window_lines / 2));
    window_x = ((COLS / 2) - (net_window_cols / 2));
    net_window = newwin(net_window_lines, net_window_cols,
            window_y, window_x);
    if (net_window == NULL) {
        errorDialog(main_cdk_screen, "Couldn't create new window!", NULL);
        goto cleanup;
    }
    net_screen = initCDKScreen(net_window);
    if (net_screen == NULL) {
        errorDialog(main_cdk_screen, "Couldn't create new CDK screen!", NULL);
        goto cleanup;
    }
    boxWindow(net_window, COLOR_DIALOG_BOX);
    wbkgd(net_window, COLOR_DIALOG_TEXT);
    wrefresh(net_window);

    if (net_conf_choice == 0) {
        /* Present the 'general network settings' screen of widgets */
        asprintf(&net_info_msg[0], "</31/B>General Network Settings");
        asprintf(&net_info_msg[1], " ");

        /* Read network configuration file (INI file) */
        ini_dict = iniparser_load(NETWORK_CONF);
        if (ini_dict == NULL) {
            errorDialog(main_cdk_screen, "Cannot parse network configuration file!", NULL);
            return;
        }
        conf_hostname = iniparser_getstring(ini_dict, "general:hostname", "");
        conf_domainname = iniparser_getstring(ini_dict, "general:domainname", "");
        conf_defaultgw = iniparser_getstring(ini_dict, "general:defaultgw", "");
        conf_nameserver1 = iniparser_getstring(ini_dict, "general:nameserver1", "");
        conf_nameserver2 = iniparser_getstring(ini_dict, "general:nameserver2", "");
        
        /* Information label */
        net_label = newCDKLabel(net_screen, (window_x + 1), (window_y + 1),
                net_info_msg, 2, FALSE, FALSE);
        if (!net_label) {
            errorDialog(main_cdk_screen, "Couldn't create label widget!", NULL);
            goto cleanup;
        }
        setCDKLabelBackgroundAttrib(net_label, COLOR_DIALOG_TEXT);

        /* Host name field */
        host_name = newCDKEntry(net_screen, (window_x + 1), (window_y + 3),
                NULL, "</B>System host name: ",
                COLOR_DIALOG_SELECT, '_' | COLOR_DIALOG_INPUT, vMIXED,
                MAX_HOSTNAME, 0, MAX_HOSTNAME, FALSE, FALSE);
        if (!host_name) {
            errorDialog(main_cdk_screen, "Couldn't create entry widget!", NULL);
            goto cleanup;
        }
        setCDKEntryBoxAttribute(host_name, COLOR_DIALOG_INPUT);
        setCDKEntryValue(host_name, conf_hostname);

        /* Domain name field */
        domain_name = newCDKEntry(net_screen, (window_x + 1), (window_y + 5),
                NULL, "</B>System domain name: ",
                COLOR_DIALOG_SELECT, '_' | COLOR_DIALOG_INPUT, vMIXED,
                MAX_DOMAINNAME, 0, MAX_DOMAINNAME, FALSE, FALSE);
        if (!domain_name) {
            errorDialog(main_cdk_screen, "Couldn't create entry widget!", NULL);
            goto cleanup;
        }
        setCDKEntryBoxAttribute(domain_name, COLOR_DIALOG_INPUT);
        setCDKEntryValue(domain_name, conf_domainname);

        /* Default gateway field */
        default_gw = newCDKEntry(net_screen, (window_x + 1), (window_y + 7),
                NULL, "</B>Default gateway (leave blank if using DHCP): ",
                COLOR_DIALOG_SELECT, '_' | COLOR_DIALOG_INPUT, vMIXED,
                MAX_IPV4_ADDR_LEN, 0, MAX_IPV4_ADDR_LEN, FALSE, FALSE);
        if (!default_gw) {
            errorDialog(main_cdk_screen, "Couldn't create entry widget!", NULL);
            goto cleanup;
        }
        setCDKEntryBoxAttribute(default_gw, COLOR_DIALOG_INPUT);
        setCDKEntryValue(default_gw, conf_defaultgw);

        /* A very small label for instructions */
        asprintf(&short_label_msg[0], "</B>Name Servers (leave blank if using DHCP)");
        short_label = newCDKLabel(net_screen, (window_x + 1), (window_y + 9),
                short_label_msg, 1, FALSE, FALSE);
        if (!short_label) {
            errorDialog(main_cdk_screen, "Couldn't create label widget!", NULL);
            goto cleanup;
        }
        setCDKLabelBackgroundAttrib(short_label, COLOR_DIALOG_TEXT);
        
        /* Primary name server field */
        name_server_1 = newCDKEntry(net_screen, (window_x + 1), (window_y + 10),
                NULL, "</B>NS 1: ",
                COLOR_DIALOG_SELECT, '_' | COLOR_DIALOG_INPUT, vMIXED,
                MAX_IPV4_ADDR_LEN, 0, MAX_IPV4_ADDR_LEN, FALSE, FALSE);
        if (!name_server_1) {
            errorDialog(main_cdk_screen, "Couldn't create entry widget!", NULL);
            goto cleanup;
        }
        setCDKEntryBoxAttribute(name_server_1, COLOR_DIALOG_INPUT);
        setCDKEntryValue(name_server_1, conf_nameserver1);

        /* Secondary name server field */
        name_server_2 = newCDKEntry(net_screen, (window_x + 1), (window_y + 11),
                NULL, "</B>NS 2: ",
                COLOR_DIALOG_SELECT, '_' | COLOR_DIALOG_INPUT, vMIXED,
                MAX_IPV4_ADDR_LEN, 0, MAX_IPV4_ADDR_LEN, FALSE, FALSE);
        if (!name_server_2) {
            errorDialog(main_cdk_screen, "Couldn't create entry widget!", NULL);
            goto cleanup;
        }
        setCDKEntryBoxAttribute(name_server_2, COLOR_DIALOG_INPUT);
        setCDKEntryValue(name_server_2, conf_nameserver2);

        /* Buttons */
        ok_button = newCDKButton(net_screen, (window_x + 26), (window_y + 14),
                "</B>   OK   ", ok_cb, FALSE, FALSE);
        if (!ok_button) {
            errorDialog(main_cdk_screen, "Couldn't create button widget!", NULL);
            goto cleanup;
        }
        setCDKButtonBackgroundAttrib(ok_button, COLOR_DIALOG_INPUT);
        cancel_button = newCDKButton(net_screen, (window_x + 36), (window_y + 14),
                "</B> Cancel ", cancel_cb, FALSE, FALSE);
        if (!cancel_button) {
            errorDialog(main_cdk_screen, "Couldn't create button widget!", NULL);
            goto cleanup;
        }
        setCDKButtonBackgroundAttrib(cancel_button, COLOR_DIALOG_INPUT);

        /* Allow user to traverse the screen */
        refreshCDKScreen(net_screen);
        traverse_ret = traverseCDKScreen(net_screen);

        /* User hit 'OK' button */
        if (traverse_ret == 1) {
            /* Check the field entry widgets for spaces */
            for (i = 0; i < net_screen->objectCount; i++) {
                CDKOBJS *obj = net_screen->object[i];
                if (obj != 0 && ObjTypeOf(obj) == vENTRY) {
                    strncpy(temp_str, getCDKEntryValue((CDKENTRY *) obj), 100);
                    j = 0;
                    while (temp_str[j] != '\0') {
                        if (isspace(temp_str[j])) {
                            errorDialog(main_cdk_screen, "The entry fields cannot contain any spaces!", NULL);
                            goto cleanup;
                        }
                        j++;
                    }
                }
            }

            /* Write to network config. file */
            if (iniparser_set(ini_dict, "general", NULL) == -1) {
                errorDialog(main_cdk_screen, "Couldn't set configuration file value!", NULL);
                goto cleanup;
            }
            if (iniparser_set(ini_dict, "general:hostname", getCDKEntryValue(host_name)) == -1) {
                errorDialog(main_cdk_screen, "Couldn't set configuration file value!", NULL);
                goto cleanup;
            }
            if (iniparser_set(ini_dict, "general:domainname", getCDKEntryValue(domain_name)) == -1) {
                errorDialog(main_cdk_screen, "Couldn't set configuration file value!", NULL);
                goto cleanup;
            }
            if (iniparser_set(ini_dict, "general:defaultgw", getCDKEntryValue(default_gw)) == -1) {
                errorDialog(main_cdk_screen, "Couldn't set configuration file value!", NULL);
                goto cleanup;
            }
            if (iniparser_set(ini_dict, "general:nameserver1", getCDKEntryValue(name_server_1)) == -1) {
                errorDialog(main_cdk_screen, "Couldn't set configuration file value!", NULL);
                goto cleanup;
            }
            if (iniparser_set(ini_dict, "general:nameserver2", getCDKEntryValue(name_server_2)) == -1) {
                errorDialog(main_cdk_screen, "Couldn't set configuration file value!", NULL);
                goto cleanup;
            }
            ini_file = fopen(NETWORK_CONF, "w");
            if (ini_file == NULL) {
                asprintf(&error_msg, "Couldn't open network config. file for writing: %s", strerror(errno));
                errorDialog(main_cdk_screen, error_msg, NULL);
                freeChar(error_msg);
            } else {
                fprintf(ini_file, "# ESOS network configuration file\n");
                fprintf(ini_file, "# This file is generated by esos_tui; do not edit\n\n");
                iniparser_dump_ini(ini_dict, ini_file);
                fclose(ini_file);
                iniparser_freedict(ini_dict);
            }
        }

    } else {
        /* Present the screen for a specific network interface configuration */
        asprintf(&net_info_msg[0], "</31/B>Configuring interface %s...",
                net_if_name[net_conf_choice]);
        asprintf(&net_info_msg[1], " ");
        asprintf(&net_info_msg[2], "MAC Address: %s", net_if_mac[net_conf_choice]);
        if (net_if_speed[net_conf_choice] == NULL)
            asprintf(&net_info_msg[3], "Link Status: None");
        else
            asprintf(&net_info_msg[3], "Link Status: %s, %s",
                    net_if_speed[net_conf_choice], net_if_duplex[net_conf_choice]);

        /* Read network configuration file (INI file) */
        ini_dict = iniparser_load(NETWORK_CONF);
        if (ini_dict == NULL) {
            errorDialog(main_cdk_screen, "Cannot parse network configuration file!", NULL);
            return;
        }
        snprintf(temp_ini_str, MAX_INI_VAL, "%s:bootproto", net_if_name[net_conf_choice]);
        conf_bootproto = iniparser_getstring(ini_dict, temp_ini_str, "");
        snprintf(temp_ini_str, MAX_INI_VAL, "%s:ipaddr", net_if_name[net_conf_choice]);
        conf_ipaddr = iniparser_getstring(ini_dict, temp_ini_str, "");
        snprintf(temp_ini_str, MAX_INI_VAL, "%s:netmask", net_if_name[net_conf_choice]);
        conf_netmask = iniparser_getstring(ini_dict, temp_ini_str, "");
        snprintf(temp_ini_str, MAX_INI_VAL, "%s:broadcast", net_if_name[net_conf_choice]);
        conf_broadcast = iniparser_getstring(ini_dict, temp_ini_str, "");

        /* Information label */
        net_label = newCDKLabel(net_screen, (window_x + 1), (window_y + 1),
                net_info_msg, 4, FALSE, FALSE);
        if (!net_label) {
            errorDialog(main_cdk_screen, "Couldn't create label widget!", NULL);
            goto cleanup;
        }
        setCDKLabelBackgroundAttrib(net_label, COLOR_DIALOG_TEXT);

        /* IP settings radio */
        ip_config = newCDKRadio(net_screen, (window_x + 1), (window_y + 6),
                NONE, 5, 10, "</B>IP Settings:", ip_opts, 3,
                '#' | COLOR_DIALOG_SELECT, 1,
                COLOR_DIALOG_SELECT, FALSE, FALSE);
        if (!ip_config) {
            errorDialog(main_cdk_screen, "Couldn't create radio widget!", NULL);
            goto cleanup;
        }
        setCDKRadioBackgroundAttrib(ip_config, COLOR_DIALOG_TEXT);
        if (strcasecmp(conf_bootproto, ip_opts[1]) == 0)
            setCDKRadioCurrentItem(ip_config, 1);
        else if (strcasecmp(conf_bootproto, ip_opts[2]) == 0)
            setCDKRadioCurrentItem(ip_config, 2);
        else
            setCDKRadioCurrentItem(ip_config, 0);

        /* A very small label for instructions */
        asprintf(&short_label_msg[0], "</B>Static IP Settings (leave blank if using DHCP)");
        short_label = newCDKLabel(net_screen, (window_x + 20), (window_y + 6),
                short_label_msg, 1, FALSE, FALSE);
        if (!short_label) {
            errorDialog(main_cdk_screen, "Couldn't create label widget!", NULL);
            goto cleanup;
        }
        setCDKLabelBackgroundAttrib(short_label, COLOR_DIALOG_TEXT);
        
        /* IP address field */
        ip_addy = newCDKEntry(net_screen, (window_x + 20), (window_y + 7),
                NULL, "</B>IP address: ",
                COLOR_DIALOG_SELECT, '_' | COLOR_DIALOG_INPUT, vMIXED,
                MAX_IPV4_ADDR_LEN, 0, MAX_IPV4_ADDR_LEN, FALSE, FALSE);
        if (!ip_addy) {
            errorDialog(main_cdk_screen, "Couldn't create entry widget!", NULL);
            goto cleanup;
        }
        setCDKEntryBoxAttribute(ip_addy, COLOR_DIALOG_INPUT);
        setCDKEntryValue(ip_addy, conf_ipaddr);

        /* Netmask field */
        netmask = newCDKEntry(net_screen, (window_x + 20), (window_y + 8),
                NULL, "</B>Netmask:    ",
                COLOR_DIALOG_SELECT, '_' | COLOR_DIALOG_INPUT, vMIXED,
                MAX_IPV4_ADDR_LEN, 0, MAX_IPV4_ADDR_LEN, FALSE, FALSE);
        if (!netmask) {
            errorDialog(main_cdk_screen, "Couldn't create entry widget!", NULL);
            goto cleanup;
        }
        setCDKEntryBoxAttribute(netmask, COLOR_DIALOG_INPUT);
        setCDKEntryValue(netmask, conf_netmask);

        /* Broadcast field */
        broadcast = newCDKEntry(net_screen, (window_x + 20), (window_y + 9),
                NULL, "</B>Broadcast:  ",
                COLOR_DIALOG_SELECT, '_' | COLOR_DIALOG_INPUT, vMIXED,
                MAX_IPV4_ADDR_LEN, 0, MAX_IPV4_ADDR_LEN, FALSE, FALSE);
        if (!broadcast) {
            errorDialog(main_cdk_screen, "Couldn't create entry widget!", NULL);
            goto cleanup;
        }
        setCDKEntryBoxAttribute(broadcast, COLOR_DIALOG_INPUT);
        setCDKEntryValue(broadcast, conf_broadcast);

        /* Buttons */
        ok_button = newCDKButton(net_screen, (window_x + 26), (window_y + 14),
                "</B>   OK   ", ok_cb, FALSE, FALSE);
        if (!ok_button) {
            errorDialog(main_cdk_screen, "Couldn't create button widget!", NULL);
            goto cleanup;
        }
        setCDKButtonBackgroundAttrib(ok_button, COLOR_DIALOG_INPUT);
        cancel_button = newCDKButton(net_screen, (window_x + 36), (window_y + 14),
                "</B> Cancel ", cancel_cb, FALSE, FALSE);
        if (!cancel_button) {
            errorDialog(main_cdk_screen, "Couldn't create button widget!", NULL);
            goto cleanup;
        }
        setCDKButtonBackgroundAttrib(cancel_button, COLOR_DIALOG_INPUT);

        /* Allow user to traverse the screen */
        refreshCDKScreen(net_screen);
        traverse_ret = traverseCDKScreen(net_screen);

        /* User hit 'OK' button */
        if (traverse_ret == 1) {
            /* Check the field entry widgets for spaces */
            for (i = 0; i < net_screen->objectCount; i++) {
                CDKOBJS *obj = net_screen->object[i];
                if (obj != 0 && ObjTypeOf(obj) == vENTRY) {
                    strncpy(temp_str, getCDKEntryValue((CDKENTRY *) obj), 100);
                    j = 0;
                    while (temp_str[j] != '\0') {
                        if (isspace(temp_str[j])) {
                            errorDialog(main_cdk_screen, "The entry fields cannot contain any spaces!", NULL);
                            goto cleanup;
                        }
                        j++;
                    }
                }
            }

            if (getCDKRadioCurrentItem(ip_config) == 0) {
                /* If the user sets the interface to disabled/unconfigured
                 * then we remove the section */
                iniparser_unset(ini_dict, net_if_name[net_conf_choice]);
                snprintf(temp_ini_str, MAX_INI_VAL, "%s:bootproto", net_if_name[net_conf_choice]);
                iniparser_unset(ini_dict, temp_ini_str);
                snprintf(temp_ini_str, MAX_INI_VAL, "%s:ipaddr", net_if_name[net_conf_choice]);
                iniparser_unset(ini_dict, temp_ini_str);
                snprintf(temp_ini_str, MAX_INI_VAL, "%s:netmask", net_if_name[net_conf_choice]);
                iniparser_unset(ini_dict, temp_ini_str);
                snprintf(temp_ini_str, MAX_INI_VAL, "%s:broadcast", net_if_name[net_conf_choice]);
                iniparser_unset(ini_dict, temp_ini_str);
            } else {
                /* Network interface should be configured (static or DHCP) */
                if (iniparser_set(ini_dict, net_if_name[net_conf_choice], NULL) == -1) {
                    errorDialog(main_cdk_screen, "Couldn't set configuration file value!", NULL);
                    goto cleanup;
                }
                snprintf(temp_ini_str, MAX_INI_VAL, "%s:bootproto", net_if_name[net_conf_choice]);
                if (iniparser_set(ini_dict, temp_ini_str, ip_opts[getCDKRadioCurrentItem(ip_config)]) == -1) {
                    errorDialog(main_cdk_screen, "Couldn't set configuration file value!", NULL);
                    goto cleanup;
                }
                snprintf(temp_ini_str, MAX_INI_VAL, "%s:ipaddr", net_if_name[net_conf_choice]);
                if (iniparser_set(ini_dict, temp_ini_str, getCDKEntryValue(ip_addy)) == -1) {
                    errorDialog(main_cdk_screen, "Couldn't set configuration file value!", NULL);
                    goto cleanup;
                }
                snprintf(temp_ini_str, MAX_INI_VAL, "%s:netmask", net_if_name[net_conf_choice]);
                if (iniparser_set(ini_dict, temp_ini_str, getCDKEntryValue(netmask)) == -1) {
                    errorDialog(main_cdk_screen, "Couldn't set configuration file value!", NULL);
                    goto cleanup;
                }
                snprintf(temp_ini_str, MAX_INI_VAL, "%s:broadcast", net_if_name[net_conf_choice]);
                if (iniparser_set(ini_dict, temp_ini_str, getCDKEntryValue(broadcast)) == -1) {
                    errorDialog(main_cdk_screen, "Couldn't set configuration file value!", NULL);
                    goto cleanup;
                }
            }
            /* Write to network config. file */
            ini_file = fopen(NETWORK_CONF, "w");
            if (ini_file == NULL) {
                asprintf(&error_msg, "Couldn't open network config. file for writing: %s", strerror(errno));
                errorDialog(main_cdk_screen, error_msg, NULL);
                freeChar(error_msg);
            } else {
                fprintf(ini_file, "# ESOS network configuration file\n");
                fprintf(ini_file, "# This file is generated by esos_tui; do not edit\n\n");
                iniparser_dump_ini(ini_dict, ini_file);
                fclose(ini_file);
                iniparser_freedict(ini_dict);
            }
        }
    }

    cleanup:
    for (i = 0; i < MAX_NET_IFACE; i++) {
        freeChar(net_scroll_msg[i]);
        freeChar(net_if_name[i]);
        freeChar(net_if_mac[i]);
        freeChar(net_if_speed[i]);
        freeChar(net_if_duplex[i]);
    }
    if (net_screen != NULL) {
        destroyCDKScreenObjects(net_screen);
        destroyCDKScreen(net_screen);
    }
    for (i = 0; i < 10; i++) {
        freeChar(net_info_msg[i]);
    }
    freeChar(short_label_msg[0]);
    delwin(net_window);
    curs_set(0);
    refreshCDKScreen(main_cdk_screen);
    
    /* Ask user if they want to restart networking */
    if (traverse_ret == 1) {
        question = questionDialog(main_cdk_screen,
                "Would you like to restart networking now?", NULL);
        if (question)
            restartNetDialog(main_cdk_screen);
    }
    return;
}


/*
 * Run the Restart Networking dialog
 */
void restartNetDialog(CDKSCREEN *main_cdk_screen) {
    CDKSWINDOW *net_restart_info = 0;
    char *swindow_info[MAX_NET_RESTART_INFO_ROWS] = {NULL};
    char *error_msg = NULL;
    char net_rc_cmd[100] = {0}, line[MAX_NET_RESTART_INFO_COLS] = {0};
    int i = 0, status = 0;
    FILE *net_rc = NULL;
    boolean confirm = FALSE;

    /* Get confirmation (and warn user) before restarting network */
    confirm = confirmDialog(main_cdk_screen,
            "If you are connected via SSH, you may lose your session!",
            "Are you sure you want to restart networking?");
    if (!confirm)
        return;

    /* Setup scrolling window widget */
    net_restart_info = newCDKSwindow(main_cdk_screen, CENTER, CENTER,
            MAX_NET_RESTART_INFO_ROWS+2, MAX_NET_RESTART_INFO_COLS+2,
            "<C></31/B>Restarting networking services...\n", MAX_NET_RESTART_INFO_ROWS,
            TRUE, FALSE);
    if (!net_restart_info) {
        errorDialog(main_cdk_screen, "Couldn't create scrolling window widget!", NULL);
        return;
    }
    setCDKSwindowBackgroundAttrib(net_restart_info, COLOR_DIALOG_TEXT);
    setCDKSwindowBoxAttribute(net_restart_info, COLOR_DIALOG_BOX);
    drawCDKSwindow(net_restart_info, TRUE);

    /* Set our line counter */
    i = 0;

    /* Stop networking */
    asprintf(&swindow_info[i], "</B>Stopping network:<!B>");
    addCDKSwindow(net_restart_info, swindow_info[i], BOTTOM);
    i++;
    snprintf(net_rc_cmd, 100, "%s stop", RC_NETWORK);
    net_rc = popen(net_rc_cmd, "r");
    if (!net_rc) {
        asprintf(&error_msg, "popen: %s", strerror(errno));
        errorDialog(main_cdk_screen, error_msg, NULL);
        freeChar(error_msg);
        goto cleanup;
    } else {
        while (fgets(line, sizeof (line), net_rc) != NULL) {
            asprintf(&swindow_info[i], "%s", line);
            addCDKSwindow(net_restart_info, swindow_info[i], BOTTOM);
            i++;
        }
        status = pclose(net_rc);
        if (status == -1) {
            asprintf(&error_msg, "pclose: %s", strerror(errno));
            errorDialog(main_cdk_screen, error_msg, NULL);
            freeChar(error_msg);
            goto cleanup;
        } else {
            if (WIFEXITED(status) && (WEXITSTATUS(status) != 0)) {
                asprintf(&error_msg, "The %s command exited with %d.",
                        RC_NETWORK, WEXITSTATUS(status));
                errorDialog(main_cdk_screen, error_msg, NULL);
                freeChar(error_msg);
                goto cleanup;
            }
        }
    }

    /* Start networking */
    asprintf(&swindow_info[i], " ");
    addCDKSwindow(net_restart_info, swindow_info[i], BOTTOM);
    i++;
    asprintf(&swindow_info[i], "</B>Starting network:<!B>");
    addCDKSwindow(net_restart_info, swindow_info[i], BOTTOM);
    i++;
    snprintf(net_rc_cmd, 100, "%s start", RC_NETWORK);
    net_rc = popen(net_rc_cmd, "r");
    if (!net_rc) {
        asprintf(&error_msg, "popen: %s", strerror(errno));
        errorDialog(main_cdk_screen, error_msg, NULL);
        freeChar(error_msg);
        goto cleanup;
    } else {
        while (fgets(line, sizeof (line), net_rc) != NULL) {
            asprintf(&swindow_info[i], "%s", line);
            addCDKSwindow(net_restart_info, swindow_info[i], BOTTOM);
            i++;
        }
        status = pclose(net_rc);
        if (status == -1) {
            asprintf(&error_msg, "pclose: %s", strerror(errno));
            errorDialog(main_cdk_screen, error_msg, NULL);
            freeChar(error_msg);
            goto cleanup;
        } else {
            if (WIFEXITED(status) && (WEXITSTATUS(status) != 0)) {
                asprintf(&error_msg, "The %s command exited with %d.",
                        RC_NETWORK, WEXITSTATUS(status));
                errorDialog(main_cdk_screen, error_msg, NULL);
                freeChar(error_msg);
                goto cleanup;
            }
        }
    }

    /* The 'g' makes the swindow widget scroll to the top, then activate */
    injectCDKSwindow(net_restart_info, 'g');
    activateCDKSwindow(net_restart_info, 0);

    /* Done */
    cleanup:
    if (net_restart_info)
        destroyCDKSwindow(net_restart_info);
    for (i = 0; i < MAX_NET_RESTART_INFO_ROWS; i++) {
        freeChar(swindow_info[i]);
    }
    return;
}


/*
 * Run the Mail Setup dialog
 */
void mailDialog(CDKSCREEN *main_cdk_screen) {
    WINDOW *mail_window = 0;
    CDKSCREEN *mail_screen = 0;
    CDKLABEL *mail_label = 0;
    CDKBUTTON *ok_button = 0, *cancel_button = 0;
    CDKENTRY *email_addr = 0, *smtp_host = 0, *smtp_port = 0,
            *auth_user = 0, *auth_pass = 0;
    CDKRADIO *use_tls = 0, *use_starttls = 0, *auth_method = 0;
    tButtonCallback ok_cb = &okButtonCB, cancel_cb = &cancelButtonCB;
    int i = 0, traverse_ret = 0, window_y = 0, window_x = 0;
    int mail_window_lines = 17, mail_window_cols = 68;
    static char *no_yes[] = {"No", "Yes"};
    static char *auth_method_opts[] = {"Plain Text", "CRAM-MD5"};
    static char *new_ld_msg[] = {"</31/B>Mail Setup"};
    char temp_str[256] = {0}, new_mailhub[MAX_INI_VAL] = {0},
            new_authmethod[MAX_INI_VAL] = {0}, new_usetls[MAX_INI_VAL] = {0},
            new_usestarttls[MAX_INI_VAL] = {0};
    char *conf_root = NULL, *conf_mailhub = NULL, *conf_authuser = NULL,
            *conf_authpass = NULL, *conf_authmethod = NULL,
            *conf_usetls = NULL, *conf_usestarttls = NULL,
            *mailhub_host = NULL, *mailhub_port = NULL,
            *error_msg = NULL;
    dictionary *ini_dict = NULL;
    FILE *ini_file = NULL;
    boolean question = FALSE;

    /* Read sSMTP configuration file (INI file) */
    ini_dict = iniparser_load(SSMTP_CONF);
    if (ini_dict == NULL) {
        errorDialog(main_cdk_screen, "Cannot parse sSMTP configuration file!", NULL);
        return;
    }
    conf_root = iniparser_getstring(ini_dict, ":root", "");
    conf_mailhub = iniparser_getstring(ini_dict, ":mailhub", "");
    conf_authuser = iniparser_getstring(ini_dict, ":authuser", "");
    conf_authpass = iniparser_getstring(ini_dict, ":authpass", "");
    conf_authmethod = iniparser_getstring(ini_dict, ":authmethod", "");
    conf_usetls = iniparser_getstring(ini_dict, ":usetls", "");
    conf_usestarttls = iniparser_getstring(ini_dict, ":usestarttls", "");

    /* Setup a new CDK screen for mail setup */
    window_y = ((LINES / 2) - (mail_window_lines / 2));
    window_x = ((COLS / 2) - (mail_window_cols / 2));
    mail_window = newwin(mail_window_lines, mail_window_cols,
            window_y, window_x);
    if (mail_window == NULL) {
        errorDialog(main_cdk_screen, "Couldn't create new window!", NULL);
        goto cleanup;
    }
    mail_screen = initCDKScreen(mail_window);
    if (mail_screen == NULL) {
        errorDialog(main_cdk_screen, "Couldn't create new CDK screen!", NULL);
        goto cleanup;
    }
    boxWindow(mail_window, COLOR_DIALOG_BOX);
    wbkgd(mail_window, COLOR_DIALOG_TEXT);
    wrefresh(mail_window);

    /* Screen title label */
    mail_label = newCDKLabel(mail_screen, (window_x + 1), (window_y + 1),
            new_ld_msg, 1, FALSE, FALSE);
    if (!mail_label) {
        errorDialog(main_cdk_screen, "Couldn't create label widget!", NULL);
        goto cleanup;
    }
    setCDKLabelBackgroundAttrib(mail_label, COLOR_DIALOG_TEXT);

    /* Email address (to send alerts to) field */
    email_addr = newCDKEntry(mail_screen, (window_x + 1), (window_y + 3),
            NULL, "</B>Alert email address: ",
            COLOR_DIALOG_SELECT, '_' | COLOR_DIALOG_INPUT, vMIXED,
            MAX_EMAIL_LEN, 0, MAX_EMAIL_LEN, FALSE, FALSE);
    if (!email_addr) {
        errorDialog(main_cdk_screen, "Couldn't create entry widget!", NULL);
        goto cleanup;
    }
    setCDKEntryBoxAttribute(email_addr, COLOR_DIALOG_INPUT);
    setCDKEntryValue(email_addr, conf_root);

    /* Split up mailhub string from configuration */
    mailhub_host = conf_mailhub;
    if ((mailhub_port = strchr(conf_mailhub, ':')) != NULL) {
        *mailhub_port = '\0';
        mailhub_port++;
    }

    /* SMTP host field */
    smtp_host = newCDKEntry(mail_screen, (window_x + 1), (window_y + 5),
            NULL, "</B>SMTP host: ",
            COLOR_DIALOG_SELECT, '_' | COLOR_DIALOG_INPUT, vMIXED,
            24, 0, MAX_SMTP_LEN, FALSE, FALSE);
    if (!smtp_host) {
        errorDialog(main_cdk_screen, "Couldn't create entry widget!", NULL);
        goto cleanup;
    }
    setCDKEntryBoxAttribute(smtp_host, COLOR_DIALOG_INPUT);
    setCDKEntryValue(smtp_host, mailhub_host);

    /* SMTP port field */
    smtp_port = newCDKEntry(mail_screen, (window_x + 38), (window_y + 5),
            NULL, "</B>SMTP port: ",
            COLOR_DIALOG_SELECT, '_' | COLOR_DIALOG_INPUT, vINT,
            5, 0, 5, FALSE, FALSE);
    if (!smtp_port) {
        errorDialog(main_cdk_screen, "Couldn't create entry widget!", NULL);
        goto cleanup;
    }
    setCDKEntryBoxAttribute(smtp_port, COLOR_DIALOG_INPUT);
    if (mailhub_port != NULL)
        setCDKEntryValue(smtp_port, mailhub_port);

    /* TLS radio */
    use_tls = newCDKRadio(mail_screen, (window_x + 1), (window_y + 7),
            NONE, 5, 10, "</B>Use TLS", no_yes, 2,
            '#' | COLOR_DIALOG_SELECT, 1,
            COLOR_DIALOG_SELECT, FALSE, FALSE);
    if (!use_tls) {
        errorDialog(main_cdk_screen, "Couldn't create radio widget!", NULL);
        goto cleanup;
    }
    setCDKRadioBackgroundAttrib(use_tls, COLOR_DIALOG_TEXT);
    if (strcasecmp(conf_usetls, "yes") == 0)
        setCDKRadioCurrentItem(use_tls, 1);
    else
        setCDKRadioCurrentItem(use_tls, 0);

    /* STARTTLS radio */
    use_starttls = newCDKRadio(mail_screen, (window_x + 15), (window_y + 7),
            NONE, 5, 10, "</B>Use STARTTLS", no_yes, 2,
            '#' | COLOR_DIALOG_SELECT, 1,
            COLOR_DIALOG_SELECT, FALSE, FALSE);
    if (!use_starttls) {
        errorDialog(main_cdk_screen, "Couldn't create radio widget!", NULL);
        goto cleanup;
    }
    setCDKRadioBackgroundAttrib(use_starttls, COLOR_DIALOG_TEXT);
    if (strcasecmp(conf_usestarttls, "yes") == 0)
        setCDKRadioCurrentItem(use_starttls, 1);
    else
        setCDKRadioCurrentItem(use_starttls, 0);

    /* Auth. Method radio */
    auth_method = newCDKRadio(mail_screen, (window_x + 29), (window_y + 7),
            NONE, 9, 10, "</B>Auth. Method", auth_method_opts, 2,
            '#' | COLOR_DIALOG_SELECT, 1,
            COLOR_DIALOG_SELECT, FALSE, FALSE);
    if (!auth_method) {
        errorDialog(main_cdk_screen, "Couldn't create radio widget!", NULL);
        goto cleanup;
    }
    setCDKRadioBackgroundAttrib(auth_method, COLOR_DIALOG_TEXT);
    if (strcasecmp(conf_authmethod, "cram-md5") == 0)
        setCDKRadioCurrentItem(auth_method, 1);
    else
        setCDKRadioCurrentItem(auth_method, 0);

    /* Auth. User field */
    auth_user = newCDKEntry(mail_screen, (window_x + 1), (window_y + 11),
            NULL, "</B>Auth. User: ",
            COLOR_DIALOG_SELECT, '_' | COLOR_DIALOG_INPUT, vMIXED,
            15, 0, MAX_SMTP_USER_LEN, FALSE, FALSE);
    if (!auth_user) {
        errorDialog(main_cdk_screen, "Couldn't create entry widget!", NULL);
        goto cleanup;
    }
    setCDKEntryBoxAttribute(auth_user, COLOR_DIALOG_INPUT);
    setCDKEntryValue(auth_user, conf_authuser);

    /* Auth. Password field */
    auth_pass = newCDKEntry(mail_screen, (window_x + 30), (window_y + 11),
            NULL, "</B>Auth. Password: ",
            COLOR_DIALOG_SELECT, '_' | COLOR_DIALOG_INPUT, vMIXED,
            15, 0, MAX_SMTP_PASS_LEN, FALSE, FALSE);
    if (!auth_pass) {
        errorDialog(main_cdk_screen, "Couldn't create entry widget!", NULL);
        goto cleanup;
    }
    setCDKEntryBoxAttribute(auth_pass, COLOR_DIALOG_INPUT);
    setCDKEntryValue(auth_pass, conf_authpass);

    /* Buttons */
    ok_button = newCDKButton(mail_screen, (window_x + 26), (window_y + 15),
            "</B>   OK   ", ok_cb, FALSE, FALSE);
    if (!ok_button) {
        errorDialog(main_cdk_screen, "Couldn't create button widget!", NULL);
        goto cleanup;
    }
    setCDKButtonBackgroundAttrib(ok_button, COLOR_DIALOG_INPUT);
    cancel_button = newCDKButton(mail_screen, (window_x + 36), (window_y + 15),
            "</B> Cancel ", cancel_cb, FALSE, FALSE);
    if (!cancel_button) {
        errorDialog(main_cdk_screen, "Couldn't create button widget!", NULL);
        goto cleanup;
    }
    setCDKButtonBackgroundAttrib(cancel_button, COLOR_DIALOG_INPUT);

    /* Allow user to traverse the screen */
    refreshCDKScreen(mail_screen);
    traverse_ret = traverseCDKScreen(mail_screen);

    /* User hit 'OK' button */
    if (traverse_ret == 1) {
        /* Check email address (field entry) */
        strncpy(temp_str, getCDKEntryValue(email_addr), MAX_EMAIL_LEN);
        i = 0;
        while (temp_str[i] != '\0') {
            /* If the user didn't input an acceptable name, then cancel out */
            if (isspace(temp_str[i])) {
                errorDialog(main_cdk_screen,
                        "The email address field cannot contain any spaces!", NULL);
                goto cleanup;
            }
            i++;
        }

        /* Check SMTP host (field entry) */
        strncpy(temp_str, getCDKEntryValue(smtp_host), MAX_SMTP_LEN);
        i = 0;
        while (temp_str[i] != '\0') {
            /* If the user didn't input an acceptable name, then cancel out */
            if (isspace(temp_str[i])) {
                errorDialog(main_cdk_screen,
                        "The SMTP host field cannot contain any spaces!", NULL);
                goto cleanup;
            }
            i++;
        }

        /* Check auth. user (field entry) */
        strncpy(temp_str, getCDKEntryValue(auth_user), MAX_SMTP_USER_LEN);
        i = 0;
        while (temp_str[i] != '\0') {
            /* If the user didn't input an acceptable name, then cancel out */
            if (isspace(temp_str[i])) {
                errorDialog(main_cdk_screen,
                        "The auth. user field cannot contain any spaces!", NULL);
                goto cleanup;
            }
            i++;
        }

        /* Check auth. password (field entry) */
        strncpy(temp_str, getCDKEntryValue(auth_pass), MAX_SMTP_PASS_LEN);
        i = 0;
        while (temp_str[i] != '\0') {
            /* If the user didn't input an acceptable name, then cancel out */
            if (isspace(temp_str[i])) {
                errorDialog(main_cdk_screen,
                        "The auth. password field cannot contain any spaces!", NULL);
                goto cleanup;
            }
            i++;
        }

        /* Set config. file */
        if (iniparser_set(ini_dict, ":root", getCDKEntryValue(email_addr)) == -1) {
            errorDialog(main_cdk_screen, "Couldn't set configuration file value!", NULL);
            goto cleanup;
        }
        snprintf(new_mailhub, MAX_INI_VAL, "%s:%s", getCDKEntryValue(smtp_host), getCDKEntryValue(smtp_port));
        if (iniparser_set(ini_dict, ":mailhub", new_mailhub) == -1) {
            errorDialog(main_cdk_screen, "Couldn't set configuration file value!", NULL);
            goto cleanup;
        }
        if (iniparser_set(ini_dict, ":authuser", getCDKEntryValue(auth_user)) == -1) {
            errorDialog(main_cdk_screen, "Couldn't set configuration file value!", NULL);
            goto cleanup;
        }
        if (iniparser_set(ini_dict, ":authpass", getCDKEntryValue(auth_pass)) == -1) {
            errorDialog(main_cdk_screen, "Couldn't set configuration file value!", NULL);
            goto cleanup;
        }
        if (getCDKRadioSelectedItem(auth_method) == 1)
            snprintf(new_authmethod, MAX_INI_VAL, "CRAM-MD5");
        else
            snprintf(new_authmethod, MAX_INI_VAL, " ");
        if (iniparser_set(ini_dict, ":authmethod", new_authmethod) == -1) {
            errorDialog(main_cdk_screen, "Couldn't set configuration file value!", NULL);
            goto cleanup;
        }
        if (getCDKRadioSelectedItem(use_tls) == 1)
            snprintf(new_usetls, MAX_INI_VAL, "YES");
        else
            snprintf(new_usetls, MAX_INI_VAL, "NO");
        if (iniparser_set(ini_dict, ":usetls", new_usetls) == -1) {
            errorDialog(main_cdk_screen, "Couldn't set configuration file value!", NULL);
            goto cleanup;
        }
        if (getCDKRadioSelectedItem(use_starttls) == 1)
            snprintf(new_usestarttls, MAX_INI_VAL, "YES");
        else
            snprintf(new_usestarttls, MAX_INI_VAL, "NO");
        if (iniparser_set(ini_dict, ":usestarttls", new_usestarttls) == -1) {
            errorDialog(main_cdk_screen, "Couldn't set configuration file value!", NULL);
            goto cleanup;
        }

        /* Write the configuration file */
        ini_file = fopen(SSMTP_CONF, "w");
        if (ini_file == NULL) {
            asprintf(&error_msg, "Couldn't open sSMTP config. file for writing: %s", strerror(errno));
            errorDialog(main_cdk_screen, error_msg, NULL);
            freeChar(error_msg);
        } else {
            /* sSMTP is very picky about its configuration file, so we can't
             * use the iniparser_dump_ini function */
            fprintf(ini_file, "# sSMTP configuration file\n");
            fprintf(ini_file, "# This file is generated by esos_tui; do not edit\n\n");
            for (i = 0; i < ini_dict->size; i++) {
                if (ini_dict->key[i] == NULL)
                    continue;
                fprintf(ini_file, "%s=%s\n", ini_dict->key[i]+1,
                        ini_dict->val[i] ? ini_dict->val[i] : "");
            }
            fclose(ini_file);
        }
    }

    cleanup:
    iniparser_freedict(ini_dict);
    if (mail_screen != NULL) {
        destroyCDKScreenObjects(mail_screen);
        destroyCDKScreen(mail_screen);
    }
    delwin(mail_window);
    curs_set(0);
    refreshCDKScreen(main_cdk_screen);
    
    /* Ask user if they want to send a test email message */
    if (traverse_ret == 1) {
        question = questionDialog(main_cdk_screen,
                "Would you like to send a test email message?", NULL);
        if (question)
            testEmailDialog(main_cdk_screen);
    }
    return;
}


/*
 * Run the Send Test Email dialog
 */
void testEmailDialog(CDKSCREEN *main_cdk_screen) {
    CDKLABEL *test_email_label = 0;
    char ssmtp_cmd[100] = {0}, email_addy[MAX_EMAIL_LEN] = {0};
    char *message[5] = {NULL};
    char *error_msg = NULL, *conf_root = NULL;
    int i = 0, status = 0;
    dictionary *ini_dict = NULL;
    FILE *ssmtp = NULL;

    /* Get the email address from the config. file */
    ini_dict = iniparser_load(SSMTP_CONF);
    if (ini_dict == NULL) {
        errorDialog(main_cdk_screen, "Cannot parse sSMTP configuration file!", NULL);
        return;
    }
    conf_root = iniparser_getstring(ini_dict, ":root", "");
    if (strcmp(conf_root, "") == 0) {
        errorDialog(main_cdk_screen, "No email address is set in the configuration file!", NULL);
        return;
    }

    /* Display a nice short label message while we sync */
    snprintf(email_addy, MAX_EMAIL_LEN, "%s", conf_root);
    asprintf(&message[0], " ");
    asprintf(&message[1], " ");
    asprintf(&message[2], "</B>   Sending a test email to %s...   ", email_addy);;
    asprintf(&message[3], " ");
    asprintf(&message[4], " ");
    test_email_label = newCDKLabel(main_cdk_screen, CENTER, CENTER,
            message, 5, TRUE, FALSE);
    if (!test_email_label) {
        errorDialog(main_cdk_screen, "Couldn't create label widget!", NULL);
        goto cleanup;
    }
    setCDKLabelBackgroundAttrib(test_email_label, COLOR_DIALOG_TEXT);
    setCDKLabelBoxAttribute(test_email_label, COLOR_DIALOG_BOX);
    refreshCDKScreen(main_cdk_screen);

    /* Send the test email message */
    snprintf(ssmtp_cmd, 100, "%s %s > /dev/null 2>&1", SSMTP_BIN, email_addy);
    ssmtp = popen(ssmtp_cmd, "w");
    if (!ssmtp) {
        asprintf(&error_msg, "popen: %s", strerror(errno));
        errorDialog(main_cdk_screen, error_msg, NULL);
        freeChar(error_msg);
        goto cleanup;
    } else {
        fprintf(ssmtp, "To: %s\n", email_addy);
        fprintf(ssmtp, "From: root\n");
        fprintf(ssmtp, "Subject: ESOS Test Email Message\n\n");
        fprintf(ssmtp, "This is an email from ESOS to verify/confirm your email settings.");
        status = pclose(ssmtp);
        if (status == -1) {
            asprintf(&error_msg, "pclose: %s", strerror(errno));
            errorDialog(main_cdk_screen, error_msg, NULL);
            freeChar(error_msg);
            goto cleanup;
        } else {
            if (WIFEXITED(status) && (WEXITSTATUS(status) != 0)) {
                asprintf(&error_msg, "The %s command exited with %d.",
                        SSMTP_BIN, WEXITSTATUS(status));
                errorDialog(main_cdk_screen, error_msg,
                        "Check the mail log for more information.");
                freeChar(error_msg);
                goto cleanup;
            }
        }
    }

    /* Done */
    cleanup:
    iniparser_freedict(ini_dict);
    if (test_email_label)
        destroyCDKLabel(test_email_label);
    for (i = 0; i < 5; i++)
        freeChar(message[i]);
    return;
}


/*
 * Run the Add User dialog
 */
void addUserDialog(CDKSCREEN *main_cdk_screen) {
    WINDOW *add_user_window = 0;
    CDKSCREEN *add_user_screen = 0;
    CDKLABEL *add_user_label = 0;
    CDKBUTTON *ok_button = 0, *cancel_button = 0;
    CDKENTRY *uname_field = 0, *pass_1_field = 0, *pass_2_field = 0;
    tButtonCallback ok_cb = &okButtonCB, cancel_cb = &cancelButtonCB;
    int i = 0, traverse_ret = 0, window_y = 0, window_x = 0, ret_val = 0, exit_stat = 0;
    int add_user_window_lines = 12, add_user_window_cols = 50;
    static char *screen_title[] = {"</31/B>Adding a new ESOS user account..."};
    char add_user_cmd[200] = {0}, chg_pass_cmd[100] = {0}, username[MAX_UNAME_LEN] = {0},
            password_1[MAX_PASSWD_LEN] = {0}, password_2[MAX_PASSWD_LEN] = {0};
    char *error_msg = NULL;
    
    /* Setup a new CDK screen for adding a new user account */
    window_y = ((LINES / 2) - (add_user_window_lines / 2));
    window_x = ((COLS / 2) - (add_user_window_cols / 2));
    add_user_window = newwin(add_user_window_lines, add_user_window_cols,
            window_y, window_x);
    if (add_user_window == NULL) {
        errorDialog(main_cdk_screen, "Couldn't create new window!", NULL);
        goto cleanup;
    }
    add_user_screen = initCDKScreen(add_user_window);
    if (add_user_screen == NULL) {
        errorDialog(main_cdk_screen, "Couldn't create new CDK screen!", NULL);
        goto cleanup;
    }
    boxWindow(add_user_window, COLOR_DIALOG_BOX);
    wbkgd(add_user_window, COLOR_DIALOG_TEXT);
    wrefresh(add_user_window);

    /* Screen title label */
    add_user_label = newCDKLabel(add_user_screen, (window_x + 1), (window_y + 1),
            screen_title, 1, FALSE, FALSE);
    if (!add_user_label) {
        errorDialog(main_cdk_screen, "Couldn't create label widget!", NULL);
        goto cleanup;
    }
    setCDKLabelBackgroundAttrib(add_user_label, COLOR_DIALOG_TEXT);

    /* Username field */
    uname_field = newCDKEntry(add_user_screen, (window_x + 1), (window_y + 3),
            NULL, "</B>Username: ",
            COLOR_DIALOG_SELECT, '_' | COLOR_DIALOG_INPUT, vMIXED,
            MAX_UNAME_LEN, 0, MAX_UNAME_LEN, FALSE, FALSE);
    if (!uname_field) {
        errorDialog(main_cdk_screen, "Couldn't create entry widget!", NULL);
        goto cleanup;
    }
    setCDKEntryBoxAttribute(uname_field, COLOR_DIALOG_INPUT);

    /* Password field (1) */
    pass_1_field = newCDKEntry(add_user_screen, (window_x + 1), (window_y + 5),
            NULL, "</B>User password:   ",
            COLOR_DIALOG_SELECT, '_' | COLOR_DIALOG_INPUT, vHMIXED,
            25, 0, MAX_PASSWD_LEN, FALSE, FALSE);
    if (!pass_1_field) {
        errorDialog(main_cdk_screen, "Couldn't create entry widget!", NULL);
        goto cleanup;
    }
    setCDKEntryBoxAttribute(pass_1_field, COLOR_DIALOG_INPUT);
    setCDKEntryHiddenChar(pass_1_field, '*' | COLOR_DIALOG_SELECT);
    
    /* Password field (2) */
    pass_2_field = newCDKEntry(add_user_screen, (window_x + 1), (window_y + 6),
            NULL, "</B>Retype password: ",
            COLOR_DIALOG_SELECT, '_' | COLOR_DIALOG_INPUT, vHMIXED,
            25, 0, MAX_PASSWD_LEN, FALSE, FALSE);
    if (!pass_2_field) {
        errorDialog(main_cdk_screen, "Couldn't create entry widget!", NULL);
        goto cleanup;
    }
    setCDKEntryBoxAttribute(pass_2_field, COLOR_DIALOG_INPUT);
    setCDKEntryHiddenChar(pass_2_field, '*' | COLOR_DIALOG_SELECT);

    /* Buttons */
    ok_button = newCDKButton(add_user_screen, (window_x + 17), (window_y + 10),
            "</B>   OK   ", ok_cb, FALSE, FALSE);
    if (!ok_button) {
        errorDialog(main_cdk_screen, "Couldn't create button widget!", NULL);
        goto cleanup;
    }
    setCDKButtonBackgroundAttrib(ok_button, COLOR_DIALOG_INPUT);
    cancel_button = newCDKButton(add_user_screen, (window_x + 27), (window_y + 10),
            "</B> Cancel ", cancel_cb, FALSE, FALSE);
    if (!cancel_button) {
        errorDialog(main_cdk_screen, "Couldn't create button widget!", NULL);
        goto cleanup;
    }
    setCDKButtonBackgroundAttrib(cancel_button, COLOR_DIALOG_INPUT);

    /* Allow user to traverse the screen */
    refreshCDKScreen(add_user_screen);
    traverse_ret = traverseCDKScreen(add_user_screen);

    /* User hit 'OK' button */
    if (traverse_ret == 1) {
        /* Check username (field entry) */
        strncpy(username, getCDKEntryValue(uname_field), MAX_UNAME_LEN);
        i = 0;
        while (username[i] != '\0') {
            /* If the user didn't input an acceptable name, then cancel out */
            if (isspace(username[i])) {
                errorDialog(main_cdk_screen, "The username field cannot contain any spaces!", NULL);
                goto cleanup;
            }
            i++;
        }
        
        /* Make sure the password fields match */
        strncpy(password_1, getCDKEntryValue(pass_1_field), MAX_PASSWD_LEN);
        strncpy(password_2, getCDKEntryValue(pass_2_field), MAX_PASSWD_LEN);
        if (strcmp(password_1, password_2) != 0) {
            errorDialog(main_cdk_screen, "The given passwords do not match!", NULL);
            goto cleanup;
        }
        
        /* Check first password field (we assume both match if we got this far) */
        i = 0;
        while (password_1[i] != '\0') {
            if (isspace(password_1[i])) {
                errorDialog(main_cdk_screen, "The password cannot contain any spaces!", NULL);
                goto cleanup;
            }
            i++;
        }

        /* Add the new user account */
        snprintf(add_user_cmd, 200, "%s -h /tmp -g 'ESOS User' -s %s -G %s -D %s > /dev/null 2>&1",
                ADDUSER_TOOL, TUI_BIN, ESOS_GROUP, username);
        ret_val = system(add_user_cmd);
        if ((exit_stat = WEXITSTATUS(ret_val)) != 0) {
            asprintf(&error_msg, "Running %s failed; exited with %d.", ADDUSER_TOOL, exit_stat);
            errorDialog(main_cdk_screen, error_msg, NULL);
            freeChar(error_msg);
            goto cleanup;
        }
        
        /* Set the password for the new account */
        snprintf(chg_pass_cmd, 100, "echo '%s:%s' | %s > /dev/null 2>&1",
                username, password_1, CHPASSWD_TOOL);
        ret_val = system(chg_pass_cmd);
        if ((exit_stat = WEXITSTATUS(ret_val)) != 0) {
            asprintf(&error_msg, "Running %s failed; exited with %d.", CHPASSWD_TOOL, exit_stat);
            errorDialog(main_cdk_screen, error_msg, NULL);
            freeChar(error_msg);
            goto cleanup;
        }
    }

    /* Done */
    cleanup:
    if (add_user_screen != NULL) {
        destroyCDKScreenObjects(add_user_screen);
        destroyCDKScreen(add_user_screen);
    }
    delwin(add_user_window);
    return;
}


/*
 * Run the Delete User dialog
 */
void delUserDialog(CDKSCREEN *main_cdk_screen) {
    int ret_val = 0, exit_stat = 0;
    char del_user_cmd[100] = {0}, del_grp_cmd[100] = {0},
            user_acct[MAX_UNAME_LEN] = {0};
    char *error_msg = NULL, *confirm_msg = NULL;
    uid_t uid = 0;
    struct passwd *passwd_entry = NULL;
    boolean confirm = FALSE;
    
    /* Have the user choose a user account */
    getUserAcct(main_cdk_screen, user_acct);
    if (user_acct[0] == '\0')
        return;
    
    /* Make sure we are not trying to delete the user that is currently logged in */
    uid = getuid();
    passwd_entry = getpwuid(uid);
    if (strcmp(passwd_entry->pw_name, user_acct) == 0) {
        errorDialog(main_cdk_screen, "Can't delete the user that is currently logged in!", NULL);
        return;
    }

    /* Can't delete the ESOS superuser account */
    if (strcmp(ESOS_SUPERUSER, user_acct) == 0) {
        errorDialog(main_cdk_screen, "Can't delete the ESOS superuser account!", NULL);
        return;
    }

    /* Get a final confirmation from user before we delete */
    asprintf(&confirm_msg, "Are you sure you want to delete user %s?", user_acct);
    confirm = confirmDialog(main_cdk_screen, confirm_msg, NULL);
    freeChar(confirm_msg);
    if (confirm) {
        /* Remove the user from the ESOS users group (deluser doesn't seem to do this) */
        snprintf(del_grp_cmd, 100, "%s %s %s > /dev/null 2>&1", DELGROUP_TOOL, user_acct, ESOS_GROUP);
        ret_val = system(del_grp_cmd);
        if ((exit_stat = WEXITSTATUS(ret_val)) != 0) {
            asprintf(&error_msg, "Running %s failed; exited with %d.", DELGROUP_TOOL, exit_stat);
            errorDialog(main_cdk_screen, error_msg, NULL);
            freeChar(error_msg);
            return;
        }

        /* Delete the user account */
        snprintf(del_user_cmd, 100, "%s %s > /dev/null 2>&1", DELUSER_TOOL, user_acct);
        ret_val = system(del_user_cmd);
        if ((exit_stat = WEXITSTATUS(ret_val)) != 0) {
            asprintf(&error_msg, "Running %s failed; exited with %d.", DELUSER_TOOL, exit_stat);
            errorDialog(main_cdk_screen, error_msg, NULL);
            freeChar(error_msg);
            return;
        }
    }
    
    /* Done */
    return;
}


/*
 * Run the Change Password dialog
 */
void chgPasswdDialog(CDKSCREEN *main_cdk_screen) {
    WINDOW *chg_pass_window = 0;
    CDKSCREEN *chg_pass_screen = 0;
    CDKLABEL *passwd_label = 0;
    CDKBUTTON *ok_button = 0, *cancel_button = 0;
    CDKENTRY *new_pass_1 = 0, *new_pass_2 = 0;
    tButtonCallback ok_cb = &okButtonCB, cancel_cb = &cancelButtonCB;
    int i = 0, traverse_ret = 0, window_y = 0, window_x = 0, ret_val = 0, exit_stat = 0;
    int chg_pass_window_lines = 10, chg_pass_window_cols = 45;
    char *screen_title[1] = {NULL};
    char chg_pass_cmd[100] = {0}, password_1[MAX_PASSWD_LEN] = {0},
            password_2[MAX_PASSWD_LEN] = {0}, user_acct[MAX_UNAME_LEN] = {0};
    char *error_msg = NULL;
    
    /* Have the user choose a user account */
    getUserAcct(main_cdk_screen, user_acct);
    if (user_acct[0] == '\0')
        return;
    
    /* Setup a new CDK screen for password change */
    window_y = ((LINES / 2) - (chg_pass_window_lines / 2));
    window_x = ((COLS / 2) - (chg_pass_window_cols / 2));
    chg_pass_window = newwin(chg_pass_window_lines, chg_pass_window_cols,
            window_y, window_x);
    if (chg_pass_window == NULL) {
        errorDialog(main_cdk_screen, "Couldn't create new window!", NULL);
        goto cleanup;
    }
    chg_pass_screen = initCDKScreen(chg_pass_window);
    if (chg_pass_screen == NULL) {
        errorDialog(main_cdk_screen, "Couldn't create new CDK screen!", NULL);
        goto cleanup;
    }
    boxWindow(chg_pass_window, COLOR_DIALOG_BOX);
    wbkgd(chg_pass_window, COLOR_DIALOG_TEXT);
    wrefresh(chg_pass_window);

    /* Screen title label */
    asprintf(&screen_title[0], "</31/B>Changing password for user %s...", user_acct);
    passwd_label = newCDKLabel(chg_pass_screen, (window_x + 1), (window_y + 1),
            screen_title, 1, FALSE, FALSE);
    if (!passwd_label) {
        errorDialog(main_cdk_screen, "Couldn't create label widget!", NULL);
        goto cleanup;
    }
    setCDKLabelBackgroundAttrib(passwd_label, COLOR_DIALOG_TEXT);

    /* New password field (1) */
    new_pass_1 = newCDKEntry(chg_pass_screen, (window_x + 1), (window_y + 3),
            NULL, "</B>New password:    ",
            COLOR_DIALOG_SELECT, '_' | COLOR_DIALOG_INPUT, vHMIXED,
            20, 0, MAX_PASSWD_LEN, FALSE, FALSE);
    if (!new_pass_1) {
        errorDialog(main_cdk_screen, "Couldn't create entry widget!", NULL);
        goto cleanup;
    }
    setCDKEntryBoxAttribute(new_pass_1, COLOR_DIALOG_INPUT);
    setCDKEntryHiddenChar(new_pass_1, '*' | COLOR_DIALOG_SELECT);
    
    /* New password field (2) */
    new_pass_2 = newCDKEntry(chg_pass_screen, (window_x + 1), (window_y + 4),
            NULL, "</B>Retype password: ",
            COLOR_DIALOG_SELECT, '_' | COLOR_DIALOG_INPUT, vHMIXED,
            20, 0, MAX_PASSWD_LEN, FALSE, FALSE);
    if (!new_pass_2) {
        errorDialog(main_cdk_screen, "Couldn't create entry widget!", NULL);
        goto cleanup;
    }
    setCDKEntryBoxAttribute(new_pass_2, COLOR_DIALOG_INPUT);
    setCDKEntryHiddenChar(new_pass_2, '*' | COLOR_DIALOG_SELECT);

    /* Buttons */
    ok_button = newCDKButton(chg_pass_screen, (window_x + 14), (window_y + 8),
            "</B>   OK   ", ok_cb, FALSE, FALSE);
    if (!ok_button) {
        errorDialog(main_cdk_screen, "Couldn't create button widget!", NULL);
        goto cleanup;
    }
    setCDKButtonBackgroundAttrib(ok_button, COLOR_DIALOG_INPUT);
    cancel_button = newCDKButton(chg_pass_screen, (window_x + 24), (window_y + 8),
            "</B> Cancel ", cancel_cb, FALSE, FALSE);
    if (!cancel_button) {
        errorDialog(main_cdk_screen, "Couldn't create button widget!", NULL);
        goto cleanup;
    }
    setCDKButtonBackgroundAttrib(cancel_button, COLOR_DIALOG_INPUT);

    /* Allow user to traverse the screen */
    refreshCDKScreen(chg_pass_screen);
    traverse_ret = traverseCDKScreen(chg_pass_screen);

    /* User hit 'OK' button */
    if (traverse_ret == 1) {
        /* Make sure the password fields match */
        strncpy(password_1, getCDKEntryValue(new_pass_1), MAX_PASSWD_LEN);
        strncpy(password_2, getCDKEntryValue(new_pass_2), MAX_PASSWD_LEN);
        if (strcmp(password_1, password_2) != 0) {
            errorDialog(main_cdk_screen, "The given passwords do not match!", NULL);
            goto cleanup;
        }
        
        /* Check first password field (we assume both match if we got this far) */
        i = 0;
        while (password_1[i] != '\0') {
            if (isspace(password_1[i])) {
                errorDialog(main_cdk_screen, "The new password cannot contain any spaces!", NULL);
                goto cleanup;
            }
            i++;
        }

        /* Set the new password */
        snprintf(chg_pass_cmd, 100, "echo '%s:%s' | %s > /dev/null 2>&1",
                user_acct, password_1, CHPASSWD_TOOL);
        ret_val = system(chg_pass_cmd);
        if ((exit_stat = WEXITSTATUS(ret_val)) != 0) {
            asprintf(&error_msg, "Running %s failed; exited with %d.", CHPASSWD_TOOL, exit_stat);
            errorDialog(main_cdk_screen, error_msg, NULL);
            freeChar(error_msg);
            goto cleanup;
        }
    }

    /* Done */
    cleanup:
    freeChar(screen_title[0]);
    if (chg_pass_screen != NULL) {
        destroyCDKScreenObjects(chg_pass_screen);
        destroyCDKScreen(chg_pass_screen);
    }
    delwin(chg_pass_window);
    return;
}


/*
 * Run the Statistics dialog
 */
void scstInfoDialog(CDKSCREEN *main_cdk_screen) {
    CDKSWINDOW *scst_info = 0;
    char scst_ver[MAX_SYSFS_ATTR_SIZE] = {0}, scst_setup_id[MAX_SYSFS_ATTR_SIZE] = {0},
            scst_threads[MAX_SYSFS_ATTR_SIZE] = {0}, scst_sysfs_res[MAX_SYSFS_ATTR_SIZE] = {0},
            tmp_sysfs_path[MAX_SYSFS_PATH_SIZE] = {0}, tmp_attr_line[MAX_SCST_INFO_COLS] = {0};
    char *swindow_info[MAX_SCST_INFO_ROWS] = {NULL};
    char *temp_pstr = NULL;
    FILE *sysfs_file = NULL;
    int i = 0;
    
    /* Setup scrolling window widget */
    scst_info = newCDKSwindow(main_cdk_screen, CENTER, CENTER,
            MAX_SCST_INFO_ROWS+2, MAX_SCST_INFO_COLS+2,
            "<C></31/B>SCST Information / Statistics:\n", MAX_SCST_INFO_ROWS,
            TRUE, FALSE);
    if (!scst_info) {
        errorDialog(main_cdk_screen, "Couldn't create scrolling window widget!", NULL);
        return;
    }
    setCDKSwindowBackgroundAttrib(scst_info, COLOR_DIALOG_TEXT);
    setCDKSwindowBoxAttribute(scst_info, COLOR_DIALOG_BOX);

    /* Grab some semi-useful information for our scrolling window widget */
    snprintf(tmp_sysfs_path, MAX_SYSFS_PATH_SIZE, "%s/version", SYSFS_SCST_TGT);
    readAttribute(tmp_sysfs_path, scst_ver);
    snprintf(tmp_sysfs_path, MAX_SYSFS_PATH_SIZE, "%s/setup_id", SYSFS_SCST_TGT);
    readAttribute(tmp_sysfs_path, scst_setup_id);
    snprintf(tmp_sysfs_path, MAX_SYSFS_PATH_SIZE, "%s/threads", SYSFS_SCST_TGT);
    readAttribute(tmp_sysfs_path, scst_threads);
    snprintf(tmp_sysfs_path, MAX_SYSFS_PATH_SIZE, "%s/last_sysfs_mgmt_res", SYSFS_SCST_TGT);
    readAttribute(tmp_sysfs_path, scst_sysfs_res);
    
    /* Add the attribute values collected above to our scrolling window widget */
    asprintf(&swindow_info[0], "</B>Version:<!B>\t%-15s</B>Setup ID:<!B>\t\t%s", scst_ver, scst_setup_id);
    addCDKSwindow(scst_info, swindow_info[0], BOTTOM);
    asprintf(&swindow_info[1], "</B>Threads:<!B>\t%-15s</B>Last sysfs result:<!B>\t%s", scst_threads, scst_sysfs_res);
    addCDKSwindow(scst_info, swindow_info[1], BOTTOM);

     /* Loop over the SGV global statistics attribute/file in sysfs */
    asprintf(&swindow_info[2], " ");
    addCDKSwindow(scst_info, swindow_info[2], BOTTOM);
    asprintf(&swindow_info[3], "</B>Global SGV cache statistics:<!B>");
    addCDKSwindow(scst_info, swindow_info[3], BOTTOM);
    i = 4;
    snprintf(tmp_sysfs_path, MAX_SYSFS_PATH_SIZE, "%s/sgv/global_stats", SYSFS_SCST_TGT);
    if ((sysfs_file = fopen(tmp_sysfs_path, "r")) == NULL) {
        asprintf(&swindow_info[i], "fopen: %s", strerror(errno));
        addCDKSwindow(scst_info, swindow_info[i], BOTTOM);
    } else {
        while (fgets(tmp_attr_line, sizeof (tmp_attr_line), sysfs_file) != NULL) {
            temp_pstr = strrchr(tmp_attr_line, '\n');
            if (temp_pstr)
                *temp_pstr = '\0';
            asprintf(&swindow_info[i], "%s", tmp_attr_line);
            addCDKSwindow(scst_info, swindow_info[i], BOTTOM);
            i++;
        }
        fclose(sysfs_file);
    }

    /* The 'g' makes the swindow widget scroll to the top, then activate */
    injectCDKSwindow(scst_info, 'g');
    activateCDKSwindow(scst_info, 0);

    /* We fell through -- the user exited the widget, but we don't care how */
    destroyCDKSwindow(scst_info);
    for (i = 0; i < MAX_SCST_INFO_ROWS; i++) {
        freeChar(swindow_info[i]);
    }
    return;
}