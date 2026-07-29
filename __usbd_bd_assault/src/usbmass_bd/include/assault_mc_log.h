#ifndef _ASSAULT_MC_LOG_H
#define _ASSAULT_MC_LOG_H

/* 日志先入内存；仅 flush 时写 MC，避免在USB/BDM线程里同步访问MC卡死 */
void assault_mc_log_init(const char *filename);
void assault_mc_log(const char *format, ...);
void assault_mc_log_flush(void);

#endif
