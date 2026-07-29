#ifndef _ASSAULT_MC_LOG_H
#define _ASSAULT_MC_LOG_H

/* 仅内存缓冲；flush 只允许在模块_start末尾调用一次，禁止后台线程写MC */
void assault_mc_log_init(const char *filename);
void assault_mc_log(const char *format, ...);
void assault_mc_log_flush(void);

#endif
