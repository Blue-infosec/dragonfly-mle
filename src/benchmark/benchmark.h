/* Copyright (C) 2017-2018 CounterFlow AI, Inc.
 *
 * You can copy, redistribute or modify this Program under the terms of
 * the GNU General Public License version 2 as published by the Free
 * Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/* 
 *
 * author Don J. Rude <dr@counterflowai.com>
 *
 */

#ifndef _BENCHMARK_H_
#define _BENCHMARK_H_


#include "dragonfly-lib.h"
#include "dragonfly-io.h"


#define ANALYZER_TEST_FILE "analyzer/analyzer.lua"
#define FILTER_TEST_FILE "filter/filter.lua"
#define CONFIG_TEST_FILE "config/config.lua"
#define MSG "{\"timestamp\":\"2019-04-22T13:45:09.000416+0000\",\"flow_id\":1598023201615874,\"event_type\":\"flow\",\"src_ip\":\"fe80:0000:0000:0000:feec:daff:fe31:c6a1\",\"src_port\":55236,\"dest_ip\":\"ff02:0000:0000:0000:0000:0000:0000:0001\",\"dest_port\":10001,\"proto\":\"UDP\",\"app_proto\":\"failed\",\"flow\":{\"pkts_toserver\":1,\"pkts_toclient\":0,\"bytes_toserver\":208,\"bytes_toclient\":0,\"start\":\"2019-04-22T13:44:38.421890+0000\",\"end\":\"2019-04-22T13:44:38.421890+0000\",\"age\":0,\"state\":\"new\",\"reason\":\"timeout\",\"alerted\":false},\"community_id\":\"1:xsUMq8fmVor0i33m7SEgkXaXd4w=\"}\n"

void dragonfly_mle_bench(const char *dragonfly_root);
void SELF_BENCH0(const char *dragonfly_root);
void SELF_BENCH1(const char *dragonfly_root);
void SELF_BENCH2(const char *dragonfly_root);
void SELF_BENCH3(const char *dragonfly_root);
#endif
