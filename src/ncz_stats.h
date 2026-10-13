/* SPDX-License-Identifier: Apache-2.0 */
/* ncz_stats.h - always-on, cheap run statistics for every GLES3 hack:
 * shader compile/link counts and time, first-frame time, and frame-time
 * percentiles for the first 5 s versus steady state. Printed at exit and on
 * SIGUSR1 as one "[stats]" line. */
#ifndef NCZ_STATS_H
#define NCZ_STATS_H
void ncz_stats_start(void);
void ncz_stats_frame(void);           /* call once per presented frame */
void ncz_stats_print(void);
double ncz_stats_p95_recent(int seconds); /* p95 frame ms over the last N s, or -1 */
#endif
