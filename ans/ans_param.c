/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2015-2017 Ansyun <anssupport@163.com>. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Ansyun <anssupport@163.com> nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <string.h>
#include <sys/queue.h>
#include <stdarg.h>
#include <errno.h>
#include <getopt.h>

#include <rte_common.h>
#include <rte_vect.h>
#include <rte_byteorder.h>
#include <rte_log.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_memzone.h>
#include <rte_tailq.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_random.h>
#include <rte_debug.h>
#include <rte_ring.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_string_fns.h>
#include <rte_ether.h>
#include <rte_ethdev.h>

#include "ans_init.h"
#include "ans_main.h"
#include "ans_param.h"

#include <rte_version.h>
#include <rte_ether.h>


#if (RTE_VERSION >= RTE_VERSION_NUM(19, 8, 0, 0))
#define ETHER_MAX_LEN RTE_ETHER_MAX_LEN
#define ETHER_ADDR_LEN RTE_ETHER_ADDR_LEN
#define ether_addr rte_ether_addr
#define ether_hdr rte_ether_hdr
#endif

/**********************************************************************
*@description:
*  display usage
*
*@parameters:
* [in]:
* [in]:
*
*@return values:
*
**********************************************************************/
int ans_check_port_config(struct ans_user_config *user_conf)
{
    unsigned portid;
    uint16_t i;

    for (i = 0; i < user_conf->rx_nb; i++)
    {
        portid = user_conf->lcore_rx[i].port_id;

        if ((user_conf->port_mask & (1 << portid)) == 0)
        {
            printf("port %u is not enabled in port mask\n", portid);
            return -1;
        }
        
        if (!rte_eth_dev_is_valid_port(portid)) 
        {
            printf("port %u is not present on the board\n", portid);
            return -1;
        }
    }
    return 0;
}


/**********************************************************************
*@description:
*  check rx lcore config
*
*@parameters:
* [in]:
* [in]:
*
*@return values:
*
**********************************************************************/
int ans_check_lcore_rx(struct ans_user_config *user_conf)
{
    uint8_t queue, lcore, port;
    uint16_t i, j;
    int socketid;

    for (i = 0; i < user_conf->rx_nb; ++i)
    {
        queue = user_conf->lcore_rx[i].queue_id;
        if (queue >= MAX_RX_QUEUE_PER_PORT)
        {
            printf("invalid queue number: %hhu\n", queue);
            return -1;
        }
        
        lcore = user_conf->lcore_rx[i].lcore_id;
        if (!rte_lcore_is_enabled(lcore))
        {
            printf("error: lcore %hhu is not enabled in lcore mask\n", lcore);
            return -1;
        }

        if ((socketid = rte_lcore_to_socket_id(lcore) != 0) && (user_conf->numa_on == 0))
        {
            printf("warning: lcore %hhu is on socket %d with numa off \n",  lcore, socketid);
        }
    }

    /* check if same port and queue mapping to different lcore */
    for (i = 0; i < user_conf->rx_nb; ++i)
    {
        port = user_conf->lcore_rx[i].port_id;
        queue = user_conf->lcore_rx[i].queue_id;
        lcore = user_conf->lcore_rx[i].lcore_id;
        
        for(j = i + 1; j < user_conf->rx_nb; ++j )
        {
            if( port == user_conf->lcore_rx[j].port_id &&
                queue == user_conf->lcore_rx[j].queue_id &&
                lcore != user_conf->lcore_rx[j].lcore_id)
            {
                printf("error: same port(%d) and queue(%d) mapping to different lcore \n", port, queue);
                return -1;
            }
        }
    }
    
    return 0;
}


/**********************************************************************
*@description:
*  display usage
*
*@parameters:
* [in]:
* [in]:
*
*@return values:
*
**********************************************************************/
static void ans_print_usage(const char *prgname)
{
  printf ("%s [EAL options] -- -p PORTMASK -P \n"
    "  -p PORTMASK: hexadecimal bitmask of ports to configure\n"
    "  -P : enable promiscuous mode\n"
    "  --config (port,queue,lcore): rx queues configuration\n"
    "  --worker (lcore,lcore...): worker lcore configuration\n"
    "  --no-numa: optional, disable numa awareness\n"
    "  --enable-kni: optional, disable kni awareness\n"
    "  --enable-ipsync: optional, sync ip/route from kernel kni interface\n"
    "  --enable-jumbo: enable jumbo frame"
    " which max packet len is PKTLEN in decimal (64-9600)\n",
    prgname);
}

/**********************************************************************
*@description:
*
*
*@parameters:
* [in]:
* [in]:
*
*@return values:
*
**********************************************************************/
static int ans_parse_max_pkt_len(const char *pktlen)
{
  char *end = NULL;
  unsigned long len;

  /* parse decimal string */
  len = strtoul(pktlen, &end, 10);
  if ((pktlen[0] == '\0') || (end == NULL) || (*end != '\0'))
    return -1;

  if (len == 0)
    return -1;

  return len;
}

/**********************************************************************
*@description:
*
*
*@parameters:
* [in]:
* [in]:
*
*@return values:
*
**********************************************************************/
static int ans_parse_portmask(const char *portmask)
{
  char *end = NULL;
  unsigned long pm;

  /* parse hexadecimal string */
  pm = strtoul(portmask, &end, 16);
  if ((portmask[0] == '\0') || (end == NULL) || (*end != '\0'))
    return -1;

  if (pm == 0)
    return -1;

  return pm;
}

/**********************************************************************
*@description:
*
*
*@parameters:
* [in]:
* [in]:
*
*@return values:
*
**********************************************************************/
static int ans_parse_config(const char *q_arg, struct ans_user_config *user_conf)
{
    char s[256];
    const char *p, *p0 = q_arg;
    char *end;

    enum fieldnames
    {
      FLD_PORT = 0,
      FLD_QUEUE,
      FLD_LCORE,
      _NUM_FLD
    };

    unsigned long int_fld[_NUM_FLD];
    char *str_fld[_NUM_FLD];
    int i;
    unsigned size;
    uint8_t lcore_id;
    
    while ((p = strchr(p0,'(')) != NULL)
    {
        ++p;
        if((p0 = strchr(p,')')) == NULL)
            return -1;

        size = p0 - p;
        if(size >= sizeof(s))
            return -1;

        snprintf(s, sizeof(s), "%.*s", size, p);
        if (rte_strsplit(s, sizeof(s), str_fld, _NUM_FLD, ',') != _NUM_FLD)
            return -1;

        for (i = 0; i < _NUM_FLD; i++)
        {
            errno = 0;
            int_fld[i] = strtoul(str_fld[i], &end, 0);
            if (errno != 0 || end == str_fld[i] || int_fld[i] > 255)
              return -1;
        }

        if (user_conf->rx_nb >= ANS_MAX_NB_LCORE)
        {
            printf("exceeded max number of lcore params: %hu\n", user_conf->rx_nb);
            return -1;
        }

        lcore_id = (uint8_t)int_fld[FLD_LCORE];
        if (rte_lcore_is_enabled(lcore_id) == 0) 
        {
            printf("lcore %d isn't enable \n", lcore_id);
            return -1;
        }

        user_conf->lcore_rx[user_conf->rx_nb].port_id = (uint8_t)int_fld[FLD_PORT];
        user_conf->lcore_rx[user_conf->rx_nb].queue_id = (uint8_t)int_fld[FLD_QUEUE];
        user_conf->lcore_rx[user_conf->rx_nb].lcore_id = (uint8_t)int_fld[FLD_LCORE];

        ++user_conf->rx_nb;
    }

    return 0;
}

/**********************************************************************
*@description:
*
*
*@parameters:
* [in]:
* [in]:
*
*@return values:
*
**********************************************************************/
static int ans_parse_worker(const char *arg,  struct ans_user_config *user_conf)
{
    const char *p = arg;
    uint8_t lcore_nb;
    uint32_t lcore;

    if (strnlen(arg, 20 + 1) == 20 + 1) 
    {
        printf("arg len too long \n");
        return -1;
    }

    lcore_nb = 0;
    while (*p != 0) 
    {

        errno = 0;
        lcore = strtoul(p, NULL, 0);
        if (errno != 0) 
        {
            return -1;
        }

        /* Check and enable lcore */
        if (rte_lcore_is_enabled(lcore) == 0)
        {
            printf("lcore %d is disable \n", lcore);
            return -1;
        }

        if (lcore >= ANS_MAX_NB_LCORE)
        {
            printf("lcore %d is too big \n", lcore);
            return -1;
        }

        user_conf->lcore_worker[user_conf->worker_nb].lcore_id = lcore;
        user_conf->worker_nb++;
        
        lcore_nb ++;

        p = strchr(p, ',');
        if (p == NULL) 
        {
            break;
        }
        p ++;
    }

    if (lcore_nb == 0)
    {
        printf("no worker lcore \n");
        return -1;
    }

    return 0;
}


/**********************************************************************
*@description:
* Parse the argument given in the command line of the application
*
*@parameters:
* [in]:
* [in]:
*
*@return values:
*
**********************************************************************/
int ans_parse_args(int argc, char **argv, struct ans_user_config *user_conf)
{
    int opt, ret;
    char **argvopt;
    int option_index;
    char *prgname = argv[0];
    static struct option lgopts[] = {
      {CMD_LINE_OPT_CONFIG, 1, 0, 0},
      {CMD_LINE_OPT_WORKER, 1, 0, 0},
      {CMD_LINE_OPT_NO_NUMA, 0, 0, 0},
      {CMD_LINE_OPT_ENABLE_KNI, 0, 0, 0},
      {CMD_LINE_OPT_ENABLE_IPSYNC, 0, 0, 0},
      {CMD_LINE_OPT_ENABLE_JUMBO, 0, 0, 0},
      {NULL, 0, 0, 0}
    };

    argvopt = argv;

    while ((opt = getopt_long(argc, argvopt, "p:P",
          lgopts, &option_index)) != EOF)
    {

      switch (opt)
       {
          /* portmask */
          case 'p':
            user_conf->port_mask = ans_parse_portmask(optarg);
            if (user_conf->port_mask == 0)
                     {
              printf("invalid portmask\n");
              ans_print_usage(prgname);
              return -1;
            }
            break;

          case 'P':
            printf("Promiscuous mode selected\n");
            user_conf->promiscuous_on = 1;
            break;

          /* long options */
          case 0:
            if (!strncmp(lgopts[option_index].name, CMD_LINE_OPT_CONFIG, sizeof (CMD_LINE_OPT_CONFIG)))
            {
                ret = ans_parse_config(optarg, user_conf);
                if (ret)
                {
                    printf("Invalid config parameter\n");
                    ans_print_usage(prgname);
                    return -1;
                }
            }

            if (!strncmp(lgopts[option_index].name, CMD_LINE_OPT_WORKER, sizeof (CMD_LINE_OPT_WORKER)))
            {
                ret = ans_parse_worker(optarg, user_conf);
                if (ret)
                {
                    printf("Invalid worker parameter\n");
                    ans_print_usage(prgname);
                    return -1;
                }
            }
            if (!strncmp(lgopts[option_index].name, CMD_LINE_OPT_NO_NUMA, sizeof(CMD_LINE_OPT_NO_NUMA)))
            {
                printf("numa is disabled \n");
                user_conf->numa_on = 0;
            }

            if (!strncmp(lgopts[option_index].name, CMD_LINE_OPT_ENABLE_KNI, sizeof(CMD_LINE_OPT_ENABLE_KNI)))
            {
                printf("KNI is enable \n");
                user_conf->kni_on = 1;
            }

            if (!strncmp(lgopts[option_index].name, CMD_LINE_OPT_ENABLE_IPSYNC, sizeof(CMD_LINE_OPT_ENABLE_IPSYNC)))
            {
                printf("IPSYNC is enable \n");
                user_conf->ipsync_on = 1;
            }
            
            if (!strncmp(lgopts[option_index].name, CMD_LINE_OPT_ENABLE_JUMBO, sizeof (CMD_LINE_OPT_ENABLE_JUMBO)))
            {
                struct option lenopts = {"max-pkt-len", required_argument, 0, 0};

                printf("jumbo frame is enabled - disabling simple TX path\n");
                user_conf->jumbo_frame_on = 1;

                /* if no max-pkt-len set, use the default value ETHER_MAX_LEN */
                if (0 == getopt_long(argc, argvopt, "", &lenopts, &option_index))
                {
                    ret = ans_parse_max_pkt_len(optarg);
                    if ((ret < 64) || (ret > MAX_JUMBO_PKT_LEN))
                    {
                        printf("invalid packet length\n");
                        ans_print_usage(prgname);
                        return -1;
                    }
                    user_conf->max_rx_pkt_len = ret;
                }
                else
                {
                    user_conf->max_rx_pkt_len = ETHER_MAX_LEN;
                }
                
                printf("set jumbo frame max packet length to %u\n", (unsigned int)user_conf->max_rx_pkt_len);
            }
            break;

          default:
            ans_print_usage(prgname);
            return -1;
        }
    }

    if (optind >= 0)
      argv[optind-1] = prgname;

    ret = optind-1;
    optind = 0; /* reset getopt lib */
    return ret;
}


